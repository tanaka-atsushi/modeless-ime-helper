#pragma once
#include "common.h"

namespace TextInjector {
// Starts/stops the worker thread that performs injections one at a time, in order.
void Start();
void Stop();
// Queues `text` (non-blocking). The worker closes the IME if it is ON (otherwise it composes or
// reorders the injected characters, e.g. in Excel), types the text as one batch of
// KEYEVENTF_UNICODE events, then restores the IME.
bool Send(const std::wstring& text);
}  // namespace TextInjector
