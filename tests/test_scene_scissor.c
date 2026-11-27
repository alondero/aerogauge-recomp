// Compare the live scene viewport entry with the same ROM code unhooked.
// The public seam is the original guest entry ABI: persistent RDRAM output and
// ordered osVirtualToPhysical calls, not the native implementation's registers.
#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "recomp.h"

#ifdef AERO_JAPAN_SUPPORT
int aero_japan = 1;
#define func_800227E4 jp_func_80023238
#define rom_func_800227E4 rom_jp_func_80023238
#define CURSOR_HOLDER 0x80169508u
#define BACKGROUND_COLOR 0x8019ADFCu
#else
#define CURSOR_HOLDER 0x8016C508u
#define BACKGROUND_COLOR 0x8019DDFCu
#endif

recomp_func_t func_800227E4, rom_func_800227E4;

#define GUEST(value) ((gpr)(int32_t)(value))

_Alignas(8) static uint8_t ram[8 * 1024 * 1024];
static uint8_t* rdram = ram;
static const gpr descriptor = GUEST(0x80400000u);
static const gpr list = GUEST(0x80400100u);
static const gpr base = GUEST(0x80401000u);
static const gpr holder = GUEST(CURSOR_HOLDER);
static const gpr stack = GUEST(0x807FF000u);
static const gpr color_image = GUEST(0x80225800u);

static gpr v2p_args[2];
static gpr v2p_returns[2];
static unsigned v2p_count;

void osVirtualToPhysical_recomp(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
    assert(v2p_count < sizeof(v2p_args) / sizeof(v2p_args[0]));
    v2p_args[v2p_count] = ctx->r4;
    ctx->r2 = (uint32_t)ctx->r4 & 0x1fffffffu;
    v2p_returns[v2p_count] = ctx->r2;
    ++v2p_count;
}

typedef struct {
    int scale_x, scale_y, trans_x, trans_y;
    unsigned inset_left, inset_top, inset_right, inset_bottom;
    uint32_t color;
    int clear;
} Fixture;

typedef struct {
    uint8_t descriptor_before[32];
    uint8_t descriptor_after[32];
    uint32_t background_before, background_after;
    uint32_t image_before, image_after;
    gpr cursor_before, cursor_after;
    uint32_t words[16];
    unsigned call_count;
    gpr call_args[2];
    gpr call_returns[2];
} Result;

static size_t ram_offset(gpr address) {
    return (size_t)((uint32_t)address & 0x7fffffu);
}

static void set_overscan_enabled(int enabled) {
#ifdef _WIN32
    assert(_putenv_s("AERO_SCENE_OVERSCAN", enabled ? "1" : "0") == 0);
#else
    assert(setenv("AERO_SCENE_OVERSCAN", enabled ? "1" : "0", 1) == 0);
#endif
}

static void clear_overscan_override(void) {
#ifdef _WIN32
    assert(_putenv_s("AERO_SCENE_OVERSCAN", "") == 0);
#else
    assert(unsetenv("AERO_SCENE_OVERSCAN") == 0);
#endif
}

static void write_fixture(const Fixture* fixture) {
    MEM_H(8, descriptor) = (int16_t)fixture->scale_x;
    MEM_H(10, descriptor) = (int16_t)fixture->scale_y;
    MEM_H(14, descriptor) = (int16_t)fixture->trans_x;
    MEM_H(16, descriptor) = (int16_t)fixture->trans_y;
    MEM_HU(24, descriptor) = (uint16_t)fixture->inset_left;
    MEM_HU(26, descriptor) = (uint16_t)fixture->inset_top;
    MEM_HU(28, descriptor) = (uint16_t)fixture->inset_right;
    MEM_HU(30, descriptor) = (uint16_t)fixture->inset_bottom;
    MEM_W(4, descriptor) = (int32_t)fixture->color;
    MEM_W(0, GUEST(BACKGROUND_COLOR)) =
        (int32_t)(fixture->clear ? ~fixture->color : fixture->color);
    MEM_W(0x18bd8, base) = (int32_t)color_image;
    MEM_W(0, holder) = (int32_t)list;
}

