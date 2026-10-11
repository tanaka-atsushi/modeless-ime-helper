#include "common.h"
#include <shellapi.h>
#include <cstdio>
#include "ImeState.h"
#include "ImeSwitcher.h"
#include "KeyboardHook.h"

namespace {
enum : UINT {
    ID_ENABLE = 1,
    ID_UNKNOWN_ON,
    ID_TRIGGER_LEFT,
    ID_TRIGGER_RIGHT,
    ID_TRIGGER_CAPS,
    ID_STARTUP,
    ID_EXIT,
};
constexpr UINT_PTR kTimerReinstall = 1;
const wchar_t kWndClass[] = L"ime2nd.Main";
const wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
const wchar_t kRunValue[] = L"ime2nd";
const wchar_t kSettingsKey[] = L"Software\\ime2nd";
const wchar_t kTriggerValue[] = L"TriggerShift";  // REG_DWORD: VK_LSHIFT, VK_RSHIFT or VK_CAPITAL

HWND g_wnd = nullptr;
UINT g_taskbarCreated = 0;
NOTIFYICONDATAW g_nid{};

// ---- startup registration (HKCU only: no admin needed) ----------------------------------------

bool StartupEnabled() {
    return RegGetValueW(HKEY_CURRENT_USER, kRunKey, kRunValue, RRF_RT_REG_SZ, nullptr, nullptr,
                        nullptr) == ERROR_SUCCESS;
}

void SetStartup(bool on) {
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS) return;
    if (on) {
        wchar_t path[MAX_PATH];
        GetModuleFileNameW(nullptr, path, MAX_PATH);
        std::wstring v = L"\"" + std::wstring(path) + L"\"";
        RegSetValueExW(key, kRunValue, 0, REG_SZ, reinterpret_cast<const BYTE*>(v.c_str()),
                       static_cast<DWORD>((v.size() + 1) * sizeof(wchar_t)));
    } else {
        RegDeleteValueW(key, kRunValue);
    }
    RegCloseKey(key);
}

// ---- user settings (HKCU only: no admin needed) -------------------------------------------------

bool LoadDword(const wchar_t* name, DWORD& v) {
    DWORD size = sizeof(v);
    return RegGetValueW(HKEY_CURRENT_USER, kSettingsKey, name, RRF_RT_REG_DWORD, nullptr, &v,
                        &size) == ERROR_SUCCESS;
}

void SaveDword(const wchar_t* name, DWORD v) {
    HKEY key;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kSettingsKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key,
                        nullptr) != ERROR_SUCCESS)
        return;
    RegSetValueExW(key, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&v), sizeof(v));
    RegCloseKey(key);
}

void LoadSettings() {
    DWORD v = 0;
    // A missing or invalid value keeps the default (left Shift).
    if (LoadDword(kTriggerValue, v) && (v == VK_LSHIFT || v == VK_RSHIFT || v == VK_CAPITAL))
        g_settings.triggerVk = static_cast<int>(v);
}

// ---- tray -------------------------------------------------------------------------------------

const wchar_t* TriggerName() {
    switch (g_settings.triggerVk) {
        case VK_RSHIFT: return L"右Shift";
        case VK_CAPITAL: return L"CapsLock";
        default: return L"左Shift";
    }
}

void SetTrayTip() {
    swprintf_s(g_nid.szTip, L"ime2nd - %s+英字 未確定に英字を混ぜる", TriggerName());
}

void AddTrayIcon() {
    g_nid = {sizeof(g_nid)};
    g_nid.hWnd = g_wnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_APP_TRAY;
    g_nid.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    SetTrayTip();
    Shell_NotifyIconW(NIM_ADD, &g_nid);
}

// A CapsLock trigger is swallowed from now on, so Caps Lock could never be switched off again
// while it is ON. Taps the key once to switch it off (the injected tap is ignored by our hook).
// GetKeyState works from any thread; the low bit of GetAsyncKeyState does not mean "on".
void TurnOffCapsLockState() {
    if (!(GetKeyState(VK_CAPITAL) & 1)) return;
    INPUT in[2] = {};
    for (INPUT& i : in) {
        i.type = INPUT_KEYBOARD;
        i.ki.wVk = VK_CAPITAL;
        i.ki.wScan = static_cast<WORD>(MapVirtualKeyW(VK_CAPITAL, MAPVK_VK_TO_VSC));
        i.ki.dwExtraInfo = kInjectTag;
    }
    in[1].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(2, in, sizeof(INPUT));
}

void SetTrigger(int vk) {
    if (g_settings.triggerVk == vk) return;
    g_settings.triggerVk = vk;
    KeyboardHook::ResetHeld();  // forget a CapsLock press tracked under the previous setting
    SaveDword(kTriggerValue, static_cast<DWORD>(vk));
    SetTrayTip();
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
}

