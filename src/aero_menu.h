#ifndef AERO_MENU_H
#define AERO_MENU_H

union SDL_Event;
struct SDL_Window;

namespace aero::menu {

// RecompFrontend/RmlUi overlay rendered by RT64 on supported host paths.
// Attach before creating the renderer; call update on the SDL/main thread.
void attach(SDL_Window* window);
bool handle_event(const SDL_Event& event);
void toggle();
bool captures_input();
void update();
void apply_window_settings();
void toggle_fullscreen();

} // namespace aero::menu

#endif
