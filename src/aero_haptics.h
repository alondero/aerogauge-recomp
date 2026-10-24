#pragma once
#include <cstdint>

namespace aero::haptics {
struct Motors { uint16_t low, high; };
// Percentages of the existing motor amplitudes; zero mutes the player/event.
struct Settings {
    int strength = 100;
    bool collision_enabled = true;
    int collision_strength = 100;
    bool turbo_enabled = true;
    int turbo_strength = 100;
    bool operator==(const Settings&) const = default;
};
// These settings are latched before starting the guest.
void configure(bool enabled, bool turbo = true);
void motor(bool on, int player = 0);
void stop_player(int player);
// The SDL thread supplies the player's persisted settings at the host boundary.
Motors sample(int player = 0, Settings settings = {});
void stop();
}
