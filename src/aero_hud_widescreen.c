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
