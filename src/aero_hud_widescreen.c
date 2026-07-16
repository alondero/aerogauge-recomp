// Per-element widescreen HUD pinning (issue #1, RT64 extended GBI).
//
// Under `ar_option: Expand` RT64 renders untagged 2D centred in the 4:3 region, so the
// 1P race HUD's edge-anchored elements float inboard of the widescreen edges. Peer ports
// bracket each element's draw call with gEXSetRectAlign, but AeroGauge has no static
// per-element draw calls to bracket: the 2D dispatcher func_80022408 walks object lists
// and calls per-object handlers indirectly, and those handlers mix LEFT and RIGHT
// elements inside one call (func_80018EA0 draws TEMP right + GLPS left), so no call
// boundary separates them. Instead the whole dispatcher is bracketed (entry latches the
// DL write cursor, the epilogue hook post-processes what the frame actually emitted):
//   1. texrects are re-emitted with RT64 rect-align + wide-scissor brackets inserted
//      around runs of same-anchor rects, each rect classified by its own coordinates
//      (aero_hud_widescreen.h; measured thresholds from the mode-4 race capture); and
//   2. the speedometer needle -- matrix-rotated 3D geometry a rect-align cannot carry --
//      gets its modelview translate.x shifted to track the pinned dial ring.
// At 4:3 / non-Expand RT64 leaves tagged rects put and the needle scale is 0, so the
// whole pass degenerates to a byte-identical re-emit -- no config gate needed.
//
// Cursor convention (live-derived): AeroGauge's DL builder holds the write cursor in a
// fixed global at 0x8016C508 (cursor = MEM_W(0, holder)); every 2D helper reads it,
// stores a command, and advances it by 8. The DL is double-buffered (the cursor
// alternates between two RDRAM buffers frame to frame), so the holder -- never an
// absolute DL address -- is the only stable handle.

#include "recomp.h"
#include "rt64_extended_gbi.h"
#include "aero_hud_widescreen.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SCREEN_WIDTH_QP (320 * 4) /* gEXSetRectAlign offsets are quarter-pixels */

#define AERO_HUD_CURSOR_HOLDER 0x8016C508u /* fixed DL write-cursor holder (live-derived) */
#define AERO_SCENE_CUR         0x8013FF80u /* scene manager: current scene id (5 = race) */
#define AERO_SCENE_RACE        5u
#define AERO_SCENE_PHASE       0x8013FF88u /* scene-local phase (see aero_warp.c) */
#define AERO_RACE_CDOWN_STEP   0x8013FF38u /* race block +0x2B0: countdown step 0..3 */

static void emit_at(uint8_t* rdram, gpr* cur, uint32_t w0, uint32_t w1) {
    MEM_W(0, *cur) = (int32_t)w0;
    MEM_W(4, *cur) = (int32_t)w1;
    *cur += 8;
}

// RT64 only honours extended opcodes after the enable hook; emit it with every align
// (idempotent) since nothing else in this port enables the extended GBI.
static void emit_rect_align_at(uint8_t* rdram, gpr* cur, uint32_t origin, int32_t xoff) {
    emit_at(rdram, cur,
            PARAM(RT64_HOOK_OPCODE, 8, 24) | PARAM(RT64_HOOK_MAGIC_NUMBER, 24, 0),
            PARAM(RT64_HOOK_OP_ENABLE, 4, 28) | PARAM(RT64_EXTENDED_OPCODE, 8, 0));
    emit_at(rdram, cur,
            PARAM(RT64_EXTENDED_OPCODE, 8, 24) | PARAM(G_EX_SETRECTALIGN_V1, 24, 0),
            PARAM(origin, 12, 0) | PARAM(origin, 12, 12));
    emit_at(rdram, cur,
            PARAM(xoff, 16, 16) | PARAM(0, 16, 0),
            PARAM(xoff, 16, 16) | PARAM(0, 16, 0));
}

