#include "PasswordFocus.h"
#include <ole2.h>  // COM basics that WIN32_LEAN_AND_MEAN (common.h) leaves out of windows.h
#include <uiautomation.h>
#include <thread>

namespace {
// Focus sequence number and the answer for it, packed so the worker can store its answer only if
// focus hasn't moved since it started asking (compare-exchange): bits 0-1 = FieldKind, rest = seq.
std::atomic<uint32_t> g_word{0};  // seq 0, Unknown
constexpr uint32_t kKindMask = 3;
constexpr DWORD kUiaTimeoutMs = 2000;  // a hung app must not hold the worker (or Stop) for long

HANDLE g_event = nullptr;
std::atomic<bool> g_quit{false};
std::thread g_thread;

FieldKind Query(IUIAutomation* uia, IUIAutomationCacheRequest* cache) {
    // Our own windows (tray menu): don't ask UIA to call back into this process.
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    if (pid == GetCurrentProcessId()) return FieldKind::Unknown;

    IUIAutomationElement* el = nullptr;
    if (FAILED(uia->GetFocusedElementBuildCache(cache, &el)) || !el) return FieldKind::Unknown;
    BOOL pw = FALSE;
    const HRESULT hr = el->get_CachedIsPassword(&pw);
    el->Release();
    if (FAILED(hr)) return FieldKind::Unknown;
    return pw ? FieldKind::Password : FieldKind::Normal;
}

void Worker() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    // CUIAutomation8 provides IUIAutomation2 (timeouts); the defaults are long.
    IUIAutomation* uia = nullptr;
    if (FAILED(CoCreateInstance(CLSID_CUIAutomation8, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&uia))))
        CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&uia));
    IUIAutomation2* uia2 = nullptr;
    if (uia && SUCCEEDED(uia->QueryInterface(IID_PPV_ARGS(&uia2)))) {
        uia2->put_ConnectionTimeout(kUiaTimeoutMs);
        uia2->put_TransactionTimeout(kUiaTimeoutMs);
        uia2->Release();
    }
    IUIAutomationCacheRequest* cache = nullptr;
    if (uia && SUCCEEDED(uia->CreateCacheRequest(&cache)))
        cache->AddProperty(UIA_IsPasswordPropertyId);

    while (WaitForSingleObject(g_event, INFINITE) == WAIT_OBJECT_0 && !g_quit) {
        uint32_t w = g_word.load();
        if ((w & kKindMask) != static_cast<uint32_t>(FieldKind::Unknown)) continue;
        const FieldKind k = (uia && cache) ? Query(uia, cache) : FieldKind::Unknown;
        // Fails (keeps Unknown) if FocusChanged ran meanwhile; its event makes us ask again.
        g_word.compare_exchange_strong(w, (w & ~kKindMask) | static_cast<uint32_t>(k));
    }

    if (cache) cache->Release();
    if (uia) uia->Release();
    CoUninitialize();
}
}  // namespace

namespace PasswordFocus {

void Start() {
    g_quit = false;
    g_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    g_thread = std::thread(Worker);
    FocusChanged();  // check the field focused at startup
}

void Stop() {
    g_quit = true;
    if (g_event) SetEvent(g_event);
    if (g_thread.joinable()) g_thread.join();
    if (g_event) CloseHandle(g_event);
    g_event = nullptr;
}

void FocusChanged() {
    // Only the main thread advances the sequence, so a plain store is enough.
    g_word.store(((g_word.load() >> 2) + 1) << 2 | static_cast<uint32_t>(FieldKind::Unknown));
    if (g_event) SetEvent(g_event);
}

FieldKind Current() { return static_cast<FieldKind>(g_word.load() & kKindMask); }

}  // namespace PasswordFocus
