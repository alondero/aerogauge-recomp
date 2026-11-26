// Race-intro drawing recovered from the USA and Japan Rev A ROMs.
#include "recomp.h"
#include "aero_region.h"
#include "rt64_extended_gbi.h"
#include <stdbool.h>
#include <string.h>

extern uint32_t aero_ws_get_output_aspect_bits(void);

#define INTRO_BASE_WIDTH_QP (320 * 4)
#define INTRO_MAX_DX_QP (INT16_MAX - INTRO_BASE_WIDTH_QP)
#define INTRO_MAX_EXTRA ((float)INTRO_MAX_DX_QP / 4.0f)
#define GUEST_ADDR(value) ((gpr)(int32_t)(value))
#define INTRO_ADDR(us, jp) GUEST_ADDR(AERO_ADDR(us, jp))

// These translated callees still own formatting, glyph decoding and the
// other race overlays. Select their entry points at the regional ABI boundary.
#define INTRO_CALLEE(us, jp) AERO_BRANCH(func_##us, jp_func_##jp)
#define DECLARE_PAIR(us, jp) \
    extern recomp_func_t func_##us; \
    extern recomp_func_t jp_func_##jp

DECLARE_PAIR(8001F6B4, 8001FF64); // text position
DECLARE_PAIR(8001F75C, 80020014); // text color
DECLARE_PAIR(8001F790, 80020048); // stock text run
DECLARE_PAIR(8001F998, 8002025C); // stock glyph
DECLARE_PAIR(800199A0, 8001A3D8);
DECLARE_PAIR(80019B90, 8001A5D0);
DECLARE_PAIR(8001C030, 8001C7C0);
DECLARE_PAIR(80009488, 80009A0C);
DECLARE_PAIR(8000F990, 8000FE60);
DECLARE_PAIR(80019508, 80019FB4);
extern recomp_func_t func_8000D670;
extern recomp_func_t func_8006BA04;

#undef DECLARE_PAIR

// Outgoing o32 argument area, a cursor holder, and a private glyph list in
// the guest game thread's stack. Both protected ROM glyph implementations
// emit at most twelve commands (0x60 bytes) per call.
enum {
    INTRO_CALL_HOLDER = 0x18,
    INTRO_GLYPH_LIST = 0x20,
    INTRO_CALL_FRAME = INTRO_GLYPH_LIST + 0x80,
};

typedef struct {
    uint8_t* rdram;
    recomp_context* ctx;
    gpr cursor;
} IntroDraw;

static gpr call_guest(IntroDraw* draw, recomp_func_t* function,
                      gpr a0, gpr a1, gpr a2, gpr a3, gpr a4) {
    uint8_t* rdram = draw->rdram;
    recomp_context* ctx = draw->ctx;
    gpr sp = ctx->r29;
    ctx->r29 = sp - INTRO_CALL_FRAME;
    ctx->r4 = a0;
    ctx->r5 = a1;
    ctx->r6 = a2;
    ctx->r7 = a3;
    MEM_W(0x10, ctx->r29) = (int32_t)a4;
    function(rdram, ctx);
    ctx->r29 = sp;
    return ctx->r2;
}

static void draw_guest(IntroDraw* draw, recomp_func_t* function, gpr text, gpr font) {
    uint8_t* rdram = draw->rdram;
    gpr holder = draw->ctx->r29 - INTRO_CALL_FRAME + INTRO_CALL_HOLDER;
    MEM_W(0, holder) = (int32_t)draw->cursor;
    call_guest(draw, function, holder, text, font, 0, 0);
    draw->cursor = MEM_W(0, holder);
}

static void emit(IntroDraw* draw, uint32_t w0, uint32_t w1) {
    uint8_t* rdram = draw->rdram;
    MEM_W(0, draw->cursor) = w0;
    MEM_W(4, draw->cursor) = w1;
    draw->cursor += 8;
}

static uint32_t xy(int x, int y) {
    return ((uint32_t)x & 0xffffu) << 16 | ((uint32_t)y & 0xffffu);
}

static void extended_op(IntroDraw* draw, unsigned op, unsigned arg) {
    emit(draw, (RT64_EXTENDED_OPCODE << 24) | op, arg);
}

