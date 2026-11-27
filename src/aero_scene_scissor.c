// Whole-function recovery of the scene viewport/scissor/background builder.
// USA entry func_800227E4; Japan Rev A entry jp_func_80023238.
//
// Guest ABI: a0 is a 32-byte viewport descriptor and a1 is a base pointer whose
// +0x18BD8 word names the color image for the optional background clear. The
// builder advances the fixed display-list cursor holder by eight bytes per
// emitted command. All descriptor, pointer, and display-list fields use guest
// endian MEM helpers. The only translated callee is osVirtualToPhysical.
//
// The full-screen viewport already maps NDC to 320x240. Its descriptor adds
// 16/8-pixel overscan insets, and the ROM subtracts another pixel at the right
// and bottom. The frozen enhancement removes those margins only for that exact
// measured descriptor. Split-screen views and custom crops keep stock output.
#include "recomp.h"
#include "aero_region.h"

#include <stdlib.h>
#include <string.h>

extern void osVirtualToPhysical_recomp(uint8_t*, recomp_context*);

enum {
    AERO_DESC_COLOR = 4,
    AERO_DESC_SCALE_X = 8,
    AERO_DESC_SCALE_Y = 10,
    AERO_DESC_TRANS_X = 14,
    AERO_DESC_TRANS_Y = 16,
    AERO_DESC_INSET_LEFT = 24,
    AERO_DESC_INSET_TOP = 26,
    AERO_DESC_INSET_RIGHT = 28,
    AERO_DESC_INSET_BOTTOM = 30,
    AERO_COLOR_IMAGE_OFFSET = 0x18BD8,
};

#define AERO_CURSOR_HOLDER AERO_ADDR(0x8016C508u, 0x80169508u)
#define AERO_BACKGROUND_COLOR AERO_ADDR(0x8019DDFCu, 0x8019ADFCu)

static int32_t s16(int32_t value) {
    return (int16_t)(uint16_t)value;
}

static int32_t clamp_s32(int32_t value, int32_t maximum) {
    if (value < 0) return 0;
    if (value > maximum) return maximum;
    return value;
}

static gpr guest(uint32_t address) {
    return (gpr)(int32_t)address;
}

static gpr emit_command(uint8_t* rdram, gpr holder, gpr cursor, uint32_t w0, uint32_t w1) {
    MEM_W(0, cursor) = (int32_t)w0;
    MEM_W(4, cursor) = (int32_t)w1;
    cursor = ADD32(cursor, 8);
    MEM_W(0, holder) = (int32_t)cursor;
    return cursor;
}

static int overscan_removal_enabled(void) {
    const char* value = getenv("AERO_SCENE_OVERSCAN");
    return value == NULL || strcmp(value, "0") != 0;
}

static int full_screen_overscan(uint8_t* rdram, gpr descriptor) {
    return MEM_H(AERO_DESC_SCALE_X, descriptor) == 640 &&
           MEM_H(AERO_DESC_SCALE_Y, descriptor) == 480 &&
           MEM_H(AERO_DESC_TRANS_X, descriptor) == 640 &&
           MEM_H(AERO_DESC_TRANS_Y, descriptor) == 480 &&
           MEM_HU(AERO_DESC_INSET_LEFT, descriptor) == 16 &&
           MEM_HU(AERO_DESC_INSET_TOP, descriptor) == 8 &&
           MEM_HU(AERO_DESC_INSET_RIGHT, descriptor) == 16 &&
           MEM_HU(AERO_DESC_INSET_BOTTOM, descriptor) == 8;
}

