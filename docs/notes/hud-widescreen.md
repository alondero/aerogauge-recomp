# Widescreen 1P HUD pinning (issue #1)

Under RT64 `ar_option: Expand`, untagged 2D is rendered centred in the 4:3 region, so the
1P race HUD's edge-anchored elements float inboard of the widescreen edges. The fix tags
the HUD's texrects with RT64 extended-GBI `gEXSetRectAlign` (+ a wide scissor so the moved
rects aren't clipped at the 4:3 edge), injected as N64Recomp `[[patches.hook]]` natives in
`src/aero_hud_widescreen.c`. At 4:3 / non-Expand RT64 leaves the tagged rects put, so every
bracket is a no-op there — no config gate needed. The pure classification/scaling math
lives in `src/aero_hud_widescreen.h` with `tests/test_hud_shift_scale.c`.

## How the HUD is drawn (live-derived; see the `.claude/hud-*.gdb` harnesses)

AeroGauge does NOT use static per-element draw calls like the Lamborghini port. The 2D
master dispatcher **`func_80022408`** walks object lists and calls each object's draw handler
**indirectly** (`jalr` through a per-object function pointer at `obj+0x104` and `obj+0x34`).
So there is no static per-element call site to bracket — the element identity lives in the
handler function and the per-object data.

- **DL write cursor holder = fixed global `0x8016C508`**; cursor = `MEM_W(0, holder)`. Every
  2D helper reads it, stores a command, advances it by 8. At a handler's entry the holder is
  passed in `a0` (== `ctx->r4`); it is clobbered inside the handler, so a bracket saves it at
  entry and the reset re-reads the cursor through the saved value.
- The DL is **double-buffered** (cursor alternates `0x8018xxxx` / `0x80173xxx` frame to
  frame), so absolute DL addresses are never stable — always go through the holder. HUD
  texrect offsets ARE stable *within* a buffer once the mode-4 race settles (~`send_dl` 900).

## Element attribution (mode-4 1P canyon race, steady HUD)

| element | screen x | anchor | handler | dispatcher site |
|---|---|---|---|---|
| speedometer box | 247..300 | RIGHT | `func_80018CF0` (exclusive) | `func_80022408` :10729 (obj+0x104) |
| TEMP gauge | 291..300 | RIGHT | `func_80018EA0` (mixed) | `func_80022408` :10871 (obj+0x34) |
| GLPS | 20..64 | LEFT | `func_80018EA0` (mixed) | `func_80022408` :10871 (obj+0x34) |

Low-level texrect emitter for all: `func_80019D0C`. The centred DAMAGE bar `func_8003A190`
is off both handler paths and is not drawn in the plain canyon race (never bracketed).

## Shipped (issue #1, first increment): speedometer RIGHT pin

Historical: the first increment bracketed `func_80018CF0` (small and speedo-exclusive, one
call/frame) with a dedicated hook pair at `0x80018CF0`/`0x80018D5C`. That bracket was
verified at the DL level (exactly one bracket per frame wrapping only the speedo texrect)
and later subsumed by the whole-frame retag pass below, which classifies the dial ring
RIGHT from its own coordinates — the dedicated hooks were removed with the retag increment.

Visual before/after (real RT64 D3D12 render, 1600x900 16:9 canyon race). `hr_option: Original`
leaves the tagged rects put (== pre-fix behaviour); `Clamp16x9` applies the pin. Note only the
speedometer moves — TEMP / GLPS / lap-timer are the retained follow-up below:

| before (`hr_option: Original`, unpinned) | after (`hr_option: Clamp16x9`, speedo pinned right) |
|---|---|
| ![before](../hud-widescreen-1p-before-16x9.png) | ![after](../hud-widescreen-1p-after-16x9.png) |

Capture recipe (windowed, this machine): set `hr_option` in
`%LOCALAPPDATA%\AeroGaugeRecomp\graphics.json`, run `AERO_WARP=1:1 ./build/aerogauge_modern.exe`,
wait ~40 s for the steady race HUD, PrintWindow-grab the "AeroGauge" window (flag 2 =
PW_RENDERFULLCONTENT captures the D3D12 swapchain). 4:3 and 21:9 remain a human spot-check.

