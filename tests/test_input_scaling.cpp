// Host test for the host input contract in src/aero_input.h: the SDL axis mapping, the
// main-thread/game-thread snapshot handoff, the harness pulse window, and -- the regression
// this file exists for -- the round trip through the runtime's convert_to_n64_range()
// (lib/N64ModernRuntime/ultramodern/src/input.cpp), which must hand the ROM the same N64 stick
// value the host intended.
//
// Why the round trip is the test: the runtime consumes a *normalized* value, not an N64 one.
// Dividing by the int8 limit 127 instead of the octagon's inradius understates every
// deflection by ~35%, so a full-deflection stick reaches the ROM as 51. The ROM's menu reader
// (func_80057930) accepts a direction only at >= 41 after its own +-7 deadzone (func_80009494),
// i.e. >= 48 raw, so the menus went dead below ~93% physical travel -- with no error, no log,
// and a still-playable steering feel. This test fails if the divisor drifts that way again.
//
// No ROM, no SDL, no renderer: it links the dependency source for the mapping under test.
#undef NDEBUG
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>

#include "aero_input.h"

// Defined in lib/N64ModernRuntime/ultramodern/src/input.cpp. It is not exported by
// ultramodern/input.hpp, so this test declares it directly.
void convert_to_n64_range(float x, float y, int8_t& stick_x, int8_t& stick_y);

// input.cpp's unrelated SI entry points reference this. The test never calls one, but the
// linker still needs a definition once the object is linked.
namespace ultramodern {
void send_si_message() { std::abort(); }
}

