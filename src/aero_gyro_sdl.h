#pragma once
#include <SDL.h>
#include "aero_gyro.h"

namespace aero::gyro {
// Call update/steering/calibrate/refresh from the SDL thread under the frontend
// presentation lock. notify_event and suspend accept SDL/Java callback threads.
void notify_event(const SDL_Event& event);
void suspend(bool background);
void update();
float steering(int player);
void calibrate(int player);
bool available(int player);
const char* description(int player);
void shutdown();
}
