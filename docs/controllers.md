# Controllers and accessories

This page records the current input and device boundaries. The default player
bindings are in the [README](../README.md).

## Player behavior

The current port exposes one physical controller as Controller 1. Keyboard
input uses the same game actions. The Controls page edits the single-player
profiles used by both devices; multiplayer assignment is not exposed.

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
profile mapping; the game thread reads its result through the runtime callback.
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
events are collision damage and the Easy Turbo assist for Controller 1.
The hook does not write guest state. Pulse lengths are host feedback settings,
not new game mechanics.

The ROM's unused motor-test routine is not the source of current race rumble.
The haptics hook observes the collision-damage value at 0x80058AD8 and the
turbo event at the Player 1 input seam. It filters to Controller 1 and race
gameplay.

When Easy Turbo is enabled, the Turbo assist reads the physical N64 R button
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
