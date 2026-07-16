#ifndef AERO_HUD_WIDESCREEN_H
#define AERO_HUD_WIDESCREEN_H

// Widescreen HUD rect-pin scaling math.
//
// Inherited from the Lamborghini port where these helpers were tuned for that game's
// race HUD's gEXSetRectAlign rect-pin travel and corresponding game-space geometry
// shifts. AeroGauge's HUD is structurally different (no rev needle; different 2D
// overlay layout), so the GAME-SPACE shift constants and the issue-67 narrative do
// NOT carry over verbatim. TODO(aerogauge): re-measure from this ROM's HUD once it
// renders; the math helpers themselves (which depend only on `aspect` / RT64's
// `extAspectPercentage`) are generic enough to keep in the meantime.
//
// `aspect` is the EFFECTIVE rect-pin aspect (see below), always >= 4/3 by
// construction; the clamp is only a guard.
static inline float aero_ws_hud_shift_scale_for_aspect(float aspect) {
    float scale = aspect * 2.25f - 3.0f;
    return scale > 0.0f ? scale : 0.0f;
}

// The rect pins do NOT always travel to the real output edges: RT64's extended GBI
// moves them by `extAspectPercentage`, which depends on hr_option (see
// rt64_workload_queue.cpp). So the geometry must scale off the aspect the rects
// EFFECTIVELY pin to, not the raw output aspect. These helpers mirror RT64's math
// so the game-space geometry tracks the rects in every hr_option, at any output
// aspect. `source` is the game-native aspect (4/3).

// Fraction of full-width travel the rects use in RT64 Manual mode: 1.0 once the
// output reaches the clamp target, tapering to 0 as the output narrows toward
// source.
static inline float aero_ws_hud_clamp_ext_percentage(float display, float source,
                                                      float ext_target) {
    float reduced_ext = ext_target - source;
    float reduced_display = display - source;
    if (reduced_ext <= 0.0f || reduced_display <= 0.0f) {
        return 0.0f;
    }
    float p = reduced_ext / reduced_display;
    return p < 1.0f ? p : 1.0f;
}

// The aspect the rects effectively pin to given how far they actually travel
// (`ext_percentage`): `source` at zero travel (rects centred), `display` at full
// travel. Feeding this into aero_ws_hud_shift_scale_for_aspect makes any geometry
// travelling with the rects match them exactly.
static inline float aero_ws_hud_effective_rect_aspect(float display, float source,
                                                       float ext_percentage) {
    return source + (display - source) * ext_percentage;
}

// --- per-texrect pin classification (whole-frame DL retag pass, issue #1) ---
//
// The 2D dispatcher's per-group handlers mix LEFT and RIGHT elements inside a single
// call (e.g. func_80018EA0 draws TEMP right + GLPS left), so no call-boundary bracket
// can split them. Instead the retag pass classifies each texrect the frame actually
// emitted, by its extent in the original 320-wide coordinate space. Everything here is
// in the texrect command's native quarter-pixel units and pure math, so it is
// host-testable without RDRAM.

enum aero_ws_pin {
    AERO_WS_PIN_NONE = 0,
    AERO_WS_PIN_LEFT = 1,
    AERO_WS_PIN_RIGHT = 2,
};

// Asymmetric deadband around the 320-wide centre (160): a rect pins only when it sits
// fully on one side with margin, so centred elements (DAMAGE 117..229, the 71..247 bottom
// panel, the countdown numerals) never move. Bounds measured from the mode-4 race capture:
// the leftmost LEFT elements are GLPS (lrx 64) and the minimap texture rect (lrx 100 --
// the white track outline is baked into that texture, NOT drawn as geometry: shifting
// every live G_MTX left it put, and it sits exactly inside the rect); the leftmost
// right-side rects (lap-time label 171.., top row 170..) start at 170. The LEFT bound
// must stay below 117: the DAMAGE bar's fill rect anchors at its left edge x=117 and
// grows rightward with damage (117..117 when empty), so any bound >= 117 would pin the
// low-damage fill LEFT while the rest of the bar stays centred.
#define AERO_WS_LEFT_MAX_QP    (100 * 4)
#define AERO_WS_RIGHT_MIN_QP   (168 * 4)

static inline int aero_ws_classify_rect_qp(int ulx_qp, int lrx_qp, int uly_qp) {
    if (lrx_qp <= AERO_WS_LEFT_MAX_QP) {
        return AERO_WS_PIN_LEFT;
    }
    if (ulx_qp >= AERO_WS_RIGHT_MIN_QP) {
        return AERO_WS_PIN_RIGHT;
    }
    // The white "TOTAL.TIME" header (measured 107..169, y23..31) is part of the timer
    // group -- natively it sits immediately left of the big time digits (170..302,
    // y16..31) -- but it crosses the centre deadband, so pure thresholds leave it
    // behind when its digits pin right. Match it tightly (top strip only) and keep it
    // travelling with the group.
    if (ulx_qp >= 104 * 4 && lrx_qp <= 172 * 4 && uly_qp <= 26 * 4) {
        return AERO_WS_PIN_RIGHT;
    }
    return AERO_WS_PIN_NONE;
}

#endif // AERO_HUD_WIDESCREEN_H
