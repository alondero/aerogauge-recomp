// Spec for the draw-distance enhancement's guPerspectiveF math
// (src/aero_draw_distance.h). AeroGauge passes fovy=55, aspect=4/3 (or 1.666 in
// menus), near=5, far=500, scale=3 to guPerspectiveF from all 11 projection
// setups; the enhancement multiplies ONLY the far plane. Pins:
//   1. far_scale=1 reproduces the original libultra matrix + perspNorm bit-for-bit
//      semantics (the pop-in-free path must be a superset, not a rewrite).
//   2. far_scale=100 changes ONLY the two z-column terms, keeps them inside
//      s15.16 range (guMtxF2L follows), and leaves perspNorm at the game's
//      original value (RSP-side numbers unchanged).
//   3. The game's fog window (fm=25600 fo=-25344 => ndc_z in [0.99, 1.0]) maps,
//      under the extended matrix, to eye distances starting ~where the original
//      far plane was -- i.e. the old fog CURTAIN becomes a gradual fade and
//      nothing closer than ~495 units picks up new fog.
//
// Standalone host test, no ROM build needed. Compile from the repo root:
//   g++ -std=c++17 -I src tests/test_draw_distance.cpp -o build/test_draw_distance
//   ./build/test_draw_distance
#include <cmath>
#include <cstdint>
#include <cstdio>

#include "../src/aero_draw_distance.h"

