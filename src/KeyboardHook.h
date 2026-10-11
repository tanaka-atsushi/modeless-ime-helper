#pragma once
#include "common.h"

namespace KeyboardHook {
bool Install(HWND notifyWnd);
void Uninstall();
// Re-registers the hook (Windows silently drops slow LL hooks). No-op while a session is active.
void Reinstall();
// Forgets a CapsLock trigger press that may have missed its key-up. Call on foreground change and
// when the trigger setting changes (same thread as the hook).
void ResetHeld();
// Text accumulated in the current session. Only touch from the thread that installed the hook.
const std::wstring& Buffer();
// Worst-case hook callback duration since install, in microseconds.
double MaxCallbackMicros();
}  // namespace KeyboardHook