void ShowTrayMenu() {
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING | (g_settings.enabled ? MF_CHECKED : 0), ID_ENABLE, L"有効");
    AppendMenuW(m, MF_STRING | (g_settings.unknownAsOn ? MF_CHECKED : 0), ID_UNKNOWN_ON,
                L"IME状態が不明のときも横取りする");
    HMENU trigger = CreatePopupMenu();  // owned by m: DestroyMenu(m) frees it
    AppendMenuW(trigger, MF_STRING, ID_TRIGGER_LEFT, L"左 Shift");
    AppendMenuW(trigger, MF_STRING, ID_TRIGGER_RIGHT, L"右 Shift");
    AppendMenuW(trigger, MF_STRING, ID_TRIGGER_CAPS, L"CapsLock（IME が ON の間は Caps Lock の切り替えは使えません）");
    CheckMenuRadioItem(trigger, ID_TRIGGER_LEFT, ID_TRIGGER_CAPS,
                       g_settings.triggerVk == VK_RSHIFT    ? ID_TRIGGER_RIGHT
                       : g_settings.triggerVk == VK_CAPITAL ? ID_TRIGGER_CAPS
                                                            : ID_TRIGGER_LEFT,
                       MF_BYCOMMAND);
    AppendMenuW(m, MF_POPUP, reinterpret_cast<UINT_PTR>(trigger), L"英字入力の開始キー");
    AppendMenuW(m, MF_STRING | (StartupEnabled() ? MF_CHECKED : 0), ID_STARTUP,
                L"Windows起動時に自動実行");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, ID_EXIT, L"終了");
    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(g_wnd);  // required so the menu dismisses on outside click
    TrackPopupMenu(m, TPM_RIGHTBUTTON, pt.x, pt.y, 0, g_wnd, nullptr);
    PostMessageW(g_wnd, WM_NULL, 0, 0);
    DestroyMenu(m);
}

// ---- main window ------------------------------------------------------------------------------

void CALLBACK OnForeground(HWINEVENTHOOK, DWORD, HWND, LONG, LONG, DWORD, DWORD) {
    ImeState::RequestRefresh();
    KeyboardHook::ResetHeld();  // a CapsLock release may have gone to a window the hook can't see
}

LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == g_taskbarCreated && m != 0) {
        AddTrayIcon();
        return 0;
    }
    switch (m) {
        case WM_APP_TRAY:
            if (LOWORD(l) == WM_RBUTTONUP || LOWORD(l) == WM_CONTEXTMENU) ShowTrayMenu();
            return 0;
        case WM_COMMAND:
            switch (LOWORD(w)) {
                case ID_ENABLE: g_settings.enabled = !g_settings.enabled; break;
                case ID_UNKNOWN_ON: g_settings.unknownAsOn = !g_settings.unknownAsOn; break;
                case ID_TRIGGER_LEFT: SetTrigger(VK_LSHIFT); break;
                case ID_TRIGGER_RIGHT: SetTrigger(VK_RSHIFT); break;
                case ID_TRIGGER_CAPS:
                    SetTrigger(VK_CAPITAL);
                    TurnOffCapsLockState();  // also when it was already selected and Caps Lock got ON
                    break;
                case ID_STARTUP: SetStartup(!StartupEnabled()); break;
                case ID_EXIT: DestroyWindow(h); break;
            }
            return 0;
        case WM_TIMER:
            if (w == kTimerReinstall) KeyboardHook::Reinstall();
            return 0;
        case WM_DESTROY:
            Shell_NotifyIconW(NIM_DELETE, &g_nid);
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

HWND CreateMainWindow(HINSTANCE inst) {
    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.lpszClassName = kWndClass;
    RegisterClassW(&wc);
    return CreateWindowExW(0, kWndClass, L"ime2nd", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, inst,
                           nullptr);
}

// ---- --selftest: exercises each component and reports to the parent console -------------------

int SelfTest(HINSTANCE inst) {
    // GUI-subsystem exe has no console: report to selftest.txt next to the exe.
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    wchar_t* slash = wcsrchr(path, L'\\') + 1;
    wcscpy_s(slash, MAX_PATH - (slash - path), L"selftest.txt");
    FILE* f = nullptr;
    _wfreopen_s(&f, path, L"w", stdout);
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[selftest] focus a text field within 3 seconds...\n");
    Sleep(3000);

    ImeMode mode = ImeState::QueryNow(100);
    printf("IME state : %s\n", mode == ImeMode::Open     ? "Open"
                              : mode == ImeMode::Closed ? "Closed"
                                                        : "Unknown");
    printf("Conv mode : %lld (raw IMC_GETCONVERSIONMODE; 9 = hiragana, 0 = half-width alphanumeric)\n",
           static_cast<long long>(ImeState::LastConversionMode()));

    // Hook latency: install the real hook for 10 s; type Shift+letters / anything meanwhile.
    CreateMainWindow(inst);
    ImeState::Start();
    ImeSwitcher::Start();
    KeyboardHook::Install();
    MSG msg;
    printf("Hook      : installed for 10 s - type now (Shift+a..z with IME on, normal typing)\n");
    for (DWORD end = GetTickCount() + 10000; GetTickCount() < end;) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) DispatchMessageW(&msg);
        Sleep(5);
    }
    KeyboardHook::Uninstall();
    ImeSwitcher::Stop();
    ImeState::Stop();
    printf("Hook max callback time: %.1f us (limit before Windows drops the hook: ~300000 us)\n",
           KeyboardHook::MaxCallbackMicros());
    return 0;
}
}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR cmd, int) {
    if (cmd && wcsstr(cmd, L"--selftest")) return SelfTest(inst);

    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\ime2nd.single-instance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) return 0;

    LoadSettings();
    g_wnd = CreateMainWindow(inst);
    g_taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    if (!g_wnd) return 1;

    ImeState::Start();
    ImeSwitcher::Start();
    if (!KeyboardHook::Install()) {
        MessageBoxW(nullptr, L"キーボードフックの登録に失敗しました。", L"ime2nd", MB_ICONERROR);
        return 1;
    }
    HWINEVENTHOOK fgHook = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND,
                                           nullptr, OnForeground, 0, 0, WINEVENT_OUTOFCONTEXT);
    AddTrayIcon();
    SetTimer(g_wnd, kTimerReinstall, 30000, nullptr);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (fgHook) UnhookWinEvent(fgHook);
    KeyboardHook::Uninstall();
    ImeSwitcher::Stop();  // restores the IME if a session was running
    ImeState::Stop();
    CloseHandle(mutex);
    return 0;
}