// The game's own scissor is untagged, so RT64 centres it on the 4:3 region and clips a
// moved element at the 4:3 edge. Bracket with a full-width scissor (ulx pinned LEFT at 0,
// lrx pinned RIGHT at 0 == the widened image's right edge), popped on close.
static void push_wide_scissor_at(uint8_t* rdram, gpr* cur) {
    emit_at(rdram, cur,
            PARAM(RT64_EXTENDED_OPCODE, 8, 24) | PARAM(G_EX_PUSHSCISSOR_V1, 24, 0),
            0);
    emit_at(rdram, cur,
            PARAM(RT64_EXTENDED_OPCODE, 8, 24) | PARAM(G_EX_SETSCISSOR_V1, 24, 0),
            PARAM(0, 2, 0) | PARAM(G_EX_ORIGIN_LEFT, 12, 2) | PARAM(G_EX_ORIGIN_RIGHT, 12, 14));
    emit_at(rdram, cur,
            PARAM(0, 16, 16) | PARAM(0, 16, 0),
            PARAM(0, 16, 16) | PARAM(240 * 4, 16, 0));
}

// A pin bracket = align to the anchored origin + a wide scissor so the moved rects are
// not clipped at the 4:3 edge. LEFT keeps original coordinates measured from the true
// left edge; RIGHT rebases them to the right edge (movedFromOrigin adds the full image
// width, so the original 320-wide space is subtracted back out -- peer-standard offset).
#define AERO_WS_BRACKET_OPEN_CMDS  6
#define AERO_WS_BRACKET_CLOSE_CMDS 4

static void bracket_open_at(uint8_t* rdram, gpr* cur, int anchor) {
    if (anchor == AERO_WS_PIN_RIGHT) {
        emit_rect_align_at(rdram, cur, G_EX_ORIGIN_RIGHT, -SCREEN_WIDTH_QP);
    } else {
        emit_rect_align_at(rdram, cur, G_EX_ORIGIN_LEFT, 0);
    }
    push_wide_scissor_at(rdram, cur);
}

static void bracket_close_at(uint8_t* rdram, gpr* cur) {
    emit_at(rdram, cur,
            PARAM(RT64_EXTENDED_OPCODE, 8, 24) | PARAM(G_EX_POPSCISSOR_V1, 24, 0),
            0);
    emit_rect_align_at(rdram, cur, G_EX_ORIGIN_NONE, 0);
}

// --- speedometer needle (RIGHT-anchored geometry) ---------------------------------------
// The dial ring is a texrect the retag pass pins; the orange needle is a single
// matrix-rotated TRIANGLE, so RT64 leaves it centred and it detaches from the pinned ring
// (issue #1 "split speedo"). There is no dedicated handler to bracket: the needle is one
// node of the generic recursive scene-graph walker func_800226AC, reached func_8001E8D8
// -> func_80022408 -> func_800222C0 -> func_800226AC. So we mirror the Lamborghini port's
// patch_load_mtx_dx: walk the DL the dispatcher emitted, find the needle's G_MTX, and add
// dx to its translate.x (element 12 = byte 24 int / byte 24+0x20 frac, s15.16 -- identical
// layout to Lamborghini's guMtxL). RT64 moves the ring by extAspectPercentage, so the
// needle shift scales off the SAME effective rect aspect (aero_ws_get_hud_rect_aspect_bits)
// -> 0 at 4:3/Original, so the walk is a no-op there (no config gate needed).
//
// The needle is pinpointed address-independently: its G_MTX (0x01020040, load modelview)
// is immediately followed by a G_DL branch to the STATIC needle mesh sub-DL 0x800995C0 (an
// orange-primcolor VTX+TRI1 ROM resource, live-derived). The matrix-pool address itself is
// double-buffered, so keying off the static mesh pointer is the only stable discriminator.

