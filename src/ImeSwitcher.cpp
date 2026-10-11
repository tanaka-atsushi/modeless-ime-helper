#include "ImeSwitcher.h"
#include <thread>
#include "ImeState.h"

namespace {
enum : UINT {
    kMsgBegin = WM_APP + 100,
    kMsgEnd,
    kMsgQuit,
};
constexpr WORD kVkImeOn = 0x16;   // VK_IME_ON: Microsoft IME enters hiragana mode
constexpr WORD kVkImeOff = 0x1A;  // VK_IME_OFF: Microsoft IME enters half-width alphanumeric mode
constexpr DWORD_PTR kCmodeNative = 0x0001;
constexpr DWORD_PTR kCmodeRoman = 0x0010;
constexpr DWORD_PTR kHiragana = 0x0009;  // IME_CMODE_NATIVE | IME_CMODE_FULLSHAPE
constexpr DWORD kFixDelayMs = 150;       // after VK_IME_ON, before correcting a non-hiragana mode

std::thread g_thread;
DWORD g_tid = 0;

// What Begin saw, for End.
struct Session {
    bool active = false;
    DWORD fgThread = 0;
    HWND imeWnd = nullptr;
    LONG_PTR conv = -1;  // conversion mode before the session (-1 = unknown)
};

void TapKey(WORD vk) {
    INPUT in[2] = {};
    for (INPUT& i : in) {
        i.type = INPUT_KEYBOARD;
        i.ki.wVk = vk;
        i.ki.wScan = static_cast<WORD>(MapVirtualKeyW(vk, MAPVK_VK_TO_VSC));
        i.ki.dwExtraInfo = kInjectTag;
    }
    in[1].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(2, in, sizeof(INPUT));
}

DWORD ForegroundThread() { return GetWindowThreadProcessId(GetForegroundWindow(), nullptr); }

void Begin(Session& s) {
    if (s.active) return;
    // Our VK_IME_OFF must not be cached as "the user turned the IME off". Set here, not in the
    // hook, so it is ordered after the previous session's End (which clears it).
    ImeState::Freeze(true);
    // Inject first: every millisecond before this, a typed letter could still go in as kana.
    TapKey(kVkImeOff);
    s.active = true;
    s.fgThread = ForegroundThread();
    s.imeWnd = ImeState::FocusImeWindow();
    s.conv = ImeState::LastConversionMode();  // read when the trigger press was judged "IME ON"
    // A session only starts with the IME ON, so a value without NATIVE is stale (e.g. a query that
    // finished after our VK_IME_OFF): restoring it would leave the IME in alphanumeric mode.
    if (s.conv >= 0 && !(static_cast<DWORD_PTR>(s.conv) & kCmodeNative)) s.conv = -1;
}

void End(Session& s) {
    if (!s.active) return;
    s.active = false;
    if (ForegroundThread() == s.fgThread) {
        TapKey(kVkImeOn);  // ordered after the keys typed in the session
        if (s.conv >= 0 && (static_cast<DWORD_PTR>(s.conv) & ~kCmodeRoman) != kHiragana) {
            Sleep(kFixDelayMs);  // let the app process VK_IME_ON first, or it would override us
            ImeState::SetConversionMode(s.imeWnd, static_cast<DWORD_PTR>(s.conv));
        }
    } else if (s.conv >= 0) {
        // The keys went elsewhere; put the original window's IME back without touching the new one.
        ImeState::SetConversionMode(s.imeWnd, static_cast<DWORD_PTR>(s.conv));
    }
    ImeState::Freeze(false);
    ImeState::RequestRefresh();
}

void Worker(HANDLE ready) {
    MSG msg;
    PeekMessageW(&msg, nullptr, 0, 0, PM_NOREMOVE);  // create the queue before Start returns
    SetEvent(ready);
    Session s;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        switch (msg.message) {
            case kMsgBegin: Begin(s); break;
            case kMsgEnd: End(s); break;
            case kMsgQuit: End(s); return;
        }
    }
}
}  // namespace

namespace ImeSwitcher {

void Start() {
    HANDLE ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    g_thread = std::thread(Worker, ready);
    g_tid = GetThreadId(g_thread.native_handle());
    WaitForSingleObject(ready, 5000);
    CloseHandle(ready);
}

void Stop() {
    if (!g_thread.joinable()) return;
    PostThreadMessageW(g_tid, kMsgQuit, 0, 0);
    g_thread.join();
}

void Begin() { PostThreadMessageW(g_tid, kMsgBegin, 0, 0); }

void End() {
    ImeState::MarkOpen();  // a quick re-press must still see "ON" while VK_IME_ON is on its way
    PostThreadMessageW(g_tid, kMsgEnd, 0, 0);
}

}  // namespace ImeSwitcher
