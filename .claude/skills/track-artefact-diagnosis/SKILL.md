---
name: track-artefact-diagnosis
description: Diagnose and fix stray or mis-occluded geometry in AeroGauge when full-track or extended draw-distance is on. Use when a visibility enhancement reveals a wedge, wall, or landmark that shouldn't be visible in steady race — including zone-object landmarks appearing in the wrong visibility row, and unflagged rock geometry that the original PVS hid.
---

# Track artefact diagnosis

Goal: a small, evidence-backed policy change — never a broad screenshot-driven
exclusion. Bisect to one display list, name the stable ROM facts (track, zone,
verified DL address), and let the host test guard against regressions on adjacent
cases. **Do not generalise from one screenshot.** Collect multiple independently
verified cases before adding a shared authoring rule.

## When NOT to use this

- HUD-only artefacts (cursor, needle, dial ring) → use the widescreen recipe in
  `docs/notes/hud-widescreen.md`.
- Audio glitches or frame-pacing issues → `port-debugging` skill (`AUDIO_RMS`,
  `AERO_FRAME_LOG`).
- Renderer/cmd-stream mismatches inside an already-correctly-routed DL → read
  the F3DEX opcodes in the captured dump; an RT64 change is the right fix when
  the *exact command stream* demonstrates a renderer semantic mismatch.

## Phase 1 — Reproduce

Deliverable: **a captured frame DL with the artefact visible from a fresh boot.**

1. Record the track, section/zone, camera position, render options, and whether
   the artefact is present with `AERO_FULL_TRACK=1` and with the faithful path.
2. **Always bisect from a fresh boot.** A restored savestate can retain a stale
   synthetic bucket: `aero_full_track.cpp`'s `BANK_SPAN`/`s_first`/`s_bank`
   toggle (lines 280–282) keeps the in-flight gfx thread's reads on a different
   bank than the rebuilt course. An A/B result from a stale state is
   inconclusive until a *fresh-boot* frame-DL dump agrees.
3. Capture the frame DL: `AERO_RACE_DL_DUMP=<basename>` (default gate state 8,
   `AERO_DL_INSPECT_STATE`/count via `AERO_DL_DUMP_AT` for menu or pre-race).
   Produces `<basename>.txt` (walked DL) + `<basename>.bin` (8 MiB RDRAM).

## Phase 2 — Attribute

Deliverable: **(track, zone, section_index, dl_address, hw4, hw6) for the
offender.**

4. From the frame-DL dump, identify the offending `G_DL` target.
5. Attribute it to the course table:
   ```bash
   python tools/rom/full_track_attr.py 0x803903B8 --rdram <basename>.bin
   # prints:
   #   track= 1 zone=13 section_index=11 hw4=0x0000 hw6=0x0002 dl=0x803903B8
   #   track= 1 zone=13 section_index=11 hw4=0x0000 hw6=0x0002 dl=0x803903B8  # <-- current track (RDRAM)
   # The display-list address is the stable ROM fact; the section index is
   # corroboration (a table reorder cannot retarget the address-based rule).
   ```
6. Narrow to one zone with the bisection knob:
   ```bash
   python tools/rom/zone_mask.py --zone 13        # -> AERO_FT_ZONE_MASK=0x2000
   AERO_FT_ZONE_MASK=0x2000 build/aerogauge_modern
   ```
   If the artefact persists with only zone 13 enabled, you have the offender.
   If it disappears, the offender is in the merge-set from another zone —
   bisect with `--range 0..63`.

## Phase 3 — Verify the offender

Deliverable: **a clean re-render with the candidate DL skipped; failing that,
the diff points at the real cause.**

7. Skip the candidate DL temporarily at frame send_dl:
   ```bash
   AERO_DL_SKIP_DL=0x803903B8 build/aerogauge_modern
   ```
   A clean re-render confirms `0x803903B8` is the cause; an unchanged render
   means the candidate was a symptom (re-check the dump — a stale bank from
   Phase 1 is the usual cause of "skip had no effect"). This is the only
   definitive evidence; a screenshot alone is not.
8. Cross-check against the ares reference (`ares-debugger` skill) if the
   question is "does the real ROM render this here?" — same DL, same zone.

## Phase 4 — Fix and verify

Three independent scenarios, with different destinations:

### 4a. Full-track over-merging (most common)

Geometry is outside the intended PVS but was merged into an always-visible
`(hw4, hw6)` bucket. Add a narrow constexpr policy case; let the host test
guard adjacent cases. Ship it as a header (canonical example:
`src/aero_full_track_policy.h`):

