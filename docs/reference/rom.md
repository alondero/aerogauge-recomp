# ROM reference

This page records facts for the one ROM identity that the current build files
describe. It is a developer reference. Players only need the short ROM note in
the [README](../../README.md).

## Accepted input

| Fact | Value |
| --- | --- |
| File name used by the build inputs | AeroGauge (USA).z64 |
| Size | 8 MiB |
| XXH3-64 | 89ea0690f3e22201 |
| CPU entrypoint | 0x80000400 |
| CPU text input range | ROM 0x1000 through 0x7F4BF, end exclusive at 0x7F4C0 |
| Symbol and recompiler inputs | aerogauge.syms.toml and aerogauge.us.toml |

The file name is part of the normal build path. A different path can be
passed to the program, but it must identify the same supported ROM. A file
that is the right size but has another dump identity is not enough evidence.

The hash above is part of the repository's build contract. Before accepting a
new dump or changing the hash, verify the algorithm and value with a small
independent tool and record the command and result in the pull request.

## Address conversion

The current ROM layout uses this conversion for CPU code:

~~~text
ROM offset = virtual address - 0x80000400 + 0x1000
virtual address = ROM offset - 0x1000 + 0x80000400
~~~

The conversion is valid for this image's contiguous code section. It is not a
general N64 rule for every asset or every ROM.

The entry trampoline at 0x80000400 transfers to the boot body at
0x800653F0. The symbol generator keeps both the trampoline and the boot body
as explicit inputs because the trampoline is part of the program's startup
contract.

## Audio microcode

The generated audio input describes the aspMain RSP microcode:

| Fact | Value |
| --- | --- |
| Text ROM offset | 0x7F330 |
| Text size | 0xE1C |
| RSP text address | 0x04001080 |
| ucode data ROM offset | 0xC8610 |
| Generated output | src/aspMain.cpp |

The table of indirect branch targets in aspMain.us.toml is part of the same
ROM-derived input. Do not edit the generated C++ output to repair an audio
problem. Check the ROM, the TOML input, and the RSPRecomp output together.
The [audio reference](audio.md) describes the runtime path.

## Scene and frame words

These words are used by current hooks and probes. They are meaningful only
for the accepted image and its current memory layout.

| Address | Meaning used by the port |
| --- | --- |
| 0x8013FF80 | Current scene |
| 0x8013FF84 | Requested scene |
| 0x8013FF88 | Scene-local phase |
| 0x8013FF8C | Scene transition or exit request |
| 0x8013FF90 | Race parameter block |
| 0x8013FF9B | Current track byte |
| 0x8013FF44 | Pointer to the current course's object table |
| 0x8016C508 | Current display-list write-cursor holder |
| 0x8016C4F0 | Time anchor repaired by the save-state path |

The first six scene and race words are read by the warp, HUD, full-track, and
save-state paths. They are not a stable public API. A change to the ROM or
the surrounding game code requires fresh evidence.

The race parameter block at 0x8013FF90 has these fields used by the port:

| Offset | Meaning |
| --- | --- |
| +0x00 | Race mode: 4 for a menu-launched race, 7 for an attract demo |
| +0x04 | Course group used by the game's record and music tables |
| +0x05 / +0x06 | Player 1 and Player 2 craft numbers |
| +0x0B | Track index, 0 through 5 |
| +0x1A | Initialization flag set by the scene manager |

The scene driver copies the requested scene into the current scene before it
runs the scene handler. Race setup must therefore publish the race parameters
and request the scene through the game's own transition path. A port hook must
not jump directly into a race runner while its loader is still active.

## Game data used by the port

The port relies on these ROM-specific game boundaries:

| Address or function | Meaning |
| --- | --- |
| 0x8013FF80, 0x8013FF84, 0x8013FF88 | Current scene, requested scene, and scene phase |
| 0x8013FF8C | Requested transition phase |
| 0x8013FF90 | Race setup block |
| 0x8013FF9B | Current track number |
| 0x8013FF44 | Pointer to the current course object table |
| 0x8016C508 | Display-list cursor holder used by the 2D HUD |
| 0x8016C4F0 | Runtime clock anchor rebased by save-state loading |
| 0x8008B290 + track * 0x14 | Course visibility and display-list row |
| 0x8005C750 | Player 1 vehicle input callback |

The race scene is scene 5. Its phase values include setup, countdown, and
playing. The course row describes a three-zone visibility window. Full-course
geometry bypasses that window by building synthetic display lists in the
reserved guest range 0x80700000 through 0x807FFFFF. See the guest-memory
boundary in [Architecture](../architecture.md).

## ROM hook boundaries

These are the entry points currently replaced or bracketed by port code. They
belong to the accepted USA image above. They are evidence for this port, not a
portable API for another ROM.

| ROM address | Current use | Port owner |
| --- | --- | --- |
| 0x80022408 | Main 2D display-list dispatcher | Widescreen HUD hook |
| 0x8006BA60 | guPerspectiveF, called by the projection sites | Draw-distance replacement |
| 0x80007150 | Course section registrar using the three-zone visibility row | Full-track section path |
| 0x80007310 | Course object registrar using the current course table | Full-track object path |
| 0x800742F0 | Controller Pak status query | aero_pak.cpp |
| 0x80075290 | 32-byte Controller Pak block read | aero_pak.cpp |
| 0x80077260 | 32-byte Controller Pak block write | aero_pak.cpp |
| 0x8005C7A8 | Post-map seam in the Player 1 vehicle-input callback | Easy Turbo and Boost Start |
| 0x80058AD8 | Collision-damage value before the ROM accumulates it | Haptics observer |

The generated symbol input records these routes in
scripts/gen_syms_toml.py. A route is not complete until its guest-memory
assumptions, thread boundary, failure behavior, and focused test are recorded
with the owning code.

## Course and display-list facts

The current course reference derives course rows from
0x8008B290, with a 0x14-byte stride per track. The original course code uses a
three-zone visibility row. The native full-track path builds synthetic display
lists in the reserved guest range 0x80700000 through 0x807FFFFF and links them
into game-owned node lists. See the
[full-track architecture section](../architecture.md#guest-memory-writes-are-transitional).

Each course row also supplies a section-to-zone map, a table of three visible
zones for each current zone, an object-list table, and section display-list
groups. Section groups use 8-byte entries containing a display-list address and
two render flags. Object entries use a 0x28-byte stride and may include a
one-shot node initializer. The original registrars use those callbacks and
the 47 usable slots in each 48-slot node arena.

Most enclosed course shells carry the ROM's hw4 bit 0x10 and must remain PVS
gated when full-track mode is enabled. Two unflagged Bikini Island entries are
also PVS gated by the current port policy. Keep their exact display-list
addresses in [the policy header](../../src/aero_full_track_policy.h) and its
[host test](../../tests/test_full_track_policy.cpp), rather than copying the
exceptions into unrelated hooks.

The 2D HUD code uses quarter-pixel display-list coordinates. Its current
classification bounds are recorded in the
[HUD source header](../../src/aero_hud_widescreen.h) and tested by
the [HUD host test](../../tests/test_hud_shift_scale.c).

## How to use an address

Before adding a hook or native replacement:

1. identify the ROM version and hash;
2. show the bytes or live-memory observation that gives the address meaning;
3. identify the game thread and frame phase that owns the access;
4. use the N64Recomp memory helpers for guest values; and
5. add a focused test or capture that would fail if the address moved.

If the address is uncertain, open a regular issue. An address copied from a
comment or another port is a lead, not proof.