void aero_build_scene_viewport(uint8_t* rdram, recomp_context* ctx) {
    const gpr descriptor = ctx->r4;
    const gpr base = ctx->r5;

    // Scale and translation are signed quarter-pixel values. The ROM divides
    // both by four with arithmetic shifts, then truncates center +/- half-extent
    // results to signed 16 bits before clamping the pixel rectangle.
    const int32_t half_x = ((int32_t)MEM_H(AERO_DESC_SCALE_X, descriptor)) >> 2;
    const int32_t half_y = ((int32_t)MEM_H(AERO_DESC_SCALE_Y, descriptor)) >> 2;
    const int32_t center_x = ((int32_t)MEM_H(AERO_DESC_TRANS_X, descriptor)) >> 2;
    const int32_t center_y = ((int32_t)MEM_H(AERO_DESC_TRANS_Y, descriptor)) >> 2;
    const int32_t left = clamp_s32(s16(center_x - half_x), 320);
    const int32_t top = clamp_s32(s16(center_y - half_y), 240);
    const int32_t right = clamp_s32(s16(center_x + half_x), 320);
    const int32_t bottom = clamp_s32(s16(center_y + half_y), 240);

    const uint32_t inset_left = MEM_HU(AERO_DESC_INSET_LEFT, descriptor);
    const uint32_t inset_top = MEM_HU(AERO_DESC_INSET_TOP, descriptor);
    const uint32_t inset_right = MEM_HU(AERO_DESC_INSET_RIGHT, descriptor);
    const uint32_t inset_bottom = MEM_HU(AERO_DESC_INSET_BOTTOM, descriptor);
    const int enhance = overscan_removal_enabled() && full_screen_overscan(rdram, descriptor);

    const gpr holder = guest(AERO_CURSOR_HOLDER);
    gpr cursor = MEM_W(0, holder);

    // G_SETSCISSOR carries unsigned 10.2 coordinates. Its lower-right bounds
    // are exclusive; the ROM's lower coordinate calculation includes its -1 pixel.
    uint32_t scissor_w0;
    uint32_t scissor_w1;
    if (enhance) {
        scissor_w0 = 0xed000000u;
        scissor_w1 = 0x005003c0u; // (0,0)..(320,240), exclusive
    } else {
        const uint32_t upper_x = (uint32_t)((left + (int32_t)inset_left) * 4) & 0xfffu;
        const uint32_t upper_y = (uint32_t)((top + (int32_t)inset_top) * 4) & 0xfffu;
        const uint32_t lower_x = (uint32_t)((right - (int32_t)inset_right - 1) * 4) & 0xfffu;
        const uint32_t lower_y = (uint32_t)((bottom - (int32_t)inset_bottom - 1) * 4) & 0xfffu;
        scissor_w0 = 0xed000000u | (upper_x << 12) | upper_y;
        scissor_w1 = (lower_x << 12) | lower_y;
    }
    cursor = emit_command(rdram, holder, cursor, scissor_w0, scissor_w1);

    const uint32_t color = (uint32_t)MEM_W(AERO_DESC_COLOR, descriptor);
    if ((uint32_t)MEM_W(0, guest(AERO_BACKGROUND_COLOR)) == color) {
        return;
    }

    cursor = emit_command(rdram, holder, cursor, 0xe7000000u, 0);
    cursor = emit_command(rdram, holder, cursor, 0xba001402u, 0x00300000u);
    cursor = emit_command(rdram, holder, cursor, 0xb900031du, 0);
    cursor = emit_command(rdram, holder, cursor, 0xe7000000u, 0);

    const gpr color_image_command = cursor;
    cursor = ADD32(cursor, 8);
    MEM_W(0, holder) = (int32_t)cursor;
    MEM_W(0, color_image_command) = (int32_t)0xff10013fu;

    ctx->r4 = MEM_W(AERO_COLOR_IMAGE_OFFSET, base);
    osVirtualToPhysical_recomp(rdram, ctx);
    MEM_W(4, color_image_command) = (int32_t)ctx->r2;

    cursor = emit_command(rdram, holder, cursor, 0xf7000000u, color);

    // G_FILLRECT uses pixel coordinates in these 10-bit fields. Unlike the
    // scissor, its lower bounds are inclusive; the stock builder subtracts 2.
    uint32_t fill_w0;
    uint32_t fill_w1;
    if (enhance) {
        fill_w0 = 0xf64fc3bcu;
        fill_w1 = 0; // (0,0)..(319,239), inclusive
    } else {
        const uint32_t lower_x = (uint32_t)(right - (int32_t)inset_right - 2) & 0x3ffu;
        const uint32_t lower_y = (uint32_t)(bottom - (int32_t)inset_bottom - 2) & 0x3ffu;
        const uint32_t upper_x = (uint32_t)(left + (int32_t)inset_left) & 0x3ffu;
        const uint32_t upper_y = (uint32_t)(top + (int32_t)inset_top) & 0x3ffu;
        fill_w0 = 0xf6000000u | (lower_x << 14) | (lower_y << 2);
        fill_w1 = (upper_x << 14) | (upper_y << 2);
    }
    cursor = emit_command(rdram, holder, cursor, fill_w0, fill_w1);
}
