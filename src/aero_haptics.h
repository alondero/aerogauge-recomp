#pragma once
#include <cstdint>

namespace aero::haptics {
struct Motors { uint16_t low, high; };
// These settings are latched before starting the guest.
void configure(bool enabled, bool turbo = true);
void motor(bool on);
Motors sample();
void stop();
}