static float intro_extra(uint8_t* rdram) {
    const char* enabled = getenv("AERO_WS_INTRO");
    if ((enabled && atoi(enabled) == 0) ||
        MEM_W(0, INTRO_ADDR(0x8013FF80u, 0x8013D000u)) != 5) {
        return 0;
    }
    uint32_t bits = aero_ws_get_output_aspect_bits();
    float aspect;
    memcpy(&aspect, &bits, sizeof(aspect));
    if (!isfinite(aspect) || aspect <= 4.0f / 3.0f) {
        return 0;
    }
    // Leave space for the 320-space right edge in signed quarter-pixels.
    float extra = 120.0f * aspect - 160.0f;
    return fminf(extra, INTRO_MAX_EXTRA);
}

static int intro_dx(float extra) {
    return (int)ceilf(extra * 4.0f);
}

static void intro_open(IntroDraw* draw, int dx) {
    // Intro runs before the steady HUD, which normally enables extended GBI.
    emit(draw, (RT64_HOOK_OPCODE << 24) | RT64_HOOK_MAGIC_NUMBER,
         (RT64_HOOK_OP_ENABLE << 28) | RT64_EXTENDED_OPCODE);
    extended_op(draw, G_EX_PUSHSCISSOR_V1, 0);
    extended_op(draw, G_EX_SETSCISSOR_V1, (G_EX_ORIGIN_NONE << 2) | (G_EX_ORIGIN_NONE << 14));
    emit(draw, xy(-dx, 0), xy(INTRO_BASE_WIDTH_QP + dx, 960));
    // Explicit coordinates already include the extra width.
    extended_op(draw, G_EX_SETRECTASPECT_V1, G_EX_ASPECT_ADJUST);
}

static void intro_close(IntroDraw* draw) {
    extended_op(draw, G_EX_SETRECTASPECT_V1, G_EX_ASPECT_AUTO);
    extended_op(draw, G_EX_POPSCISSOR_V1, 0);
}

static void extended_rect(IntroDraw* draw, int x0, int y0, int x1, int y1,
                          unsigned tile, uint32_t st, uint32_t delta) {
    extended_op(draw, G_EX_TEXRECT_V1, tile | (G_EX_ORIGIN_NONE << 3) | (G_EX_ORIGIN_NONE << 15));
    emit(draw, xy(x0, y0), xy(x1, y1));
    emit(draw, st, delta);
}

static void draw_strip(IntroDraw* draw, int dx, int x0, int y0, int x1, int y1) {
    if (dx > 0) {
        extended_rect(draw, -dx, y0, INTRO_BASE_WIDTH_QP + dx, y1, 0, 0, 0x04000400);
    } else {
        emit(draw, 0xe4000000u | (x1 << 12) | y1, (x0 << 12) | y0);
        emit(draw, 0xb4000000, 0);
        emit(draw, 0xb3000000, 0x04000400);
    }
    emit(draw, 0xe7000000, 0);
}

static void draw_banner(IntroDraw* draw, unsigned alpha, int dx) {
    emit(draw, 0xe7000000, 0);
    emit(draw, 0xba001402, 0);
    emit(draw, 0xb900031d, 0x00504240);
    emit(draw, 0xfb000000, 0xffffff00u | (alpha & 0xff));
    emit(draw, 0xfcffffff, 0xfffcfa7d);
    emit(draw, 0xba001301, 0);
    emit(draw, 0xba001001, 0);
    emit(draw, 0xba000c02, 0x2000);
    emit(draw, 0xba000903, 0x0c00);

    emit(draw, 0xfd100000, 0x0100a268); // palette
    emit(draw, 0xe8000000, 0);
    emit(draw, 0xf5000100, 0x07000000);
    emit(draw, 0xe6000000, 0);
    emit(draw, 0xf0000000, 0x0703c000);
    emit(draw, 0xe7000000, 0);
    emit(draw, 0xba000e02, 0x8000);
    emit(draw, 0xfd480007, 0x0100a1a8); // banner tile
    emit(draw, 0xf5480200, 0x07080040);
    emit(draw, 0xe6000000, 0);
    emit(draw, 0xf4000000, 0x0701e058);
    emit(draw, 0xe7000000, 0);
    emit(draw, 0xf5400200, 0x00080040);
    emit(draw, 0xf2000000, 0x0003c058);
    draw_strip(draw, dx, 16 * 4, 180 * 4, 303 * 4, 203 * 4);
}

