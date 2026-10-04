#ifndef AERO_STARTUP_H
#define AERO_STARTUP_H

// Desktop startup contract, owned here rather than in main() so the decision is
// host-testable. The game is the default: a desktop launch starts the game
// without showing the launcher, and AERO_LAUNCHER=1 is the only way to opt back
// in. The same rule applies on Windows and Linux; Android and the headless
// paths never consult it and always auto-start.
//
// Environment contract, in full:
//   AERO_LAUNCHER=1   open the launcher before the game starts (precedence:
//                     nothing overrides it, and nothing re-enables it)
//   AERO_LAUNCHER=0   start the game, which is already the default
//   AERO_AUTOSTART=1  start the game, which is already the default
//   AERO_LAUNCHER     any other value, or unset, starts the game
//
// AERO_LAUNCHER=0 and AERO_AUTOSTART=1 are retained no-op aliases kept because
// existing run scripts set them. They are not startup inputs; do not restore a
// branch that reads them.
//
// The automation variables (AERO_MODERN_MAX_VIS, AERO_WARP, AERO_WARP_AT,
// AERO_CRASH_TEST) have no startup effect either. A finite or scripted run needs
// no launcher escape hatch, because the game is already what it gets.
namespace aero::startup {

enum class Mode { AutoStart, Launcher };

// Pure classification of one AERO_LAUNCHER value. Only the exact string "1"
// selects the launcher, so an absent, empty, or differently spelled value
// starts the game.
Mode desktop_mode(const char* aero_launcher_value);

// Reads AERO_LAUNCHER and classifies it. This is the whole environment read.
Mode desktop_mode();

}  // namespace aero::startup

#endif