#define AERO_NEEDLE_MESH_ADDR 0x800995C0u /* static needle VTX+TRI1 sub-DL (live-derived) */

// 16:9 shift magnitude in the needle matrix's translate.x units. The unit is ONE
// 320-space pixel: live-logged at the hook, the needle modelview's translate.x int part
// is 114 and 160 + 114 = 274 = the needle's screen x -- and pixel-measuring a probe
// confirmed the ratio (a 41-unit shift moved the needle 158 px while the rect-aligned
// dial travelled 203 px; 41/53.3 == 158/205). So the needle must travel exactly the
// rects' 16:9 travel in the same space: 320 * (16/9 / (4/3) - 1) / 2 = 53.33 px. (The
// original hand calibration of 41 was systematically short -- the "split speedo" the
// user reported.) AERO_WS_NEEDLE_DX overrides it for recalibration.
#define AERO_WS_NEEDLE_DX 53.333f

// Bridge from rt64_renderer.cpp: the effective rect-pin aspect as raw float bits (a
// uint32_t crosses the C/C++ boundary without a shared float ABI assumption).
extern uint32_t aero_ws_get_hud_rect_aspect_bits(void);

static float aero_ws_needle_shift_scale(void) {
    uint32_t bits = aero_ws_get_hud_rect_aspect_bits();
    float aspect;
    memcpy(&aspect, &bits, sizeof(aspect));
    return aero_ws_hud_shift_scale_for_aspect(aspect);
}

// Steady-HUD gate shared by the needle shift and the retag pass; the policy (and why the
// countdown step participates) lives with aero_ws_hud_gate in aero_hud_widescreen.h.
static int aero_ws_pinnable_hud(uint8_t* rdram) {
    return aero_ws_hud_gate((uint32_t)MEM_W(0, (gpr)(int32_t)AERO_SCENE_CUR),
                            (uint32_t)MEM_W(0, (gpr)(int32_t)AERO_SCENE_PHASE),
                            (uint32_t)MEM_W(0, (gpr)(int32_t)AERO_RACE_CDOWN_STEP));
}

static void aero_ws_needle_shift(uint8_t* rdram, gpr start, gpr end) {
    float scale = aero_ws_needle_shift_scale();
    if (scale <= 0.0f) {
        return; /* 4:3 / non-Expand output: the ring pins don't travel either */
    }
    float dx = AERO_WS_NEEDLE_DX;
    const char* env = getenv("AERO_WS_NEEDLE_DX");
    if (env != NULL) {
        dx = (float)atof(env);
    }
    dx *= scale;

    gpr last_mtx = 0;
    for (gpr p = start; p + 8 <= end; p += 8) {
        uint32_t w0 = (uint32_t)MEM_W(0, p);
        uint32_t w1 = (uint32_t)MEM_W(4, p);
        // G_MTX (F3D style): 0x01xx0040, len 0x40. Track the most recent one; w1 is the
        // full 0x80xxxxxx matrix pointer (masked by the MEM_ accessors below).
        if ((w0 & 0xFF0000FFu) == 0x01000040u) {
            last_mtx = (gpr)(int32_t)w1;
            continue;
        }
        // G_DL branch (0x06) to the static needle mesh: the preceding G_MTX is the needle's.
        if ((w0 >> 24) == 0x06u && w1 == AERO_NEEDLE_MESH_ADDR && last_mtx != 0) {
            gpr mtx = last_mtx;
            int32_t ip = (int16_t)MEM_H(24, mtx);       /* translate.x integer */
            uint32_t fp = (uint16_t)MEM_HU(24 + 0x20, mtx); /* translate.x fraction */
            int32_t fixed = (int32_t)(((uint32_t)ip << 16) | fp);
            fixed += (int32_t)(dx * 65536.0f);
            MEM_H(24, mtx) = (int16_t)(fixed >> 16);
            MEM_HU(24 + 0x20, mtx) = (uint16_t)(fixed & 0xFFFF);
            return; /* one needle per frame */
        }
    }
}