static void draw_ticker_text(IntroDraw* draw, gpr text, float extra, int origin) {
    uint8_t* rdram = draw->rdram;
    gpr font = INTRO_ADDR(0x8008BEBCu, 0x8008BA5Cu);
    if (extra <= 0) {
        draw_guest(draw, INTRO_CALLEE(8001F790, 80020048), text, font);
        return;
    }

    // The shared text style and pen fields belong to the ROM text renderer.
    gpr pen = INTRO_ADDR(0x8008C8DCu, 0x8008C47Cu);
    gpr advance = INTRO_ADDR(0x8008C910u, 0x8008C4B0u);
    // Fixed setup from the USA and Rev A text-run entry points.
    emit(draw, 0xe7000000, 0);
    emit(draw, 0xba001402, 0);
    emit(draw, 0xbb000001, 0x08000800);
    emit(draw, 0xba000e02, 0);
    emit(draw, 0xba001301, 0);
    emit(draw, 0xb900031d, 0x00504240);
    emit(draw, 0xfc119623, 0xff2fffff);
    emit(draw, 0xb9000002, 0);
    emit(draw, 0xba000402, 0x30);

    float ratio = 1.0f + extra / 160.0f;
    float shift = extra + 90.0f + (origin - 230) * (ratio - 1.0f);
    gpr frame = draw->ctx->r29 - INTRO_CALL_FRAME;
    gpr holder = frame + INTRO_CALL_HOLDER;
    gpr glyph_list = frame + INTRO_GLYPH_LIST;
    for (unsigned ch; (ch = MEM_BU(0, text)) != 0; ++text) {
        int x = MEM_W(0, pen);
        MEM_W(0, holder) = (int32_t)glyph_list;
        gpr glyph = call_guest(draw, INTRO_CALLEE(8001F998, 8002025C), holder,
                               (gpr)x, MEM_W(4, pen), ch, font);
        if (!glyph) {
            continue;
        }

        // Keep the shared glyph decoder and texture setup. Convert its single
        // E4/B4/B3 rectangle while copying from scratch into the live list.
        gpr end = MEM_W(0, holder);
        for (gpr p = glyph_list; p < end; p += 8) {
            uint32_t w0 = MEM_W(0, p);
            uint32_t w1 = MEM_W(4, p);
            if ((w0 >> 24) != 0xe4) {
                emit(draw, w0, w1);
                continue;
            }
            int left = (int)lroundf((x + shift) * 4);
            int width = (((w0 >> 12) & 4095) - ((w1 >> 12) & 4095)) & 4095;
            extended_rect(draw, left, w1 & 4095, left + width, w0 & 4095,
                          (w1 >> 24) & 7, MEM_W(12, p), MEM_W(20, p));
            p += 16;
        }

        int spacing = MEM_W(0, advance);
        MEM_W(0, pen) = ADD32(x, spacing < 0 ? MEM_H(8, glyph) : spacing);
    }
}

static void set_text_position(IntroDraw* draw, int x) {
    call_guest(draw, INTRO_CALLEE(8001F6B4, 8001FF64), (gpr)x, 185, 0, 0, 0);
}

static void set_text_color(IntroDraw* draw, unsigned r, unsigned g, unsigned b, unsigned alpha) {
    call_guest(draw, INTRO_CALLEE(8001F75C, 80020014), r, g, b, alpha & 0xff, 0);
}

