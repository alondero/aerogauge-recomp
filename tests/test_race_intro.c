// Compare the normal recompiled entry points with the same ROM code unhooked.
#undef NDEBUG
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "recomp.h"
#include "aero_region.h"
#include "rt64_extended_gbi.h"

#ifdef AERO_JAPAN_SUPPORT
int aero_japan;
#endif

#define ADDRESS(value) ((gpr)(int32_t)(value))
#define ADDRESS_PAIR(us, jp) ADDRESS(AERO_ADDR(us, jp))
#define FUNCTION_PAIR(us, jp) AERO_BRANCH(func_##us, jp_func_##jp)
#define REFERENCE_PAIR(us, jp) AERO_BRANCH(reference_func_##us, reference_jp_func_##jp)
#define DECLARE_ENTRY(us, jp) \
    extern recomp_func_t func_##us, jp_func_##jp; \
    extern recomp_func_t reference_func_##us, reference_jp_func_##jp

DECLARE_ENTRY(8000D708, 8000DDA0);
DECLARE_ENTRY(8000D97C, 8000E144);
DECLARE_ENTRY(800191FC, 80019C94);

_Alignas(8) static uint8_t ram[8 * 1024 * 1024];
static uint8_t* rdram = ram;
static const gpr list = ADDRESS(0x80200000u);
static const gpr cursor = ADDRESS(0x80300000u);
static const gpr state = ADDRESS(0x80301000u);
static float output_aspect;
static int replay_status;

uint32_t aero_ws_get_output_aspect_bits(void) {
    uint32_t bits;
    memcpy(&bits, &output_aspect, sizeof(bits));
    return bits;
}

void switch_error(const char* func, uint32_t vram, uint32_t table) {
    fprintf(stderr, "Unexpected jump table branch in %s at %08x, table %08x\n", func, vram, table);
    abort();
}

typedef struct {
    gpr format, arg1, arg2, arg3;
} FormatCall;
static FormatCall formatted[2];
static unsigned format_count;

// The formatter is outside the drawing boundary. Verify its arguments, then
// feed the same fixture to the actual ROM text and glyph routines.
void func_8006BA04(uint8_t* rdram, recomp_context* ctx) {
    assert(format_count < 2);
    FormatCall* call = &formatted[format_count++];
    *call = (FormatCall){.format = ctx->r5};
    if (ctx->r5 == ADDRESS(0x80095538)) {
        call->arg1 = ctx->r6;
        call->arg2 = ctx->r7;
        call->arg3 = MEM_W(0x10, ctx->r29);
    } else if (ctx->r5 == ADDRESS(0x80095510)) {
        call->arg1 = ctx->r6;
    } else {
        assert(ctx->r5 == ADDRESS(0x80095524));
    }
    static const char text[] = "A?B";
    for (size_t i = 0; i < sizeof(text); ++i) {
        MEM_B(i, ctx->r4) = text[i];
    }
}

static void overlay_marker(uint8_t* rdram, recomp_context* ctx, unsigned marker) {
    gpr p = MEM_W(0, ctx->r4);
    MEM_W(0, p) = 0;
    MEM_W(4, p) = marker;
    MEM_W(0, ctx->r4) = p + 8;
}
#define OVERLAY_STUB(name, marker) \
    void name(uint8_t* rdram, recomp_context* ctx) { overlay_marker(rdram, ctx, marker); }

OVERLAY_STUB(func_800199A0, 1)
OVERLAY_STUB(func_80019B90, 2)
OVERLAY_STUB(func_8001C030, 3)
OVERLAY_STUB(func_8000F990, 4)
OVERLAY_STUB(func_80019508, 5)
OVERLAY_STUB(jp_func_8001A3D8, 1)
OVERLAY_STUB(jp_func_8001A5D0, 2)
OVERLAY_STUB(jp_func_8001C7C0, 3)
OVERLAY_STUB(jp_func_8000FE60, 4)
OVERLAY_STUB(jp_func_80019FB4, 5)

void func_80009488(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
    ctx->r2 = replay_status;
}
void jp_func_80009A0C(uint8_t* rdram, recomp_context* ctx) {
    func_80009488(rdram, ctx);
}

