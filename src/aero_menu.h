#ifndef AERO_MENU_H
#define AERO_MENU_H

#include <functional>

union SDL_Event;
struct SDL_Window;

namespace aero::menu {

// RecompFrontend/RmlUi overlay rendered by RT64 on supported host paths.
// Attach before creating the renderer; call update on the SDL/main thread.
void attach(SDL_Window* window);
bool handle_event(const SDL_Event& event);
void toggle();
void report_mod_load_error(const char* message);
bool captures_input();
void update();
// Serialize SDL input/assignment access with the frontend presentation thread.
void run_input_update(const std::function<void()>& update);
void apply_window_settings();
void toggle_fullscreen();

} // namespace aero::menu

#endif