// The ticker uses these region-specific game-thread globals during the race
// intro. Frame and alpha are words, the USA phase anchor is a signed halfword,
// and the adjacent input flags and race settings keep their original widths.
static void draw_ticker(IntroDraw* draw) {
    uint8_t* rdram = draw->rdram;
    gpr clock = INTRO_ADDR(0x8013FC88u, 0x8013CD08u);
    gpr race = INTRO_ADDR(0x8013FF90u, 0x8013D010u);
    gpr alpha = INTRO_ADDR(0x80082148u, 0x80081EB0u);
    float extra = intro_extra(rdram);
    int32_t frame = MEM_W(0, clock);
    int mode = MEM_B(0, race);
    int32_t elapsed = AERO_IS_JP ? 0 : SUB32(frame, MEM_H(0x2c0, clock));
    int origin;
    if (AERO_IS_JP) {
        int32_t scroll = ADD32(frame, frame);
        origin = SUB32(230, scroll < 500 ? scroll : 500);
    } else {
        int32_t scroll = ADD32(ADD32(frame, frame), frame);
        origin = SUB32(230, scroll);
        if (origin < -500) {
            origin = -500;
        }
    }

    if (frame == 2) {
        MEM_W(0, alpha) = 255;
        if (!AERO_IS_JP) {
            gpr result = mode == 3 ? call_guest(draw, func_8000D670, 0, 0, 0, 0, 0) : 0;
            MEM_W(0, GUEST_ADDR(0x80082120u)) = (int32_t)result;
        }
    }
    if (AERO_IS_JP) {
        set_text_position(draw, origin);
    }

    unsigned flags = MEM_HU(4, clock);
    if (mode == 3) {
        gpr transitions = INTRO_ADDR(0x8019E034u, 0x8019B034u);
        if (frame == 150) {
            MEM_B(1, transitions) = 30;
        }
        if (frame == 180 && !(flags & 0x4000)) {
            MEM_B(0, transitions) = 30;
        }
    }

    int32_t opacity = MEM_W(0, alpha);
    if (AERO_IS_JP) {
        if (mode == 4) {
            if ((uint32_t)frame >= 150) {
                opacity = SUB32(opacity, 8);
            }
            MEM_W(0, alpha) = opacity > 0 ? opacity : 0;
        }
    } else {
        bool championship_start = mode == 3 && !(flags & 0x4000);
        if (championship_start ? (uint32_t)frame >= 171 : elapsed >= 96) {
            opacity = SUB32(opacity, 25);
            MEM_W(0, alpha) = opacity > 0 ? opacity : 0;
        }
        set_text_position(draw, origin);
    }

    int dx = intro_dx(extra);
    if (dx > 0) {
        intro_open(draw, dx);
    }
    set_text_color(draw, 206, 206, 206, MEM_BU(3, alpha));
    if (AERO_IS_JP) {
        // The course order follows the Rev A jump table at 0x80093114.
        static const uint32_t captions[][2] = {
            {0x80092f80, 0x80092fa0}, {0x80093018, 0x80093040},
            {0x80093064, 0x80093090}, {0x80092fc0, 0x80092fec},
            {0x800930bc, 0x800930e8},
        };
        unsigned course = MEM_BU(11, race);
        if ((uint32_t)frame < 181 && course < sizeof(captions) / sizeof(captions[0])) {
            draw_banner(draw, MEM_W(0, alpha), dx);
            if (course == 2 || course == 4) {
                set_text_position(draw, origin - 30);
            }
            if (mode == 3 || mode == 4) {
                draw_ticker_text(draw, (gpr)(int32_t)captions[course][mode == 3], extra, origin);
            }
        }
    } else if (elapsed < 106) {
        draw_banner(draw, MEM_W(0, alpha), dx);
        if (mode == 3 || mode == 4) {
            int course = MEM_B(11, race);
            gpr delta = GUEST_ADDR(0x80082124u);
            gpr text = GUEST_ADDR(0x8013F7E0u);
            call_guest(draw, func_8006BA04, text, GUEST_ADDR(0x80095538u), course + 1,
                       MEM_W(course * 4, delta), GUEST_ADDR(0x80095500u) + (mode == 3 ? 8 : 0));
            draw_ticker_text(draw, text, extra, origin);
        }
        if (mode == 3) {
            set_text_color(draw, 181, 96, 20, MEM_BU(3, alpha));
            int result = MEM_W(0, GUEST_ADDR(0x80082120u));
            gpr text = GUEST_ADDR(0x8013F830u);
            gpr format = GUEST_ADDR(0x80095524u);
            if (result > 0) {
                format = GUEST_ADDR(0x80095510u);
            }
            call_guest(draw, func_8006BA04, text, format, (gpr)result, 0, 0);
            draw_ticker_text(draw, text, extra, origin);
        }
    }
    if (dx > 0) {
        intro_close(draw);
    }
}

