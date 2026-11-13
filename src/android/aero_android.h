#pragma once
#include <cstdint>
#include <SDL.h>

namespace aero::android {
bool initialize();
[[noreturn]] void startup_error(const char* message);
// Lifecycle publication may run from JNI or SDL's event filter. Pause flushes
// stores before SDL can park; callers must not hold the frontend lock.
void notify_event(const SDL_Event& event);
void flush_stores(); // Thread-safe store barriers; no lifecycle/frontend lock.
void controller_back();
void sample_touch(uint16_t& buttons, int& x, int& y);
}
