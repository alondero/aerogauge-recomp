// Real ROM input repacking verifies why both virtual ports remain responsive:
// an unresponsive P1 would compact P2 into the first player's record.
#undef NDEBUG
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "recomp.h"
#include "aero_region.h"

#ifdef AERO_JAPAN_SUPPORT
int aero_japan = 1;
#define read_input jp_func_8000982C
#else
#define read_input func_800092C4
#endif
void read_input(uint8_t*, recomp_context*);
static uint16_t buttons[2];
static int missing_p1;

void osContGetReadData_recomp(uint8_t* rdram, recomp_context* ctx) {
    for (int player = 0; player < 4; ++player) {
        const gpr pad = ctx->r4 + player * 6;
        MEM_H(0, pad) = player < 2 ? buttons[player] : 0;
        MEM_B(2, pad) = player == 0 ? 80 : player == 1 ? -80 : 0;
        MEM_B(3, pad) = 0;
        MEM_B(4, pad) = player >= 2 || (player == 0 && missing_p1) ? 8 : 0;
    }
}

int main(void) {
    _Alignas(8) static uint8_t memory[8 * 1024 * 1024];
    uint8_t* rdram = memory;
    const gpr presence = (gpr)(int32_t)AERO_ADDR(0x80081FD0u, 0x80081D60u);
    const gpr pad = (gpr)(int32_t)AERO_ADDR(0x8010CAB0u, 0x80109BA0u);
    recomp_context ctx = {0};
    ctx.r29 = (gpr)(int32_t)0x807FF000;
    MEM_B(0, presence) = 3;
    buttons[0] = 0x8000; buttons[1] = 0x4010;
    read_input(rdram, &ctx);
    assert(MEM_HU(2, pad) == buttons[0]);
    assert(MEM_HU(10, pad) == buttons[1]);
    assert(MEM_B(0, pad) > 0 && MEM_B(8, pad) < 0);
    // Vacant P1 is still a responsive neutral port, so P2 stays P2.
    buttons[0] = 0;
    read_input(rdram, &ctx);
    assert(MEM_HU(2, pad) == 0 && MEM_HU(10, pad) == buttons[1]);
    // Falsify the contract: a no-response P1 really does move P2 into P1.
    missing_p1 = 1;
    read_input(rdram, &ctx);
    assert(MEM_HU(2, pad) == buttons[1]);
    return 0;
}