static Result run(recomp_func_t* function, const Fixture* fixture) {
    Result result = {0};
    recomp_context ctx = {0};
    memset(ram, 0, sizeof(ram));
    write_fixture(fixture);
    memcpy(result.descriptor_before, rdram + ram_offset(descriptor),
           sizeof(result.descriptor_before));
    result.background_before = (uint32_t)MEM_W(0, GUEST(BACKGROUND_COLOR));
    result.image_before = (uint32_t)MEM_W(0x18bd8, base);
    result.cursor_before = MEM_W(0, holder);
    v2p_count = 0;

    ctx.r29 = stack;
    ctx.r4 = descriptor;
    ctx.r5 = base;
    function(rdram, &ctx);
    assert(ctx.r29 == stack);

    memcpy(result.descriptor_after, rdram + ram_offset(descriptor),
           sizeof(result.descriptor_after));
    result.background_after = (uint32_t)MEM_W(0, GUEST(BACKGROUND_COLOR));
    result.image_after = (uint32_t)MEM_W(0x18bd8, base);
    result.cursor_after = MEM_W(0, holder);
    for (unsigned i = 0; i < sizeof(result.words) / sizeof(result.words[0]); ++i) {
        result.words[i] = (uint32_t)MEM_W(i * 4, list);
    }
    result.call_count = v2p_count;
    for (unsigned i = 0; i < v2p_count; ++i) {
        result.call_args[i] = v2p_args[i];
        result.call_returns[i] = v2p_returns[i];
    }
    return result;
}

static void assert_inputs_unmodified(const Result* result) {
    assert(memcmp(result->descriptor_before, result->descriptor_after,
                  sizeof(result->descriptor_before)) == 0);
    assert(result->background_before == result->background_after);
    assert(result->image_before == result->image_after);
}

static void compare_result(const Result* actual, const Result* expected) {
    assert_inputs_unmodified(actual);
    assert_inputs_unmodified(expected);
    assert(actual->cursor_before == expected->cursor_before);
    if (actual->cursor_after != expected->cursor_after) {
        fprintf(stderr, "cursor actual=%llx expected=%llx before=%llx calls=%u/%u\n",
                (unsigned long long)actual->cursor_after,
                (unsigned long long)expected->cursor_after,
                (unsigned long long)actual->cursor_before,
                actual->call_count, expected->call_count);
    }
    assert(actual->cursor_after == expected->cursor_after);
    assert(memcmp(actual->words, expected->words, sizeof(actual->words)) == 0);
    assert(actual->call_count == expected->call_count);
    for (unsigned i = 0; i < actual->call_count; ++i) {
        assert(actual->call_args[i] == expected->call_args[i]);
        assert(actual->call_returns[i] == expected->call_returns[i]);
    }
}

static void compare_stock(const Fixture* fixture) {
    Result expected = run(rom_func_800227E4, fixture);
    Result actual = run(func_800227E4, fixture);
    compare_result(&actual, &expected);
    assert(expected.call_count == (fixture->clear ? 1u : 0u));
    assert((uint32_t)(expected.cursor_after - expected.cursor_before) ==
           (fixture->clear ? 64u : 8u));
}

