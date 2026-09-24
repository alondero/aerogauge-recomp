// Run the actual generated ROM builder so the hook address, register contract,
// quarter-pixel encoding, and untouched sub-viewports are tested together.
#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "recomp.h"
#ifdef AERO_JAPAN_SUPPORT
#define func_800227E4 jp_func_80023238
#define CURSOR_HOLDER 0x80169508
#else
#define CURSOR_HOLDER 0x8016C508
#endif

void func_800227E4(uint8_t*, recomp_context*);
void aero_scene_scissor(uint8_t*, recomp_context*);
void aero_scene_background(uint8_t*, recomp_context*);
void osVirtualToPhysical_recomp(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
    ctx->r2 = (uint32_t)ctx->r4 & 0x1fffffffu;
}

_Alignas(8) static uint8_t ram[8 * 1024 * 1024];
static void check(int sx, int sy, int tx, int ty, unsigned left, unsigned top,
                  unsigned right, unsigned bottom, uint32_t w0, uint32_t w1,
                  int clear_background) {
    uint8_t* rdram = ram;
    const gpr desc = (gpr)(int32_t)0x80400000;
    const gpr dl = desc + 0x100;
    const gpr holder = (gpr)(int32_t)CURSOR_HOLDER;
    recomp_context ctx = {0};
    memset(ram, 0, sizeof(ram));
    ctx.r29 = (gpr)(int32_t)0x807ff000;
    ctx.r4 = desc;
    ctx.r5 = desc + 0x1000;
    MEM_H(8, desc) = sx; MEM_H(10, desc) = sy;
    MEM_H(14, desc) = tx; MEM_H(16, desc) = ty;
    MEM_H(24, desc) = left; MEM_H(26, desc) = top;
    MEM_H(28, desc) = right; MEM_H(30, desc) = bottom;
    // Matching background colors suppress the optional background clear.
    if (clear_background) {
        MEM_W(4, desc) = 0x12341234;
        MEM_W(0x18bd8, ctx.r5) = (int32_t)0x80225800;
    }
    MEM_W(0, holder) = dl;
    unsigned char original[32];
    memcpy(original, rdram + (desc & 0x7fffff), sizeof(original));
    func_800227E4(rdram, &ctx);
    printf("scissor: %08x %08x\n", (uint32_t)MEM_W(0, dl), (uint32_t)MEM_W(4, dl));
    fflush(stdout);
    assert((uint32_t)MEM_W(0, dl) == w0);
    assert((uint32_t)MEM_W(4, dl) == w1);
    if (clear_background) {
        assert((uint32_t)MEM_W(0, holder) == (uint32_t)(dl + 64));
        // Fill-cycle bounds include the last pixel; the scissor excludes it.
        assert((uint32_t)MEM_W(0, dl + 56) == (0xf6000000u | (w1 - 0x4004u)));
        assert((uint32_t)MEM_W(4, dl + 56) == (w0 & 0x00ffffffu));
        assert(MEM_W(4, dl + 48) == 0x12341234);
    } else {
        assert((uint32_t)MEM_W(0, holder) == (uint32_t)(dl + 8));
    }
    assert(memcmp(original, rdram + (desc & 0x7fffff), sizeof(original)) == 0);
}

static void setup_descriptor(uint8_t* rdram, gpr descriptor) {
    MEM_H(8, descriptor) = 640; MEM_H(10, descriptor) = 480;
    MEM_H(14, descriptor) = 640; MEM_H(16, descriptor) = 480;
    MEM_H(24, descriptor) = 16; MEM_H(26, descriptor) = 8;
    MEM_H(28, descriptor) = 16; MEM_H(30, descriptor) = 8;
}

static void check_pointer_and_command_guards(void) {
    uint8_t* rdram = ram;
    const gpr descriptor = (gpr)(int32_t)0x80400000;
    const gpr command = (gpr)(int32_t)0x80400100;
    recomp_context ctx = {0};
    memset(ram, 0, sizeof(ram));
    setup_descriptor(rdram, descriptor);

    // Low 32-bit pointers are normalized before MEM_* access. Test both hooks.
    MEM_W(0, command) = (int32_t)0xed040020u;
    MEM_W(4, command) = 0x004bc39c;
    ctx.r16 = 0x80400000u; ctx.r3 = 0x80400100u;
    aero_scene_scissor(rdram, &ctx);
    assert((uint32_t)MEM_W(0, command) == 0xed000000u);
    assert((uint32_t)MEM_W(4, command) == 0x005003c0u);
    MEM_W(0, command) = (int32_t)0xf64b8398u; MEM_W(4, command) = 0x00040020;
    aero_scene_background(rdram, &ctx);
    assert((uint32_t)MEM_W(0, command) == 0xf64fc3bcu);
    assert(MEM_W(4, command) == 0);

    // Malformed descriptor and command pointers fail closed without touching DL.
    const gpr cases[] = {0x00001000u, 0x80400002u, 0x80800000u};
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        MEM_W(0, command) = (int32_t)0xed040020u; MEM_W(4, command) = 0x004bc39c;
        ctx.r16 = cases[i]; ctx.r3 = command;
        aero_scene_scissor(rdram, &ctx);
        assert((uint32_t)MEM_W(0, command) == 0xed040020u);
        assert((uint32_t)MEM_W(4, command) == 0x004bc39cu);
    }
    MEM_W(0, command) = (int32_t)0xed040020u; MEM_W(4, command) = 0x004bc39c;
    ctx.r16 = descriptor; ctx.r3 = 0x1000u;
    aero_scene_scissor(rdram, &ctx);
    assert((uint32_t)MEM_W(0, command) == 0xed040020u);
    assert((uint32_t)MEM_W(4, command) == 0x004bc39cu);

    // A valid full-screen descriptor with an unexpected command is untouched.
    MEM_W(0, command) = 0xdeadbeef; MEM_W(4, command) = 0x12345678;
    ctx.r16 = descriptor; ctx.r3 = command;
    aero_scene_scissor(rdram, &ctx);
    assert((uint32_t)MEM_W(0, command) == 0xdeadbeefu);
    assert((uint32_t)MEM_W(4, command) == 0x12345678u);
}

int main(void) {
    check_pointer_and_command_guards();
    // Captured full-screen race viewport: scale and translation are full size;
    // only the scissor removes world pixels. RDP lower bounds are exclusive.
    check(640, 480, 640, 480, 16, 8, 16, 8, 0xed000000, 0x005003c0, 0);
    check(640, 480, 640, 480, 16, 8, 16, 8, 0xed000000, 0x005003c0, 1);
    // Split-screen and deliberate custom crops must retain their original bounds.
    check(320, 480, 320, 480, 16, 8, 16, 8, 0xed040020, 0x0023c39c, 0);
    check(320, 480, 320, 480, 16, 8, 16, 8, 0xed040020, 0x0023c39c, 1);
    check(320, 480, 960, 480, 16, 8, 16, 8, 0xed2c0020, 0x004bc39c, 0);
    check(640, 240, 640, 240, 16, 8, 16, 8, 0xed040020, 0x004bc1bc, 0);
    check(640, 240, 640, 720, 16, 8, 16, 8, 0xed040200, 0x004bc39c, 1);
    check(640, 480, 640, 480, 32, 24, 32, 24, 0xed080060, 0x0047c35c, 0);
    puts("PASS scene_scissor");
    return 0;
}
