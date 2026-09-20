#pragma once
#include <cstdint>
#include <SDL.h>

namespace aero::android {
bool initialize();
[[noreturn]] void startup_error(const char* message);
void handle_event(const SDL_Event& event);
void controller_back();
void sample_touch(uint16_t& buttons, int& x, int& y);
void wait_foreground(); // VI boundary; no SDL calls and no runtime locks held.
}
