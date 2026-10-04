#include "aero_startup.h"

#include <cstdlib>
#include <cstring>

namespace aero::startup {

Mode desktop_mode(const char* aero_launcher_value) {
    return (aero_launcher_value != nullptr && std::strcmp(aero_launcher_value, "1") == 0)
               ? Mode::Launcher
               : Mode::AutoStart;
}

Mode desktop_mode() { return desktop_mode(std::getenv("AERO_LAUNCHER")); }

}  // namespace aero::startup
