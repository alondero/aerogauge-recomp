// Accelerator-only Boost Start + button-operated race Turbo (opt-in).
// Runs after func_8005C9E4 maps P1's configured controls at 0x8005C7A8.
// In races the configured drift button becomes Turbo; its action is consumed
// so pressing it cannot start a drift. Steering and accelerator are untouched.
// The award mirrors ROM 0x800584B8..0x800584D4: craft-specific duration,
// effect timer 5, clear the pending award flag. The unmodified ROM update at
// 0x8005AE00 owns turbo thrust, heat accumulation and overheating cancellation.
#include <stdint.h>

#include "recomp.h"

#define RACE_PHASE 0x8013FF88u
#define RACE_STEP  0x8013FF38u

#define PHASE_SETUP     1u
#define PHASE_COUNTDOWN 2u
#define PHASE_RACING    3u
#define STEP_AFTER_SET  3u

// Semantic bits written by func_8005C9E4 at car+0x40. These are independent
// of which physical N64 buttons the player has assigned to each action.
#define CONTROL_ACCEL 0x80u
#define CONTROL_BRAKE 0x40u
#define CONTROL_DRIFT 0x20u
#define CAR_SETTINGS    0x20u
#define CAR_FLAGS       0x34u
#define CAR_CONTROLS    0x40u
#define CAR_BOOST_TIMER 0x55u
#define CAR_EFFECT_TIMER 0x56u
#define SETTINGS_TURBO_DURATION 0x28u
#define TURBO_PENDING_FLAG 0x00001000u

extern int aero_easy_turbo_enabled(void);

// Require a release after losing the car/context. Track the button even when
// disabled and during countdown so enabling the option or GO isn't a press.
static int g_button_down = 1;

void aero_turbo_boost_tick(uint8_t* rdram, recomp_context* ctx) {
    if (rdram == NULL || ctx == NULL) {
        g_button_down = 1;
        return;
    }
    const gpr car = ctx->r16;
    const uint32_t car_address = (uint32_t)car;
    if (car_address < 0x80000000u || car_address > 0x807FFFA8u) {
        g_button_down = 1;
        return;
    }
    uint8_t actions = (uint8_t)MEM_BU(CAR_CONTROLS, car);
    const int button_down = (actions & CONTROL_DRIFT) != 0;
    const int pressed = button_down && !g_button_down;
    g_button_down = button_down;
    if (!aero_easy_turbo_enabled()) return;

    const uint32_t phase = (uint32_t)MEM_W(0, (gpr)(int32_t)RACE_PHASE);
    const uint32_t step = (uint32_t)MEM_W(0, (gpr)(int32_t)RACE_STEP);
    if (phase == PHASE_SETUP || phase == PHASE_COUNTDOWN) {
        if ((actions & CONTROL_ACCEL) == 0) return;
        if (phase == PHASE_SETUP || step < STEP_AFTER_SET) {
            actions |= CONTROL_BRAKE;
        } else {
            actions &= (uint8_t)~CONTROL_BRAKE;
        }
        MEM_B(CAR_CONTROLS, car) = actions;
        return;
    }
    if (phase != PHASE_RACING) return;

    MEM_B(CAR_CONTROLS, car) = actions & (uint8_t)~CONTROL_DRIFT;
    // Do not extend an active turbo or queue a press for when it expires.
    if (!pressed || MEM_BU(CAR_BOOST_TIMER, car) != 0) return;

    const gpr settings = (gpr)(int32_t)MEM_W(CAR_SETTINGS, car);
    const uint32_t settings_address = (uint32_t)settings;
    if (settings_address < 0x80000000u || settings_address > 0x807FFFD7u) return;
    MEM_W(CAR_FLAGS, car) = (uint32_t)MEM_W(CAR_FLAGS, car) & ~TURBO_PENDING_FLAG;
    MEM_B(CAR_EFFECT_TIMER, car) = 5; // ROM 0x800584C0 / 0x800584D0
    MEM_B(CAR_BOOST_TIMER, car) = MEM_BU(SETTINGS_TURBO_DURATION, settings);
}
