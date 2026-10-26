# Settings frontend

## Status

The shared RecompFrontend settings screen is part of the Windows, Linux, and
Android game paths. Android presents it from the in-game menu and omits the
desktop-only window, API, and developer-tool controls. It still needs a
graphics device.

The desktop app starts the game directly by default. Set `AERO_LAUNCHER=1` to
open the launcher with Start Game, Settings, and Mods options before gameplay.
Package installation needs that pre-game launcher, because the Mods manager
disables its scan and install operations during gameplay: its refresh closes
package handles used by the running game. The Mods tab stays available from
Settings while playing, for listing packages, toggling runtime-toggleable
content, and disabling a package that failed to load.

Headless test runs do not show the menu.

## Player behavior

Open the menu with **Escape**, **F10**, or controller **Back**. It can open while
the game is fullscreen. Use the mouse, keyboard, or controller D-pad. The page
shows the active button prompts.

While the menu has focus, RecompFrontend disables gameplay mappings and the
port queues the raw SDL events for the frontend. The race is not paused.
Hotplug events continue to be handled. Closing the menu returns input to the
game.

The shared frontend has these pages:

| Page | Options | Save behavior |
| --- | --- | --- |
| Graphics | Resolution, window mode and size, widescreen, HUD placement, presentation rate, manual FPS, anti-aliasing, precision, graphics API, texture paths, and Force Full LOD | Apply saves the edited graphics fields. Discard restores the page snapshot. Graphics API, developer tools, and texture paths take effect after restart. |
| Enhancements | Draw distance, full-course geometry, and Easy Turbo + Boost Start | Changes become permanent immediately and are written by the port configuration layer. |
| Cheats | Player 1 Infinite Damage and Always First Position | Immediate, session-only toggles. Reset on application launch; no cheats.json. See [Cheats](cheats.md). |
| Controls | Assign up to two players and edit each player's keyboard or controller profile for driving and menu actions | The frontend saves bindings to `controls.json` when the page closes. |
| Rumble | Independent overall strength and collision/Turbo enable and strength controls for each player | Changes apply immediately; the port saves them in `enhancements.json`. Global rumble launch gates disable the affected controls without replacing saved preferences. |
| Audio | Master volume percentage and a mute switch | Changes apply immediately; the port saves them in `enhancements.json`. The volume slider is disabled while muted. AERO_MASTER_VOLUME pins the level for the run. |
| Mods | Install and manage AeroGauge code packages and RT64 texture packs | The runtime stores package enablement and order in `mods.json`; code mods load at game start, while texture packs can be toggled and reordered during play. |

F11 and Alt+Enter still switch fullscreen. The shared General page is hidden
because this port does not implement all of its audio, mouse, and gyro services.
The Controls page provides Assign players and a separate profile editor for
each player. See [Controllers](controllers.md) for setup and disconnection behavior.

Full-course geometry is experimental. It can cost performance or show visual
errors. Easy Turbo changes the driving controls and is off by default.
Mod loading is experimental. If an enabled package fails at startup, the
settings overlay opens on Mods with the runtime error so the package can be
disabled before starting AeroGauge again. A failed package does not retry by
itself and does not cause a boot loop.

## Environment overrides

These variables are read-only launch overrides. The matching menu option is
disabled when one is present:

- AERO_TEXTURE_PACK
- AERO_TEXTURE_DUMP
- AERO_FULL_TRACK
- AERO_FORCE_FULL_LOD
- AERO_EASY_TURBO
- AERO_EASY_TURBO_P2
- AERO_DRAW_DISTANCE_SCALE
- AERO_MASTER_VOLUME

The override wins for that run. The menu must not copy an overridden value into
the user's JSON file.

AERO_RUMBLE=0 disables the Rumble page's controls for both players;
AERO_RUMBLE_TURBO=0 disables only their Turbo controls. Other values leave
the saved strengths and switches active. Neither gate changes stored preferences.

AERO_MASTER_VOLUME pins the Audio page's level for the run and disables its
slider. The mute switch stays available, because muting is independent of the
saved level. The override does not change the stored value.

## Ownership and threads

The frontend owns temporary page values and confirmation state. The port owns
the JSON files and the live settings snapshot. aero_menu::update drains host
actions on the SDL main thread.

The frontend callbacks must not write JSON, change the SDL window, or call game
code directly. Persistent settings capture values and queue a main-thread action;
session-only cheats publish atomic flags directly, without guest memory or I/O. The render
callback and the menu share a lock in the current integration. Running a
blocking action while that lock is held can block rendering or deadlock.

Disk I/O is not part of that lock any more. The port configuration layer records
a change in memory and a background worker coalesces the changes and writes the
file after a short debounce, so a menu action or hotkey never blocks the event
loop on the filesystem. [Configuration](configuration.md) owns the debounce and
flush contract, including the flush on the quit path.

See [Architecture](architecture.md). The ownership rule is unchanged: JSON and
SDL actions still belong on the SDL main thread. Cross-thread *reads* of the live
configuration are safe, because patch 0018 makes the runtime hand back a snapshot
copy rather than an unlocked reference; see the
[patch inventory](../patches/README.md).

## Dependency boundary

Patch 0015 gives the port ownership of JSON storage. Patch 0016 contains both
generic frontend changes and AeroGauge integration. Patch 0017 preserves the
port's direct-game presentation path after the runtime update. Patch 0018 makes
the live configuration readable from any thread.

The generic parts of patch 0016 are candidates for an upstream proposal, but no
upstream contribution has been accepted in this branch. The settings frontend
does not create a mod API. Texture paths remain RT64 asset inputs, not a
general code or asset mod system.

The release needs the files in assets/frontend/ and the RmlUi fonts beside the
executable. Windows also needs freetype.dll.

## Verification

Run the focused checks from the repository root:

~~~text
ctest --test-dir build -R "^(frontend_settings|live_config_updates)$" --output-on-failure
~~~

These checks cover settings registration, persistence, Apply/Discard, merge
behavior, queued actions, and the port configuration layer. They do not prove
the visual layout, window creation, graphics-device setup, release-archive
asset discovery, physical-controller navigation, or Linux behavior.
