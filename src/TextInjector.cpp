#include "TextInjector.h"
#include <algorithm>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>
#include "ImeState.h"

namespace {
// SendInput only queues the events; the app consumes them later and there is no completion signal.
// The IME must stay closed until then, so wait a little before reopening it.
constexpr DWORD kSettleBaseMs = 30;
constexpr DWORD kSettlePerCharMs = 3;
constexpr DWORD kSettleMaxMs = 250;

std::mutex g_mu;
std::condition_variable g_cv;
std::deque<std::wstring> g_queue;
bool g_quit = false;
std::thread g_thread;

// Notepad's spell check starts when a space or punctuation mark ends a word and keeps its thread
// busy for ~30-40 ms. VK_PACKET characters that pile up in that window all come out as the last
// character ("hoge hoge" -> "hoge eeee") or get dropped. Space , . ; : ! ? " ( ) / trigger it,
// while _ - ' @ * & # % + = do not; which ones depend on the app's word breaker, so every
// character that is not a letter or digit is treated as a possible word end. The text is sent in
// chunks that each end with such a character, with this pause after every chunk.
// The busy time grows with the document: on a short document 40 ms was clean, on a ~100-line one
// 40 ms failed 3/10 and 60 ms 1/10, 80 ms was clean for letters but failed 2/5 once punctuation
// was mixed in, and 120/200 ms were clean. The default (kDefaultBreakDelayMs = 150 ms) leaves some
// margin; a longer document or a slower PC may need more, so the value is chosen in the tray menu
// (g_settings.breakDelayMs, 0 = no pause). Idea: detect idle instead of waiting a fixed time.

bool IsWordChar(wchar_t c) {
    return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9');
}

void SendChars(const std::wstring& text, size_t from, size_t to) {
    std::vector<INPUT> in;
    in.reserve((to - from) * 2);
    for (size_t k = from; k < to; ++k) {
        INPUT i{};
        i.type = INPUT_KEYBOARD;
        i.ki.wScan = text[k];
        i.ki.dwFlags = KEYEVENTF_UNICODE;
        i.ki.dwExtraInfo = kInjectTag;
        in.push_back(i);
        i.ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
        in.push_back(i);
    }
    SendInput(static_cast<UINT>(in.size()), in.data(), sizeof(INPUT));
}

void SendBatch(const std::wstring& text) {
    size_t start = 0;
    while (start < text.size()) {
        size_t end = start;
        while (end < text.size() && IsWordChar(text[end])) ++end;  // letters/digits of one word
        const bool endsWithBreak = end < text.size();
        if (endsWithBreak) ++end;  // a chunk ends right after the first non-word character
        SendChars(text, start, end);
        start = end;
        // Also after a trailing break, so the IME is not reopened while the app is still busy.
        const int pauseMs = g_settings.breakDelayMs.load();
        if (endsWithBreak && pauseMs > 0) Sleep(static_cast<DWORD>(pauseMs));
    }
}

void Inject(const std::wstring& text) {
    ImeState::Saved saved;
    const bool closed = ImeState::Suspend(saved);
    SendBatch(text);
    if (closed) {
        Sleep(std::min<DWORD>(kSettleMaxMs, kSettleBaseMs + kSettlePerCharMs * static_cast<DWORD>(text.size())));
        ImeState::Resume(saved);
    }
}

void Worker() {
    std::unique_lock<std::mutex> lock(g_mu);
    for (;;) {
        g_cv.wait(lock, [] { return g_quit || !g_queue.empty(); });
        if (g_queue.empty()) return;  // quit requested and nothing left to type
        std::wstring text = std::move(g_queue.front());
        g_queue.pop_front();
        lock.unlock();
        Inject(text);
        lock.lock();
    }
}
}  // namespace

namespace TextInjector {

void Start() {
    g_quit = false;
    g_thread = std::thread(Worker);
}

void Stop() {
    {
        std::lock_guard<std::mutex> lock(g_mu);
        g_quit = true;
    }
    g_cv.notify_all();
    if (g_thread.joinable()) g_thread.join();
}

bool Send(const std::wstring& text) {
    if (text.empty()) return true;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        g_queue.push_back(text);
    }
    g_cv.notify_one();
    return true;
}

}  // namespace TextInjector
