#pragma once
#include "common.h"

enum class ImeMode { Unknown, Closed, Open };

namespace ImeState {
void Start();
void Stop();
// Asks the worker thread to refresh the cache (cheap, non-blocking). `delayMs` lets a just-pressed
// IME key take effect before the query.
void RequestRefresh(DWORD delayMs = 0);
// Cached state; falls back to a short synchronous query only if the cache is stale.
ImeMode Current();
// Direct query of the foreground window's IME (blocks up to timeoutMs).
ImeMode QueryNow(DWORD timeoutMs);
// Default IME window of the foreground thread's focus window (target of WM_IME_CONTROL).
HWND FocusImeWindow();
// Raw conversion mode seen by the last query (-1 if unknown).
LONG_PTR LastConversionMode();
// While a capture session has switched the IME to alphanumeric, refreshes would read "OFF":
// Freeze(true) keeps the cache unchanged until Freeze(false). Cheap: safe from the hook.
void Freeze(bool on);
// Marks the cache "ON, just checked" (the session end restores the IME). Cheap: safe from the hook.
void MarkOpen();
// IMC_SETCONVERSIONMODE on `ime` (blocks up to 150 ms; not from the hook).
bool SetConversionMode(HWND ime, DWORD_PTR conv);
}  // namespace ImeState
