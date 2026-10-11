#include "KeyboardHook.h"
#include "ImeState.h"

namespace {
HHOOK g_hook = nullptr;
HWND g_notify = nullptr;

bool g_swallowed[256] = {};  // vk codes whose key-down we ate; their key-up is eaten too
std::wstring g_buf;
bool g_active = false;

LARGE_INTEGER g_freq{};
double g_maxMicros = 0;

// Modifier state is read from the system rather than tracked from events: a missed key-up
// (elevated window in front, secure desktop, injected events) would otherwise leave a flag stuck
// and capture all plain typing. Inside an LL hook this is the state *before* the current event.
bool Down(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }
// Only the trigger key (left Shift, right Shift or CapsLock: a user setting) starts/holds a capture
// session. With a Shift trigger the other Shift is a plain Shift that uppercases captured letters;
// with the CapsLock trigger either Shift does.
int TriggerVk() { return g_settings.triggerVk.load(); }
int OtherShiftVk() {
    switch (TriggerVk()) {
        case VK_LSHIFT: return VK_RSHIFT;
        case VK_RSHIFT: return VK_LSHIFT;
        default: return VK_SHIFT;  // either one
    }
}

// A swallowed key never reaches the system key-state table (measured: GetAsyncKeyState stays "up"
// after a swallowed CapsLock press), so a CapsLock trigger has to be tracked from events. A missed
// key-up (elevated foreground window) would leave it stuck, hence ResetHeld() on foreground change,
// on trigger change and on the periodic reinstall.
bool g_capsHeld = false;
bool CaptureShiftHeld() {
    const int trigger = TriggerVk();
    return trigger == VK_CAPITAL ? g_capsHeld : Down(trigger);
}
bool CtrlOrWinHeld() { return Down(VK_CONTROL) || Down(VK_LWIN) || Down(VK_RWIN); }

// Number row and every OEM punctuation key (JIS and US): 0-9, ; = , - . / ` [ \ ] ' and the extra
// JIS/ISO key. Numpad and non-text keys are not captured.
bool IsSymbolKey(DWORD vk) {
    return (vk >= '0' && vk <= '9') || (vk >= VK_OEM_1 && vk <= VK_OEM_3) ||
           (vk >= VK_OEM_4 && vk <= VK_OEM_8) || vk == VK_OEM_102;
}

// The character this key types on the foreground window's layout, with or without Shift.
// Flag 0x4 keeps ToUnicodeEx from consuming/changing a pending dead key. Dead keys and anything
// that is not exactly one printable character are not captured (they pass through).
bool SymbolChar(const KBDLLHOOKSTRUCT* k, bool shifted, wchar_t& out) {
    BYTE state[256] = {};
    if (shifted) state[VK_SHIFT] = 0x80;
    const DWORD tid = GetWindowThreadProcessId(GetForegroundWindow(), nullptr);
    wchar_t buf[4] = {};
    const int n = ToUnicodeEx(k->vkCode, k->scanCode, state, buf, 4, 0x4, GetKeyboardLayout(tid));
    if (n != 1 || buf[0] < 0x20 || buf[0] == 0x7F) return false;
    out = buf[0];
    return true;
}

bool ImeIsOn() {
    switch (ImeState::Current()) {
        case ImeMode::Open: return true;
        case ImeMode::Unknown: return g_settings.unknownAsOn.load();
        default: return false;
    }
}

void EndSessionCommit() {
    if (!g_active) return;
    g_active = false;
    auto* text = new std::wstring(std::move(g_buf));
    g_buf.clear();
    if (!PostMessageW(g_notify, WM_APP_COMMIT, 0, reinterpret_cast<LPARAM>(text))) delete text;
}

void NotifyUpdate(bool started) {
    PostMessageW(g_notify, WM_APP_UPDATE, started ? 1 : 0, 0);
}

LRESULT Handle(WPARAM w, LPARAM l, bool& swallow) {
    auto* k = reinterpret_cast<KBDLLHOOKSTRUCT*>(l);
    if (k->flags & LLKHF_INJECTED) return 0;  // ours or another tool's: never touch
    const bool down = (w == WM_KEYDOWN || w == WM_SYSKEYDOWN);
    const DWORD vk = k->vkCode;

    if (vk == VK_CAPITAL && TriggerVk() == VK_CAPITAL) {
        // The CapsLock trigger is eaten (it no longer toggles Caps Lock) so it cannot reach the app.
        if (down) {
            if (!g_settings.enabled.load()) return 0;  // disabled: a plain CapsLock
            if (!g_capsHeld) ImeState::RequestRefresh();
            g_capsHeld = true;
            g_swallowed[VK_CAPITAL] = true;
            swallow = true;
        } else {
            g_capsHeld = false;
            if (g_swallowed[VK_CAPITAL]) {  // release of a press we ate
                g_swallowed[VK_CAPITAL] = false;
                swallow = true;
            }
            EndSessionCommit();
        }
        return 0;
    }
    if (vk == static_cast<DWORD>(TriggerVk())) {
        if (down) {
            ImeState::RequestRefresh();
        } else {
            EndSessionCommit();  // trigger Shift released
        }
        return 0;  // Shift itself always passes through
    }
    if (vk == static_cast<DWORD>(OtherShiftVk())) {
        return 0;  // never ours; it only uppercases letters while the trigger Shift is held
    }

    if (g_swallowed[vk & 0xFF]) {
        if (!down) {  // release of a key we ate
            g_swallowed[vk & 0xFF] = false;
            swallow = true;
            return 0;
        }
        // Auto-repeat or a fresh press after a lost key-up: evaluate as a new press below.
        g_swallowed[vk & 0xFF] = false;
    }
    if (!down || !g_settings.enabled.load()) return 0;

    const bool alt = (k->flags & LLKHF_ALTDOWN) != 0;
    const bool letter = vk >= 'A' && vk <= 'Z';
    if (letter || vk == VK_SPACE || IsSymbolKey(vk)) {  // all text keys are captured, Space as ' '
        if (!CaptureShiftHeld()) {  // Shift went away without us seeing it: don't keep a stale session
            EndSessionCommit();
            return 0;
        }
        if (alt || CtrlOrWinHeld()) return 0;
        // The trigger Shift is not a "real" Shift: keys give their plain character (letters lowercase,
        // digits as digits); also holding the other Shift gives the shifted one (A, !, ?, ...).
        const bool shifted = Down(OtherShiftVk());
        wchar_t ch = L' ';
        if (letter) ch = static_cast<wchar_t>((shifted ? L'A' : L'a') + (vk - 'A'));
        else if (vk != VK_SPACE && !SymbolChar(k, shifted, ch)) return 0;
        if (!g_active && !ImeIsOn()) return 0;
        const bool started = !g_active;
        g_active = true;
        g_buf.push_back(ch);
        g_swallowed[vk] = true;
        swallow = true;
        NotifyUpdate(started);
    } else if (g_active && (vk == VK_BACK || vk == VK_ESCAPE)) {
        if (vk == VK_ESCAPE) g_buf.clear();
        else if (!g_buf.empty()) g_buf.pop_back();
        if (g_buf.empty()) g_active = false;
        g_swallowed[vk] = true;
        swallow = true;
        NotifyUpdate(false);
    }
    return 0;
}

LRESULT CALLBACK Proc(int code, WPARAM w, LPARAM l) {
    if (code != HC_ACTION) return CallNextHookEx(nullptr, code, w, l);
    LARGE_INTEGER t0, t1;
    QueryPerformanceCounter(&t0);
    bool swallow = false;
    Handle(w, l, swallow);
    QueryPerformanceCounter(&t1);
    double us = (t1.QuadPart - t0.QuadPart) * 1e6 / g_freq.QuadPart;
    if (us > g_maxMicros) g_maxMicros = us;
    if (swallow) return 1;
    return CallNextHookEx(nullptr, code, w, l);
}
}  // namespace

namespace KeyboardHook {

bool Install(HWND notifyWnd) {
    g_notify = notifyWnd;
    QueryPerformanceFrequency(&g_freq);
    g_hook = SetWindowsHookExW(WH_KEYBOARD_LL, Proc, GetModuleHandleW(nullptr), 0);
    return g_hook != nullptr;
}

void Uninstall() {
    if (g_hook) UnhookWindowsHookEx(g_hook);
    g_hook = nullptr;
}

void Reinstall() {
    if (g_active) return;
    if (g_capsHeld) {  // held or stuck: can't tell, so heal it and try again next time
        g_capsHeld = false;
        return;
    }
    if (CaptureShiftHeld()) return;
    Uninstall();
    Install(g_notify);
}

void ResetHeld() {
    g_capsHeld = false;
    g_swallowed[VK_CAPITAL] = false;
}

const std::wstring& Buffer() { return g_buf; }
double MaxCallbackMicros() { return g_maxMicros; }

}  // namespace KeyboardHook
