# Settings frontend

## Status

The shared RecompFrontend settings screen is part of the Windows, Linux, and
Android game paths. Android presents it from the in-game menu and omits the
desktop-only window, API, and developer-tool controls. It still needs a
graphics device.

Headless test runs do not show the menu.

## Player behavior

Open the menu with **Escape**, **F10**, or controller **Back**. It can open while
the game is fullscreen. Use the mouse, keyboard, or controller D-pad. The page
shows the active button prompts.

While the menu has focus, the port clears the gameplay input snapshot and
queues the raw SDL events for RecompFrontend. The race is not paused. Hotplug
events continue to be handled. Closing the menu returns input to the game.

The shared frontend has these pages:

| Page | Options | Save behavior |
| --- | --- | --- |
| Graphics | Resolution, window mode and size, widescreen, HUD placement, presentation rate, manual FPS, anti-aliasing, precision, graphics API, and texture paths | Apply saves the edited graphics fields. Discard restores the page snapshot. Graphics API, developer tools, and texture paths take effect after restart. |
| Enhancements | Draw distance, full-course geometry, and Easy Turbo + Boost Start | Changes become permanent immediately and are written by the port configuration layer. |

F11 and Alt+Enter still switch fullscreen. The menu does not add a new control
binding page. The shared General page is hidden because this port does not
implement all of its audio, mouse, gyro, and binding services.

Full-course geometry is experimental. It can cost performance or show visual
errors. Easy Turbo changes the driving controls and is off by default.

## Environment overrides

These variables are read-only launch overrides. The matching menu option is
disabled when one is present:

- AERO_TEXTURE_PACK
- AERO_TEXTURE_DUMP
- AERO_FULL_TRACK
- AERO_EASY_TURBO
- AERO_DRAW_DISTANCE_SCALE

The override wins for that run. The menu must not copy an overridden value into
the user's JSON file.

## Ownership and threads

The frontend owns temporary page values and confirmation state. The port owns
the JSON files and the live settings snapshot. aero_menu::update drains host
actions on the SDL main thread.

The frontend callbacks must not write JSON, change the SDL window, or call game
code directly. They capture values and queue a main-thread action. The render
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
