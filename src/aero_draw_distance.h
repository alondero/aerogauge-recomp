// Draw-distance enhancement: libultra guPerspectiveF math with an extended far plane.
//
// AeroGauge's infamous pop-in is a single global far clip plane of 500 world units
// (near=5, matrix scale=3), passed to guPerspectiveF (ROM 0x8006BA60) by every one
// of the game's 11 projection setups -- 9 scene setups as float immediates (0x43FA)
// and the two in-race camera-object methods (0x8001F59C / 0x80020748) via camera
// structs (+0x10). The game's fog (fm=25600 fo=-25344) occupies only ndc_z in
// [0.99, 1.0] -- a razor-thin curtain hugging the far plane, which is why the
// pop-in was never masked. Because NDC z is nonlinear, scaling ONLY the far plane
// stretches that same fog window from just beyond the original far distance out
// to the new far plane (~980..50000 units at scale 100; tests/test_draw_distance
// .cpp pins the mapping): everything the original game could show becomes
// fog-free, fogged tracks keep a gradual distance haze, and whatever still pops
// at the (now distant) far plane pops in fully fogged.
//
// `far_scale` semantics (set by graphics.json `draw_distance_scale` /
// AERO_DRAW_DISTANCE_SCALE, validated in aero_config.cpp):
//   == 0.0f  infinite far plane -- m22 = -1, m32 = -2*n (no clip whatsoever;
//             user's explicit "we should be able to have an infinite draw distance")
//   == 1.0f  original game (500 unit far plane)
//   >  1.0f  finite scaled far plane (e.g. 100.0f = 50,000 units, the default)
//   <  0 or == NaN  rejected by clamp; the original 500-unit plane is used
//
// This header is the pure math core, host-includable with no recomp dependencies
// (tests/test_draw_distance.cpp). src/aero_draw_distance.cpp wraps it as the
// native `guPerspectiveF` symbol that the recompiled call sites link against
// (routed via `[patches] ignored` in aerogauge.us.toml; see scripts/
// gen_syms_toml.py NATIVE_NAMES).
#ifndef AERO_DRAW_DISTANCE_H
#define AERO_DRAW_DISTANCE_H

#include <math.h>
#include <stdint.h>

// Faithful libultra guPerspectiveF (fovy in degrees), except the far plane used
// for the matrix is far_plane*far_scale (or +inf for far_scale == 0). perspNorm
// matches the ROM's branch on (n+f) <= 2.0 with the same threshold values
// (0xFFFF near-equal, otherwise 131072/(n+f) clamped to >=1 -- byte-verified at
// 0x8006BBB4..0x8006BC88). The deg->rad constant below is the ROM's own double
// at 0x80098D20 (== 3.1415926/180.0 exactly), so the half-angle computation
// matches the ROM's `(double)fovy * piOver180 / 2.0f` byte-for-byte at f64, then
// goes to f32 for the cot. The (n+f)/(n-f) term order matches the ROM bytes at
// 0x8006BA60..0x8006BBB0 (libultra prints them in the same order; the float
// round-trip is what it is).
static inline void aero_gu_perspective_f(float mf[4][4], uint16_t* persp_norm,
                                         float fovy_deg, float aspect,
                                         float near_plane, float far_plane,
                                         float scale, float far_scale) {
    int i, j;
    for (i = 0; i < 4; i++) {
        for (j = 0; j < 4; j++) {
            mf[i][j] = (i == j) ? 1.0f : 0.0f;
        }
    }

    // Resolve the effective far plane. The two-pass gate (sanity first, then the
    // far-scale interpretation) avoids pathological behavior on degenerate inputs.
    int infinite = (far_scale == 0.0f);
    float far_ext = far_plane;
    if (!infinite && far_scale > 1.0f && far_plane > near_plane && near_plane > 0.0f) {
        far_ext = far_plane * far_scale;
    }

    float half = (float)((double)fovy_deg * (3.1415926 / 180.0)) / 2.0f;
    float cot = cosf(half) / sinf(half);

    mf[0][0] = cot / aspect;
    mf[1][1] = cot;
    if (infinite) {
        // Closed-form limit as f -> infinity: (n+f)/(n-f) -> -1, 2nf/(n-f) -> -2n.
        mf[2][2] = -1.0f;
        mf[3][2] = -2.0f * near_plane;
    } else {
        mf[2][2] = (near_plane + far_ext) / (near_plane - far_ext);
        mf[3][2] = 2.0f * near_plane * far_ext / (near_plane - far_ext);
    }
    mf[2][3] = -1.0f;
    mf[3][3] = 0.0f;

    for (i = 0; i < 4; i++) {
        for (j = 0; j < 4; j++) {
            mf[i][j] *= scale;
        }
    }

    if (persp_norm != 0) {
        // Matches the ROM branch at 0x8006BBB4..0x8006BBDC (verified 2026-07-11):
        // the `c.le.d f0, f8` with f8=0 compares (n+f) against 2.0; if <=, the
        // 0xFFFF sentinel is stored and execution skips the divisor entirely.
        if ((double)near_plane + (double)far_plane <= 2.0) {
            *persp_norm = (uint16_t)0xFFFF;
        } else {
            uint16_t pn = (uint16_t)((2.0 * 65536.0) / ((double)near_plane + (double)far_plane));
            *persp_norm = (pn == 0) ? (uint16_t)0x0001 : pn;
        }
    }
}

#endif // AERO_DRAW_DISTANCE_H