```cpp
namespace aero::full_track {
struct SectionOverride { uint8_t track; uint8_t zone; uint32_t display_list; };
inline constexpr SectionOverride kPvsGatedExceptions[] = {
    // <track>, <zone>, <verified DL address> — derived from Phase 2/3.
};
constexpr bool pvs_gated_section(uint8_t track, uint8_t zone,
                                 uint32_t display_list, uint16_t hw4) {
    if ((hw4 & 0x10u) != 0) return true;          // general shell rule
    for (const SectionOverride& ex : kPvsGatedExceptions) {
        if (ex.track == track && ex.zone == zone &&
            ex.display_list == display_list) return true;
    }
    return false;
}
}
```

Generate the host test from the same inputs:
```bash
python tools/rom/gen_policy_test.py --track 1 --zone 13 --dl 0x803903B8 \
    --append tests/test_full_track_policy.cpp
```

The generator emits the four-assertion pattern (positive, wrong-address,
adjacent-zone, adjacent-track) — the structure that caught every regression
during the Bikini fix. Register the test in `CMakeLists.txt` if it's new.

### 4b. DL is already in the faithful PVS but renders wrong

Inspect the F3DEX render-state commands and layer ordering in the captured
dump before touching visibility. Only escalate to an RT64 change when the
*exact command stream* demonstrates a renderer semantic mismatch.

### 4c. Generalisation from one case → do not

"For additional incidents, do not generalise from one screenshot. Collect
multiple ROM/PVS cases first; use one narrow policy case per independently
verified geometry unless a shared authoring rule is demonstrated."

## Phase 5 — Regression and cleanup

9. Build the full target when submodules/generated sources are available
   (`cmake --build build --target aerogauge_modern`); run the host tests;
   launch a fresh live race with `AERO_FULL_TRACK=1` and re-run the captured
   repro path. The saved state from Phase 1 should *not* look different —
   it was the stale-bank trap.
10. Record the derivation: `docs/notes/rom-map.md` gets a one-line entry in
    the existing "Course zone/visibility model" section, with the same
    discipline as the existing entries: name the method (e.g. "saved-state
    frame-DL runtime bisection"), the date, and the track/zone/DL tuple.
11. Remove temporary probes (`AERO_DL_SKIP_DL`, `AERO_RACE_DL_DUMP`,
    `AERO_FT_TRACE`), screenshots, and `.bin`/`.txt` dumps unless they are
    deliberately documented project fixtures. Confirm no generated recompiler
    files or periodic debug prints were changed, then `git diff --check` and
    review `git status`.

## Bisection-knob catalogue (see `port-debugging` skill for the full list)

| Knob | Purpose |
|---|---|
| `AERO_DL_SKIP_DL=<hex,hex,...>` | replace these `G_DL` targets with `G_SPNOOP` at send_dl — **the definitive "is this the cause" tool** (Phase 3) |
| `AERO_DL_DUMP_AT=<count>` / `AERO_RACE_DL_DUMP=<basename>` | capture a single frame DL + RDRAM for offline bisection |
| `AERO_DL_GEOMSET=<stride>` | per-frame G_DL target set as one line — diff runs to prove geometry-identical |
| `AERO_FT_SECTIONS=0` / `AERO_FT_OBJECTS=0` | force the windowed path for that registrar only |
| `AERO_FT_ZONE_MASK=<hex>` | full-track includes only zones whose bit is set — **the zone-level bisector** (`tools/rom/zone_mask.py` computes the value) |
| `AERO_FT_TRACE=1` | one-shot stderr trace during course rebuild (event-driven, not periodic) |
| `AERO_FULL_TRACK=0` | force off entirely (a baseline comparator) |

## Known failure mode

The Bikini Island tunnel case was initially misattributed because a restored
savestate retained an old synthetic bucket from a *previous* course rebuild.
**The lesson: A/B results from a restored savestate are unreliable for full-track
attribution — the in-flight gfx thread's reads are pinned to a specific
synthetic bank, and a state restore can land you on a bank the rebuilt course
no longer writes to.** The decisive evidence was a *fresh-boot* frame-DL
bisection: internal track 1, zone 13, section entry 11, `DL 0x803903B8`, `hw4=0`,
`hw6=2` — an unflagged three-triangle rock wedge spanning roughly
`x=-6486..-5250, z=7151..7954`. Treating it as `hw4 & 0x10` and gating its
registration on the faithful PVS kept the geometry off the always-visible
bucket. **Treat those values as a worked example, not a rule for other tracks.**

## Related tools and docs

- `tools/rom/full_track_attr.py` — DL → course table attribution (Phase 2)
- `tools/rom/zone_mask.py` — generate `AERO_FT_ZONE_MASK` from zone ids
- `tools/rom/gen_policy_test.py` — emit the four-assertion test from a policy case
- `src/aero_full_track.cpp` — full-track registrars (the policy header is
  included here; the build-flow gotcha in `native-patching` still applies)
- `src/aero_full_track_policy.h` — canonical policy header pattern
- `tests/test_full_track_policy.cpp` — canonical test pattern
- `docs/notes/rom-map.md` — record every new derivation in the existing
  "Course zone/visibility model" section
