#include "PopupWindow.h"
#include <ShellScalingApi.h>

namespace {
const wchar_t kClass[] = L"ime2nd.Popup";
HWND g_wnd = nullptr;
HFONT g_font = nullptr;
UINT g_fontDpi = 0;
std::wstring g_text;
RECT g_anchor{};

void EnsureFont(UINT dpi) {
    if (g_font && g_fontDpi == dpi) return;
    if (g_font) DeleteObject(g_font);
    g_font = CreateFontW(-MulDiv(14, dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                         DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                         CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    g_fontDpi = dpi;
}

// Resizes to fit the text and positions just below the anchor (above it near the screen bottom).
void Place() {
    HMONITOR mon = MonitorFromRect(&g_anchor, MONITOR_DEFAULTTONEAREST);
    UINT dx = 96, dy = 96;
    if (FAILED(GetDpiForMonitor(mon, MDT_EFFECTIVE_DPI, &dx, &dy))) dx = 96;
    EnsureFont(dx);

    HDC dc = GetDC(g_wnd);
    HGDIOBJ old = SelectObject(dc, g_font);
    SIZE sz{};
    const std::wstring& t = g_text.empty() ? std::wstring(L" ") : g_text;
    GetTextExtentPoint32W(dc, t.c_str(), static_cast<int>(t.size()), &sz);
    SelectObject(dc, old);
    ReleaseDC(g_wnd, dc);

    const int padX = MulDiv(8, dx, 96), padY = MulDiv(4, dx, 96), gap = MulDiv(4, dx, 96);
    int w = sz.cx + padX * 2, h = sz.cy + padY * 2;

    MONITORINFO mi{sizeof(mi)};
    GetMonitorInfoW(mon, &mi);
    int x = g_anchor.left;
    int y = g_anchor.bottom + gap;
    if (y + h > mi.rcWork.bottom) y = g_anchor.top - h - gap;
    if (x + w > mi.rcWork.right) x = mi.rcWork.right - w;
    if (x < mi.rcWork.left) x = mi.rcWork.left;
    if (y < mi.rcWork.top) y = mi.rcWork.top;

    SetWindowPos(g_wnd, HWND_TOPMOST, x, y, w, h, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    InvalidateRect(g_wnd, nullptr, TRUE);
}

LRESULT CALLBACK Proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
        case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
        case WM_NCHITTEST: return HTTRANSPARENT;
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(h, &ps);
            RECT rc;
            GetClientRect(h, &rc);
            HBRUSH bg = CreateSolidBrush(RGB(32, 32, 36));
            FillRect(dc, &rc, bg);
            DeleteObject(bg);
            HBRUSH border = CreateSolidBrush(RGB(0, 120, 215));
            FrameRect(dc, &rc, border);
            DeleteObject(border);
            HGDIOBJ old = SelectObject(dc, g_font);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, RGB(245, 245, 245));
            DrawTextW(dc, g_text.c_str(), static_cast<int>(g_text.size()), &rc,
                      DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_NOPREFIX);
            SelectObject(dc, old);
            EndPaint(h, &ps);
            return 0;
        }
    }
    return DefWindowProcW(h, m, w, l);
}
}  // namespace

namespace PopupWindow {

bool Create(HINSTANCE inst) {
    WNDCLASSW wc{};
    wc.lpfnWndProc = Proc;
    wc.hInstance = inst;
    wc.lpszClassName = kClass;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassW(&wc);
    g_wnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, kClass, L"",
                            WS_POPUP, 0, 0, 10, 10, nullptr, nullptr, inst, nullptr);
    return g_wnd != nullptr;
}

void Destroy() {
    if (g_wnd) DestroyWindow(g_wnd);
    g_wnd = nullptr;
    if (g_font) DeleteObject(g_font);
    g_font = nullptr;
}

void Show(const std::wstring& text, const RECT& anchor) {
    g_text = text;
    g_anchor = anchor;
    Place();
}

void SetText(const std::wstring& text) {
    g_text = text;
    Place();
}

void MoveToCaret(const RECT& caret) {
    if (!IsWindowVisible(g_wnd)) return;
    g_anchor = caret;
    Place();
}

void Hide() {
    if (g_wnd) ShowWindow(g_wnd, SW_HIDE);
}

}  // namespace PopupWindow
