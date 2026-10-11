#pragma once
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <atomic>
#include <string>

// dwExtraInfo tag on every event we inject, so our own hook ignores them.
constexpr ULONG_PTR kInjectTag = 0x32656D69;  // 'ime2'

enum : UINT {
    WM_APP_UPDATE = WM_APP + 1,  // wParam: 1 = new session started, lParam unused
    WM_APP_COMMIT,               // lParam: heap std::wstring* (owned by receiver)
    WM_APP_CARET,                // wParam: request generation
    WM_APP_TRAY,
    WM_APP_FOREGROUND,
};

// Pause after every non-alphanumeric character when injecting text (works around Notepad's spell
// check garbling characters that arrive while it is busy; see TextInjector.cpp). Menu-selectable.
constexpr int kDefaultBreakDelayMs = 150;
constexpr int kMaxBreakDelayMs = 1000;

struct Settings {
    std::atomic<bool> enabled{true};
    std::atomic<bool> unknownAsOn{false};  // treat undeterminable IME state as ON
    // Key that starts/holds a capture session: VK_LSHIFT, VK_RSHIFT or VK_CAPITAL (saved in HKCU).
    // With a Shift trigger the other Shift is a plain Shift that uppercases captured letters; with
    // CapsLock either Shift does.
    std::atomic<int> triggerVk{VK_LSHIFT};
    std::atomic<int> breakDelayMs{kDefaultBreakDelayMs};  // 0 = never wait (saved in HKCU)
};
inline Settings g_settings;
