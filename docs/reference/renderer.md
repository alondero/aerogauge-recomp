# Renderer reference

The normal player renderer is
[RT64](https://github.com/rt64/rt64), pinned by this repository at
[f0728a2](https://github.com/rt64/rt64/tree/f0728a2520d5aa735886240de3fee75cc805f6d6).
The current worktree does not have the submodule contents initialized, so
the audit below uses the public RT64 documentation and the port's checked-in
patches. Before removing a patch, compare it with the exact pinned source.

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

## Local RT64 and Plume patches

The current source tree carries five behavioral RT64 patches, one
MinGW/Windows RT64 compatibility patch, and one Plume compatibility patch:

| Patch | Current purpose | First question before keeping it local |
| --- | --- | --- |
| 0005 | GCC/MinGW compatibility in RT64 | Does current upstream build cleanly with the supported MinGW toolchain? |
| 0006 | Angular-velocity-aware interpolation matching | Is the matching rule useful to other games, and can a small upstream test show it? |
| 0008 | Stretching a sky backdrop without translating it | Is this an AeroGauge camera policy or a general backdrop feature? |
| 0009 | Split-screen wide-subviewport handling | Can RT64 expose the behavior as a general viewport option? |
| 0010 | View/projection decomposition with an axis-aligned pivot choice | Is the NaN avoidance a general numerical fix with a reproducible matrix case? |
| 0011 | Aspect adjustment that tolerates symmetric overscan | Is the overscan detection valid for other games and window systems? |
| 0004 | Plume D3D12 COM ABI compatibility for MinGW | Can the toolchain fix be accepted upstream without a game-specific assumption? |

This table describes intent from the local patch names and comments. It is
not a claim that any patch is ready to upstream. A developer must compare the
patch with current upstream source, create a minimal reproduction, and obtain
maintainer approval before changing the dependency boundary.

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

## Upstream path

For each local renderer patch:

1. record the exact pinned base and the patch's smallest failing example;
2. decide whether the behavior is game-specific, host-toolchain-specific, or
   a general renderer rule;
3. add or update a test that fails without the change;
4. propose a general fix upstream when the evidence supports one; and
5. remove the local patch only after the pinned upstream version contains the
   equivalent behavior and the AeroGauge tests pass.

The long-term architecture keeps ROM-specific behavior in named source
patches or code mods and keeps general renderer behavior in RT64. The current
custom patch stack is a transition point, not proof that every local change
belongs in the port.

One historical note in the original ROM research says that G_CULLDL was made
a no-op in a renderer path. Treat that as an experiment to reverify against
the pinned RT64 source, not as a permanent renderer contract.