static int failures = 0;
#define CHECK(cond, msg)                                                                 \
    do {                                                                                 \
        if (!(cond)) {                                                                   \
            std::fprintf(stderr, "FAIL: %s (%s)\n", msg, #cond);                         \
            ++failures;                                                                  \
        }                                                                                \
    } while (0)

static bool nearf(float a, float b, float tol) { return std::fabs(a - b) <= tol; }

// The game's universal projection arguments (measured at all 11 ROM call sites).
static const float kFovy = 55.0f, kAspect = 4.0f / 3.0f, kNear = 5.0f, kFar = 500.0f,
                   kScale = 3.0f;

// Eye distance at which the extended-far matrix produces a given ndc_z
// (row-vector convention: ndc = (d*m22 + m32) / (d*m23 + m33), signs folded for
// an eye-space point at distance d in front of the camera).
static float eye_distance_at_ndc(const float mf[4][4], float ndc) {
    // z_clip = -d*m[2][2] + m[3][2], w_clip = -d*m[2][3] + m[3][3]
    // solve ndc = z_clip / w_clip for d:
    //   -d*m22 + m32 = ndc * (-d*m23 + m33)
    //   d * (ndc*m23 - m22) = ndc*m33 - m32
    return (ndc * mf[3][3] - mf[3][2]) / (ndc * mf[2][3] - mf[2][2]);
}

int main() {
    // --- 1. far_scale = 1: the original libultra matrix ---------------------
    float mf[4][4];
    uint16_t pn = 0;
    aero_gu_perspective_f(mf, &pn, kFovy, kAspect, kNear, kFar, kScale, 1.0f);

    // cot(27.5 deg) = 1.92098213..., times scale 3 (and /aspect for m00).
    const float cot = std::cos(27.5f * 3.14159265f / 180.0f) /
                      std::sin(27.5f * 3.14159265f / 180.0f);
    CHECK(nearf(mf[0][0], cot / kAspect * kScale, 1e-4f), "m00 = cot/aspect*scale");
    CHECK(nearf(mf[1][1], cot * kScale, 1e-4f), "m11 = cot*scale");
    CHECK(nearf(mf[2][2], (kNear + kFar) / (kNear - kFar) * kScale, 1e-5f),
          "m22 = (n+f)/(n-f)*scale = -3.0606...");
    CHECK(nearf(mf[3][2], 2.0f * kNear * kFar / (kNear - kFar) * kScale, 1e-3f),
          "m32 = 2nf/(n-f)*scale = -30.303...");
    CHECK(mf[2][3] == -1.0f * kScale, "m23 = -scale");
    CHECK(mf[3][3] == 0.0f, "m33 = 0");
    CHECK(mf[0][1] == 0.0f && mf[1][0] == 0.0f && mf[3][0] == 0.0f && mf[3][1] == 0.0f,
          "off-diagonal zeros");
    CHECK(pn == (uint16_t)(131072.0 / 505.0), "perspNorm = 131072/(n+f) = 259");

    // --- 2. far_scale = 100: only the z column moves, s15.16-safe -----------
    float mx[4][4];
    uint16_t pnx = 0;
    aero_gu_perspective_f(mx, &pnx, kFovy, kAspect, kNear, kFar, kScale, 100.0f);
    CHECK(mx[0][0] == mf[0][0] && mx[1][1] == mf[1][1] && mx[2][3] == mf[2][3],
          "fov terms unchanged by far_scale");
    CHECK(nearf(mx[2][2], (5.0f + 50000.0f) / (5.0f - 50000.0f) * kScale, 1e-4f),
          "m22 -> -3.0006 with far*100");
    CHECK(nearf(mx[3][2], 2.0f * 5.0f * 50000.0f / (5.0f - 50000.0f) * kScale, 1e-2f),
          "m32 -> -30.006 with far*100");
    CHECK(std::fabs(mx[2][2]) < 32768.0f && std::fabs(mx[3][2]) < 32768.0f,
          "z terms stay inside s15.16 (guMtxF2L follows)");
    CHECK(pnx == pn, "perspNorm computed from ORIGINAL far (RSP numbers unchanged)");

    // --- 3. the fog curtain becomes a gradual fade, never NEW near fog ------
    // Game fog: fm=25600 fo=-25344 => fog=0 at ndc 0.99, fog=255 at ndc 1.0.
    // Original matrix: that window is eye distance ~338..500 -- a curtain at the
    // pop wall. Extended matrix: it stretches to ~980..50000 -- everything the
    // original game could ever show (d < 500) is now fog-FREE, and geometry only
    // reaches full fog (= invisible pop) at the new far plane.
    CHECK(nearf(eye_distance_at_ndc(mf, 1.0f), kFar, 1.0f),
          "original matrix: the pop wall is the 500-unit far plane");
    const float fog_start = eye_distance_at_ndc(mx, 0.99f);
    const float fog_full = eye_distance_at_ndc(mx, 1.0f);
    CHECK(fog_start > kFar && fog_start < 1500.0f,
          "extended matrix: fog fade starts beyond the ORIGINAL far plane");
    CHECK(fog_full > 45000.0f, "full fog only at the new far plane (invisible pop)");

    // --- degenerate inputs pass through unscaled -----------------------------
    float md[4][4];
    uint16_t pnd = 0;
    aero_gu_perspective_f(md, &pnd, kFovy, kAspect, 5.0f, 0.0f, kScale, 100.0f);
    CHECK(nearf(md[2][2], (5.0f + 0.0f) / (5.0f - 0.0f) * kScale, 1e-5f),
          "far<=near left untouched");

    // --- 4. far_scale == 0: explicit "infinite" sentinel --------------------
    // Closed form: m22 = -scale, m32 = -2*near*scale, no clip whatsoever. The
    // user's "we should be able to have an infinite draw distance" maps to this.
    float mi[4][4];
    uint16_t pni = 0;
    aero_gu_perspective_f(mi, &pni, kFovy, kAspect, kNear, kFar, kScale, 0.0f);
    CHECK(nearf(mi[2][2], -1.0f * kScale, 1e-6f), "infinite: m22 = -scale");
    CHECK(nearf(mi[3][2], -2.0f * kNear * kScale, 1e-5f), "infinite: m32 = -2*n*scale");
    CHECK(mi[0][0] == mf[0][0] && mi[1][1] == mf[1][1] && mi[2][3] == mf[2][3],
          "infinite: fov terms unchanged (the matrix still has a valid projection)");

    if (failures == 0) std::puts("PASS test_draw_distance");
    return failures == 0 ? 0 : 1;
}
