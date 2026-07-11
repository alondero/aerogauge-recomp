// Spec for RT64's overscan-tolerant full-width check (patch 0011).
//
// AeroGauge renders its 3D scene inside an overscan border -- scissor/viewport
// (64,32)..(1212,924) of a (0,0)..(1280,960) framebuffer (10.2 fixed-point, i.e.
// game-space x 16..303 of 320). RT64's automatic aspect widening required the
// projection to cover the framebuffer's exact full width, so such scenes were never
// widened (pillarboxed under AspectRatio::Expand). coversWidthWithOverscan accepts a
// small symmetric border while still excluding split-screen sub-viewports.
//
// Standalone host test (Lamborghini tests/ convention). From the repo root:
//   g++ -std=c++20 -w -I lib/rt64/src/contrib/hlslpp/include \
//       tests/test_aspect_overscan.cpp lib/rt64/src/common/rt64_common.cpp \
//       -o build/test_aspect_overscan && ./build/test_aspect_overscan

#include <cstdio>

#include "../lib/rt64/src/common/rt64_common.h"

// The header declares FixedRect's ctors in rt64_common.cpp; build rects directly.
static RT64::FixedRect make(int32_t ulx, int32_t uly, int32_t lrx, int32_t lry) {
    RT64::FixedRect r;
    r.ulx = ulx; r.uly = uly; r.lrx = lrx; r.lry = lry;
    return r;
}

static int failures = 0;
#define CHECK(cond, msg)                                              \
    do {                                                              \
        if (!(cond)) {                                                \
            std::fprintf(stderr, "FAIL: %s (%s)\n", msg, #cond);      \
            ++failures;                                               \
        }                                                             \
    } while (0)

int main() {
    using RT64::coversWidthWithOverscan;
    const RT64::FixedRect fb = make(0, 0, 1280, 960);

    // Exact edge-to-edge coverage (the old accepted case) still passes.
    CHECK(coversWidthWithOverscan(make(0, 0, 1280, 960), fb), "exact cover");
    // Coverage beyond the edges passes.
    CHECK(coversWidthWithOverscan(make(-8, 0, 1300, 960), fb), "over cover");
    // AeroGauge's measured scene rect: symmetric ~5% border.
    CHECK(coversWidthWithOverscan(make(64, 32, 1212, 924), fb), "aerogauge overscan");
    // Max allowed border: 1/8th per side.
    CHECK(coversWidthWithOverscan(make(160, 0, 1120, 960), fb), "max symmetric border");
    // Border too large (1/4 per side = letterboxed sub-view) is rejected.
    CHECK(!coversWidthWithOverscan(make(320, 0, 960, 960), fb), "oversized border rejected");
    // Half-width split-screen viewports are rejected (flush left / flush right).
    CHECK(!coversWidthWithOverscan(make(0, 0, 640, 960), fb), "left half rejected");
    CHECK(!coversWidthWithOverscan(make(640, 0, 1280, 960), fb), "right half rejected");
    // Asymmetric inset (one side flush, other side inset) is rejected.
    CHECK(!coversWidthWithOverscan(make(0, 0, 1180, 960), fb), "asymmetric inset rejected");
    // Degenerate rect is rejected.
    CHECK(!coversWidthWithOverscan(make(500, 0, 500, 960), fb), "degenerate rejected");

    if (failures == 0) {
        std::puts("PASS test_aspect_overscan");
        return 0;
    }
    std::fprintf(stderr, "%d failure(s)\n", failures);
    return 1;
}