## Shipped (issue #1, second increment): speedometer NEEDLE pin (2026-07-12)

The first increment moved ONLY the cyan dial-ring texrect (`func_80018CF0`, DL slot
`0x18C440`, decode `(247,172)-(300,224)`), leaving the orange needle and the "0" MPH digit
behind (user-reported "split speedo"). The needle is now pinned too; live-derived facts:

- **Orange needle = a single matrix-rotated triangle, NOT a texrect.** In the DL it is
  `MTX 0x18C4C8 (01030040 -> 0x801854B0, push)`, `MTX 0x18C510 (01020040 -> 0x80185970,
  load modelview)`, then `G_DL 0x18C518 (06000000 -> 0x800995C0)`. The sub-DL `0x800995C0`
  is a STATIC ROM resource (`SETCOMBINE / SETPRIMCOLOR B56014FF (orange) / VTX 0x80099590 /
  TRI1 0-2-4 / ENDDL`) — address-stable, so it is the robust discriminator: "the `0102/0103
  0040` MTX immediately before a `G_DL -> 0x800995C0`" uniquely identifies the needle without
  depending on the double-buffered matrix-pool address.
- **No dedicated handler.** The needle is one node of the generic RECURSIVE scene-graph
  transform walker `func_800226AC` (build mtx via `func_80024370` concat + `func_8006C230`
  store to obj+0x9C, push `G_MTX`, recurse obj+0xA0/0xA4), reached `func_8001E8D8 ->
  func_80022408 -> func_800222C0 -> func_800226AC(recursive)`. So the shift is NOT a
  dedicated-handler bracket like the dial ring; it mirrors Lamborghini's `patch_load_mtx_dx`.
- **Implementation** (`src/aero_hud_widescreen.c`, hooks in `scripts/gen_syms_toml.py`
  PATCH_BLOCKS): a matched pair brackets the whole 2D dispatcher `func_80022408` (not
  recursive, same holder `0x8016C508` buffer) — entry `before_vram=0x80022408`
  (`aero_ws_hud_scan_begin`) latches the DL cursor; epilogue `before_vram=0x80022680`
  (`aero_ws_needle_shift`, after every child handler appended, before the register restores)
  walks `[start,end)`, finds the needle MTX by the `0x800995C0` key, and adds `dx` to its
  matrix translate.x (element 12 = byte 24 int / byte 24+0x20 frac, s15.16 — identical layout
  to Lamborghini's guMtxL; MEM_H/MEM_HU handle the endian + KSEG masking).
- **Matrix convention differs from Lamborghini — calibrated live, not analytic.** The needle
  modelview `0x80185970` is PROJECTION-COMBINED (decoded translate ~(-19713, 29184, 256),
  `m[3][3]=0`, recurring `256` scale), so Lambo's `530 units` does NOT carry over. Calibrated
  by windowed PrintWindow capture at 1616x939 (>=16:9 client => internal scale 1.0): the dial
  ring pins right ~152 screen px, the needle travels ~3.73 px/unit, so **`AERO_WS_NEEDLE_DX =
  41`** lands the needle hub back on the dial (needle centroid 1196 -> 1350 px vs target 1348
  = native offset preserved). Env-overridable for recalibration. Scaled off
  `aero_ws_get_hud_rect_aspect_bits()` => 0 at 4:3/Original (no-op, no config gate), caps at
  16:9 under Clamp16x9.

Before/after (real RT64 D3D12, 1616x939 16:9 canyon race; needle only — the "0" digit below
is the retained follow-up):

| before (needle detached, drifts inboard) | after (`AERO_WS_NEEDLE_DX=41`, needle on the dial) |
|---|---|
| ![needle before](../hud-widescreen-1p-needle-before-16x9.png) | ![needle after](../hud-widescreen-1p-needle-after-16x9.png) |

Windowed capture recipe for recalibration: `scratchpad/capture.ps1 <out.png> <dx> [waitSec]`
(recreate on demand — launches windowed `AERO_WARP=1:1`, waits for the steady HUD, PrintWindow
flag 2 grabs the swapchain). Attribution harness: `.claude/needle-watch.gdb` (watches the
needle modelview `0x80185970` write). `hr_option: Original` = unshifted reference.

## Shipped (issue #1, third increment): whole-frame per-texrect retag pass (2026-07-16)

The remaining rect elements ("0" MPH digit, TEMP, GLPS, lap times, top row, …) all flow
through MIXED handlers — `func_80018EA0` draws TEMP (right) **and** GLPS (left) inside one
call — and the dispatcher disassembly killed the hoped-for finer seam: `func_80022408`'s
`jalr group+0x34` site (`0x80022644`) is called once per *group* (four groups per frame,
`v0 += 0x38` stride), not per element, so no call-boundary bracket can ever separate them.

The shipped mechanism instead post-processes what the frame actually emitted. The existing
dispatcher bracket (`aero_ws_hud_scan_begin` at entry, `aero_ws_hud_frame_end` at the
`0x80022680` epilogue) snapshots the emitted `[start, end)` and re-emits it in place with
pin brackets (rect-align + wide scissor push/pop) inserted around runs of same-anchor
texrects, each rect classified **by its own coordinates** in the original 320-wide space
(`aero_ws_classify_rect_qp`, thresholds measured from the steady mode-4 capture):

- RIGHT if `ulx >= 168`: dial ring 247..300, lap-time row 171..301, MPH digit 247..267 +
  its scale ticks 187.., TEMP 274..300, top lap/position row 170..302.
- LEFT if `lrx <= 100` **and** `uly >= 180` (the bottom band): the GLPS ladder 20..64.
  The 100 bound stays below the DAMAGE bar's left edge (117) so its left-anchored fill
  rect (117..117 when empty, growing rightward) never pins at any damage level.
  The band gate keeps the minimap (bg rect 20..100 at y70..172 + craft blips) centred —
  its track polyline is matrix-drawn geometry a rect-align cannot carry (follow-up below).
- everything else stays: DAMAGE 117..229, the 71..247 bottom panel, top-centre bar
  107..169, countdown numerals.

Safety posture (each verified against the pre capture): race scene gate (`0x8013FF80 == 5`;
menus compose 4:3 layouts that must not be pinned), whole-frame skip if any in-range `G_DL`
target, force-close on the game's raw mid-HUD `G_SETSCISSOR` and on 3D commands, bounded
growth (~10 commands per bracket, ~6 brackets/frame) against a measured >=4KB of free
space after the frame DL end, `AERO_WS_RETAG=0` kill-switch for pre/post captures.

Before/after (real RT64 D3D12, 1600x900 16:9 canyon race; `hr_option: Original` = the
unpinned reference layout, `Clamp16x9` = every rect element pinned):

| `Original` (unpinned reference) | `Clamp16x9` (retag pass live) |
|---|---|
| ![retag before](../hud-widescreen-1p-retag-original-16x9.png) | ![retag after](../hud-widescreen-1p-retag-after-16x9.png) |

Known cosmetic gap: the white "TOTAL.TIME" header (107..169, y23) straddles the centre
deadband so it stays centred while its digit row pins right — per-rect geometry alone
cannot attribute it to the timer group. Candidate refinement alongside the minimap work.

## NOT yet shipped (retained, follow-up)

- **Minimap → LEFT**: needs a G_MTX translate shift for the track polyline (same approach
  and calibration recipe as the needle; the polyline G_MTX/G_DL pairs sit well before the
  HUD rects in the frame DL), then the classifier's bottom-band LEFT gate can be relaxed
  so the bg rect + blips travel with it.
- **Human spot-check** at 4:3 / 16:9 / 21:9 with Original/Clamp16x9/Full HUD modes, plus
  the race pause overlay and 2P behaviour (the retag currently applies wherever scene 5
  draws, including the attract-race and pause overlays).
