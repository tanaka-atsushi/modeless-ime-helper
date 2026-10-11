#pragma once
#include "common.h"

// Switches the foreground IME to half-width alphanumeric for a capture session and back, from its
// own worker thread. Begin/End are cheap (PostThreadMessage) and meant to be called from the hook.
namespace ImeSwitcher {
void Start();
// Ends a running session (restores the IME) and stops the worker.
void Stop();
// Session start: injects VK_IME_OFF. It joins the input stream ahead of the keys typed after the
// trigger, so they reach the IME in alphanumeric mode and are added to a pending composition as-is.
void Begin();
// Session end: injects VK_IME_ON (ordered after the last typed key), then corrects the conversion
// mode if it was not hiragana. If the foreground changed meanwhile, the saved IME window gets its
// conversion mode back with IMC_SETCONVERSIONMODE instead, so the new window is not touched.
void End();
}  // namespace ImeSwitcher
