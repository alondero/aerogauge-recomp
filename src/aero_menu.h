#ifndef AERO_MENU_H
#define AERO_MENU_H

union SDL_Event;
struct SDL_Window;

namespace aero::menu {

// Attach the native quick-access menu bar where the platform supports it
// (currently Win32). All menu actions update the live renderer and graphics.json.
void attach(SDL_Window* window);
bool handle_event(const SDL_Event& event);
void toggle_fullscreen();

} // namespace aero::menu

#endif
