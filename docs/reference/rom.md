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

The hash above was recorded by the repository's current release and build
documentation. Before accepting a new dump or changing the hash, verify the
algorithm and value with a small independent tool and record the command and
result in a dated investigation.

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

## Course and display-list facts

The current full-track investigation derives course rows from
0x8008B290, with a 0x14-byte stride per track. The original course code uses a
three-zone visibility row. The native full-track path builds synthetic display
lists in the reserved guest range 0x80700000 through 0x807FFFFF and links them
into game-owned node lists. See the
[full-track architecture section](../architecture.md#guest-memory-writes-are-transitional)
and the [historical ROM map](../notes/rom-map.md).

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

Record the result in [investigations](../investigations/index.md). An address
copied from a comment or another port is a lead, not proof.
