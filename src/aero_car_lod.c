// USA ROM car rendering policy. All hooks run on the game thread, at the
// instructions documented in gen_syms_toml.py and docs/reference/rom.md.
// Thresholds are render units (5x simulation positions). Only comparison
// operands change: real distance, near rejection and angular culling survive.
#include <math.h>
#include "recomp.h"
#include "aero_region.h"

int aero_force_full_lod_enabled(void);

void aero_car_lod_visibility(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
    const int enabled = aero_force_full_lod_enabled();
    ctx->f24.fl = enabled ? INFINITY : 750.0f;
    ctx->f28.fl = enabled ? INFINITY : 150.0f;
}

void aero_car_lod_model(uint8_t* rdram, recomp_context* ctx) {
    // a0 is a game-owned car, valid for this update. Access bytes through MEM
    // helpers (N64 endian layout); never retain pointers across frames/restores.
    // Fail closed for malformed pointers or unknown craft IDs. Only the two
    // existing flag bytes are written; the ROM rebuilds its own model nodes.
    const uint32_t address = (uint32_t)ctx->r4;
    if ((address & 3) || address < 0x80000400u || address > 0x80700000u - 0x548u)
        return;
    const gpr car = (gpr)(int32_t)address;
    const unsigned craft = MEM_BU(9, car);
    if (craft >= 10) return;
    const int enabled = aero_force_full_lod_enabled();
    const unsigned old_flags = MEM_BU(0, car);
    const unsigned flags = enabled ? old_flags & ~1u : old_flags;
    const int low = !enabled && ((flags & 1) || MEM_B(0, (gpr)(int32_t)AERO_ADDR(0x8013FF90u, 0x8013D010u)) == 5);
    const unsigned part = MEM_BU(craft * 2, (gpr)(int32_t)AERO_ADDR(0x80098618u, 0x80095E88u));
    if (part >= 31) return;
    const gpr table = (gpr)(int32_t)(low ? AERO_ADDR(0x8008F7A4u, 0x8008D704u) : AERO_ADDR(0x8008F728u, 0x8008D688u));
    // Comparing the installed root mesh also handles disabling in mode 5 and
    // restoring a save state without a host-side cache of previous LOD state.
    // Rev A's render-node block begins 12 bytes earlier (0x48C vs 0x498).
    if (flags != old_flags || MEM_W(AERO_ADDR(0x544, 0x538), car) != MEM_W(part * 4, table))
        MEM_BU(1, car) |= 0x80;
    MEM_BU(0, car) = flags;
}

void aero_car_lod_mesh_mode(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
    // t6 holds the race-mode byte immediately before its comparison with 5.
    if (aero_force_full_lod_enabled()) ctx->r14 = 0;
}

void aero_car_lod_animation_mode(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
    // The matching animated-part transform selector uses t9 for that byte.
    if (aero_force_full_lod_enabled()) ctx->r25 = 0;
}
