#include "CaretLocator.h"
#include <objbase.h>  // defines `interface`, which UIAutomation.h needs under WIN32_LEAN_AND_MEAN
#include <UIAutomation.h>
#include <oleacc.h>
#include <wrl/client.h>
#include <mutex>
#include <thread>

namespace {

// ---- 1. UI Automation: TextPattern selection -> bounding rectangle ----------------------------

bool FirstRect(IUIAutomationTextRange* range, RECT& out) {
    SAFEARRAY* sa = nullptr;
    if (FAILED(range->GetBoundingRectangles(&sa)) || !sa) return false;
    bool ok = false;
    double* d = nullptr;
    long lo = 0, hi = -1;
    SafeArrayGetLBound(sa, 1, &lo);
    SafeArrayGetUBound(sa, 1, &hi);
    if (hi - lo + 1 >= 4 && SUCCEEDED(SafeArrayAccessData(sa, reinterpret_cast<void**>(&d)))) {
        // Layout: left, top, width, height (repeated per line)
        if (d[3] > 0) {
            out = {(LONG)d[0], (LONG)d[1], (LONG)(d[0] + d[2]), (LONG)(d[1] + d[3])};
            ok = true;
        }
        SafeArrayUnaccessData(sa);
    }
    SafeArrayDestroy(sa);
    return ok;
}

bool UiaCaret(IUIAutomation* uia, RECT& rc) {
    Microsoft::WRL::ComPtr<IUIAutomationElement> el;
    if (FAILED(uia->GetFocusedElement(&el)) || !el) return false;
    BOOL pw = FALSE;
    el->get_CurrentIsPassword(&pw);
    if (pw) return false;

    Microsoft::WRL::ComPtr<IUIAutomationTextPattern> tp;
    if (FAILED(el->GetCurrentPatternAs(UIA_TextPatternId, IID_PPV_ARGS(&tp))) || !tp) return false;
    Microsoft::WRL::ComPtr<IUIAutomationTextRangeArray> sel;
    if (FAILED(tp->GetSelection(&sel)) || !sel) return false;
    int n = 0;
    sel->get_Length(&n);
    if (n < 1) return false;
    Microsoft::WRL::ComPtr<IUIAutomationTextRange> r;
    if (FAILED(sel->GetElement(0, &r)) || !r) return false;

    RECT b;
    if (FirstRect(r.Get(), b)) { rc = {b.left, b.top, b.left + 1, b.bottom}; return true; }

    // Collapsed caret with no rectangle: borrow the previous character's right edge...
    Microsoft::WRL::ComPtr<IUIAutomationTextRange> c;
    if (SUCCEEDED(r->Clone(&c)) && c) {
        int moved = 0;
        c->MoveEndpointByUnit(TextPatternRangeEndpoint_Start, TextUnit_Character, -1, &moved);
        if (moved != 0 && FirstRect(c.Get(), b)) { rc = {b.right, b.top, b.right + 1, b.bottom}; return true; }
    }
    // ...or the next character's left edge.
    c.Reset();
    if (SUCCEEDED(r->Clone(&c)) && c) {
        int moved = 0;
        c->MoveEndpointByUnit(TextPatternRangeEndpoint_End, TextUnit_Character, 1, &moved);
        if (moved != 0 && FirstRect(c.Get(), b)) { rc = {b.left, b.top, b.left + 1, b.bottom}; return true; }
    }
    return false;
}

// ---- 2. MSAA OBJID_CARET (Chrome exposes this where UIA gives nothing, e.g. Google Docs) -----

bool MsaaCaret(HWND focus, RECT& rc) {
    Microsoft::WRL::ComPtr<IAccessible> acc;
    if (FAILED(AccessibleObjectFromWindow(focus, static_cast<DWORD>(OBJID_CARET), IID_IAccessible,
                                          reinterpret_cast<void**>(acc.GetAddressOf()))) || !acc)
        return false;
    long l = 0, t = 0, w = 0, h = 0;
    VARIANT self;
    VariantInit(&self);
    self.vt = VT_I4;
    self.lVal = static_cast<LONG>(CHILDID_SELF);
    if (FAILED(acc->accLocation(&l, &t, &w, &h, self))) return false;
    if (h <= 0 || h >= 200 || (l == 0 && t == 0)) return false;  // 0,0,0,0 means "no caret"
    rc = {l, t, l + (w > 0 ? w : 1), t + h};
    return true;
}

// ---- 3. Classic Win32 caret ------------------------------------------------------------------

bool Win32Caret(const GUITHREADINFO& gi, RECT& rc) {
    if (!gi.hwndCaret) return false;
    RECT c = gi.rcCaret;
    if (c.right <= c.left && c.bottom <= c.top) return false;
    POINT tl{c.left, c.top}, br{c.right, c.bottom};
    if (!ClientToScreen(gi.hwndCaret, &tl) || !ClientToScreen(gi.hwndCaret, &br)) return false;
    rc = {tl.x, tl.y, br.x, br.y};
    return true;
}

Microsoft::WRL::ComPtr<IUIAutomation> g_uia;  // created lazily on the worker thread

const wchar_t* Locate(RECT& rc) {
    HWND fg = GetForegroundWindow();
    if (!fg) return nullptr;
    GUITHREADINFO gi{sizeof(gi)};
    GetGUIThreadInfo(GetWindowThreadProcessId(fg, nullptr), &gi);
    HWND focus = gi.hwndFocus ? gi.hwndFocus : fg;

    if (!g_uia)
        CoCreateInstance(__uuidof(CUIAutomation), nullptr, CLSCTX_INPROC_SERVER,
                         IID_PPV_ARGS(&g_uia));
    if (g_uia && UiaCaret(g_uia.Get(), rc)) return L"UI Automation";
    if (MsaaCaret(focus, rc)) return L"MSAA";
    if (Win32Caret(gi, rc)) return L"GetGUIThreadInfo";
    return nullptr;
}

// ---- worker plumbing -------------------------------------------------------------------------

HWND g_notify = nullptr;
HANDLE g_event = nullptr;
std::atomic<bool> g_quit{false};
std::atomic<unsigned> g_requested{0};
std::mutex g_mu;
unsigned g_doneGen = 0;
RECT g_doneRect{};
std::thread g_thread;

void Worker() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    while (WaitForSingleObject(g_event, INFINITE) == WAIT_OBJECT_0 && !g_quit) {
        unsigned gen = g_requested.load();  // latest request wins
        RECT rc{};
        if (Locate(rc)) {
            {
                std::lock_guard<std::mutex> lk(g_mu);
                g_doneGen = gen;
                g_doneRect = rc;
            }
            PostMessageW(g_notify, WM_APP_CARET, gen, 0);
        }
    }
    g_uia.Reset();
    CoUninitialize();
}
}  // namespace

namespace CaretLocator {

void Start(HWND notifyWnd) {
    g_notify = notifyWnd;
    g_quit = false;
    g_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    g_thread = std::thread(Worker);
}

void Stop() {
    g_quit = true;
    if (g_event) SetEvent(g_event);
    if (g_thread.joinable()) g_thread.join();
    if (g_event) CloseHandle(g_event);
    g_event = nullptr;
}

void Request(unsigned gen) {
    g_requested = gen;
    if (g_event) SetEvent(g_event);
}

bool GetResult(unsigned& gen, RECT& rc) {
    std::lock_guard<std::mutex> lk(g_mu);
    gen = g_doneGen;
    rc = g_doneRect;
    return gen != 0;
}

const wchar_t* LocateNow(RECT& rc) {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const wchar_t* how = Locate(rc);
    g_uia.Reset();
    CoUninitialize();
    return how;
}

}  // namespace CaretLocator