// --- whole-frame texrect retag pass ------------------------------------------------------
// Re-emit the dispatcher's [start, end) with pin brackets inserted around runs of
// same-anchor rects. The stream is snapshotted to a native scratch copy first so the
// grown re-emit can safely overwrite the original in place; the cursor holder is then
// advanced past the new end (later frame-closing commands append there as normal).
//
// Safety posture (all verified against the mode-4 race capture, hud-pre dump):
//  - only runs in the race scene's steady phases (menus compose 4:3 layouts and the race
//    entry phases sweep full-screen wipe rects; neither must be pinned);
//  - skipped whole-frame if any G_DL in the range targets the range itself (re-emitting
//    would move the branch target; the real stream's sub-DL targets are all static);
//  - a bracket force-closes on the game's own raw G_SETSCISSOR (one occurs mid-HUD; a
//    raw scissor inside a bracket would defeat the wide scissor for the rects after it)
//    and on 3D commands (G_MTX/G_VTX/G_TRI1/G_DL), so geometry never inherits a bracket;
//  - rects already inside an explicit extended-GBI align (none today; defensive) pass
//    through untouched;
//  - bounded growth (~10 commands per bracket, ~6 brackets in the steady race HUD) with
//    a hard cap, against a measured >=4KB of free space after the frame DL's end.
// AERO_WS_RETAG=0 disables the pass (pre/post capture comparisons).

#define AERO_RETAG_MAX_CMDS   4096 /* steady race frame DL is ~3.2k commands total */
#define AERO_RETAG_MAX_GROWTH 2048 /* bytes; ~6 brackets * 10 cmds * 8B = 480B measured */

static uint32_t s_retag_w0[AERO_RETAG_MAX_CMDS];
static uint32_t s_retag_w1[AERO_RETAG_MAX_CMDS];

static int aero_ws_retag_enabled(void) {
    static int s_cached = -1;
    if (s_cached < 0) {
        const char* env = getenv("AERO_WS_RETAG");
        s_cached = (env == NULL || atoi(env) != 0) ? 1 : 0;
    }
    return s_cached;
}

static int aero_ws_is_geometry_op(uint32_t op) {
    return op == 0x01u /* G_MTX */ || op == 0x04u /* G_VTX */ ||
           op == 0x06u /* G_DL */ || op == 0xBFu /* G_TRI1 */;
}

// The retag state machine, one command per call, shared verbatim by the analysis and
// re-emit passes so the growth computed by the first is exact for the second.
// Returns the desired anchor after this command: AERO_WS_PIN_* on a transition point
// (rects carry their classification; raw scissors and 3D commands force NONE so a wipe
// or geometry never inherits a bracket), or -1 for "no state change".
static int aero_ws_retag_step(uint32_t w0, uint32_t w1, int* ext_aligned) {
    uint32_t op = w0 >> 24;
    if (op == RT64_EXTENDED_OPCODE && (w0 & 0xFFFFFFu) == G_EX_SETRECTALIGN_V1) {
        *ext_aligned = (w1 & 0xFFFu) != G_EX_ORIGIN_NONE;
    }
    if (op == 0xE4u || op == 0xE5u) {
        return *ext_aligned ? AERO_WS_PIN_NONE
                            : aero_ws_classify_rect_qp((int)((w1 >> 12) & 0xFFFu),
                                                       (int)((w0 >> 12) & 0xFFFu),
                                                       (int)(w1 & 0xFFFu));
    }
    if (op == 0xEDu || aero_ws_is_geometry_op(op)) {
        return AERO_WS_PIN_NONE;
    }
    return -1;
}

