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

// dwExtraInfo tag on every event we inject (only the Caps Lock reset tap), so our hook ignores it.
constexpr ULONG_PTR kInjectTag = 0x32656D69;  // 'ime2'

enum : UINT {
    WM_APP_TRAY = WM_APP + 1,
};

struct Settings {
    std::atomic<bool> enabled{true};
    std::atomic<bool> unknownAsOn{false};  // treat undeterminable IME state as ON
    // Key that starts/holds a capture session: VK_LSHIFT, VK_RSHIFT or VK_CAPITAL (saved in HKCU).
    // With a Shift trigger the other Shift is a plain Shift that uppercases captured letters; with
    // CapsLock either Shift does.
    std::atomic<int> triggerVk{VK_LSHIFT};
};
inline Settings g_settings;