// Unreachable tail of the Japanese texture-coordinate helper's symbol span.
#define UNREACHABLE(name) \
    void name(uint8_t* rdram, recomp_context* ctx) { (void)rdram; (void)ctx; abort(); }
UNREACHABLE(jp_func_8006C630)
UNREACHABLE(jp_func_80024F00)
UNREACHABLE(jp_func_8006C520)
UNREACHABLE(jp_func_800265B0)

typedef struct {
    int mode, phase, frame, anchor, course, flags;
    int fade_out, fade_in, spacing, points, scene;
} Fixture;

static void seed_text(void) {
    gpr font = ADDRESS_PAIR(0x8008bebc, 0x8008ba5c);
    gpr alphabet = ADDRESS(0x80302000u);
    gpr glyphs = ADDRESS(0x80302100u);
    gpr textures = ADDRESS(0x80302200u);
    MEM_W(0, font) = alphabet;
    MEM_W(4, font) = glyphs;
    MEM_B(0, alphabet) = 'A';
    MEM_B(1, alphabet) = 'B';
    for (int i = 0; i < 2; ++i) {
        gpr glyph = glyphs + i * 12;
        gpr texture = textures + i * 0x20;
        MEM_W(0, glyph) = texture;
        MEM_H(4, glyph) = 1;
        MEM_H(6, glyph) = 2;
        MEM_H(8, glyph) = 10 + i;
        MEM_H(10, glyph) = 14;
        int texture_format = i == 0 ? 4 : 10;
        MEM_W(0, texture) = 0x01004000;
        MEM_W(4, texture) = texture_format;
        MEM_H(8, texture) = 16;
        MEM_B(0x14, texture) = 2;
        MEM_B(0x15, texture) = 2;
        gpr format = ADDRESS_PAIR(0x8008c914, 0x8008c4b4) + texture_format * 12;
        MEM_W(0, format) = 0x3f800000;
        MEM_W(8, format) = 2;
    }
    gpr derivatives = ADDRESS_PAIR(0x8008c900, 0x8008c4a0);
    MEM_W(0, derivatives) = 0x3ff00000;
    MEM_W(8, derivatives) = 0x3ff00000;

    // Distinct captions make it possible to compare all JP jump-table arms
    // and both race modes without embedding the shipped strings.
    static const uint32_t captions[] = {
        0x80092f80, 0x80092fa0, 0x80092fc0, 0x80092fec, 0x80093018,
        0x80093040, 0x80093064, 0x80093090, 0x800930bc, 0x800930e8,
    };
    for (unsigned i = 0; i < sizeof(captions) / sizeof(captions[0]); ++i) {
        gpr text = ADDRESS(captions[i]);
        MEM_B(0, text) = i % 2 ? 'B' : 'A';
        MEM_B(1, text) = '?';
        MEM_B(2, text) = (i / 2) % 2 ? 'B' : 'A';
        MEM_B(3, text) = (i / 2) % 3 ? 'A' : 0;
    }
}

static void setup(Fixture f) {
    memset(ram, 0, sizeof(ram));
    gpr scene = ADDRESS_PAIR(0x8013ff80, 0x8013d000);
    gpr clock = ADDRESS_PAIR(0x8013fc88, 0x8013cd08);
    gpr rgb = ADDRESS_PAIR(0x80097110, 0x800949f0);
    MEM_W(0, scene) = f.scene;
    MEM_W(8, scene) = f.phase;
    MEM_B(0x10, scene) = f.mode;
    MEM_B(0x1b, scene) = f.course;
    MEM_W(0, clock) = f.frame;
    MEM_H(4, clock) = f.flags;
    if (!AERO_IS_JP) {
        MEM_H(0x2c0, clock) = f.anchor;
        MEM_BU(0, ADDRESS(0x80097988u)) = 3;
        MEM_BU(1, ADDRESS(0x8008213cu)) = 9;
        MEM_BU(0, ADDRESS(0x8019ed3au)) = 2;
        for (int i = 0; i < 5; ++i) {
            MEM_W(i * 4, ADDRESS(0x80082124u)) = 8 + i;
        }
        MEM_W(0, ADDRESS(0x80082120u)) = f.points;
    }
    MEM_W(0, ADDRESS_PAIR(0x80082148, 0x80081eb0)) = 240;
    MEM_W(0, ADDRESS_PAIR(0x8008c910, 0x8008c4b0)) = f.spacing;
    MEM_B(0, rgb) = 11;
    MEM_B(1, rgb) = 23;
    MEM_B(2, rgb) = 37;
    MEM_B(0x244, state) = f.fade_out;
    MEM_B(0x245, state) = f.fade_in;
    MEM_W(0, cursor) = (int32_t)list;
    seed_text();
    format_count = 0;
    memset(formatted, 0, sizeof(formatted));
}

