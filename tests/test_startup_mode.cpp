#undef NDEBUG
#include <cassert>
#include <cstdio>
#include <cstdlib>

#include "aero_startup.h"

#if defined(_WIN32)
#define aero_test_setenv(name, value) _putenv_s((name), (value))
#define aero_test_unsetenv(name) _putenv_s((name), "")
#else
#define aero_test_setenv(name, value) setenv((name), (value), 1)
#define aero_test_unsetenv(name) unsetenv(name)
#endif

using aero::startup::Mode;
using aero::startup::desktop_mode;

// Guards the desktop startup default (PR #91). Before it, a bare launch showed
// the launcher and these assertions failed: the game is now what an unset
// environment gets, and AERO_LAUNCHER=1 is the only way back to the launcher.
int main() {
    // The default. An absent variable is the path every player now takes.
    assert(desktop_mode(nullptr) == Mode::AutoStart);

    // The single opt-in. Nothing overrides it, and nothing else reaches it.
    assert(desktop_mode("1") == Mode::Launcher);

    // Only the exact string "1" opens the launcher. This keeps a stray value
    // from silently stranding a player on a screen they did not ask for.
    assert(desktop_mode("0") == Mode::AutoStart);
    assert(desktop_mode("") == Mode::AutoStart);
    assert(desktop_mode("01") == Mode::AutoStart);
    assert(desktop_mode("1 ") == Mode::AutoStart);
    assert(desktop_mode(" 1") == Mode::AutoStart);
    assert(desktop_mode("true") == Mode::AutoStart);
    assert(desktop_mode("yes") == Mode::AutoStart);

    // The same decision made through the real environment. This is the part
    // that regressed: AERO_AUTOSTART, AERO_LAUNCHER=0, AERO_MODERN_MAX_VIS,
    // AERO_WARP, AERO_WARP_AT, and AERO_CRASH_TEST all used to be read here.
    // None of them selects the launcher any more, and none of them is required
    // to start the game, so a run script may set any combination of them.
    static const char* const kIgnored[] = {
        "AERO_AUTOSTART", "AERO_MODERN_MAX_VIS", "AERO_WARP",
        "AERO_WARP_AT",   "AERO_CRASH_TEST",
    };
    aero_test_unsetenv("AERO_LAUNCHER");
    for (const char* name : kIgnored) {
        aero_test_setenv(name, "1");
        assert(desktop_mode() == Mode::AutoStart);
    }
    // AERO_LAUNCHER=0 is the retained alias for the same default.
    aero_test_setenv("AERO_LAUNCHER", "0");
    assert(desktop_mode() == Mode::AutoStart);

    // AERO_LAUNCHER=1 still wins while every one of those is set.
    aero_test_setenv("AERO_LAUNCHER", "1");
    for (const char* name : kIgnored) {
        aero_test_setenv(name, "1");
        assert(desktop_mode() == Mode::Launcher);
    }

    // Leave the process environment as it was found.
    aero_test_unsetenv("AERO_LAUNCHER");
    for (const char* name : kIgnored) aero_test_unsetenv(name);
    assert(desktop_mode() == Mode::AutoStart);

    std::printf("startup mode: default AutoStart, AERO_LAUNCHER=1 Launcher\n");
    return 0;
}
