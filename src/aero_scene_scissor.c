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

static int guest_range(gpr address, uint32_t size) {
    uint32_t a = (uint32_t)address;
    return a >= 0x80000000u && a <= 0x80800000u - size && (a & 3u) == 0;
}

static int full_screen_overscan(uint8_t* rdram, recomp_context* ctx) {
    const gpr descriptor = ctx->r16;
    const gpr command = ctx->r3;
    // Fail closed if the ROM boundary changes; never scan or append to the DL.
    if (!guest_range(descriptor, 32) || !guest_range(command, 8)) return 0;
    if (MEM_H(8, descriptor) != 640 || MEM_H(10, descriptor) != 480 ||
        MEM_H(14, descriptor) != 640 || MEM_H(16, descriptor) != 480) return 0;
    return MEM_HU(24, descriptor) == 16 && MEM_HU(26, descriptor) == 8 &&
           MEM_HU(28, descriptor) == 16 && MEM_HU(30, descriptor) == 8;
}

void aero_scene_scissor(uint8_t* rdram, recomp_context* ctx) {
    const gpr command = ctx->r3;
    if (!full_screen_overscan(rdram, ctx)) return;
    if ((uint32_t)MEM_W(0, command) != 0xed040020u ||
        (uint32_t)MEM_W(4, command) != 0x004bc39cu) return;

    // G_SETSCISSOR uses unsigned 10.2 coordinates and exclusive lower bounds.
    MEM_W(0, command) = (int32_t)0xed000000u;
    MEM_W(4, command) = 0x005003c0; // (0,0)..(320,240)
}

// The same builder optionally clears the scene to its background color. Its
// final store is at 0x80022B28, with the same s0/v1 contract. Fill-cycle lower
// bounds are inclusive, unlike scissor bounds; cover through (319,239).
void aero_scene_background(uint8_t* rdram, recomp_context* ctx) {
    const gpr command = ctx->r3;
    if (!full_screen_overscan(rdram, ctx)) return;
    if ((uint32_t)MEM_W(0, command) != 0xf64b8398u ||
        (uint32_t)MEM_W(4, command) != 0x00040020u) return;
    MEM_W(0, command) = (int32_t)0xf64fc3bcu;
    MEM_W(4, command) = 0;
}
