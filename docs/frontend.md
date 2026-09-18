# Portable settings menu

The settings screen uses RecompFrontend, the same shared menu library used by the
Automobili Lamborghini port. It is drawn by RT64, so it works on Windows and Linux.
RecompFrontend is pinned to `b1a1477`. N64ModernRuntime is pinned to `cdf5abb` for
the configuration API. Fonts and other menu files are in `assets/frontend`.

The screen has three pages, in this tab order:

- **Graphics:** resolution, supersampling, aspect ratio, HUD position, refresh rate,
  manual FPS, MSAA, precision, window mode, window size presets, graphics API,
  and texture pack/dump paths.
- **Enhancements:** draw distance (unlimited or a multiplier), full-course geometry,
  and Easy Turbo/Boost Start.
- **Debug:** the RT64 developer overlay (moved off Graphics; takes effect after a
  restart). Created last so it renders as the right-most tab.

The window size is a preset picker (640x360 through 3840x2160, 16:10 and 4:3
favourites included) applied with the Graphics page's Apply button, like the
rest of that page; a window size typed directly into graphics.json shows as
Custom and is kept on Apply unless a preset is picked. Enums with more than
four options
(such as the resolution picker) render as one-at-a-time cyclers: the selected
value sits between two arrow buttons and pressing left/right rotates through the
options (the RecompUI framework gained this widget for this port via patch 0016).
Draw distance offers an explicit
Unlimited choice that maps to the internal 0 sentinel (no far clipping plane);
when it is off, the far-plane multiplier slider applies, stepping in 10x
increments from 1. The Easy Turbo entry
describes both triggers: hold Accelerate through the countdown for the Boost
Start, and press the dedicated Turbo button (R or E on keyboard, right trigger
or right shoulder on gamepad) during player-1 races.

Fog and sky settings from the Lamborghini port are not shown because AeroGauge does
not use them. Input bindings also stay in the existing game controls; the menu does
not add controls that the game cannot use.

The menu draws on its own thread, while the SDL main thread changes the game settings,
writes JSON files, and changes the window. A lock protects menu data shared by these
threads. Opening the menu blocks game input, but it does not pause the race.

The game owns the config files (patch 0015), so the menu library cannot replace them.
Graphics changes wait for **Apply** or **Discard**. Enhancement changes are saved at
once. Applying graphics changes only the fields edited in the menu, so an F11
fullscreen change or an unknown JSON field is not lost. Graphics API, developer mode,
and texture paths take effect after restarting. Settings supplied by environment
variables are disabled in the menu.

Patch 0016 contains the Linux and Windows fixes for the menu: it finds assets next to
the executable, links FreeType on MinGW, sends quit events to the host, and supports
the extra tab keys. It does not copy Lamborghini's input/profile migration. Patch
0017 keeps AeroGauge's early-presentation behavior after the runtime update. Because
AeroGauge starts directly in the game, it supplies an empty launcher callback and its
display name instead of using the frontend launcher list.

To test the menu, build the port and run CTest's `frontend_settings` and
`live_config_updates` tests. The integration test checks loading settings, Apply,
Discard, fullscreen changes, window changes, enhancement persistence, and unknown
JSON fields. For a manual check, open the menu with Escape, F10, or controller Back
in windowed and fullscreen modes. Try mouse, keyboard, and D-pad navigation, then
restart and confirm that saved settings remain. Release packages must include
`assets/` and (on Windows) `freetype.dll`.

Windows verification covered eight targeted tests and a manual check of opening,
tab switching, fullscreen, Apply/Discard, and restart persistence. Ubuntu under WSL
also built successfully, passed both menu tests, and booted for 120 headless VIs and
300 Vulkan VIs with llvmpipe. Physical-controller navigation still needs a hardware
check.
