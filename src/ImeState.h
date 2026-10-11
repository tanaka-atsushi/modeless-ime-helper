#pragma once
#include "common.h"

enum class ImeMode { Unknown, Closed, Open };

namespace ImeState {
void Start();
void Stop();
// Asks the worker thread to refresh the cache (cheap, non-blocking).
void RequestRefresh();
// Cached state; falls back to a short synchronous query only if the cache is stale.
ImeMode Current();
// Direct query of the foreground window's IME (blocks up to timeoutMs).
ImeMode QueryNow(DWORD timeoutMs);

// Temporarily closing the IME so injected text bypasses composition (call from a worker thread:
// both functions block for up to a few hundred ms).
struct Saved {
    HWND imeWnd = nullptr;
    DWORD_PTR conv = 0;
    bool hasConv = false;
    bool valid = false;
};
// If the foreground IME is effectively ON, closes it and fills `saved`; returns true when closed.
bool Suspend(Saved& saved);
// Reopens the IME and restores the conversion mode saved by Suspend. No-op if !saved.valid.
void Resume(const Saved& saved);
}  // namespace ImeState
