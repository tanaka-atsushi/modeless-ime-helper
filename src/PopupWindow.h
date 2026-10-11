#pragma once
#include "common.h"

namespace PopupWindow {
bool Create(HINSTANCE inst);
void Destroy();
// Starts showing the popup anchored at `anchor` (screen coords; usually a mouse-position fallback).
void Show(const std::wstring& text, const RECT& anchor);
void SetText(const std::wstring& text);
void MoveToCaret(const RECT& caret);
void Hide();
}  // namespace PopupWindow
