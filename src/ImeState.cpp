#include "ImeState.h"
#include <imm.h>
#include <thread>

namespace {
constexpr WPARAM kImcGetConversionMode = 0x0001;
constexpr WPARAM kImcSetConversionMode = 0x0002;
constexpr WPARAM kImcGetOpenStatus = 0x0005;
constexpr DWORD_PTR kCmodeNative = 0x0001;  // IME_CMODE_NATIVE
constexpr ULONGLONG kFreshMs = 400;

std::atomic<int> g_mode{static_cast<int>(ImeMode::Unknown)};
std::atomic<ULONGLONG> g_stamp{0};
std::atomic<LONG_PTR> g_lastConv{-1};  // raw conversion mode from the last query, -1 = unknown
std::atomic<bool> g_frozen{false};     // our own session switched the IME: keep the cache as it is
std::atomic<DWORD> g_delayMs{0};       // wait before the next query (lets an IME key take effect)
HANDLE g_event = nullptr;
std::atomic<bool> g_quit{false};
std::thread g_thread;

void Worker() {
    while (WaitForSingleObject(g_event, INFINITE) == WAIT_OBJECT_0) {
        if (g_quit) break;
        if (DWORD d = g_delayMs.exchange(0)) Sleep(d);
        if (g_frozen) continue;
        ImeMode m = ImeState::QueryNow(100);
        if (g_frozen) continue;  // a session started while we were asking
        g_mode = static_cast<int>(m);
        g_stamp = GetTickCount64();
    }
}

bool Control(HWND ime, WPARAM cmd, LPARAM value, DWORD timeoutMs, UINT extraFlags, DWORD_PTR* out) {
    DWORD_PTR r = 0;
    bool ok = SendMessageTimeoutW(ime, WM_IME_CONTROL, cmd, value, SMTO_ABORTIFHUNG | extraFlags,
                                  timeoutMs, &r) != 0;
    if (out) *out = r;
    return ok;
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

void RequestRefresh(DWORD delayMs) {
    if (delayMs) g_delayMs = delayMs;
    if (g_event) SetEvent(g_event);
}

// ImmGetContext does not work across processes; the default IME window is asked instead.
HWND FocusImeWindow() {
    HWND fg = GetForegroundWindow();
    if (!fg) return nullptr;
    DWORD tid = GetWindowThreadProcessId(fg, nullptr);
    GUITHREADINFO gi{sizeof(gi)};
    HWND target = (GetGUIThreadInfo(tid, &gi) && gi.hwndFocus) ? gi.hwndFocus : fg;
    return ImmGetDefaultIMEWnd(target);
}

ImeMode QueryNow(DWORD timeoutMs) {
    HWND ime = FocusImeWindow();
    if (!ime) return ImeMode::Unknown;
    DWORD_PTR open = 0;
    // SMTO_BLOCK: no re-entry into the hook while waiting.
    if (!Control(ime, kImcGetOpenStatus, 0, timeoutMs, SMTO_BLOCK, &open)) return ImeMode::Unknown;
    if (!open) return ImeMode::Closed;
    // The Windows 11 Microsoft IME stays "open" in its direct-input (A) mode and only clears
    // IME_CMODE_NATIVE in the conversion mode, so open status alone reports ON when it is off.
    DWORD_PTR conv = 0;
    if (Control(ime, kImcGetConversionMode, 0, timeoutMs, SMTO_BLOCK, &conv)) {
        g_lastConv = static_cast<LONG_PTR>(conv);
        if (!(conv & kCmodeNative)) return ImeMode::Closed;
    } else {
        g_lastConv = -1;
    }
    return ImeMode::Open;
}

ImeMode Current() {
    ULONGLONG age = GetTickCount64() - g_stamp.load();
    if (age > kFreshMs && !g_frozen) {
        ImeMode m = QueryNow(30);
        g_mode = static_cast<int>(m);
        g_stamp = GetTickCount64();
        return m;
    }
    return static_cast<ImeMode>(g_mode.load());
}

LONG_PTR LastConversionMode() { return g_lastConv.load(); }

void Freeze(bool on) { g_frozen = on; }

void MarkOpen() {
    g_mode = static_cast<int>(ImeMode::Open);
    g_stamp = GetTickCount64();
}

bool SetConversionMode(HWND ime, DWORD_PTR conv) {
    return ime && Control(ime, kImcSetConversionMode, static_cast<LPARAM>(conv), 150, 0, nullptr);
}

}  // namespace ImeState
