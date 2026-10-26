#ifndef AERO_DEVKEYS_H
#define AERO_DEVKEYS_H

// Developer keyboard chords (src/aero_devkeys.h).
//
// The port samples the keyboard on the SDL main thread; the decision below is
// pure, so a host test can pin it without SDL, a ROM, or a window.
//
// The developer race warp is bound to Ctrl+Shift+1..6 (the top-row digit keys).
// A bare function key is one keystroke away from launching a race, and the
// function keys are the natural home for the RT64 developer tools and for
// player bindings, so the warp needs a chord that no single key press can
// produce.
//
// The chord state is sampled per frame; the tracker turns it into one request
// per press. The game thread consumes the request (src/aero_warp.c); nothing
// here touches guest memory or the game thread.

namespace aero::devkeys {

// The six warp slots map to the top-row digits 1..6, in order.
inline constexpr int kWarpSlots = 6;

// One frame of the warp chord: the two modifiers and the digit keys.
struct WarpChord {
    bool ctrl = false;
    bool shift = false;
    bool digit[kWarpSlots] = {false, false, false, false, false, false};
};

// Both modifiers are required. A digit on its own -- for example a digit key
// bound to a game button -- must never start a race.
inline bool warp_chord_held(const WarpChord& chord) {
    return chord.ctrl && chord.shift;
}

// Edge detection for the warp slots.
//
// update() returns the 0-based track index the chord newly selected, or -1
// when nothing new was selected. A chord held down never repeats, and
// releasing any part of the chord re-arms the slot, so pressing the chord
// again warps again. When several digits go down in the same frame the
// lowest index wins, so the request is deterministic.
class WarpChordTracker {
public:
    int update(const WarpChord& chord) {
        const bool held = warp_chord_held(chord);
        int selected = -1;
        for (int i = 0; i < kWarpSlots; ++i) {
            const bool down = held && chord.digit[i];
            if (down && !armed_[i] && selected < 0) selected = i;
            armed_[i] = down;
        }
        return selected;
    }

private:
    bool armed_[kWarpSlots] = {false, false, false, false, false, false};
};

} // namespace aero::devkeys

#endif