typedef struct {
    uint32_t words[1024];
    size_t word_count;
    int fade_out, fade_in, alpha, text_x, points;
    uint16_t transitions;
    unsigned format_count;
    FormatCall formatted[2];
} Result;

static Result run(recomp_func_t* entry, Fixture f, gpr arg, float aspect) {
    setup(f);
    output_aspect = aspect;
    recomp_context ctx = {0};
    ctx.r29 = ADDRESS(0x807ff000u);
    ctx.r4 = cursor;
    ctx.r5 = arg;
    ctx.f_odd = &ctx.f0.u32h;
    entry(rdram, &ctx);
    assert(ctx.r29 == ADDRESS(0x807ff000u));

    gpr end = MEM_W(0, cursor);
    Result result = {0};
    assert(end >= list && (end - list) % 8 == 0);
    result.word_count = (end - list) / 4;
    assert(result.word_count <= sizeof(result.words) / sizeof(result.words[0]));
    for (size_t i = 0; i < result.word_count; ++i) {
        result.words[i] = MEM_W(i * 4, list);
    }
    result.fade_out = MEM_B(0x244, state);
    result.fade_in = MEM_B(0x245, state);
    result.alpha = MEM_W(0, ADDRESS_PAIR(0x80082148, 0x80081eb0));
    result.text_x = MEM_W(0, ADDRESS_PAIR(0x8008c8dc, 0x8008c47c));
    result.transitions = MEM_HU(0, ADDRESS_PAIR(0x8019e034, 0x8019b034));
    if (!AERO_IS_JP) {
        result.points = MEM_W(0, ADDRESS(0x80082120u));
    }
    result.format_count = format_count;
    memcpy(result.formatted, formatted, sizeof(formatted));
    return result;
}

static void compare_state(const Result* native, const Result* stock) {
    assert(native->fade_out == stock->fade_out && native->fade_in == stock->fade_in);
    assert(native->alpha == stock->alpha && native->text_x == stock->text_x);
    assert(native->transitions == stock->transitions && native->points == stock->points);
    assert(native->format_count == stock->format_count);
    assert(memcmp(native->formatted, stock->formatted, sizeof(native->formatted)) == 0);
}

static void compare_lists(const Result* native, const Result* stock) {
    assert(native->word_count == stock->word_count);
    for (size_t i = 0; i < stock->word_count; ++i) {
        if (native->words[i] != stock->words[i]) {
            fprintf(stderr, "Display-list word %zu: %08x != %08x\n",
                    i, native->words[i], stock->words[i]);
            abort();
        }
    }
}

static uint32_t xy(int x, int y) {
    return ((uint32_t)x & 0xffffu) << 16 | ((uint32_t)y & 0xffffu);
}

static void append(Result* list, uint32_t w0, uint32_t w1) {
    assert(list->word_count + 2 <= sizeof(list->words) / sizeof(list->words[0]));
    list->words[list->word_count++] = w0;
    list->words[list->word_count++] = w1;
}

static void append_extended(Result* list, unsigned op, unsigned arg) {
    append(list, (RT64_EXTENDED_OPCODE << 24) | op, arg);
}

static void open_expected(Result* list, int dx) {
    append(list, (RT64_HOOK_OPCODE << 24) | RT64_HOOK_MAGIC_NUMBER,
           (RT64_HOOK_OP_ENABLE << 28) | RT64_EXTENDED_OPCODE);
    append_extended(list, G_EX_PUSHSCISSOR_V1, 0);
    append_extended(list, G_EX_SETSCISSOR_V1, (G_EX_ORIGIN_NONE << 2) | (G_EX_ORIGIN_NONE << 14));
    append(list, xy(-dx, 0), xy(1280 + dx, 960));
    append_extended(list, G_EX_SETRECTASPECT_V1, G_EX_ASPECT_ADJUST);
}

