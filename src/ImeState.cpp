#include "ImeState.h"
#include <imm.h>
#include <thread>

namespace {
constexpr WPARAM kImcGetConversionMode = 0x0001;
constexpr WPARAM kImcSetConversionMode = 0x0002;
constexpr WPARAM kImcGetOpenStatus = 0x0005;
constexpr WPARAM kImcSetOpenStatus = 0x0006;
constexpr DWORD_PTR kCmodeNative = 0x0001;  // IME_CMODE_NATIVE
constexpr ULONGLONG kFreshMs = 400;
constexpr DWORD kSwitchWaitMs = 150;  // how long Suspend/Resume wait for the IME to follow

std::atomic<int> g_mode{static_cast<int>(ImeMode::Unknown)};
std::atomic<ULONGLONG> g_stamp{0};
HANDLE g_event = nullptr;
std::atomic<bool> g_quit{false};
std::thread g_thread;

void Worker() {
    while (WaitForSingleObject(g_event, INFINITE) == WAIT_OBJECT_0) {
        if (g_quit) break;
        ImeMode m = ImeState::QueryNow(100);
        g_mode = static_cast<int>(m);
        g_stamp = GetTickCount64();
    }
}

// ImmGetContext does not work across processes; the default IME window is asked instead.
HWND FindImeWindow() {
    HWND fg = GetForegroundWindow();
    if (!fg) return nullptr;
    DWORD tid = GetWindowThreadProcessId(fg, nullptr);
    GUITHREADINFO gi{sizeof(gi)};
    HWND target = (GetGUIThreadInfo(tid, &gi) && gi.hwndFocus) ? gi.hwndFocus : fg;
    return ImmGetDefaultIMEWnd(target);
}

bool Control(HWND ime, WPARAM cmd, LPARAM value, DWORD timeoutMs, UINT extraFlags, DWORD_PTR* out) {
    DWORD_PTR r = 0;
    bool ok = SendMessageTimeoutW(ime, WM_IME_CONTROL, cmd, value, SMTO_ABORTIFHUNG | extraFlags,
                                  timeoutMs, &r) != 0;
    if (out) *out = r;
    return ok;
}

bool WaitOpenStatus(HWND ime, bool want, DWORD budgetMs) {
    for (ULONGLONG end = GetTickCount64() + budgetMs;;) {
        DWORD_PTR open = 0;
        if (Control(ime, kImcGetOpenStatus, 0, 100, 0, &open) && (open != 0) == want) return true;
        if (GetTickCount64() >= end) return false;
        Sleep(5);
    }
}
}  // namespace

namespace ImeState {

void Start() {
    g_quit = false;
    g_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    g_thread = std::thread(Worker);
    RequestRefresh();
}

void Stop() {
    g_quit = true;
    if (g_event) SetEvent(g_event);
    if (g_thread.joinable()) g_thread.join();
    if (g_event) CloseHandle(g_event);
    g_event = nullptr;
}

void RequestRefresh() {
    if (g_event) SetEvent(g_event);
}

ImeMode QueryNow(DWORD timeoutMs) {
    HWND ime = FindImeWindow();
    if (!ime) return ImeMode::Unknown;
    DWORD_PTR open = 0;
    // SMTO_BLOCK: no re-entry into the hook while waiting.
    if (!Control(ime, kImcGetOpenStatus, 0, timeoutMs, SMTO_BLOCK, &open)) return ImeMode::Unknown;
    if (!open) return ImeMode::Closed;
    // The Windows 11 Microsoft IME stays "open" in its direct-input (A) mode and only clears
    // IME_CMODE_NATIVE in the conversion mode, so open status alone reports ON when it is off.
    DWORD_PTR conv = 0;
    if (Control(ime, kImcGetConversionMode, 0, timeoutMs, SMTO_BLOCK, &conv) && !(conv & kCmodeNative))
        return ImeMode::Closed;
    return ImeMode::Open;
}

ImeMode Current() {
    ULONGLONG age = GetTickCount64() - g_stamp.load();
    if (age > kFreshMs) {
        ImeMode m = QueryNow(30);
        g_mode = static_cast<int>(m);
        g_stamp = GetTickCount64();
        return m;
    }
    return static_cast<ImeMode>(g_mode.load());
}

bool Suspend(Saved& saved) {
    saved = {};
    HWND ime = FindImeWindow();
    if (!ime) return false;
    DWORD_PTR open = 0, conv = 0;
    if (!Control(ime, kImcGetOpenStatus, 0, 100, 0, &open) || !open) return false;
    bool hasConv = Control(ime, kImcGetConversionMode, 0, 100, 0, &conv);
    if (hasConv && !(conv & kCmodeNative)) return false;  // already in direct-input mode
    saved.imeWnd = ime;
    saved.conv = conv;
    saved.hasConv = hasConv;
    saved.valid = true;  // from here on Resume restores even if the close could not be confirmed
    Control(ime, kImcSetOpenStatus, 0, kSwitchWaitMs, 0, nullptr);
    WaitOpenStatus(ime, false, kSwitchWaitMs);
    return true;
}

void Resume(const Saved& saved) {
    if (!saved.valid) return;
    Control(saved.imeWnd, kImcSetOpenStatus, 1, kSwitchWaitMs, 0, nullptr);
    if (saved.hasConv)
        Control(saved.imeWnd, kImcSetConversionMode, static_cast<LPARAM>(saved.conv), kSwitchWaitMs, 0,
                nullptr);
    WaitOpenStatus(saved.imeWnd, true, kSwitchWaitMs);
    // Keep the hook's cache from reporting the transient "closed" state, then let the worker verify.
    g_mode = static_cast<int>(ImeMode::Open);
    g_stamp = GetTickCount64();
    RequestRefresh();
}

}  // namespace ImeState