static void draw_fade(IntroDraw* draw, unsigned alpha, int dx) {
    uint8_t* rdram = draw->rdram;
    gpr rgb = INTRO_ADDR(0x80097110u, 0x800949F0u); // color index zero
    if (dx > 0) {
        intro_open(draw, dx);
    }
    emit(draw, 0xe7000000, 0);
    emit(draw, 0xba001402, 0);
    emit(draw, 0xb900031d, 0x00504340);
    emit(draw, 0xba000402, 0x30);
    emit(draw, 0xfcffffff, 0xfffdf6fb);
    emit(draw, 0xfa000000, ((uint32_t)MEM_BU(0, rgb) << 24)
         | ((uint32_t)MEM_BU(1, rgb) << 16) | ((uint32_t)MEM_BU(2, rgb) << 8) | alpha);
    if (dx > 0) {
        draw_strip(draw, dx, 0, 0, 0, 240 * 4);
        intro_close(draw);
    } else {
        draw_strip(draw, 0, 0, 8 * 4, 319 * 4, 231 * 4);
    }
}

static void draw_intro_overlays(IntroDraw* draw, gpr state) {
    uint8_t* rdram = draw->rdram;
    gpr scene = INTRO_ADDR(0x8013FF80u, 0x8013D000u);
    gpr race = INTRO_ADDR(0x8013FF90u, 0x8013D010u);
    int mode = MEM_B(0, race);
    int phase = MEM_W(8, scene);
    emit(draw, 0xed040020, 0x004bc39c);
    if ((mode == 3 || mode == 4) && (phase == 2 || (!AERO_IS_JP && phase == 0))) {
        draw_ticker(draw);
    }

    unsigned flags = MEM_HU(0, INTRO_ADDR(0x8013FC8Cu, 0x8013CD0Cu));
    if (flags & 0x1000) {
        mode = MEM_B(0, race);
        if (mode == 9 || mode == 10) {
            draw_guest(draw, INTRO_CALLEE(8001C030, 8001C7C0), 0, 0);
        } else if (mode == 7 && !call_guest(draw, INTRO_CALLEE(80009488, 80009A0C), 0, 0, 0, 0, 0)) {
            draw_guest(draw, INTRO_CALLEE(8000F990, 8000FE60), 0, 0);
        }
    } else if (flags & 2) {
        draw_guest(draw, INTRO_CALLEE(800199A0, 8001A3D8), 0, 0);
        draw_guest(draw, INTRO_CALLEE(80019B90, 8001A5D0), 0, 0);
    }

    // Signed frame counters at +0x244/+0x245 of the race overlay state.
    // The ROM maps a negative fade-in opacity to the low byte of UINT32_MAX.
    int fade_out = MEM_B(0x244, state);
    if (fade_out >= 0) {
        draw_fade(draw, (unsigned)(fade_out * 255.0f / 30.0f) & 0xff, intro_dx(intro_extra(rdram)));
        MEM_B(0x244, state) = fade_out - 1;
    }
    int fade_in = MEM_B(0x245, state);
    if (fade_in >= 0) {
        float alpha = (1.0f - fade_in / 30.0f) * 255.0f;
        draw_fade(draw, (alpha < 0 ? 255 : (unsigned)alpha) & 0xff, intro_dx(intro_extra(rdram)));
        MEM_B(0x245, state) = fade_in - 1;
    }

    if (MEM_W(8, scene) == 6) {
        draw_guest(draw, INTRO_CALLEE(80019508, 80019FB4), 0, 0);
    }
}

// Whole-entry adapters. Cursor holders and the overlay state are guest
// pointers supplied by the callers through the original o32 ABI.
void aero_intro_draw_ticker(uint8_t* rdram, recomp_context* ctx) {
    gpr holder = ctx->r4;
    IntroDraw draw = {rdram, ctx, MEM_W(0, holder)};
    draw_ticker(&draw);
    MEM_W(0, holder) = (int32_t)draw.cursor;
}

void aero_intro_draw_banner(uint8_t* rdram, recomp_context* ctx) {
    gpr holder = ctx->r4;
    IntroDraw draw = {rdram, ctx, MEM_W(0, holder)};
    draw_banner(&draw, (unsigned)ctx->r5, 0);
    MEM_W(0, holder) = (int32_t)draw.cursor;
}

void aero_intro_draw_overlays(uint8_t* rdram, recomp_context* ctx) {
    gpr holder = ctx->r4;
    IntroDraw draw = {rdram, ctx, MEM_W(0, holder)};
    draw_intro_overlays(&draw, ctx->r5);
    MEM_W(0, holder) = (int32_t)draw.cursor;
}
