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

#endif // AERO_HUD_WIDESCREEN_H
