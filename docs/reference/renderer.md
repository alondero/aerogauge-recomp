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
| Racer-shadow depth comparison | aero_shadow_depth.cpp, called from rt64_renderer.cpp | AeroGauge-specific task display-list correction |
| Widescreen HUD retagging and needle matrix shift | aero_hud_widescreen.c | AeroGauge-specific display-list rewrite |
| Draw-distance replacement | aero_draw_distance.cpp | AeroGauge-specific native replacement |
| Full-screen overscan removal | aero_scene_scissor.c | AeroGauge-specific scissor hook |
| Headless software renderer | stub_renderer.cpp | Test and capture instrument, not the player renderer |
| Window and input wiring | main.cpp and aero_menu.cpp | Port integration |

The distinction matters. A local rule about AeroGauge's HUD coordinates does
not belong in RT64. A general interpolation or viewport defect should be
reduced to a small renderer test and proposed upstream.

## AeroGauge display-list behavior

### Racer shadows and tunnel occlusion

In the USA race display list, the course writes depth with render modes
`C8112078` and `C8110038`. The root list then sets geometry mode `00002000`
and render mode `00504240` for eight racer-shadow display lists. Those shadow
settings disable both `G_ZBUFFER` and `Z_CMP`, so their dark quads can blend over
a tunnel wall even when the racers are behind it. The following car-mesh mode is
`00552078`; later HUD passes also use `00504240` and must not be changed.

The game-specific correction in [aero_shadow_depth.cpp](../../src/aero_shadow_depth.cpp),
called from [rt64_renderer.cpp](../../src/rt64_renderer.cpp), recognizes that
exact shadow setup after a depth-writing course mode and before each car-mesh
mode. On the graphics thread, before RT64 consumes the current task's root
list, it enables `G_ZBUFFER`, `Z_CMP`, and `ZMODE_DEC` for each matching shadow
pass while leaving depth writes off. RT64 maps ordinary `Z_CMP` to a strict `LESS` test;
the projected racer shadows lie on the course surface and can have equal depth,
so that test can flicker as depth values round. `ZMODE_DEC` uses RT64's per-pixel
coplanar-depth tolerance for the shadows while still rejecting tunnel-wall depth.
The scanner reads host-order 32-bit display-list words from the
8-byte-aligned task address and stays within the task and 8 MiB RDRAM bounds.
The runtime submits one root display list per graphics task; a root may still
contain multiple viewport/course/shadow/car sequences, so the scanner resets
its course gate after each car mode and keeps looking until `G_ENDDL`. A later
HUD command reusing `00504240` has no fresh course mode and remains untouched.
The same scanner is host-testable with synthetic RDRAM. It leaves unexpected
patterns intact. A future named game-source patch to the shadow-list builder
should replace this RDRAM bridge.

The diagnostic capture is an unpaused replay of a saved Bikini Island tunnel
race: the original player renderer shows moving dark flecks on the lower-right
wall; the decal-corrected player renderer hides them and retains the visible
player shadow on the tunnel floor. A second replay accelerates from an
eight-racer Canyon Rush start. With strict depth comparison, the player's
shadow disappears and returns on the flat starting straight; with decal
comparison it stays visible over the road and starting-grid markings. This
comparison uses Windows D3D12, 8x MSAA, and the original game frame rate, so
the dropout does not depend on interpolated frames. The corrected road shadow
also remains visible with refresh rate set to Display. These captures cover the
reported surface-depth failure and tunnel occlusion, not every course or GPU
backend.

### VI colour correction

The USA ROM disables VI gamma correction and gamma dithering. Its startup
function at `0x8001E4E0` calls `osViSetSpecialFeatures(0x5A)` at `0x8001E528`.
The callee at `0x8006CAA0` also enables divot and dither filtering, producing
control word `0x13012` from the mode's `0x311E`. These are OS feature flags;
their bit positions differ from the VI control-register bits.

The symbol generator must identify this callee as `osViSetSpecialFeatures`
so N64Recomp routes it through librecomp to ultramodern's live VI state.
Leaving it as a translated function only updates the unused guest
`__osViNext` context. RT64 then receives `0x311E` and applies unwanted gamma
brightening during presentation. No renderer brightness adjustment is needed:
the game already specifies the correct behaviour through the public VI API.

