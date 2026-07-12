// Per-element widescreen HUD pinning (issue #1, RT64 extended GBI).
//
// Under `ar_option: Expand` RT64 renders untagged 2D centred in the 4:3 region, so the
// 1P race HUD's edge-anchored elements float inboard of the widescreen edges. Peer ports
// bracket each element's draw with gEXSetRectAlign; this repo has no MIPS patch pipeline,
// so the brackets are injected as N64Recomp [[patches.hook]] text (aerogauge.us.toml)
// around the drawing, calling the natives below. Structurally reused from the Lamborghini
// port's lambo_hud_widescreen.c (same extended-GBI emission, rect-align, wide-scissor
// push/pop, 4:3 no-op), but every address, cursor convention and per-element scope was
// RE-DERIVED live from THIS ROM (scratchpad/ATTRIBUTION.md) -- AeroGauge's HUD is drawn by
// object/table-driven handlers through a shared cursor holder, NOT Lamborghini's static
// per-group dispatcher call sites, so nothing carries over verbatim.
//
// Cursor convention (live-derived): AeroGauge's DL builder holds the write cursor in a
// fixed global at 0x8016C508 (cursor = MEM_W(0, holder)); every 2D helper reads it, stores
// a command, and advances it by 8. At a handler's entry that holder address is passed in
// a0 == ctx->r4, so each bracket saves ctx->r4 at entry and the matching reset re-reads the
// cursor through it (the register is clobbered inside the handler, hence the save). The DL
// is double-buffered (the cursor alternates between two RDRAM buffers frame to frame), so
// the holder -- never an absolute DL address -- is the only stable handle.
//
// SPEEDO (issue #1, first proven increment): the bottom-right speedometer (texrect box at
// screen x247..300) is drawn by func_80018CF0, a small handler that draws ONLY the speedo
// (~1 call/frame; live-verified func_8003A190 DAMAGE and the other HUD groups are off this
// path). A matched pair brackets it: the entry hook pins RIGHT + pushes a wide scissor; the
// reset hook (after the handler writes its cursor back, before its epilogue) pops + clears
// the alignment. The speedo is texrect-only -- no 3D geometry travels with it -- so unlike
// the Lamborghini needle/minimap there is no game-space matrix shift here, and thus no use
// of aero_ws_get_hud_rect_aspect_bits(): RT64 moves the tagged rects per hr_option itself,
// degenerating to the original coordinates at 4:3 / non-Expand, so no config gate is needed.

#include "recomp.h"
#include "rt64_extended_gbi.h"
#include "aero_hud_widescreen.h"
#include <stdlib.h>
#include <string.h>

#define SCREEN_WIDTH_QP (320 * 4) /* gEXSetRectAlign offsets are quarter-pixels */

static void emit_cmd(uint8_t* rdram, gpr holder, uint32_t w0, uint32_t w1) {
    gpr cur = MEM_W(0, holder);
    MEM_W(0, cur) = (int32_t)w0;
    MEM_W(4, cur) = (int32_t)w1;
    MEM_W(0, holder) = (int32_t)(cur + 8);
}

// RT64 only honours extended opcodes after the enable hook; emit it every bracket
// (idempotent) since nothing else in this port enables the extended GBI.
static void emit_rect_align(uint8_t* rdram, gpr holder, uint32_t origin, int32_t xoff) {
    emit_cmd(rdram, holder,
             PARAM(RT64_HOOK_OPCODE, 8, 24) | PARAM(RT64_HOOK_MAGIC_NUMBER, 24, 0),
             PARAM(RT64_HOOK_OP_ENABLE, 4, 28) | PARAM(RT64_EXTENDED_OPCODE, 8, 0));
    emit_cmd(rdram, holder,
             PARAM(RT64_EXTENDED_OPCODE, 8, 24) | PARAM(G_EX_SETRECTALIGN_V1, 24, 0),
             PARAM(origin, 12, 0) | PARAM(origin, 12, 12));
    emit_cmd(rdram, holder,
             PARAM(xoff, 16, 16) | PARAM(0, 16, 0),
             PARAM(xoff, 16, 16) | PARAM(0, 16, 0));
}

// The game's own scissor is untagged, so RT64 centres it on the 4:3 region and clips the
// moved element at the 4:3 edge. Bracket with a full-width scissor (ulx pinned LEFT at 0,
// lrx pinned RIGHT at 0 == the widened image's right edge), popped on reset.
static void push_wide_scissor(uint8_t* rdram, gpr holder) {
    emit_cmd(rdram, holder,
             PARAM(RT64_EXTENDED_OPCODE, 8, 24) | PARAM(G_EX_PUSHSCISSOR_V1, 24, 0),
             0);
    emit_cmd(rdram, holder,
             PARAM(RT64_EXTENDED_OPCODE, 8, 24) | PARAM(G_EX_SETSCISSOR_V1, 24, 0),
             PARAM(0, 2, 0) | PARAM(G_EX_ORIGIN_LEFT, 12, 2) | PARAM(G_EX_ORIGIN_RIGHT, 12, 14));
    emit_cmd(rdram, holder,
             PARAM(0, 16, 16) | PARAM(0, 16, 0),
             PARAM(0, 16, 16) | PARAM(240 * 4, 16, 0));
}

// --- speedometer (RIGHT-anchored) -------------------------------------------------------
// func_80018CF0 is drawn ONLY in the race (live-verified 453/453 calls in scene 5, 0 outside,
// over a full boot->menu->demo->1P-race run), so no scene gate is needed -- matching the
// Lamborghini reference and the 4:3 no-op above. The reset re-reads the cursor through the
// holder saved at entry; the flag keeps the pop balanced (the handler's cursor writeback runs
// on every path, so a saved-and-open bracket is always closable).
static gpr s_speedo_holder;
static int s_speedo_open;

