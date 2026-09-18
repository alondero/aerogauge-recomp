# Renderer reference

The normal player renderer is
[RT64](https://github.com/rt64/rt64), pinned by this repository at
[f0728a2](https://github.com/rt64/rt64/tree/f0728a2520d5aa735886240de3fee75cc805f6d6).
The parent gitlink is the version used by the build. Before removing or
refreshing a local patch, compare it with that exact source.

## What RT64 already owns

RT64's public project documentation covers:

- N64 display-list interpretation;
- D3D12, Vulkan, and Metal backends;
- output scaling and arbitrary aspect ratios;
- limited display-list interpolation;
- extended GBI commands;
- texture replacement and texture dumping; and
- host presentation and renderer resource management.

The public [extended GBI header](https://github.com/rt64/rt64/blob/main/include/rt64_extended_gbi.h)
defines commands such as rectangle alignment and scissor control. The
[texture-pack guide](https://github.com/rt64/rt64/blob/main/TEXTURE-PACKS.md)
describes the asset replacement boundary.

The port should not reimplement those services in a game hook. Its renderer
responsibility is to pass the correct window, task, ROM, settings, and
game-specific evidence to RT64.

## Headless renderer

stub_renderer.cpp is a software renderer used for bounded tests and captures.
It reads guest RDRAM and writes a private host framebuffer. It does not write
guest state and it is not the normal player renderer.

It covers the display-list, matrix, texture, lighting, fog, and combiner
subset needed by the port's current boot and capture tests. Unsupported
formats and some texture level-of-detail behavior use a limited fallback.
Passing a headless test does not prove that RT64 or a physical graphics device
will produce the same image.

## Local and upstream responsibilities

| Behavior | Current owner | Status |
| --- | --- | --- |
| Normal display-list interpretation and presentation | RT64 | Upstream-supported |
| Widescreen output and standard aspect settings | RT64 plus port configuration | Upstream-supported capability; port defaults are project policy |
| Rectangle alignment and wide scissor commands | RT64 extended GBI plus the HUD hook | Upstream command support; AeroGauge classification is project-specific |
| Texture packs and dumps | RT64, configured by the port | Upstream-supported path; the port's file settings are local |
| Full-course geometry registration | aero_full_track.cpp | AeroGauge-specific transitional hook |
| Widescreen HUD retagging and needle matrix shift | aero_hud_widescreen.c | AeroGauge-specific display-list rewrite |
| Draw-distance replacement | aero_draw_distance.cpp | AeroGauge-specific native replacement |
| Headless software renderer | stub_renderer.cpp | Test and capture instrument, not the player renderer |
| Window and input wiring | main.cpp and aero_menu.cpp | Port integration |

The distinction matters. A local rule about AeroGauge's HUD coordinates does
not belong in RT64. A general interpolation or viewport defect should be
reduced to a small renderer test and proposed upstream.

## AeroGauge display-list behavior

The race HUD is emitted by a shared 2D dispatcher, not by one static draw call
per element. The dispatcher walks object lists and calls handlers indirectly.
The display-list cursor is passed through a guest-memory holder, and the
display list alternates between two buffers. Port code must use the holder and
must not assume that an absolute display-list address stays stable.

The widescreen HUD hook scans the emitted commands. It classifies rectangle
coordinates in the original 320-pixel space, then adds RT64 rectangle-alignment
and scissor commands around left- and right-anchored groups. It also identifies
the speedometer needle by its display-list marker and shifts the related
matrix. At 4:3 output, the adjustment is a no-op.

These rules are specific to this ROM and its current display-list layout. The
classification and matrix math have host tests. A change to the HUD hook must
keep the ROM evidence, the guest-memory cursor rules, and the test together.

### HUD hook contract

The current HUD pass uses the original 320-pixel coordinate space. A rectangle
with a right edge at or below 100 pixels pins left. A rectangle with a left
edge at or above 168 pixels pins right. The centre band stays in its original
position. The minimap has a separate 16-to-108 pixel box because its moving
craft marker can cross the normal left threshold.

The pass runs only for the race scene once the HUD has reached its stable
layout: race phase 3 or 7, or countdown phase 2 after countdown step 2. This
avoids moving the fade, ticker, READY banner, or other transitional rectangles.
The speedometer needle is not a rectangle. The pass identifies its static
display-list target at 0x800995C0 and shifts its model-view matrix instead.

The main 2D dispatcher is function 0x80022408. It calls handlers indirectly
and some handlers mix left and right elements, so the port brackets the whole
dispatcher rather than guessing an element-specific call boundary. The cursor
holder is guest address 0x8016C508; its pointed-to display-list buffer changes
between frames. Port code must read and update the holder with N64Recomp
memory helpers.

Race announcements, intro banners, and championship result pages use separate
ROM-disassembled ranges. Their hooks preserve the game's stack-local cursor
and keep each centred message together. The exact ranges and generated hook
text are maintained in [the symbol generator](../../scripts/gen_syms_toml.py).
Do not move a range or add a new renderer command without a matching
display-list test or capture.

## Local patch boundary

The [patch inventory](../../patches/README.md) lists each local RT64 and Plume
patch, its platform, and its purpose. It is the build inventory, not a list of
upstream proposals.

A game-specific display-list rule belongs in the port. A general interpolation
or viewport behavior needs a small renderer test and a proposal to RT64. A
toolchain compatibility patch should be removed when the pinned upstream
supports the same toolchain.

## Renderer-specific failure modes

- If the RT64 device cannot start, run the headless path to separate host
  setup from game display-list production.
- If a frame is empty, inspect the task's display-list pointer, ucode, and
  RDRAM contents before changing RT64.
- If a HUD element is misplaced, compare the original coordinates, the
  effective RT64 rectangle aspect, and the HUD gate. Do not move every
  display list as a shortcut.
- If a full-track capture differs, disable full_track and the draw-distance
  enhancement separately. These are two different interventions.
- If interpolation changes motion, compare game update cadence with VI
  presentation. A rendered intermediate frame is not a new game simulation.

The headless renderer has display-list census, dump, and framebuffer probes.
Their current switches are listed in [configuration](../configuration.md).
The local interpolation patch also accepts RT64_MATCH_DEBUG for diagnostic
logging; it is not an RT64 player setting.

The long-term architecture keeps ROM-specific behavior in named source
patches or code mods and keeps general renderer behavior in RT64. The current
custom patch stack is a transition point, not proof that every local change
belongs in the port.