static void aero_ws_retag_rects(uint8_t* rdram, gpr start, gpr end) {
    if (!aero_ws_retag_enabled()) {
        return; /* caller (aero_ws_hud_frame_end) already applied the steady-race gate */
    }
    size_t n = (size_t)(end - start) / 8;
    if (n == 0 || n > AERO_RETAG_MAX_CMDS) {
        return;
    }

    // Snapshot + analysis: count bracket opens (for the growth cap) and abort on any
    // G_DL branching back into the range (29-bit physical compare, KSEG-agnostic).
    uint32_t phys_start = (uint32_t)start & 0x1FFFFFFFu;
    uint32_t phys_end = (uint32_t)end & 0x1FFFFFFFu;
    int opens = 0;
    int open_anchor = AERO_WS_PIN_NONE;
    int ext_aligned = 0;
    for (size_t i = 0; i < n; i++) {
        gpr p = start + (gpr)(i * 8);
        uint32_t w0 = s_retag_w0[i] = (uint32_t)MEM_W(0, p);
        uint32_t w1 = s_retag_w1[i] = (uint32_t)MEM_W(4, p);
        uint32_t op = w0 >> 24;
        if (op == 0x06u) {
            uint32_t tgt = w1 & 0x1FFFFFFFu;
            if (tgt >= phys_start && tgt < phys_end) {
                return; /* in-range branch: re-emitting would move its target */
            }
        }
        int anchor = aero_ws_retag_step(w0, w1, &ext_aligned);
        if (anchor >= 0 && anchor != open_anchor) {
            if (anchor != AERO_WS_PIN_NONE) {
                opens++;
            }
            open_anchor = anchor;
        }
    }
    if (opens == 0) {
        return;
    }
    if ((size_t)opens * (AERO_WS_BRACKET_OPEN_CMDS + AERO_WS_BRACKET_CLOSE_CMDS) * 8 >
        AERO_RETAG_MAX_GROWTH) {
        return;
    }

    // Re-emit with brackets, driven by the same aero_ws_retag_step state machine.
    gpr cur = start;
    open_anchor = AERO_WS_PIN_NONE;
    ext_aligned = 0;
    for (size_t i = 0; i < n; i++) {
        uint32_t w0 = s_retag_w0[i];
        uint32_t w1 = s_retag_w1[i];
        int anchor = aero_ws_retag_step(w0, w1, &ext_aligned);
        if (anchor >= 0 && anchor != open_anchor) {
            if (open_anchor != AERO_WS_PIN_NONE) {
                bracket_close_at(rdram, &cur);
            }
            if (anchor != AERO_WS_PIN_NONE) {
                bracket_open_at(rdram, &cur, anchor);
            }
            open_anchor = anchor;
        }
        emit_at(rdram, &cur, w0, w1);
    }
    if (open_anchor != AERO_WS_PIN_NONE) {
        bracket_close_at(rdram, &cur);
    }
    MEM_W(0, (gpr)(int32_t)AERO_HUD_CURSOR_HOLDER) = (int32_t)cur;
}

// --- dispatcher bracket (the [[patches.hook]] entry points) ------------------------------

static gpr s_hud_scan_start;
static int s_hud_scan_open;

// before_vram = 0x80022408 (func_80022408 entry). Latch the DL write cursor so the exit
// hook knows where the 2D dispatcher started appending.
void aero_ws_hud_scan_begin(uint8_t* rdram, recomp_context* ctx) {
    (void)ctx;
    s_hud_scan_start = MEM_W(0, (gpr)(int32_t)AERO_HUD_CURSOR_HOLDER);
    s_hud_scan_open = 1;
}