// before_vram = 0x80018CF0 (func_80018CF0 entry). a0 == ctx->r4 == the cursor holder.
void aero_ws_speedo_pin(uint8_t* rdram, recomp_context* ctx) {
    gpr holder = (gpr)(int32_t)ctx->r4;
    // Rebase x to the right edge: movedFromOrigin adds the full image width, so the original
    // 320-wide coordinate space is subtracted back out (peer-standard offset).
    emit_rect_align(rdram, holder, G_EX_ORIGIN_RIGHT, -SCREEN_WIDTH_QP);
    push_wide_scissor(rdram, holder);
    s_speedo_holder = holder;
    s_speedo_open = 1;
}

// before_vram = 0x80018D5C (after func_80018CF0 writes its cursor back at 0x80018D58,
// before its epilogue). Pops the wide scissor and clears the sticky alignment.
void aero_ws_speedo_reset(uint8_t* rdram, recomp_context* ctx) {
    (void)ctx;
    if (!s_speedo_open) {
        return;
    }
    s_speedo_open = 0;
    emit_cmd(rdram, s_speedo_holder,
             PARAM(RT64_EXTENDED_OPCODE, 8, 24) | PARAM(G_EX_POPSCISSOR_V1, 24, 0),
             0);
    emit_rect_align(rdram, s_speedo_holder, G_EX_ORIGIN_NONE, 0);
}

// --- speedometer needle (RIGHT-anchored geometry) ---------------------------------------
// The dial ring above is a texrect that RT64 moves for us; the orange needle is a single
// matrix-rotated TRIANGLE, so RT64 leaves it centred and it detaches from the pinned ring
// (issue #1 "split speedo"). Unlike the ring there is no dedicated handler to bracket: the
// needle is one node of the generic recursive scene-graph walker func_800226AC, reached
// func_8001E8D8 -> func_80022408 -> func_800222C0 -> func_800226AC. So we mirror the
// Lamborghini port's patch_load_mtx_dx: bracket the whole 2D dispatcher func_80022408, and
// on exit walk the DL it emitted, find the needle's G_MTX, and add dx to its translate.x
// (element 12 = byte 24 int / byte 24+0x20 frac, s15.16 -- identical layout to Lamborghini's
// guMtxL). RT64 moves the ring by extAspectPercentage, so the needle shift scales off the
// SAME effective rect aspect (aero_ws_get_hud_rect_aspect_bits) -> 0 at 4:3/Original, so the
// walk is a no-op there (no config gate needed, matching the ring bracket).
//
// The needle is pinpointed address-independently: its G_MTX (0x01020040, load modelview) is
// immediately followed by a G_DL branch to the STATIC needle mesh sub-DL 0x800995C0 (an
// orange-primcolor VTX+TRI1 ROM resource, live-derived). The matrix-pool address itself is
// double-buffered, so keying off the static mesh pointer is the only stable discriminator.

#define AERO_HUD_CURSOR_HOLDER 0x8016C508u /* fixed DL write-cursor holder (live-derived) */
#define AERO_NEEDLE_MESH_ADDR  0x800995C0u /* static needle VTX+TRI1 sub-DL (live-derived) */

// 16:9 shift magnitude in the needle matrix's translate.x units (the projection-combined
// modelview, NOT Lamborghini's 10-units/px convention -- so 530 does NOT carry over).
// Live-calibrated 2026-07-12: at a >=16:9 Clamp16x9 window (internal scale == 1.0) the dial
// ring pins right by ~152 screen px and the needle travels ~3.73 px per unit, so 41 units
// lands the needle hub back on the dial (verified: needle centroid 1196 -> 1350 px, target
// 1348 = native offset preserved). AERO_WS_NEEDLE_DX overrides it for recalibration.
#define AERO_WS_NEEDLE_DX 41.0f

static float aero_ws_needle_shift_scale(void) {
    extern uint32_t aero_ws_get_hud_rect_aspect_bits(void);
    uint32_t bits = aero_ws_get_hud_rect_aspect_bits();
    float aspect;
    memcpy(&aspect, &bits, sizeof(aspect));
    return aero_ws_hud_shift_scale_for_aspect(aspect);
}

static gpr s_hud_scan_start;
static int s_hud_scan_open;

// before_vram = 0x80022408 (func_80022408 entry). Latch the DL write cursor so the exit hook
// knows where the 2D dispatcher started appending.
void aero_ws_hud_scan_begin(uint8_t* rdram, recomp_context* ctx) {
    (void)ctx;
    s_hud_scan_start = MEM_W(0, (gpr)(int32_t)AERO_HUD_CURSOR_HOLDER);
    s_hud_scan_open = 1;
}

// before_vram = 0x80022680 (func_80022408 epilogue, after every child handler has appended
// its commands, before the register restores). Walk [start, end) for the needle G_MTX and
// shift its matrix's translate.x so the needle tracks the RIGHT-pinned dial ring.
void aero_ws_needle_shift(uint8_t* rdram, recomp_context* ctx) {
    (void)ctx;
    if (!s_hud_scan_open) {
        return;
    }
    s_hud_scan_open = 0;

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

    gpr end = MEM_W(0, (gpr)(int32_t)AERO_HUD_CURSOR_HOLDER);
    gpr start = s_hud_scan_start;
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
