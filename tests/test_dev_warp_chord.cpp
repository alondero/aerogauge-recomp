// Host test for the developer warp chord (src/aero_devkeys.h).
//
// The chord is the port's developer shortcut for jumping straight into a race.
// It must be hard to hit by accident, so the rules pinned here are:
//
//   1. Both modifiers are required. A bare digit, Ctrl alone, or Shift alone
//      never selects a track.
//   2. One press selects one track. Holding the chord does not repeat it.
//   3. Releasing any part of the chord re-arms the slot, so the next press
//      selects again.
//   4. The six digits map to tracks 0..5 in order, and a frame that presses
//      two digits at once resolves to the lowest index.
//
// Standalone host test: no ROM, SDL, or renderer. The tracker is header-only
// and free of SDL calls, so the port samples the keyboard on the main thread
// and this test pins the decision itself.
//   cmake -S . -B build/host-checks -DAERO_HOST_TESTS_ONLY=ON
//   cmake --build build/host-checks --target test_dev_warp_chord
//   ./build/host-checks/test_dev_warp_chord
#include "aero_devkeys.h"

#include <cstdio>
#include <cstdlib>

static void require(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

// A chord with both modifiers held and one digit down.
static aero::devkeys::WarpChord chord_with(int digit) {
    aero::devkeys::WarpChord chord;
    chord.ctrl = true;
    chord.shift = true;
    if (digit >= 0) chord.digit[digit] = true;
    return chord;
}

int main() {
    aero::devkeys::WarpChordTracker tracker;

    // 1a. A digit without modifiers selects nothing.
    aero::devkeys::WarpChord digit_only;
    digit_only.digit[0] = true;
    require(tracker.update(digit_only) == -1, "a bare digit must not start a race");
    require(tracker.update(digit_only) == -1, "a held bare digit must not start a race");

    // 1b. One modifier is not enough.
    aero::devkeys::WarpChord ctrl_only;
    ctrl_only.ctrl = true;
    ctrl_only.digit[0] = true;
    require(tracker.update(ctrl_only) == -1, "Ctrl+digit must not start a race");
    aero::devkeys::WarpChord shift_only;
    shift_only.shift = true;
    shift_only.digit[0] = true;
    require(tracker.update(shift_only) == -1, "Shift+digit must not start a race");

    // 2. The chord selects the track once.
    require(tracker.update(chord_with(2)) == 2, "Ctrl+Shift+3 must select track index 2");
    require(tracker.update(chord_with(2)) == -1, "a held chord must not repeat the warp");

    // 3a. Releasing the digit re-arms the slot.
    require(tracker.update(chord_with(-1)) == -1, "releasing the digit selects nothing");
    require(tracker.update(chord_with(2)) == 2, "the chord selects again after a release");

    // 3b. Releasing a modifier re-arms the slot too: the digit is still held,
    //     so the next modifier press is a new edge.
    require(tracker.update(chord_with(-1)) == -1, "releasing the digit selects nothing");
    aero::devkeys::WarpChord ctrl_shift_digit;
    ctrl_shift_digit.ctrl = true;
    ctrl_shift_digit.shift = true;
    ctrl_shift_digit.digit[2] = true;
    require(tracker.update(ctrl_shift_digit) == 2, "the chord selects track index 2");
    aero::devkeys::WarpChord digit_held = ctrl_shift_digit;
    digit_held.ctrl = false;
    require(tracker.update(digit_held) == -1, "dropping Ctrl must not select");
    digit_held.ctrl = true;
    require(tracker.update(digit_held) == 2, "restoring Ctrl re-arms and selects again");

    // 4a. The six digits map to tracks 0..5 in order.
    for (int digit = 0; digit < aero::devkeys::kWarpSlots; ++digit) {
        aero::devkeys::WarpChordTracker slot_tracker;
        require(slot_tracker.update(chord_with(digit)) == digit,
                "each digit must select its own track index");
    }

    // 4b. Two digits in one frame resolve to the lowest index.
    aero::devkeys::WarpChordTracker pair_tracker;
    aero::devkeys::WarpChord pair = chord_with(4);
    pair.digit[1] = true;
    require(pair_tracker.update(pair) == 1, "simultaneous digits must resolve to the lowest index");

    std::puts("PASS dev warp chord: modifiers required, one press per warp, re-armed on release");
    return 0;
}