static void stock_parity(void) {
    static const Fixture fixtures[] = {
        // Captured full-screen race viewport and its split-screen/custom-crop variants.
        {640, 480, 640, 480, 16, 8, 16, 8, 0x12341234, 0},
        {320, 480, 320, 480, 16, 8, 16, 8, 0x12341234, 0},
        {320, 480, 960, 480, 16, 8, 16, 8, 0x12341234, 0},
        {640, 240, 640, 240, 16, 8, 16, 8, 0x12341234, 0},
        {640, 240, 640, 720, 16, 8, 16, 8, 0x12341234, 0},
        {640, 480, 640, 480, 32, 24, 32, 24, 0x12341234, 0},
        // Clamped out-of-bounds centers and negative or wrapping signed inputs.
        {640, 480, 0, 0, 0, 0, 0, 0, 0x12341234, 0},
        {640, 480, 2000, 1800, 0, 0, 0, 0, 0x12341234, 0},
        {-640, -480, 640, 480, 16, 8, 16, 8, 0x12341234, 0},
        {32764, -32768, 32764, -32768, 0, 0, 0, 0, 0x12341234, 0},
        // Unsigned inset extremes feed the fixed-point masks without touching inputs.
        {640, 480, 640, 480, 65535, 65535, 65535, 65535, 0x12341234, 0},
    };
    set_overscan_enabled(0);
    for (unsigned i = 0; i < sizeof(fixtures) / sizeof(fixtures[0]); ++i) {
        for (int clear = 0; clear < 2; ++clear) {
            Fixture fixture = fixtures[i];
            fixture.clear = clear;
            compare_stock(&fixture);
        }
    }

    // Deterministic bit-pattern coverage for the signed 16-bit geometry inputs
    // and unsigned insets, including wrapping center +/- half-extent cases.
    uint32_t state = 0xAE04711Fu;
    for (unsigned i = 0; i < 96; ++i) {
        unsigned values[8];
        for (unsigned j = 0; j < 8; ++j) {
            state = state * 1664525u + 1013904223u;
            values[j] = state >> 16;
        }
        const Fixture fixture = {
            (int16_t)values[0], (int16_t)values[1],
            (int16_t)values[2], (int16_t)values[3],
            values[4], values[5], values[6], values[7],
            state, (int)(i & 1u),
        };
        compare_stock(&fixture);
    }
}

static void enhanced_parity(void) {
    const Fixture full = {640, 480, 640, 480, 16, 8, 16, 8, 0x12341234, 0};
    clear_overscan_override(); // unset is the default enabled mode
    for (int clear = 0; clear < 2; ++clear) {
        Fixture fixture = full;
        fixture.clear = clear;
        Result actual = run(func_800227E4, &fixture);
        assert_inputs_unmodified(&actual);
        assert(actual.words[0] == 0xed000000u);
        assert(actual.words[1] == 0x005003c0u);
        assert((uint32_t)(actual.cursor_after - actual.cursor_before) ==
               (clear ? 64u : 8u));
        assert(actual.call_count == (clear ? 1u : 0u));
        if (clear) {
            assert(actual.words[10] == 0xff10013fu);
            assert(actual.words[11] == 0x00225800u);
            assert(actual.words[12] == 0xf7000000u);
            assert(actual.words[13] == fixture.color);
            assert(actual.words[14] == 0xf64fc3bcu);
            assert(actual.words[15] == 0);
            assert(actual.call_args[0] == color_image);
            assert(actual.call_returns[0] == 0x00225800u);
        }
    }

    // The enhancement remains frozen to the measured full-screen overscan case:
    // sub-viewports and custom crops are bit-identical to the ROM even when enabled.
    static const Fixture passthrough[] = {
        {320, 480, 320, 480, 16, 8, 16, 8, 0x12341234, 0},
        {320, 480, 960, 480, 16, 8, 16, 8, 0x12341234, 0},
        {640, 480, 640, 480, 32, 24, 32, 24, 0x12341234, 0},
    };
    for (unsigned i = 0; i < sizeof(passthrough) / sizeof(passthrough[0]); ++i) {
        for (int clear = 0; clear < 2; ++clear) {
            Fixture fixture = passthrough[i];
            fixture.clear = clear;
            compare_stock(&fixture);
        }
    }
}

int main(void) {
    stock_parity();
    enhanced_parity();
    puts("PASS scene_scissor: stock ROM parity and frozen overscan enhancement");
    return 0;
}
