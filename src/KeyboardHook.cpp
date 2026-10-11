#include "KeyboardHook.h"
#include "ImeState.h"
#include "ImeSwitcher.h"
#include "PasswordFocus.h"

namespace {
HHOOK g_hook = nullptr;

// State of the trigger key (one of kTriggerKeys: left/right Shift, 無変換, 変換 or カタカナひらがな;
// a user setting).
// Swallowed: pressed while the IME was ON. Its press (and repeats, and release) are eaten, so the
//   app never sees it: the keys typed meanwhile pass through untouched, as plain unshifted keys, while
//   ImeSwitcher holds the IME in half-width alphanumeric mode.
// Passed: pressed while the IME was OFF or undeterminable, or in a password / not yet checked field
//   (PasswordFocus) (or disabled, or with Ctrl/Alt/Win held): an ordinary key
//   until it is released, even if the IME turns ON meanwhile (the app already saw it go down).
// A swallowed key never reaches the system key-state table (measured with CapsLock:
// GetAsyncKeyState stays "up"), so the state is tracked from events. A missed release (elevated
// window in front) is healed by the next press/release, by ResetHeld() and by the reinstall tick.
enum class Trigger { Up, Swallowed, Passed };
Trigger g_trigger = Trigger::Up;

LARGE_INTEGER g_freq{};
double g_maxMicros = 0;

bool Down(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }
int TriggerVk() { return g_settings.triggerVk.load(); }
bool IsTrigger(const KBDLLHOOKSTRUCT* k) {
    const int vk = TriggerVk();
    if (k->vkCode == static_cast<DWORD>(vk)) return true;
    // JIS カタカナひらがな etc.: the vk can change with modifiers (Shift gives VK_DBE_KATAKANA),
    // the scan code does not.
    const TriggerKey* t = FindTriggerKey(vk);
    return t && t->scan && k->scanCode == t->scan && !(k->flags & LLKHF_EXTENDED);
}
bool CtrlAltOrWinHeld() {
    return Down(VK_CONTROL) || Down(VK_MENU) || Down(VK_LWIN) || Down(VK_RWIN);
}

// Only a definite ON counts: an undeterminable state (Unknown) passes the trigger through. So does a
// password field (or a field not checked yet): there the IME doesn't compose even when the window's
// IME reads ON (Chromium, WPF, ...), and eating the Shift would silently lose capitals.
bool ImeIsOn() {
    return PasswordFocus::Current() == FieldKind::Normal && ImeState::Current() == ImeMode::Open;
}

// Keys that change the IME mode. The cache is refreshed a little later, after the app has
// processed the key, so the next trigger press is judged on the new mode.
bool IsImeModeKey(DWORD vk) {
    return vk == VK_KANA || vk == 0x16 /* VK_IME_ON */ || vk == VK_KANJI || vk == 0x1A /* VK_IME_OFF */ ||
           vk == VK_NONCONVERT || vk == VK_CAPITAL || (vk >= 0xF0 && vk <= 0xF6);  // JIS 英数, 半角/全角, ...
}

void EndSession() {
    if (g_trigger == Trigger::Swallowed) ImeSwitcher::End();
    g_trigger = Trigger::Up;
}

LRESULT Handle(WPARAM w, LPARAM l, bool& swallow) {
    auto* k = reinterpret_cast<KBDLLHOOKSTRUCT*>(l);
    if (k->flags & LLKHF_INJECTED) return 0;  // ours (VK_IME_ON/OFF) or another tool's: never touch
    const bool down = (w == WM_KEYDOWN || w == WM_SYSKEYDOWN);
    const DWORD vk = k->vkCode;

    if (!IsTrigger(k)) {
        // Every other key passes through untouched (the trigger is the only key we ever eat).
        if (down && IsImeModeKey(vk)) ImeState::RequestRefresh(80);
        return 0;
    }

    if (!down) {
        if (g_trigger == Trigger::Swallowed) swallow = true;  // release of a press we ate
        EndSession();
        return 0;
    }
    switch (g_trigger) {
        case Trigger::Swallowed: swallow = true; return 0;  // auto-repeat
        case Trigger::Passed: return 0;                     // auto-repeat of an ordinary press
        case Trigger::Up: break;
    }
    // A fresh press. With Ctrl/Alt/Win already down it is part of a shortcut (Ctrl+Shift+Z,
    // Win+Shift+S, ...): eating the Shift would turn it into a different shortcut.
    if (!g_settings.enabled.load() || CtrlAltOrWinHeld() || !ImeIsOn()) {
        g_trigger = Trigger::Passed;
        return 0;
    }
    g_trigger = Trigger::Swallowed;
    swallow = true;
    ImeSwitcher::Begin();
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

bool Install() {
    QueryPerformanceFrequency(&g_freq);
    g_hook = SetWindowsHookExW(WH_KEYBOARD_LL, Proc, GetModuleHandleW(nullptr), 0);
    return g_hook != nullptr;
}

void Uninstall() {
    if (g_hook) UnhookWindowsHookEx(g_hook);
    g_hook = nullptr;
}

void Reinstall() {
    // A passed trigger is visible to GetAsyncKeyState: really held means wait for the next tick.
    if (g_trigger == Trigger::Passed && Down(TriggerVk())) return;
    if (g_trigger != Trigger::Up) {  // a swallowed one can't be told apart from a stuck one: heal it
        EndSession();
        return;
    }
    Uninstall();
    Install();
}

void ResetHeld() { EndSession(); }

double MaxCallbackMicros() { return g_maxMicros; }

}  // namespace KeyboardHook
