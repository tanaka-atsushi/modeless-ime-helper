#pragma once
#include "common.h"

// Is the focused field a password field? Chromium, WPF, WinUI, ... draw their fields inside one
// window and keep the window's IME "ON" in a password field, so ImeState can't tell. UI Automation
// (IsPassword of the focused element) can. Queried on a worker thread after each focus change;
// the hook only reads the cached answer.
enum class FieldKind { Unknown, Normal, Password };

namespace PasswordFocus {
void Start();
void Stop();
// Focus moved (EVENT_OBJECT_FOCUS / foreground change): the field counts as Unknown until the
// worker has checked it. Cheap, non-blocking; call from the main thread only.
void FocusChanged();
// Cached answer for the focused field. Cheap: safe from the hook.
FieldKind Current();
}  // namespace PasswordFocus
