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

    // --- per-texrect pin classification (retag pass) ---------------------------------
    // Cases are the real rects from the mode-4 1P canyon-race capture (hud-pre dump),
    // in the texrect command's quarter-pixel units: classify(ulx, lrx, uly).
#define QP(px) ((px) * 4)
    // speedometer dial ring (247..300, y172): RIGHT.
    assert(aero_ws_classify_rect_qp(QP(247), QP(300), QP(172)) == AERO_WS_PIN_RIGHT);
    // TOTAL.TIME / LAPTIME row (label 171.., digits ..301, y31): every rect RIGHT.
    assert(aero_ws_classify_rect_qp(QP(171), QP(227), QP(31)) == AERO_WS_PIN_RIGHT);
    assert(aero_ws_classify_rect_qp(QP(293), QP(301), QP(33)) == AERO_WS_PIN_RIGHT);
    // "0" MPH digit (247..267, y191) and its scale ticks (187.., y191): RIGHT.
    assert(aero_ws_classify_rect_qp(QP(247), QP(267), QP(191)) == AERO_WS_PIN_RIGHT);
    assert(aero_ws_classify_rect_qp(QP(187), QP(207), QP(191)) == AERO_WS_PIN_RIGHT);
    // TEMP gauge (274..300, y106..157): RIGHT.
    assert(aero_ws_classify_rect_qp(QP(291), QP(297), QP(110)) == AERO_WS_PIN_RIGHT);
    assert(aero_ws_classify_rect_qp(QP(274), QP(300), QP(157)) == AERO_WS_PIN_RIGHT);
    // top lap/position row (170..302, y16): RIGHT, including the leftmost rect at 170.
    assert(aero_ws_classify_rect_qp(QP(170), QP(190), QP(16)) == AERO_WS_PIN_RIGHT);
    // GLPS ladder (20..64, y194..224 -- the bottom-left band): LEFT.
    assert(aero_ws_classify_rect_qp(QP(22), QP(51), QP(197)) == AERO_WS_PIN_LEFT);
    assert(aero_ws_classify_rect_qp(QP(20), QP(64), QP(194)) == AERO_WS_PIN_LEFT);
    // DAMAGE bar (117..229) and the 71..247 bottom panel straddle centre: never move.
    assert(aero_ws_classify_rect_qp(QP(117), QP(229), QP(216)) == AERO_WS_PIN_NONE);
    assert(aero_ws_classify_rect_qp(QP(71), QP(247), QP(207)) == AERO_WS_PIN_NONE);
    // the DAMAGE fill rect anchors at the bar's left edge and grows with damage: it must
    // stay centred at EVERY fill level, including the empty 117..117 sliver (caught live:
    // a 152 LEFT bound pinned the empty fill while the bar frame stayed put).
    assert(aero_ws_classify_rect_qp(QP(117), QP(117), QP(216)) == AERO_WS_PIN_NONE);
    assert(aero_ws_classify_rect_qp(QP(117), QP(140), QP(216)) == AERO_WS_PIN_NONE);
    // top-centre bar (107..169, y23): straddles the deadband, stays.
    assert(aero_ws_classify_rect_qp(QP(107), QP(169), QP(23)) == AERO_WS_PIN_NONE);
    // minimap background (20..100, y70) and a craft blip (80..88, y100): left by x but
    // ABOVE the bottom band -- must stay centred with the matrix-drawn track polyline
    // until the polyline gets its own G_MTX shift (retained follow-up).
    assert(aero_ws_classify_rect_qp(QP(20), QP(100), QP(70)) == AERO_WS_PIN_NONE);
    assert(aero_ws_classify_rect_qp(QP(80), QP(88), QP(100)) == AERO_WS_PIN_NONE);
#undef QP

    printf("all HUD shift-scale assertions passed\n");
    return 0;
}