namespace {

// The ROM's menu reader: a direction byte needs the translated stick at >= 0x29 = 41, and the
// ROM subtracts a further +-7 deadzone before that comparison.
constexpr int kRomMenuDirectionThreshold = 41;
constexpr int kRomDeadzone = 7;
constexpr int kRomMenuRawMinimum = kRomMenuDirectionThreshold + kRomDeadzone;

struct N64Pair {
    int8_t x;
    int8_t y;
};

N64Pair round_trip(int8_t x, int8_t y) {
    int8_t out_x = 0;
    int8_t out_y = 0;
    convert_to_n64_range(aero_n64_stick_to_normalized(x), aero_n64_stick_to_normalized(y),
                         out_x, out_y);
    return {out_x, out_y};
}

// The divisor must be the dependency's octagon inradius. Pinned separately from the round trip
// so a future change that alters both sides consistently still has to justify itself here:
// 80/127 = 0.63, 80/82 = 0.976.
void check_normalization_uses_the_octagon_inradius() {
    const float normalized_full = aero_n64_stick_to_normalized((int8_t)N64_STICK_MAX);
    assert(normalized_full > 0.97f && normalized_full < 0.98f);
    assert(aero_n64_stick_to_normalized(0) == 0.0f);
    assert(aero_n64_stick_to_normalized((int8_t)-N64_STICK_MAX) < 0.0f);
}

void check_cardinal_round_trip() {
    for (int v : {0, 20, 48, 53, 74, 76, 80, -53, -80}) {
        const N64Pair px = round_trip((int8_t)v, 0);
        const N64Pair py = round_trip(0, (int8_t)v);
        // One unit of slack: the dependency round-trips through float and truncates toward
        // zero, so 64 comes back as 63. The old 127 divisor was off by 19-29 units, far
        // outside this band.
        assert(std::abs(px.x - v) <= 1 && px.y == 0);
        assert(std::abs(py.y - v) <= 1 && py.x == 0);
    }
}

void check_full_deflection_clears_the_rom_menu_threshold() {
    // Cardinal full deflection.
    const N64Pair cardinal = round_trip((int8_t)N64_STICK_MAX, 0);
    assert(cardinal.x >= kRomMenuRawMinimum);

    // A diagonal push, which is how menus are actually driven. The dependency's octagon
    // corner is colder than the cardinal range (~69 measured), so this is the tight case.
    const N64Pair diagonal = round_trip((int8_t)N64_STICK_MAX, (int8_t)N64_STICK_MAX);
    assert(diagonal.x >= kRomMenuRawMinimum && diagonal.y >= kRomMenuRawMinimum);

    // The reported failure case: a ~92% push produced no menu direction at all before.
    const N64Pair most_of_the_way = round_trip(74, 0);
    assert(most_of_the_way.x >= kRomMenuRawMinimum);
}

void check_sdl_axis_mapping() {
    // Inside the deadzone the axis reads as centered.
    assert(aero_pad_axis_to_n64(0) == 0);
    assert(aero_pad_axis_to_n64(PAD_AXIS_DEADZONE - 1) == 0);
    assert(aero_pad_axis_to_n64(-(PAD_AXIS_DEADZONE - 1)) == 0);

    // Outside it the axis is live and clamped to the calibrated N64 travel, which is the
    // value the round-trip checks above assume.
    assert(aero_pad_axis_to_n64(PAD_AXIS_DEADZONE) > 0);
    assert(aero_pad_axis_to_n64(32767) == N64_STICK_MAX);
    assert(aero_pad_axis_to_n64(-32767) == -N64_STICK_MAX);
    assert(aero_pad_axis_to_n64(-32768) == -N64_STICK_MAX);

    // Partial deflection stays inside the range instead of saturating early.
    const int8_t half = aero_pad_axis_to_n64(16383);
    assert(half > 0 && half < N64_STICK_MAX);
}

void check_normalized_sampling() {
    // RecompFrontend reports normalized axes. The port snapshot stores the
    // calibrated N64 value before input_get_input normalizes it again.
    assert(aero_normalized_stick_to_n64(1.0f) == N64_STICK_MAX);
    assert(aero_normalized_stick_to_n64(-1.0f) == -N64_STICK_MAX);
    assert(aero_normalized_stick_to_n64(0.5f) == N64_STICK_MAX / 2);
    assert(aero_normalized_stick_to_n64(0.0f) == 0);
}

void check_touch_sampling() {
    // Android publishes the already-calibrated N64 stick value from the Java
    // overlay. Do not send it through normalized floating-point conversion.
    assert(aero_touch_axis_to_n64(80) == N64_STICK_MAX);
    assert(aero_touch_axis_to_n64(-80) == -N64_STICK_MAX);
    assert(aero_touch_axis_to_n64(200) == N64_STICK_MAX);
    assert(aero_touch_axis_to_n64(-200) == -N64_STICK_MAX);
}

void check_normalized_snapshot_sampling() {
    const uint32_t snapshot = aero_input_pack(
        0, aero_normalized_stick_to_n64(1.0f), aero_normalized_stick_to_n64(-0.5f));
    assert(aero_input_snapshot_stick_x(snapshot) == N64_STICK_MAX);
    assert(aero_input_snapshot_stick_y(snapshot) == -(N64_STICK_MAX / 2));
}

void check_sample_input_merge() {
    const AeroSampledInput frontend = aero_sample_input(0x4000, 0.5f, -0.5f, 0x0001, 80, -80);
    assert(frontend.buttons == 0x4001);
    assert(frontend.stick_x == N64_STICK_MAX / 2);
    assert(frontend.stick_y == -(N64_STICK_MAX / 2));

    const AeroSampledInput touch_fallback = aero_sample_input(0, 0.0f, 0.0f, 0x0200, 80, -80);
    assert(touch_fallback.buttons == 0x0200);
    assert(touch_fallback.stick_x == N64_STICK_MAX);
    assert(touch_fallback.stick_y == -N64_STICK_MAX);
}

void check_snapshot_handoff() {
    const uint32_t snapshot = aero_input_pack((uint16_t)0x9000, (int8_t)-80, (int8_t)53);
    assert(aero_input_snapshot_buttons(snapshot) == 0x9000);
    assert(aero_input_snapshot_stick_x(snapshot) == -80);
    assert(aero_input_snapshot_stick_y(snapshot) == 53);

    // Both stick bytes must survive as signed values, and buttons must not bleed into them.
    const uint32_t negative = aero_input_pack((uint16_t)0xFFFF, (int8_t)-1, (int8_t)-128);
    assert(aero_input_snapshot_buttons(negative) == 0xFFFF);
    assert(aero_input_snapshot_stick_x(negative) == -1);
    assert(aero_input_snapshot_stick_y(negative) == -128);
}

void check_pulse_window() {
    // AERO_INPUT_PULSE=BTNHEX:PERIOD:DUTY[:STARTVI[:COUNT]]: down for DUTY out of every
    // PERIOD, from STARTVI, optionally stopping after COUNT presses.
    assert(!aero_input_pulse_active(99, 60, 6, 100, 0));   // before STARTVI
    assert(aero_input_pulse_active(100, 60, 6, 100, 0));   // first VI of the duty window
    assert(aero_input_pulse_active(105, 60, 6, 100, 0));   // last VI of the duty window
    assert(!aero_input_pulse_active(106, 60, 6, 100, 0));  // released
    assert(!aero_input_pulse_active(166, 60, 6, 100, 0));  // released between presses
    assert(aero_input_pulse_active(160, 60, 6, 100, 0));   // second press

    // COUNT stops the pulse so a headless walk can park on a target screen.
    assert(aero_input_pulse_active(160, 60, 6, 100, 2));
    assert(!aero_input_pulse_active(220, 60, 6, 100, 2));

    // A period of 0 disables the pulse entirely.
    assert(!aero_input_pulse_active(100, 0, 6, 100, 0));
}

} // namespace

int main() {
    check_normalization_uses_the_octagon_inradius();
    check_cardinal_round_trip();
    check_full_deflection_clears_the_rom_menu_threshold();
    check_sdl_axis_mapping();
    check_normalized_sampling();
    check_touch_sampling();
    check_normalized_snapshot_sampling();
    check_sample_input_merge();
    check_snapshot_handoff();
    check_pulse_window();
    std::printf("PASS: host stick mapping round-trips through convert_to_n64_range; "
                "full deflection clears the ROM menu threshold (%d raw)\n",
                kRomMenuRawMinimum);
    return 0;
}
