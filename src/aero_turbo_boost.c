// Accelerator-only Boost Start + assisted race Turbo.
//
// This hook runs at 0x8005C7A8, immediately after func_8005C750 has mapped P1's
// configured buttons and stick into the car's semantic control word. Working at
// this seam is important: it honours remapped controls and feeds the same state
// machine as a real player instead of editing a controller slot or a camera path.
//
// With the enhancement enabled, the player holds accelerator for launch and
// explicitly holds drift while steering hard for a race Turbo:
//   * through SET, brake is held too; immediately afterward it is released;
//   * in a hard turn, drift is held until AeroGauge exposes its turbo-ready flag,
//     then the assist performs the game's two-tick release and accelerator press.
// The ROM still decides whether either boost is awarded and applies all thrust,
// heat and effects. The hook never writes velocity or turbo timers directly.
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
#define CONTROL_ACTIONS (CONTROL_ACCEL | CONTROL_BRAKE | CONTROL_DRIFT)

#define CAR_FLAGS       0x34u
#define CAR_CONTROLS    0x40u
#define CAR_BOOST_TIMER 0x55u

// Observed in the live ROM: the low flag rises after a sufficiently developed
// drift. Releasing for two input ticks and re-pressing accelerator while it is
// active causes the game to award car+0x55 = 10. The high flag is set by the
// ROM several ticks after a successful Boost Start.
#define TURBO_READY_FLAG 0x00002000u
// Port-owned activation policy: 16/20 is the outer 20% of the game's semantic
// steering range, keeping the assist out of ordinary course corrections.
#define TURBO_ASSIST_TURN_THRESHOLD 16

extern int aero_easy_turbo_enabled(void);

enum turbo_assist_state {
    TURBO_CHARGING = 0,
    TURBO_RELEASE_SECOND,
    TURBO_REPRESS,
};

static enum turbo_assist_state g_turbo_state = TURBO_CHARGING;
static uint32_t g_last_phase;
static int g_turbo_used;

static void reset_race_assist(void) {
    g_turbo_state = TURBO_CHARGING;
    g_turbo_used = 0;
}

static int semantic_turn(uint16_t controls) {
    // func_8005C9E4 stores horizontal stick as (turn + 20) in bits 6..11.
    return (int)((controls >> 6) & 0x3Fu) - 20;
}

void aero_turbo_boost_tick(uint8_t* rdram, recomp_context* ctx) {
    if (ctx == NULL || !aero_easy_turbo_enabled()) {
        reset_race_assist();
        g_last_phase = 0;
        return;
    }

    const gpr car = ctx->r16;
    const uint32_t car_address = (uint32_t)car;
    if (car_address < 0x80000000u || car_address > 0x807FFFFFu) {
        reset_race_assist();
        return;
    }
    const uint32_t phase = (uint32_t)MEM_W(0, (gpr)(int32_t)RACE_PHASE);
    const uint32_t step = (uint32_t)MEM_W(0, (gpr)(int32_t)RACE_STEP);
    uint8_t actions = (uint8_t)MEM_BU(CAR_CONTROLS, car);
    const int user_accelerating = (actions & CONTROL_ACCEL) != 0;

    if (phase == PHASE_SETUP && g_last_phase != PHASE_SETUP) {
        reset_race_assist();
    }
    g_last_phase = phase;

    if (phase == PHASE_SETUP || phase == PHASE_COUNTDOWN) {
        reset_race_assist();
        if (!user_accelerating) return;

        if (phase == PHASE_SETUP || step < STEP_AFTER_SET) {
            actions |= CONTROL_BRAKE;
        } else {
            actions &= (uint8_t)~CONTROL_BRAKE;
        }
        MEM_B(CAR_CONTROLS, car) = actions;
        return;
    }

    if (phase != PHASE_RACING) {
        reset_race_assist();
        return;
    }

    const uint32_t flags = (uint32_t)MEM_W(CAR_FLAGS, car);
    const uint8_t boost_timer = (uint8_t)MEM_BU(CAR_BOOST_TIMER, car);
    if (boost_timer != 0) {
        // Let the player's freshly mapped controls through while the ROM-owned
        // turbo runs. A new assisted drift can begin after the timer expires.
        g_turbo_state = TURBO_CHARGING;
        return;
    }

    const int user_drifting = (actions & CONTROL_DRIFT) != 0;
    if (!user_accelerating || !user_drifting) {
        reset_race_assist();
        return;
    }

    const uint16_t packed_controls = (uint16_t)MEM_HU(CAR_CONTROLS, car);
    int turn = semantic_turn(packed_controls);
    if (turn < 0) turn = -turn;
    if (turn < TURBO_ASSIST_TURN_THRESHOLD) {
        reset_race_assist();
        return;
    }
    switch (g_turbo_state) {
        case TURBO_CHARGING: {
            if (g_turbo_used) break;
            if ((flags & TURBO_READY_FLAG) != 0) {
                actions &= (uint8_t)~CONTROL_ACTIONS;
                g_turbo_state = TURBO_RELEASE_SECOND;
                g_turbo_used = 1;
            }
            break;
        }
        case TURBO_RELEASE_SECOND:
            actions &= (uint8_t)~CONTROL_ACTIONS;
            g_turbo_state = TURBO_REPRESS;
            break;
        case TURBO_REPRESS:
            actions = (uint8_t)((actions & ~CONTROL_ACTIONS) | CONTROL_ACCEL);
            // The ROM award is visible on the next P1 callback. If terrain
            // invalidates the attempt, charging resumes immediately.
            g_turbo_state = TURBO_CHARGING;
            break;
    }

    MEM_B(CAR_CONTROLS, car) = actions;
}
