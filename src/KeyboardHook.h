#pragma once
#include "common.h"

namespace KeyboardHook {
// Only the trigger key is ever eaten: pressed while the IME is ON, it starts a session in which
// ImeSwitcher holds the IME in half-width alphanumeric mode and every other key passes through.
bool Install();
void Uninstall();
// Re-registers the hook (Windows silently drops slow LL hooks). While a trigger press is tracked it
// heals a possibly stuck one instead (ends the session) and re-registers on a later tick.
void Reinstall();
// Ends the session and forgets the tracked trigger press (its release may have gone to a window
// the hook can't see). Call on foreground change and trigger change (same thread as the hook).
void ResetHeld();
// Worst-case hook callback duration since install, in microseconds.
double MaxCallbackMicros();
}  // namespace KeyboardHook
