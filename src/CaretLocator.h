#pragma once
#include "common.h"

namespace CaretLocator {
void Start(HWND notifyWnd);
void Stop();
// Asynchronously locates the caret; posts WM_APP_CARET(wParam=gen) when a rectangle was found.
void Request(unsigned gen);
bool GetResult(unsigned& gen, RECT& rc);
// Synchronous variant used by --selftest. Returns which strategy succeeded (nullptr = none).
const wchar_t* LocateNow(RECT& rc);
}  // namespace CaretLocator