The ROM's guest VI context in ares v147 reads `0x13012` as well. Its MMIO
readback reports `0x3012` because that version's
[VI register reader](https://github.com/ares-emulator/ares/blob/v147/ares/n64/vi/io.cpp)
returns only the low 16 control bits. Compare gamma bits or the guest context
when using that emulator as a reference. The `vi_special_features` test
checks RT64's live register input during startup and after entering a race.

### HUD display lists

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

### Full-screen overscan

The USA ROM's full-screen race viewport has scale and translation
`(640,480,511)` in quarter-pixel units, covering 320 by 240 pixels. The
scene scissor builder at `0x800227E4` adds 16-pixel horizontal and 8-pixel
vertical insets, then subtracts one more pixel at the lower bounds. Its
`ED040020 004BC39C` command clips drawing to `(16,8)..(303,231)`.
The black framebuffer clear remains visible outside that rectangle. RT64
widens the projection but scales these margins with the output, making them
particularly noticeable on ultrawide screens. The original hardware motive
for these margins has not been established.

[aero_scene_scissor.c](../../src/aero_scene_scissor.c) changes this command
to `(0,0)..(320,240)`, with exclusive lower bounds. It runs on the game thread
immediately after the command store at `0x800229C4`, using `s0` for the
descriptor and `v1` for the command. It validates guest ranges, full-screen
scale/translation, and the original command before writing two 32-bit words.
Unexpected inputs, split-screen views, and custom crops are left intact.
Camera matrices and viewport data do not change: the newly exposed area
shows additional world geometry at the existing scale. This is enabled by
default and needs no new RT64 patch or aspect setting.

When that builder also emits a solid background clear, the companion hook
after `0x80022B28` expands its fill rectangle to `(0,0)..(319,239)`. Fill-cycle
lower bounds are inclusive. The original fill color is preserved, and the
same full-screen descriptor and command checks guard the change.

The `scene_scissor` regression test extracts the actual generated function
and exercises its hooks with full-screen, split-screen, and custom-crop
descriptors. ROM-derived output stays in the build directory. A future
named source implementation of this builder can replace the register-based
hook without changing the clipping contract.

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

### Adreno compute-pipeline rejection

The Adreno 750 system Vulkan driver (driver version `0x802FA029`, Android 16)
returns `VK_ERROR_UNKNOWN` from `vkCreateComputePipelines` for a compute shader
that contains the 16-bit byte swap `((i << 8) & 0xFF00) | ((i >> 8) & 0xFF)`,
even though the SPIR-V is valid. The reduced reproducer is a compute shader
whose whole body is that expression; dropping either mask, changing the second
mask, or replacing the `|` with `^` or `+` makes the driver accept the same
pipeline, and the 32-bit swap is not affected.

Five RT64 compute pipelines hit this: the framebuffer change-detection and
writeback shaders built in `rt64_shader_library.cpp`, which reach the swap
through `EndianSwapUINT`. Plume's `VulkanComputePipeline` constructor logged the
failure and returned early, leaving the object's `VkPipeline` at
`VK_NULL_HANDLE`, and `NativeTarget::copyFromRAM` then passed that null handle
to the driver. The tombstone recorded SIGSEGV with fault address `0x8` inside
`qglinternal::vkCmdBindPipeline`, called from
`rt64_native_target.cpp:195`. A failed compute-pipeline creation is still only
logged, so a future rejected shader would present as a driver crash rather than
a clean startup failure.

[Patch 0023](../../patches/0023-rt64-adreno-endian-swap.patch) spells the swap
with XOR. The two masked fields occupy disjoint bit ranges, so the value is
identical on every driver, and the Adreno compiler accepts it.
`tests/test_shader_endian_swap.py` proves that equivalence over the full 16-bit
domain and compiles the helper through dxc to assert the rejected opcode is
absent from the emitted SPIR-V.

## Renderer-specific failure modes

- If the RT64 device cannot start, run the headless path to separate host
  setup from game display-list production.
- If a shader fails to create a pipeline on one driver while the same build
  works elsewhere, reduce the shader to the smallest expression that driver
  rejects before changing the pipeline layout, the device features, or the
  queue setup.
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
