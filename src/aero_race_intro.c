// Race-intro drawing only: preserve the ROM's timers and text metrics.
#include "recomp.h"
#include "rt64_extended_gbi.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

extern uint32_t aero_ws_get_output_aspect_bits(void);
static float ticker_extra, fade_extra;
static int ticker_origin;

#define AERO_INTRO_BASE_WIDTH_QP (320 * 4)
#define AERO_INTRO_MAX_DX_QP (INT16_MAX - AERO_INTRO_BASE_WIDTH_QP)
#define AERO_INTRO_MAX_EXTRA ((float)AERO_INTRO_MAX_DX_QP / 4.0f)

static void intro_emit(uint8_t* rdram, gpr* p, uint32_t a, uint32_t b) {
    MEM_W(0, *p) = a;
    MEM_W(4, *p) = b;
    *p += 8;
}
static uint32_t xy(int x, int y) {
    return ((uint32_t)x & 0xffffu) << 16 | ((uint32_t)y & 0xffffu);
}
static void intro_op(uint8_t* rdram, gpr* p, unsigned op, unsigned arg) {
    intro_emit(rdram, p, (RT64_EXTENDED_OPCODE << 24) | op, arg);
}
static float intro_extra(uint8_t* rdram) {
    const char* enabled = getenv("AERO_WS_INTRO");
    if ((enabled && atoi(enabled) == 0) ||
        MEM_W(0, (gpr)(int32_t)0x8013FF80u) != 5) return 0;
    uint32_t bits = aero_ws_get_output_aspect_bits();
    float aspect;
    memcpy(&aspect, &bits, sizeof(aspect));
    if (!isfinite(aspect) || aspect <= 4.0f / 3.0f) return 0;
    // Extended rectangle coordinates are signed 16-bit quarter-pixels. Clamp the
    // extra width so both -dx and the 320-space right edge remain representable.
    float extra = 120.0f * aspect - 160.0f;
    return extra < AERO_INTRO_MAX_EXTRA ? extra : AERO_INTRO_MAX_EXTRA;
}
static int intro_dx(float extra) {
    float qpixels = extra * 4.0f;
    if (!isfinite(qpixels) || qpixels <= 0.0f) return 0;
    int dx = (int)ceilf(qpixels);
    return dx < AERO_INTRO_MAX_DX_QP ? dx : AERO_INTRO_MAX_DX_QP;
}
static void intro_open(uint8_t* rdram, gpr holder, float extra) {
    gpr p = MEM_W(0, holder);
    int dx = intro_dx(extra);
    // Intro runs before the steady HUD, which normally enables extended GBI.
    intro_emit(rdram, &p, (RT64_HOOK_OPCODE << 24) | RT64_HOOK_MAGIC_NUMBER,
               (RT64_HOOK_OP_ENABLE << 28) | RT64_EXTENDED_OPCODE);
    intro_op(rdram, &p, G_EX_PUSHSCISSOR_V1, 0);
    intro_op(rdram, &p, G_EX_SETSCISSOR_V1,
             (G_EX_ORIGIN_NONE << 2) | (G_EX_ORIGIN_NONE << 14));
    intro_emit(rdram, &p, xy(-dx, 0), xy(1280 + dx, 960));
    // Force aspect correction: the explicit coordinates already include the
    // extra width. Full-width auto-stretch would apply it a second time.
    intro_op(rdram, &p, G_EX_SETRECTASPECT_V1, G_EX_ASPECT_ADJUST);
    MEM_W(0, holder) = p;
}
static void intro_close(uint8_t* rdram, gpr holder) {
    gpr p = MEM_W(0, holder);
    intro_op(rdram, &p, G_EX_SETRECTASPECT_V1, G_EX_ASPECT_AUTO);
    intro_op(rdram, &p, G_EX_POPSCISSOR_V1, 0);
    MEM_W(0, holder) = p;
}
// Replace an ordinary E4/B4/B3 triplet with a signed extended rectangle of
// exactly the same size. Texture coordinates, derivatives and tile survive.
static void intro_rect(uint8_t* rdram, gpr p, int x0, int x1, int y0, int y1) {
    uint32_t w1 = MEM_W(4, p), st = MEM_W(12, p), delta = MEM_W(20, p);
    intro_op(rdram, &p, G_EX_TEXRECT_V1, ((w1 >> 24) & 7) |
             (G_EX_ORIGIN_NONE << 3) | (G_EX_ORIGIN_NONE << 15));
    intro_emit(rdram, &p, xy(x0, y0), xy(x1, y1));
    intro_emit(rdram, &p, st, delta);
}

void aero_intro_ticker_begin(uint8_t* rdram, recomp_context* ctx) {
    ticker_extra = intro_extra(rdram);
    if (ticker_extra > 0) intro_open(rdram, ctx->r4, ticker_extra);
}
void aero_intro_ticker_origin(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
    ticker_origin = (int32_t)ctx->r6; // max(230 - 3*frame, -500), ROM D860
}
void aero_intro_ticker_end(uint8_t* rdram, recomp_context* ctx) {
    if (ticker_extra > 0) intro_close(rdram, ctx->r29 + 0x2c);
    // intro_close rewrote sp+0x2c; the original sw t6, (t8) wants the new value.
    ctx->r14 = MEM_W(0x2c, ctx->r29);
    ticker_extra = 0;
}
void aero_intro_banner(uint8_t* rdram, recomp_context* ctx) {
    if (ticker_extra <= 0) return;
    // D97C ends with one banner triplet followed by a pipe sync.
    gpr p = MEM_W(0x64, ctx->r29) - 32;
    int dx = intro_dx(ticker_extra);
    intro_rect(rdram, p, -dx, 1280 + dx, 180 * 4, 203 * 4);
}
void aero_intro_glyph(uint8_t* rdram, recomp_context* ctx) {
    if (ticker_extra <= 0 || ctx->r2 == 0) return; // missing glyph emits nothing
    gpr p = MEM_W(0, MEM_W(0xe0, ctx->r29)) - 32;
    uint32_t a = MEM_W(0, p), b = MEM_W(4, p);
    // Read the unmasked x argument: offscreen glyphs can wrap the RDP's
    // original 12-bit coordinate field. Every string shares one scroll origin.
    int x = MEM_W(0xe4, ctx->r29);
    float ratio = 1.0f + ticker_extra / 160.0f;
    float shift = ticker_extra + 90.0f + (ticker_origin - 230) * (ratio - 1.0f);
    int left = (int)lroundf((x + shift) * 4);
    int width = (((a >> 12) & 4095) - ((b >> 12) & 4095)) & 4095;
    intro_rect(rdram, p, left, left + width, b & 4095, a & 4095);
}
void aero_intro_fade_begin(uint8_t* rdram, recomp_context* ctx) {
    fade_extra = intro_extra(rdram);
    if (fade_extra > 0) intro_open(rdram, ctx->r4, fade_extra);
}
void aero_intro_fade_end(uint8_t* rdram, recomp_context* ctx) {
    if (fade_extra <= 0) return;
    gpr holder = ctx->r29 + 0x34;
    gpr p = MEM_W(0, holder) - 32;
    int dx = intro_dx(fade_extra);
    intro_rect(rdram, p, -dx, 1280 + dx, 0, 960);
    intro_close(rdram, holder);
    fade_extra = 0;
}