static void close_expected(Result* list) {
    append_extended(list, G_EX_SETRECTASPECT_V1, G_EX_ASPECT_AUTO);
    append_extended(list, G_EX_POPSCISSOR_V1, 0);
}

static size_t convert_rect(Result* expected, const Result* stock, size_t i,
                           int x0, int y0, int x1, int y1) {
    unsigned tile = stock->words[i + 1] >> 24 & 7;
    assert(stock->words[i + 2] == 0xb4000000 && stock->words[i + 4] == 0xb3000000);
    append_extended(expected, G_EX_TEXRECT_V1, tile | (G_EX_ORIGIN_NONE << 3) | (G_EX_ORIGIN_NONE << 15));
    append(expected, xy(x0, y0), xy(x1, y1));
    append(expected, stock->words[i + 3], stock->words[i + 5]);
    return i + 4;
}

static void compare_wide_ticker(Fixture f, float aspect) {
    Result stock = run(REFERENCE_PAIR(8000D708, 8000DDA0), f, 0, 4.0f / 3);
    Result native = run(FUNCTION_PAIR(8000D708, 8000DDA0), f, 0, aspect);
    compare_state(&native, &stock);

    float extra = fminf(120 * aspect - 160, (INT16_MAX - 1280) / 4.0f);
    int dx = (int)ceilf(extra * 4.0f);
    int origin = AERO_IS_JP ? 230 - (int)fminf(2 * f.frame, 500)
                            : (int)fmaxf(230 - 3 * f.frame, -500);
    float shift = extra + 90 + (origin - 230) * ((1 + extra / 160) - 1);
    int start_x = origin - (AERO_IS_JP && (f.course == 2 || f.course == 4) ? 30 : 0);
    // These fixtures use fixed spacing and contain a missing glyph between
    // the first and second visible glyphs, so each rectangle advances once.
    unsigned glyphs = 0;
    Result expected = {0};
    open_expected(&expected, dx);
    for (size_t i = 0; i < stock.word_count; i += 2) {
        uint32_t w0 = stock.words[i], w1 = stock.words[i + 1];
        if (w0 == 0xe44bc32c) {
            i = convert_rect(&expected, &stock, i, -dx, 720, 1280 + dx, 812);
        } else if (w0 >> 24 == 0xe4) {
            int left = (int)lroundf((start_x + (int)glyphs * f.spacing + shift) * 4);
            int width = ((w0 >> 12) - (w1 >> 12)) & 4095;
            i = convert_rect(&expected, &stock, i, left, w1 & 4095, left + width, w0 & 4095);
            ++glyphs;
        } else {
            append(&expected, w0, w1);
        }
    }
    close_expected(&expected);
    if (f.frame == 0) {
        assert(glyphs >= 2);
    }
    compare_lists(&native, &expected);
}

static void compare_wide_fades(float aspect) {
    Fixture f = {.mode = 5, .fade_out = 30, .fade_in = 0, .scene = 5};
    Result stock = run(REFERENCE_PAIR(800191FC, 80019C94), f, state, 4.0f / 3);
    Result native = run(FUNCTION_PAIR(800191FC, 80019C94), f, state, aspect);
    compare_state(&native, &stock);

    Result expected = {0};
    int dx = (int)ceilf(fminf((120 * aspect - 160) * 4, INT16_MAX - 1280));
    append(&expected, stock.words[0], stock.words[1]); // dispatcher's scissor
    assert(stock.word_count == 2 + 2 * 20);
    for (size_t start = 2; start < stock.word_count; start += 20) {
        open_expected(&expected, dx);
        for (size_t i = start; i < start + 20; i += 2) {
            if (i == start + 12) {
                i = convert_rect(&expected, &stock, i, -dx, 0, 1280 + dx, 960);
            } else {
                append(&expected, stock.words[i], stock.words[i + 1]);
            }
        }
        close_expected(&expected);
    }
    compare_lists(&native, &expected);
}