// Diagnostic probe (AERO_WS_TRACE=1, =2 adds per-rect dumps every 25th frame): per-frame
// race-scene log of the scene phase, the fade/wipe channel bytes (0x8019DDF0 +0x244/+0x245),
// the countdown step, the gate decision, and how the frame's rects classify. This is how
// the phase-2 countdown timeline in docs/notes/hud-widescreen.md was measured.
static void aero_ws_trace(uint8_t* rdram, gpr start, gpr end) {
    static int s_on = -1;
    if (s_on < 0) {
        const char* env = getenv("AERO_WS_TRACE");
        s_on = (env != NULL) ? atoi(env) : 0;
    }
    if (!s_on) {
        return;
    }
    uint32_t scene = (uint32_t)MEM_W(0, (gpr)(int32_t)AERO_SCENE_CUR);
    if (scene != AERO_SCENE_RACE) {
        return;
    }
    uint32_t phase = (uint32_t)MEM_W(0, (gpr)(int32_t)AERO_SCENE_PHASE);
    uint32_t frame = (uint32_t)MEM_W(0, (gpr)(int32_t)0x8013FC88u); /* race frame counter */
    uint32_t cdown = (uint32_t)MEM_W(0, (gpr)(int32_t)0x8013FF38u); /* countdown step +0x2B0 */
    int8_t w244 = (int8_t)MEM_B(0, (gpr)(int32_t)0x8019E034u);      /* fade ch A +0x244 */
    int8_t w245 = (int8_t)MEM_B(0, (gpr)(int32_t)0x8019E035u);      /* fade ch B +0x245 */
    int rects = 0, left = 0, right = 0, minulx = 4096, maxlrx = -1;
    int ext_aligned = 0;
    for (gpr p = start; p + 8 <= end; p += 8) {
        uint32_t w0 = (uint32_t)MEM_W(0, p);
        uint32_t w1 = (uint32_t)MEM_W(4, p);
        uint32_t op = w0 >> 24;
        if (op == 0xE4u || op == 0xE5u) {
            int ulx = (int)((w1 >> 12) & 0xFFFu), lrx = (int)((w0 >> 12) & 0xFFFu);
            int uly = (int)(w1 & 0xFFFu), lry = (int)(w0 & 0xFFFu);
            int cls = aero_ws_retag_step(w0, w1, &ext_aligned);
            rects++;
            if (cls == AERO_WS_PIN_LEFT) left++;
            if (cls == AERO_WS_PIN_RIGHT) right++;
            if (ulx < minulx) minulx = ulx;
            if (lrx > maxlrx) maxlrx = lrx;
            if (s_on >= 2 && (frame % 25) == 0) {
                fprintf(stderr, "[wsrect] frame=%u (%d,%d)-(%d,%d) cls=%d\n",
                        frame, ulx / 4, uly / 4, lrx / 4, lry / 4, cls);
            }
        }
    }
    fprintf(stderr,
            "[wstrace] frame=%u phase=%u cdown=%u wipe=%d/%d gate=%d rects=%d L=%d R=%d ulx_min=%d lrx_max=%d\n",
            frame, phase, cdown, (int)w244, (int)w245,
            aero_ws_hud_gate(scene, phase, cdown), rects, left, right,
            minulx == 4096 ? -1 : minulx / 4, maxlrx < 0 ? -1 : maxlrx / 4);
}

// before_vram = 0x80022680 (func_80022408 epilogue, after every child handler has appended
// its commands, before the register restores). Shift the needle matrix (on the original
// range), then re-emit the range with the per-rect pin brackets.
void aero_ws_hud_frame_end(uint8_t* rdram, recomp_context* ctx) {
    (void)ctx;
    if (!s_hud_scan_open) {
        return;
    }
    s_hud_scan_open = 0;
    aero_ws_trace(rdram, s_hud_scan_start, MEM_W(0, (gpr)(int32_t)AERO_HUD_CURSOR_HOLDER));
    if (!aero_ws_pinnable_hud(rdram)) {
        return;
    }
    gpr start = s_hud_scan_start;
    gpr end = MEM_W(0, (gpr)(int32_t)AERO_HUD_CURSOR_HOLDER);
    aero_ws_needle_shift(rdram, start, end);
    aero_ws_retag_rects(rdram, start, end);
}
