#include "common.h"
#include <shellapi.h>
#include <cstdio>
#include "ImeState.h"
#include "ImeSwitcher.h"
#include "KeyboardHook.h"
#include "PasswordFocus.h"

namespace {
enum : UINT {
    ID_ENABLE = 1,
    ID_STARTUP = 6,
    ID_EXIT,
};
// Trigger choices in the tray menu: menu id = kTriggerFirstId + index in kTriggerKeys.
constexpr UINT kTriggerFirstId = 200;
constexpr UINT kTriggerCount = sizeof(kTriggerKeys) / sizeof(kTriggerKeys[0]);
constexpr UINT_PTR kTimerReinstall = 1;
const wchar_t kWndClass[] = L"ime2nd.Main";
const wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
const wchar_t kRunValue[] = L"ime2nd";
const wchar_t kSettingsKey[] = L"Software\\ime2nd";
const wchar_t kTriggerValue[] = L"TriggerShift";  // REG_DWORD: the vk of one of kTriggerKeys

HWND g_wnd = nullptr;
UINT g_taskbarCreated = 0;
NOTIFYICONDATAW g_nid{};

// Full path of this exe ("" on failure). GetModuleFileNameW truncates silently when the buffer is
// too small (it returns the buffer size), so retry with a larger one.
std::wstring ExePath() {
    std::wstring path(MAX_PATH, L'\0');
    for (;;) {
        const DWORD n = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (n == 0) return {};
        if (n < path.size()) {
            path.resize(n);
            return path;
        }
        if (path.size() >= 32768) return {};  // longer than any Windows path
        path.resize(path.size() * 2);
    }
}

// ---- startup registration (HKCU only: no admin needed) ----------------------------------------

bool StartupEnabled() {
    return RegGetValueW(HKEY_CURRENT_USER, kRunKey, kRunValue, RRF_RT_REG_SZ, nullptr, nullptr,
                        nullptr) == ERROR_SUCCESS;
}

void SetStartup(bool on) {
    const std::wstring path = on ? ExePath() : std::wstring();
    if (on && path.empty()) return;  // never register a broken path
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS) return;
    if (on) {
        std::wstring v = L"\"" + path + L"\"";
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
    if (LoadDword(kTriggerValue, v) && FindTriggerKey(static_cast<int>(v)))
        g_settings.triggerVk = static_cast<int>(v);
}

// ---- tray -------------------------------------------------------------------------------------

const wchar_t* TriggerName() {
    const TriggerKey* t = FindTriggerKey(g_settings.triggerVk);
    return t ? t->name : L"?";
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

void SetTrigger(int vk) {
    if (g_settings.triggerVk == vk) return;
    g_settings.triggerVk = vk;
    KeyboardHook::ResetHeld();  // forget a press tracked under the previous setting
    SaveDword(kTriggerValue, static_cast<DWORD>(vk));
    SetTrayTip();
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
}

void ShowTrayMenu() {
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING | (g_settings.enabled ? MF_CHECKED : 0), ID_ENABLE, L"有効");
    HMENU trigger = CreatePopupMenu();  // owned by m: DestroyMenu(m) frees it
    for (UINT i = 0; i < kTriggerCount; ++i) {
        const TriggerKey& t = kTriggerKeys[i];
        if (t.vk == VK_NONCONVERT) AppendMenuW(trigger, MF_SEPARATOR, 0, nullptr);  // JIS IME keys
        std::wstring label = std::wstring(t.name) + t.note;
        AppendMenuW(trigger, MF_STRING, kTriggerFirstId + i, label.c_str());
        if (t.vk == g_settings.triggerVk)
            CheckMenuRadioItem(trigger, kTriggerFirstId, kTriggerFirstId + kTriggerCount - 1,
                               kTriggerFirstId + i, MF_BYCOMMAND);
    }
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

ULONGLONG g_logStart = 0;  // --selftest: log focus events relative to this tick (0 = off)
HWINEVENTHOOK g_fgHook = nullptr;
HWINEVENTHOOK g_focusHook = nullptr;

void CALLBACK OnForeground(HWINEVENTHOOK, DWORD, HWND, LONG, LONG, DWORD, DWORD) {
    ImeState::RequestRefresh();
    PasswordFocus::FocusChanged();
    KeyboardHook::ResetHeld();  // a trigger release may have gone to a window the hook can't see
}

// Focus moved inside a window too (e.g. from a page's user-name field to its password field).
void CALLBACK OnFocus(HWINEVENTHOOK, DWORD, HWND h, LONG, LONG, DWORD, DWORD) {
    PasswordFocus::FocusChanged();
    if (g_logStart) {
        wchar_t cls[64] = L"";
        GetClassNameW(h, cls, 64);
        printf("[%6llu ms] focus event (%ls)\n", GetTickCount64() - g_logStart, cls);
    }
}

// Out-of-context: the callbacks run on this (the main) thread.
void InstallWinEvents() {
    g_fgHook = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr,
                               OnForeground, 0, 0, WINEVENT_OUTOFCONTEXT);
    g_focusHook = SetWinEventHook(EVENT_OBJECT_FOCUS, EVENT_OBJECT_FOCUS, nullptr, OnFocus, 0, 0,
                                  WINEVENT_OUTOFCONTEXT);
}

void UninstallWinEvents() {
    if (g_fgHook) UnhookWinEvent(g_fgHook);
    if (g_focusHook) UnhookWinEvent(g_focusHook);
    g_fgHook = g_focusHook = nullptr;
}

const char* FieldName(FieldKind k) {
    return k == FieldKind::Normal ? "normal" : k == FieldKind::Password ? "PASSWORD" : "unknown";
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
            if (LOWORD(w) >= kTriggerFirstId && LOWORD(w) < kTriggerFirstId + kTriggerCount) {
                SetTrigger(kTriggerKeys[LOWORD(w) - kTriggerFirstId].vk);
                return 0;
            }
            switch (LOWORD(w)) {
                case ID_ENABLE: g_settings.enabled = !g_settings.enabled; break;
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
    // A hidden top-level window, not HWND_MESSAGE: message-only windows don't receive broadcasts,
    // so the TaskbarCreated broadcast (Explorer restart) would never re-add the tray icon.
    return CreateWindowExW(WS_EX_TOOLWINDOW, kWndClass, L"ime2nd", WS_POPUP, 0, 0, 0, 0, nullptr,
                           nullptr, inst, nullptr);
}

// Elevated, the hook would also see keys typed into elevated windows, and the exe usually sits in a
// user-writable folder (a non-elevated process could swap it). Never run with admin rights.
bool IsElevated() {
    TOKEN_ELEVATION e{};
    DWORD size = 0;
    return GetTokenInformation(GetCurrentProcessToken(), TokenElevation, &e, sizeof(e), &size) &&
           e.TokenIsElevated;
}

// ---- --selftest: exercises each component and reports to the parent console -------------------

int SelfTest(HINSTANCE inst) {
    // GUI-subsystem exe has no console: report to selftest.txt next to the exe.
    std::wstring path = ExePath();
    const size_t slash = path.find_last_of(L'\\');
    if (slash == std::wstring::npos) return 1;
    path.erase(slash + 1) += L"selftest.txt";
    FILE* f = nullptr;
    if (_wfreopen_s(&f, path.c_str(), L"w", stdout) != 0) return 1;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[selftest] focus a text field within 3 seconds...\n");
    Sleep(3000);

    ImeMode mode = ImeState::QueryNow(100);
    printf("IME state : %s\n", mode == ImeMode::Open     ? "Open"
                              : mode == ImeMode::Closed ? "Closed"
                                                        : "Unknown");
    printf("Conv mode : %lld (raw IMC_GETCONVERSIONMODE; 9 = hiragana, 0 = half-width alphanumeric)\n",
           static_cast<long long>(ImeState::LastConversionMode()));

    // Install the real hook for 20 s: type Shift+letters / anything, move between fields meanwhile.
    // Focus events and the field check (normal / PASSWORD / unknown) are logged with timestamps.
    CreateMainWindow(inst);
    ImeState::Start();
    ImeSwitcher::Start();
    PasswordFocus::Start();
    KeyboardHook::Install();
    g_logStart = GetTickCount64();
    InstallWinEvents();
    MSG msg;
    printf("Hook      : installed for 20 s - type now (Shift+a..z with IME on, normal typing)\n");
    FieldKind shown = FieldKind::Unknown;
    for (ULONGLONG end = GetTickCount64() + 20000; GetTickCount64() < end;) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) DispatchMessageW(&msg);
        if (const FieldKind k = PasswordFocus::Current(); k != shown) {
            shown = k;
            printf("[%6llu ms] field: %s\n", GetTickCount64() - g_logStart, FieldName(k));
        }
        Sleep(5);
    }
    UninstallWinEvents();
    g_logStart = 0;
    KeyboardHook::Uninstall();
    PasswordFocus::Stop();
    ImeSwitcher::Stop();
    ImeState::Stop();
    printf("Hook max callback time: %.1f us (limit before Windows drops the hook: ~300000 us)\n",
           KeyboardHook::MaxCallbackMicros());
    return 0;
}
}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR cmd, int) {
    if (IsElevated()) {
        MessageBoxW(nullptr,
                    L"ime2nd は管理者として実行できません。\n通常の権限で起動してください。",
                    L"ime2nd", MB_ICONERROR);
        return 1;
    }
    if (cmd && wcsstr(cmd, L"--selftest")) return SelfTest(inst);

    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\ime2nd.single-instance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) return 0;

    LoadSettings();
    g_wnd = CreateMainWindow(inst);
    g_taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    if (!g_wnd) return 1;

    ImeState::Start();
    ImeSwitcher::Start();
    PasswordFocus::Start();
    if (!KeyboardHook::Install()) {
        MessageBoxW(nullptr, L"キーボードフックの登録に失敗しました。", L"ime2nd", MB_ICONERROR);
        return 1;
    }
    InstallWinEvents();
    AddTrayIcon();
    SetTimer(g_wnd, kTimerReinstall, 30000, nullptr);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    UninstallWinEvents();
    KeyboardHook::Uninstall();
    PasswordFocus::Stop();
    ImeSwitcher::Stop();  // restores the IME if a session was running
    ImeState::Stop();
    CloseHandle(mutex);
    return 0;
}
