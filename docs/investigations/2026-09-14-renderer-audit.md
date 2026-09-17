# Renderer capability audit — 2026-09-14

## Method

I inspected the checked-in renderer sources, the RT64 and Plume patch files,
the CMake wiring, and the public RT64 README and extended-GBI header. The RT64
submodule is not initialized in this worktree, so this is not yet a
line-by-line comparison with the pinned source commit. That comparison is a
required follow-up before a patch is removed or upstreamed.

## Findings

RT64 already documents the core services that the port consumes: display-list
interpretation, host presentation, D3D12/Vulkan/Metal backends, aspect-ratio
handling, limited interpolation, extended GBI commands, and texture
replacement/dumping. The port should use those services through the renderer
context.

The following behavior remains local because it depends on AeroGauge's ROM
layout or current evidence:

| Local code | Why it is local today | Risk |
| --- | --- | --- |
| aero_hud_widescreen.c | Classifies this ROM's texrect coordinates and rewrites one matrix after indirect 2D handlers emit commands | A changed HUD layout can misclassify or duplicate commands |
| aero_full_track.cpp | Replaces this ROM's three-zone course registrars and builds synthetic display lists | Guest-memory layout, node arenas, and renderer timing must stay in agreement |
| aero_draw_distance.cpp | Replaces this ROM's perspective helper to change the far plane | The replacement changes game-visible projection policy |
| stub_renderer.cpp | Measures and captures actual task streams without a graphics device | It is diagnostic infrastructure, not an alternative player renderer |
| rt64_renderer.cpp | Connects the ROM, SDL window, settings, task flow, and local probes to RT64 | Host and guest ownership can be confused if the seam grows |

## Patch classification

The current RT64 stack has five behavioral patches:

- interpolation matching that considers angular velocity;
- sky backdrop stretching without camera translation;
- split-screen wide-subviewport handling;
- a view/projection decomposition pivot choice; and
- aspect adjustment that tolerates symmetric overscan.

It also has a MinGW/GCC compatibility patch in RT64 and a D3D12 COM ABI
compatibility patch in Plume. The exact patch names and current questions are
in the [renderer reference](../reference/renderer.md).

The five behavioral changes may be general renderer improvements, but the
checked-in repository does not yet provide a small upstream reproduction for
each. The compatibility patches may be obsolete when the supported toolchain
or upstream headers change. These are hypotheses, not upstream decisions.

## Proposed review for each patch

For every patch, a developer should record:

1. the pinned upstream commit and the changed files;
2. a smallest input that fails without the patch;
3. whether the input is an AeroGauge display list, a general matrix, a
   general viewport, or a compiler/platform case;
4. a test that passes with and fails without the patch;
5. an upstream issue or contribution when the behavior is general; and
6. the maintainer's decision to keep, replace, or remove the local patch.

Do not move AeroGauge's HUD thresholds or course memory writes into RT64 to
make them look more general. Do not keep a general numerical or viewport fix
local merely because the first failing game is AeroGauge.

## Open questions

- Does the current pinned RT64 already contain any part of patches 0006,
  0008, 0009, 0010, or 0011?
- Which interpolation matching rule is correct for games with rotating
  objects, and what should the upstream API expose?
- Is sky backdrop stretching a renderer feature or an AeroGauge camera
  choice?
- Can the split-screen viewport and overscan changes be expressed as general
  renderer tests?
- Can the MinGW fixes be deleted after the supported compiler and DirectX
  headers are refreshed?

Until these questions have evidence, the local patches remain part of the
build contract and the docs describe them as transitional.
