// Native replacement for the ROM's guPerspectiveF (0x8006BA60) -- the single
// function every AeroGauge projection flows through -- extending the 500-unit
// far clip plane that causes the game's infamous scenery pop-in. Routed here by
// `[patches] ignored` in aerogauge.us.toml (gen_syms_toml.py NATIVE_NAMES): the
// recompiler skips the ROM body and the 11 recompiled call sites (plus the
// guPerspective fixed-point wrapper) link against this symbol directly.
//
// The math lives in aero_draw_distance.h (host-testable); this file only
// unpacks the o32 call frame and applies the configured far-plane scale.
#include "aero_draw_distance.h"

#include <cstring>

#include "recomp.h"

#include "aero_config.h"

namespace {

float bits_to_float(uint32_t bits) {
    float f;
    std::memcpy(&f, &bits, sizeof(f));
    return f;
}

uint32_t float_to_bits(float f) {
    uint32_t bits;
    std::memcpy(&bits, &f, sizeof(bits));
    return bits;
}

} // anonymous namespace

// o32 frame observed at every ROM call site: a0=mf (float[4][4] out),
// a1=perspNorm (u16 out, may be null), a2=fovy degrees (float bits),
// a3=aspect (float bits), 0x10($sp)=near, 0x14($sp)=far, 0x18($sp)=scale.
extern "C" void guPerspectiveF(uint8_t* rdram, recomp_context* ctx) {
    gpr mf_addr = ctx->r4;
    gpr persp_norm_addr = ctx->r5;
    float fovy_deg = bits_to_float((uint32_t)ctx->r6);
    float aspect = bits_to_float((uint32_t)ctx->r7);
    float near_plane = bits_to_float((uint32_t)MEM_W(0x10, ctx->r29));
    float far_plane = bits_to_float((uint32_t)MEM_W(0x14, ctx->r29));
    float scale = bits_to_float((uint32_t)MEM_W(0x18, ctx->r29));

    float mf[4][4];
    uint16_t persp_norm = 0;
    aero_gu_perspective_f(mf, &persp_norm, fovy_deg, aspect, near_plane, far_plane,
                          scale, aero::config::draw_distance_scale());

    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            MEM_W(4 * (i * 4 + j), mf_addr) = (int32_t)float_to_bits(mf[i][j]);
        }
    }
    if (persp_norm_addr != 0) {
        MEM_HU(0, persp_norm_addr) = persp_norm;
    }
}
