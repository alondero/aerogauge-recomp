# Headless renderer findings — 2026-09-14

This record moves the durable parts of the software-renderer research out of
the source file. The source comments should explain the local invariant; this
page keeps the measurements, limitations, and historical comparison context.

The values below come from the existing repository comments, host-test
fixtures, and historical capture notes. This audit did not rerun the captures.
Use a fixed ROM, commit, trigger, and output when checking any value again.

## Recorded display-list facts

- The race display list is F3DEX version 1, not F3DEX2.
- The state-8 capture used by the original investigation contained textured
  and transformed 3D geometry.
- That capture recorded 1,213 triangles, 1,831 vertices, and 45 matrices.
  These counts describe one historical capture, not a required frame budget.
- Display-list walks follow G_DL branches and segmented addresses.
- Aligned 32-bit guest words can be read directly from the byte-swapped host
  buffer. Guest 16-bit and 8-bit values need the same offset adjustment as
  the N64Recomp MEM helpers.

## Software-renderer coverage

The in-tree renderer currently handles the subset needed for its boot and
capture probes:

- N64 fixed-point matrices, vertex transforms, near/far clipping, and
  viewport mapping;
- RGBA16 and CI4/CI8 texture reads with TLUT data;
- two-cycle colour-combiner inputs;
- fog and blender state used by the dusk race; and
- directional and ambient vertex-light data when the display list supplies it.

The renderer reads guest RDRAM and writes only its private host framebuffer.
It does not write guest state. Its texture and colour-combiner approximations
are not a promise that it matches RT64 for every display list. In particular,
TEX1/LOD behavior is simplified and unsupported formats fall back to a
limited shading path.

## Self-test

The lighting self-test builds synthetic byte-swapped RDRAM and runs the real
software-renderer path without a ROM. It is useful for the host math and
memory-layout boundary. It cannot prove that the translated game supplies
the expected light commands in a live race.

## Comparison rule

For a port-versus-reference capture, record the ROM hash, port commit,
dependency gitlinks, window/aspect settings, display-list trigger, input,
and output files. A difference should produce a smaller hypothesis and a
falsifying check. Do not use a single screenshot as an acceptance claim.
