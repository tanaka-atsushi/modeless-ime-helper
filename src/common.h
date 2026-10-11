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

// dwExtraInfo tag on every event we inject (VK_IME_ON/OFF), so our hook ignores it.
constexpr ULONG_PTR kInjectTag = 0x32656D69;  // 'ime2'

enum : UINT {
    WM_APP_TRAY = WM_APP + 1,
};

// Keys that can be chosen as the trigger (tray menu, saved as its vk). `scan` != 0: also matched by
// scan code, for JIS keys whose virtual key depends on the layout and modifiers.
struct TriggerKey {
    int vk;
    DWORD scan;
    const wchar_t* name;  // tray tooltip and menu
    const wchar_t* note;  // appended to the menu label ("" = none)
};
inline constexpr TriggerKey kTriggerKeys[] = {
    {VK_LSHIFT, 0, L"左 Shift", L""},
    {VK_RSHIFT, 0, L"右 Shift", L""},
    {VK_NONCONVERT, 0x7B, L"無変換", L"（IME が ON の間は無変換キーとして使えません）"},
    {VK_CONVERT, 0x79, L"変換", L"（IME が ON の間は変換キーとして使えません）"},
    {0xF2 /* VK_DBE_HIRAGANA */, 0x70, L"カタカナひらがな", L"（IME が ON の間はこのキーの機能は使えません）"},
};
inline const TriggerKey* FindTriggerKey(int vk) {
    for (const TriggerKey& t : kTriggerKeys)
        if (t.vk == vk) return &t;
    return nullptr;
}

struct Settings {
    std::atomic<bool> enabled{true};
    // Key that starts/holds a capture session: the vk of one of kTriggerKeys (saved in HKCU).
    std::atomic<int> triggerVk{VK_LSHIFT};
};
inline Settings g_settings;