static void set_intro_enabled(bool enabled) {
#ifdef _WIN32
    assert(_putenv_s("AERO_WS_INTRO", enabled ? "1" : "0") == 0);
#else
    assert(setenv("AERO_WS_INTRO", enabled ? "1" : "0", 1) == 0);
#endif
}

int main(int argc, char** argv) {
    set_intro_enabled(true);
#ifdef AERO_JAPAN_SUPPORT
    aero_japan = argc > 1 && strcmp(argv[1], "jp") == 0;
#else
    (void)argc;
    (void)argv;
#endif
    const float original = 4.0f / 3;
    Fixture banner = {.scene = 5};
    Result expected = run(REFERENCE_PAIR(8000D97C, 8000E144), banner, 384, original);
    Result actual = run(FUNCTION_PAIR(8000D97C, 8000E144), banner, 384, original);
    compare_lists(&actual, &expected);

    const Fixture fixtures[] = {
        {.mode = 3, .phase = 2, .frame = 2, .spacing = -1, .fade_out = 30, .fade_in = -1, .scene = 5},
        {.mode = 3, .phase = 0, .frame = 150, .anchor = 60, .fade_out = 15, .fade_in = 8, .scene = 5},
        {.mode = 3, .phase = 2, .frame = 171, .anchor = 70, .flags = 2, .points = 6, .scene = 5},
        {.mode = 3, .phase = 2, .frame = 180, .anchor = 75, .points = 6, .scene = 5},
        {.mode = 3, .phase = 2, .frame = 181, .anchor = 75, .flags = 0x4000, .scene = 5},
        {.mode = 4, .phase = 2, .frame = 96, .course = 4, .spacing = -1, .scene = 5},
        {.mode = 4, .phase = 2, .frame = 150, .course = 3, .scene = 5},
        {.mode = 4, .phase = 2, .frame = 160, .course = 5, .fade_out = 0, .fade_in = 30, .scene = 5},
        {.mode = 9, .phase = 6, .flags = 0x1002, .fade_out = -1, .fade_in = -1, .scene = 5},
        {.mode = 7, .phase = 6, .flags = 0x1000, .fade_out = 127, .fade_in = 31, .scene = 5},
        {.mode = 7, .phase = 0, .flags = 0x1000, .fade_out = -1, .fade_in = 15, .scene = 4},
    };
    for (unsigned i = 0; i < sizeof(fixtures) / sizeof(fixtures[0]); ++i) {
        Fixture f = fixtures[i];
        replay_status = i % 2;
        expected = run(REFERENCE_PAIR(800191FC, 80019C94), f, state, original);
        actual = run(FUNCTION_PAIR(800191FC, 80019C94), f, state, original);
        compare_state(&actual, &expected);
        compare_lists(&actual, &expected);
        if (f.scene != 5) {
            actual = run(FUNCTION_PAIR(800191FC, 80019C94), f, state, 16.0f / 9);
            compare_lists(&actual, &expected);
        }
    }

    Fixture disabled = {.mode = 4, .phase = 2, .spacing = 10, .scene = 5};
    expected = run(REFERENCE_PAIR(800191FC, 80019C94), disabled, state, original);
    actual = run(FUNCTION_PAIR(800191FC, 80019C94), disabled, state, NAN);
    compare_lists(&actual, &expected);
    set_intro_enabled(false);
    actual = run(FUNCTION_PAIR(800191FC, 80019C94), disabled, state, 16.0f / 9);
    compare_lists(&actual, &expected);
    set_intro_enabled(true);

    const float aspects[] = {16.0f / 9, 21.0f / 9, 32.0f / 9, 100};
    for (unsigned i = 0; i < sizeof(aspects) / sizeof(aspects[0]); ++i) {
        for (int course = 0; course < 5; ++course) {
            Fixture f = {.mode = 4, .course = course, .spacing = 10, .scene = 5};
            compare_wide_ticker(f, aspects[i]);
            f.frame = AERO_IS_JP ? 179 : 250;
            f.anchor = 150;
            f.mode = 3;
            compare_wide_ticker(f, aspects[i]);
        }
        Fixture past_scroll = {.mode = 4, .frame = 260, .spacing = 10, .scene = 5};
        compare_wide_ticker(past_scroll, aspects[i]);
        compare_wide_fades(aspects[i]);
    }
}
