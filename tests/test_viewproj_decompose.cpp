// Spec for RT64's view*projection decomposition on AXIS-ALIGNED cameras
// (widescreen investigation, 2026-07-11).
//
// AeroGauge loads a combined view*projection matrix as its G_MTX PROJECTION (the
// camera is pre-multiplied in). RT64 detects this (isMatrixViewProj: m[3][3] not in
// {0,1}) and decomposes it back into view + proj so perspective classification, the
// aspect-ratio Hor+ widening, the skybox no-parallax heuristic, and frame
// interpolation's camera matching all work. But matrixDecomposeViewProj recovered
// p[2][2] from ROW 0 unconditionally (p22 = vp[0][2] / -vp[0][3]) -- for a camera
// whose world-X axis is perpendicular to the view direction (vp[0][3] == 0; true of
// AeroGauge's attract/demo cameras and any axis-aligned intro cam), that is 0/0 =
// NaN, the NaN fallback keeps the whole VP as "proj", and the scene is classified
// orthographic: no widescreen. Patch 0010 pivots to the largest |vp[i][3]| row.
//
// Matrices below are AeroGauge's real ones, measured from the demo race via the
// swrender's AERO_PROJ_PROBE (see stub_renderer.cpp).
//
// Standalone host test (Lamborghini tests/ convention). From the repo root:
//   g++ -std=c++20 -I lib/rt64/src/contrib/hlslpp/include \
//       tests/test_viewproj_decompose.cpp lib/rt64/src/common/rt64_math.cpp \
//       -o build/test_viewproj_decompose && ./build/test_viewproj_decompose

#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "../lib/rt64/src/common/rt64_math.h"

using RT64::isMatrixViewProj;
using RT64::matrixDecomposeViewProj;

static hlslpp::float4x4 from_rows(const float r[16]) {
    return hlslpp::float4x4(r[0], r[1], r[2], r[3], r[4], r[5], r[6], r[7],
                            r[8], r[9], r[10], r[11], r[12], r[13], r[14], r[15]);
}

static int failures = 0;
#define CHECK(cond, msg)                                                                 \
    do {                                                                                 \
        if (!(cond)) {                                                                   \
            std::fprintf(stderr, "FAIL: %s (%s)\n", msg, #cond);                         \
            ++failures;                                                                  \
        }                                                                                \
    } while (0)

static void check_decomposes_to_perspective(const float rows[16], const char* name) {
    hlslpp::float4x4 vp = from_rows(rows);
    CHECK(isMatrixViewProj(vp), name);

    hlslpp::float4x4 v, p;
    matrixDecomposeViewProj(vp, v, p);

    // The recovered proj must satisfy RT64's perspective classification
    // (rt64_rsp.cpp getCurrentProjectionType): p[3][3]==0 and |p[1][1]|>1e-6.
    CHECK(std::abs((float)p[3][3]) < 1e-6f, name);
    CHECK(std::abs((float)p[1][1]) > 1e-6f, name);

    // And the parts must actually multiply back to the input (the contract the
    // existing generic-camera path already meets). Tolerance is scaled: the
    // matrices carry values up to ~1e4 from the fixed-point decode.
    hlslpp::float4x4 recomposed = hlslpp::mul(v, p);
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            float a = (float)vp[i][j], b = (float)recomposed[i][j];
            float tol = 1e-3f * std::max(1.0f, std::abs(a));
            if (std::abs(a - b) > tol) {
                std::fprintf(stderr, "FAIL: %s recompose [%d][%d] %f != %f\n", name, i, j, a, b);
                ++failures;
            }
        }
    }
}

int main() {
    // AeroGauge demo-race scene: view*proj with the camera's world-X axis
    // perpendicular to the view direction (vp[0][3] == 0 -- the old row-0 pivot
    // divides 0/0 here).
    const float scene[16] = {
           2.0468f,   0.0000f,    0.0000f,    0.0000f,
           0.0000f,   2.7105f,   -0.2689f,   -0.2670f,
           0.0000f,  -0.3183f,   -2.2900f,   -2.2744f,
        1380.5920f, 919.4868f, 9814.0195f, 9760.6465f,
    };
    check_decomposes_to_perspective(scene, "scene (axis-aligned camera)");

    // AeroGauge backdrop/sky projection: translation-only view (eye at z=120).
    const float backdrop[16] = {
        0.75f, 0.0f,   0.0000f,   0.0f,
        0.00f, 1.0f,   0.0000f,   0.0f,
        0.00f, 0.0f,  -1.0084f,  -1.0f,
        0.00f, 0.0f, 118.9958f, 120.0f,
    };
    check_decomposes_to_perspective(backdrop, "backdrop (translation-only view)");

    // Regression guard for the generic-camera path the old code already handled:
    // a synthetic P with a yawed+translated rigid view multiplied in (all of
    // vp[i][3] nonzero). Built as V*P with row vectors: yaw 30deg about Y,
    // translation (100, 50, -400).
    {
        const float sx = 1.5f, sy = 2.0f, q = -1.002f, zt = -20.02f;
        const float c = 0.8660254f, s = 0.5f;
        // Row-vector view: rows are world axes expressed in eye space.
        const float view[16] = {
              c,   0.0f,   s,  0.0f,
            0.0f,  1.0f, 0.0f, 0.0f,
             -s,   0.0f,   c,  0.0f,
           100.0f, 50.0f, -400.0f, 1.0f,
        };
        const float proj[16] = {
            sx, 0.0f, 0.0f, 0.0f,
            0.0f, sy, 0.0f, 0.0f,
            0.0f, 0.0f, q, -1.0f,
            0.0f, 0.0f, zt, 0.0f,
        };
        hlslpp::float4x4 vp = hlslpp::mul(from_rows(view), from_rows(proj));
        float rows[16];
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j) rows[i * 4 + j] = (float)vp[i][j];
        check_decomposes_to_perspective(rows, "generic yawed camera");
    }

    if (failures == 0) {
        std::puts("PASS test_viewproj_decompose");
        return 0;
    }
    std::fprintf(stderr, "%d failure(s)\n", failures);
    return 1;
}
