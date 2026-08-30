---
name: track-artefact-diagnosis
description: Diagnose and fix stray track geometry, occluding wedges, walls, or landmarks in AeroGauge when full-track or extended draw-distance rendering is enabled. Use when a visibility change exposes an apparent track artefact.
---

# Track artefact diagnosis

Use this workflow to identify the exact display list and visibility decision
before changing renderer or game code. The goal is a small, evidence-backed
policy change, not a broad screenshot-driven exclusion.

## Establish the reproduction

1. Record the track, section/zone, camera position, render options, and whether
   the artefact is present with `AERO_FULL_TRACK=1` and with the faithful path.
2. Prefer a fresh boot or a state captured after the relevant course build. A
   savestate contains RDRAM, including the double-buffered synthetic display-list
   banks used by `src/aero_full_track.cpp`; changing options or restoring a state
   can leave an old bank in the frame. Treat an A/B result from a stale state as
   inconclusive until a live frame display-list dump agrees.
3. Read `docs/notes/rom-map.md`, `src/aero_full_track.cpp`, and the
   `port-debugging`, `rom-analysis`, and `native-patching` skills before deriving
   new addresses or changing generated files.

## Isolate the submitted display list

Use event-gated instrumentation, never periodic logging on the gfx or VI thread:

- Dump one frame with `AERO_DL_DUMP_AT` (or the equivalent existing probe).
- Temporarily add an environment-gated display-list skip, analogous to the
  prior investigation's `AERO_DL_SKIP_ROOT`, which replaces selected `G_DL`
  calls with a no-op immediately before the renderer receives them.
- Bisect synthetic root buckets first, then nested `G_DL` targets. A clean
  disappearance when one exact target is skipped is the useful proof; a
  screenshot alone is not.

For the winning target, map the address back to the course table and record:
track, zone, section-entry index, display-list address, `hw4`, `hw6`, current
zone, and the three-zone faithful PVS row. This distinguishes geometry merged by
full-track mode from geometry that the original PVS already submits.

## Choose the fix

- If the geometry is outside the intended PVS but was merged into an
  always-visible `(hw4, hw6)` bucket, keep it out of `build_course` and register
  it only through the faithful PVS path. Put the rule in a small constexpr
  policy header, keyed by stable ROM facts (track/zone plus the verified
  display-list address). Record the table index in the evidence note, but do not
  make a table-order detail part of the policy key. Keep the
  general `hw4 & 0x10` shell rule unchanged.
- If the winning display list is already in the faithful PVS, inspect its F3DEX
  render-state commands and layer ordering before touching visibility. Only
  escalate to an RT64 change when the exact command stream demonstrates a
  renderer semantic mismatch.
- For additional incidents, do not generalise from one screenshot. Collect
  multiple ROM/PVS cases first; use one narrow policy case per independently
  verified geometry unless a shared authoring rule is demonstrated.

## Regression and cleanup

- Add a host test for the pure policy/classification logic, including the exact
  positive case, wrong-address case, and adjacent track/zone/index negatives.
- Register the test in the host-test section of `CMakeLists.txt`.
- Add the derivation and geometry evidence to `docs/notes/rom-map.md`.
- Build the full target when submodules/generated sources are available, run
  the host tests, and launch a fresh live race with `AERO_FULL_TRACK=1`.
- Remove temporary probes, dumps, screenshots, and savestates unless they are
  deliberately documented project fixtures. Confirm no generated recompiler
  files or periodic debug prints were changed, then run `git diff --check` and
  review `git status`.

## Known failure mode

The Bikini Island tunnel case was initially misattributed because a restored
state retained an old synthetic bucket. The decisive evidence was a frame-DL
bisection: zone 13 entry 11, `DL 0x803903B8`, `hw4=0`, `hw6=2`. The exact
address-qualified policy now keeps that wedge on the faithful PVS window. Treat
these values as a worked example, not a rule for other tracks.
