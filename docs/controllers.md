# Controllers and accessories

This page records the current input and device boundaries. The default player
bindings are in the [README](../README.md).

## Player behavior

The port exposes two virtual N64 controllers. Before assignment, keyboard
and gamepads use the existing Player 1 profiles and Player 2 is neutral.
Open Controls > Assign players and press a button on each gamepad in player
order, or use the keyboard for one player. Confirm, then use each player's
Edit Profile to change its bindings. Two gamepads, including identical pads,
or a keyboard and a gamepad can drive independently. The shared assignment
screen accepts one keyboard. Android touch input belongs to Player 1.

Both virtual ports stay responsive even when unassigned or disconnected.
The ROM caches controller presence during initialization and repacks responsive
physical ports into consecutive player slots in func_800092C4 (Japan Rev A:
0x8000982C). Keeping a vacant port neutral permits assignment after boot and
prevents Player 2 moving into Player 1's record when Player 1 disconnects.
Ports 3 and 4 remain absent. Select the original game's 2 Players mode for
split-screen racing; assigning devices alone does not change the game mode.

Each player starts with its own controller profile copied from the existing
single-player bindings; keyboard profiles also inherit the existing keyboard
bindings when first created. Profile mappings and selections are saved in
controls.json when leaving the Controls page. Device assignments last for the
current process only. Reassign after restart or reconnecting; unplugging leaves
that slot vacant and does not give its inputs or rumble to another player.

The gamepad path uses SDL's standard game-controller mapping. A connected
controller without a rumble motor can still provide input.

The host hands the runtime a normalized stick value, not an N64-scale one.
`ultramodern`'s `convert_to_n64_range` maps that input through the N64 stick
octagon, whose cardinal inradius is 82. The port therefore normalizes stick
deflection by dividing by that inradius (`sx / 82.0`) before handing it to the
runtime, so a full-deflection stick arrives at the translated game as +-80.
Any other divisor shrinks the whole analog range that the game's steering and
its own menu thresholds are calibrated against, with no error and no log line.

## Input ownership

The input path is:

~~~text
SDL main thread
    -> RecompFrontend event pump and input state
    -> profile mapping through get_n64_input
    -> runtime input callback
    -> translated game on its game thread
~~~

SDL owns the window, event pump, and physical devices. RecompFrontend owns the
profile mapping. The SDL thread publishes a coherent input snapshot for each
player; the game thread reads these through the runtime callback. The port
serializes SDL sampling, assignment changes, and controller cleanup with the
frontend presentation lock so a device cannot be closed during sampling.
A menu event is not proof that the same input reached the race.

While the settings menu has focus, RecompFrontend disables gameplay mappings and
the port queues raw SDL events for the frontend. The race is not paused. Closing
the menu returns input to the game.

## Controller Pak and EEPROM

The port provides one raw 32 KiB Controller Pak image for Controller 1. The
image uses the ROM's PFS calls and is stored below the application save
directory. A successful block write publishes a complete image through a
temporary file and atomic replacement. A failed publication keeps the
previous image.

The format is a raw Controller Pak image. There is no emulator-container
import screen. A correctly sized image can still be rejected by the original
game's integrity checks.

EEPROM is a separate 4 Kbit save device provided by the runtime. Do not treat
the EEPROM file and the Controller Pak image as the same save.

### ROM and host boundary

The port replaces only the Controller Pak block-device calls. The game's
initialization, checksum, note allocation, file lookup, and ghost code remain
translated game code.

| ROM call | Purpose |
| --- | --- |
| 0x800742F0 | Report whether a Pak is present |
| 0x80075290 | Read one 32-byte block |
| 0x80077260 | Write one 32-byte block |

The block index is a block number, not a byte address. The ROM protects ID
blocks 1 through 6 unless the call's force argument is 1. The host keeps the
previous image when an atomic file replacement fails. The complete routing
boundary is in the [ROM reference](reference/rom.md#rom-hook-boundaries).

## Rumble

The port turns selected game events into SDL gamepad vibration. The current
events are collision damage and active Turbo for each player's assigned gamepad.
The hook does not write guest state. Pulse lengths are host feedback settings,
not new game mechanics.

The ROM's unused motor-test routine is not the source of current race rumble.
The haptics hook observes the collision-damage value at 0x80058AD8 and the
turbo timer on each local car. The car's input callback identifies Player 1 or
Player 2; AI and replay callbacks are excluded. Feedback is sampled separately
for each assigned gamepad and cleared on reassignment, disconnection, or leaving
race gameplay. Keyboard and touch players have no gamepad rumble sink.

Easy Turbo + Boost Start has separate, default-off options for Player 1 and
Player 2. Existing easy_turbo_boost settings and AERO_EASY_TURBO affect Player 1;
easy_turbo_boost_player2 and AERO_EASY_TURBO_P2 affect Player 2. Each player has
its own press/release state, so a held or simultaneous Turbo press cannot
trigger the other car. The original timers, heat limits, steering, and drift
controls remain under the game's control.

When that player's Easy Turbo is enabled, the assist reads its N64 R button
before the game maps semantic controls. With the default bindings this is the
keyboard E or R key, gamepad right shoulder, or right trigger. A remapped
control can therefore trigger both its mapped action and the assist.

Host device behavior still needs a physical-controller check. Headless tests
can verify event routing and motor requests, but they cannot prove vibration
on a real gamepad.

## Changing input or accessory code

Open a regular issue for an uncertain ROM or guest-memory finding. A useful
finding names the ROM identity, host, tool, address or field width, byte
order, owner, and a test that could disprove the hypothesis. Once confirmed,
put the lasting rule in the source comment, focused test, or this page.
