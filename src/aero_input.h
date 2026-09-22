#ifndef AERO_INPUT_H
#define AERO_INPUT_H

#include <algorithm>
#include <cstdint>

// Host input math shared by src/main.cpp (the SDL sampler and the runtime input callback) and
// its host test, tests/test_input_scaling.cpp.
//
// Two coordinate spaces meet here, and the difference is easy to lose:
//
//   * The N64 space the ROM is written for. A calibrated N64 stick reaches roughly +-80 and
//     the ROM's own consumers are tuned to that range. The menus are the strictest: the menu
//     reader derives a direction byte with a +-41 threshold applied after a further +-7
//     deadzone (func_80057930, func_80009494).
//   * The runtime's normalized space. ultramodern's osContGetReadData() takes the callback's
//     (x, y) and hands them to convert_to_n64_range(), which maps a normalized input through
//     the N64 stick octagon whose cardinal inradius is r0 = 82
//     (lib/N64ModernRuntime/ultramodern/src/input.cpp).
//
// The host therefore clamps to N64 space and then divides by that inradius -- not by the int8
// limit 127. Dividing by 127 understates every deflection by ~35%: a full-deflection stick
// arrives at the ROM as 51, below the menus' 48-unit requirement, so menu navigation dies
// everywhere except past ~93% of physical travel and the failure is silent.
//
// Everything here is pure: no SDL, no guest memory, no atomics. Keep it that way so a host
// test can pin the contract without starting the runtime.

// Calibrated N64 stick travel. The ROM is tuned for this range, so the host clamps to it
// rather than to the int8 limit of 127.
static constexpr int N64_STICK_MAX = 80;

// SDL axis deadzone, in int16 axis units: ~24% of travel, SDL's recommended value.
static constexpr int PAD_AXIS_DEADZONE = 8000;

// Cardinal inradius of the octagon inside ultramodern's convert_to_n64_range(). Dividing an
// N64 value by this makes convert_to_n64_range() hand the same value back to the game.
static constexpr float ULTRAMODERN_STICK_INRADIUS = 82.0f;

// int16 SDL axis -> N64 stick: deadzoned, clamped to the calibrated travel.
static inline int8_t aero_pad_axis_to_n64(int v) {
    if (v > -PAD_AXIS_DEADZONE && v < PAD_AXIS_DEADZONE) return 0;
    float f = v / 32767.0f;
    if (f >  1.0f) f =  1.0f;
    if (f < -1.0f) f = -1.0f;
    return (int8_t)(f * N64_STICK_MAX);
}

// RecompFrontend reports analog input in normalized units. Store the calibrated
// N64 value in the cross-thread snapshot; input_get_input() normalizes it again
// for ultramodern's controller conversion.
static inline int8_t aero_normalized_stick_to_n64(float value) {
    if (value > 1.0f) value = 1.0f;
    if (value < -1.0f) value = -1.0f;
    return static_cast<int8_t>(value * N64_STICK_MAX);
}

// Android's Java touch overlay already publishes calibrated N64 stick units.
static inline int8_t aero_touch_axis_to_n64(int value) {
    return static_cast<int8_t>(std::clamp(value, -N64_STICK_MAX, N64_STICK_MAX));
}

struct AeroSampledInput {
    uint16_t buttons;
    int8_t stick_x;
    int8_t stick_y;
};

// Merge normalized frontend axes with the already-calibrated Android touch axes before the
// snapshot crosses to the game thread. Touch only supplies an axis when the frontend has none.
static inline AeroSampledInput aero_sample_input(uint16_t frontend_buttons,
                                                  float normalized_x,
                                                  float normalized_y,
                                                  uint16_t touch_buttons = 0,
                                                  int touch_x = 0,
                                                  int touch_y = 0) {
    const bool use_touch_x = normalized_x == 0.0f && touch_x != 0;
    const bool use_touch_y = normalized_y == 0.0f && touch_y != 0;
    return {
        static_cast<uint16_t>(frontend_buttons | touch_buttons),
        use_touch_x ? aero_touch_axis_to_n64(touch_x) : aero_normalized_stick_to_n64(normalized_x),
        use_touch_y ? aero_touch_axis_to_n64(touch_y) : aero_normalized_stick_to_n64(normalized_y),
    };
}

// N64 stick value -> the normalized value the runtime input callback must return.
static inline float aero_n64_stick_to_normalized(int8_t v) {
    return v / ULTRAMODERN_STICK_INRADIUS;
}

// Snapshot handoff between the SDL main thread and the game thread: one coherent uint32_t.
// [15:0] buttons, [23:16] stick_x (int8), [31:24] stick_y (int8).
static constexpr uint32_t AERO_INPUT_SNAPSHOT_BUTTONS = 0x0000FFFFu;
static constexpr int AERO_INPUT_SNAPSHOT_STICK_X_SHIFT = 16;
static constexpr int AERO_INPUT_SNAPSHOT_STICK_Y_SHIFT = 24;

static inline uint32_t aero_input_pack(uint16_t buttons, int8_t sx, int8_t sy) {
    return (uint32_t)buttons
         | ((uint32_t)(uint8_t)sx << AERO_INPUT_SNAPSHOT_STICK_X_SHIFT)
         | ((uint32_t)(uint8_t)sy << AERO_INPUT_SNAPSHOT_STICK_Y_SHIFT);
}

static inline uint16_t aero_input_snapshot_buttons(uint32_t snapshot) {
    return (uint16_t)(snapshot & AERO_INPUT_SNAPSHOT_BUTTONS);
}

static inline int8_t aero_input_snapshot_stick_x(uint32_t snapshot) {
    return (int8_t)((snapshot >> AERO_INPUT_SNAPSHOT_STICK_X_SHIFT) & 0xFFu);
}

static inline int8_t aero_input_snapshot_stick_y(uint32_t snapshot) {
    return (int8_t)((snapshot >> AERO_INPUT_SNAPSHOT_STICK_Y_SHIFT) & 0xFFu);
}

// AERO_INPUT_PULSE=BTNHEX:PERIOD:DUTY[:STARTVI[:COUNT]] window test. A held mask is one edge
// forever, so a pulse is what walks a headless menu chain: the button is down for DUTY VIs out
// of every PERIOD, starting at STARTVI, optionally stopping after COUNT presses.
static inline int aero_input_pulse_active(int vi, int period, int duty, int start, int count) {
    if (period <= 0 || vi < start) return 0;
    if (count > 0 && (vi - start) / period >= count) return 0;
    return ((vi - start) % period) < duty;
}

#endif // AERO_INPUT_H
