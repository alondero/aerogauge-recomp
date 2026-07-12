// Standalone spec for the widescreen HUD-geometry shift scale (issue #1).
//
// The scale is pure float math (no game/RDRAM/RT64 state), so it is unit-testable in
// isolation — compile and run directly with the host compiler, no ROM build needed:
//   gcc -I. tests/test_hud_shift_scale.c -lm -o test_hud_shift_scale && ./test_hud_shift_scale

#include <assert.h>
#include <math.h>
#include <stdio.h>

#include "../src/aero_hud_widescreen.h"

static int approx(float a, float b) { return fabsf(a - b) < 1e-4f; }

#define SRC (4.0f / 3.0f) /* game-native aspect */

// End-to-end: the scale the geometry actually uses, given a display aspect + hr_option's
// ext_percentage, is scale_for_aspect(effective_rect_aspect(...)).
static float end_to_end(float display, float ext_percentage) {
    return aero_ws_hud_shift_scale_for_aspect(
        aero_ws_hud_effective_rect_aspect(display, SRC, ext_percentage));
}

int main(void) {
    // 4:3: rect pins do not travel, so geometry must not move either.
    assert(approx(aero_ws_hud_shift_scale_for_aspect(SRC), 0.0f));
    // 16:9: the measured game-space displacement is used unchanged.
    assert(approx(aero_ws_hud_shift_scale_for_aspect(16.0f / 9.0f), 1.0f));
    // 21:9 Full: geometry tracks proportional rect travel.
    assert(approx(aero_ws_hud_shift_scale_for_aspect(21.0f / 9.0f), 2.25f));
    // Defensive: a sub-4:3 aspect never shifts the wrong way.
    assert(aero_ws_hud_shift_scale_for_aspect(1.0f) == 0.0f);

    // Clamp16x9 ext-percentage (RT64 Manual mode), ext_target = 16/9.
    assert(approx(aero_ws_hud_clamp_ext_percentage(16.0f / 9.0f, SRC, 16.0f / 9.0f), 1.0f));
    assert(approx(aero_ws_hud_clamp_ext_percentage(21.0f / 9.0f, SRC, 16.0f / 9.0f), 4.0f / 9.0f));
    assert(aero_ws_hud_clamp_ext_percentage(SRC, SRC, 16.0f / 9.0f) == 0.0f);

    // 21:9 under Clamp16x9: rects clamp at 16:9, so geometry must do the same.
    assert(approx(end_to_end(21.0f / 9.0f, 4.0f / 9.0f), 1.0f));
    // 21:9 Full: rects reach output edges and geometry uses the 2.25 scale.
    assert(approx(end_to_end(21.0f / 9.0f, 1.0f), 2.25f));
    // Original: rects and geometry both stay centred.
    assert(approx(end_to_end(21.0f / 9.0f, 0.0f), 0.0f));
    // 16:9 Clamp16x9: the shipped default uses the measured displacement unchanged.
    assert(approx(end_to_end(16.0f / 9.0f, 1.0f), 1.0f));

    printf("all HUD shift-scale assertions passed\n");
    return 0;
}
