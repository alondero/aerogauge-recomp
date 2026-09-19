// USA ROM scene-scissor hook, immediately after func_800227E4 emits G_SETSCISSOR
// at 0x800229C4. The game thread owns the descriptor (s0) and command (v1).
// The hook retains neither pointer; the emitted command lives in the frame's
// display-list buffer until graphics consumption. Fields use guest-endian MEM
// helpers: signed 16-bit scale/translation in quarter-pixels, 32-bit DL words.
//
// The full-screen viewport already maps NDC to 320x240. The descriptor adds
// 16/8-pixel overscan insets, and the ROM subtracts another pixel at the right
// and bottom. Removing only that scissor reveals world geometry at the same
// scale, including when RT64 widens the projection. Camera matrices, viewport,
// HUD positions, split-screen boundaries, and custom crops remain game-owned.
#include "recomp.h"

enum {
    AERO_DESC_SCALE_X = 8,
    AERO_DESC_SCALE_Y = 10,
    AERO_DESC_TRANS_X = 14,
    AERO_DESC_TRANS_Y = 16,
    AERO_DESC_INSET_LEFT = 24,
    AERO_DESC_INSET_TOP = 26,
    AERO_DESC_INSET_RIGHT = 28,
    AERO_DESC_INSET_BOTTOM = 30,
};

static const uint32_t AERO_SCISSOR_ORIGINAL_W0 = 0xed040020u;
static const uint32_t AERO_SCISSOR_ORIGINAL_W1 = 0x004bc39cu;
static const uint32_t AERO_SCISSOR_FULL_W0 = 0xed000000u;
static const uint32_t AERO_SCISSOR_FULL_W1 = 0x005003c0u;
static const uint32_t AERO_FILL_ORIGINAL_W0 = 0xf64b8398u;
static const uint32_t AERO_FILL_ORIGINAL_W1 = 0x00040020u;
static const uint32_t AERO_FILL_FULL_W0 = 0xf64fc3bcu;
static const uint32_t AERO_FILL_FULL_W1 = 0;

static gpr canonical_guest_address(gpr address) {
    // Recompiled guest pointers are 32-bit addresses carried in a 64-bit GPR.
    // Accept either the canonical sign-extended form or a zero-extended value,
    // then always feed the canonical form to MEM_*.
    return (gpr)(int32_t)(uint32_t)address;
}

static int guest_range(gpr address, uint32_t size) {
    uint32_t a = (uint32_t)canonical_guest_address(address);
    return a >= 0x80000000u && a <= 0x80800000u - size && (a & 3u) == 0;
}

static int full_screen_overscan(uint8_t* rdram, recomp_context* ctx) {
    const gpr descriptor = canonical_guest_address(ctx->r16);
    const gpr command = canonical_guest_address(ctx->r3);
    // Fail closed if the ROM boundary changes; never scan or append to the DL.
    if (!guest_range(descriptor, 32) || !guest_range(command, 8)) return 0;
    if (MEM_H(AERO_DESC_SCALE_X, descriptor) != 640 ||
        MEM_H(AERO_DESC_SCALE_Y, descriptor) != 480 ||
        MEM_H(AERO_DESC_TRANS_X, descriptor) != 640 ||
        MEM_H(AERO_DESC_TRANS_Y, descriptor) != 480) return 0;
    return MEM_HU(AERO_DESC_INSET_LEFT, descriptor) == 16 &&
           MEM_HU(AERO_DESC_INSET_TOP, descriptor) == 8 &&
           MEM_HU(AERO_DESC_INSET_RIGHT, descriptor) == 16 &&
           MEM_HU(AERO_DESC_INSET_BOTTOM, descriptor) == 8;
}

void aero_scene_scissor(uint8_t* rdram, recomp_context* ctx) {
    const gpr command = canonical_guest_address(ctx->r3);
    if (!full_screen_overscan(rdram, ctx)) return;
    if ((uint32_t)MEM_W(0, command) != AERO_SCISSOR_ORIGINAL_W0 ||
        (uint32_t)MEM_W(4, command) != AERO_SCISSOR_ORIGINAL_W1) return;

    // G_SETSCISSOR uses unsigned 10.2 coordinates and exclusive lower bounds.
    MEM_W(0, command) = (int32_t)AERO_SCISSOR_FULL_W0;
    MEM_W(4, command) = AERO_SCISSOR_FULL_W1; // (0,0)..(320,240)
}

// The same builder optionally clears the scene to its background color. Its
// final store is at 0x80022B28, with the same s0/v1 contract. Fill-cycle lower
// bounds are inclusive, unlike scissor bounds; cover through (319,239).
void aero_scene_background(uint8_t* rdram, recomp_context* ctx) {
    const gpr command = canonical_guest_address(ctx->r3);
    if (!full_screen_overscan(rdram, ctx)) return;
    if ((uint32_t)MEM_W(0, command) != AERO_FILL_ORIGINAL_W0 ||
        (uint32_t)MEM_W(4, command) != AERO_FILL_ORIGINAL_W1) return;
    MEM_W(0, command) = (int32_t)AERO_FILL_FULL_W0;
    MEM_W(4, command) = AERO_FILL_FULL_W1;
}
