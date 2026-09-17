// SPDX-License-Identifier: GPL-3.0-or-later
// Host audio boundary for the translated game.
//
// Ownership: the runtime invokes these callbacks from the game audio path.
// This module owns the SDL device, conversion stream, queueing, and the
// headless virtual FIFO. The generated aspMain code owns RSP mixing; the
// game owns the source task and AI buffer contract.
//
// A missing host device is reported by the callback path and does not make
// the generated audio code correct or incorrect by itself. See
// docs/reference/audio.md and docs/testing.md for the evidence boundary.
#ifndef AERO_AUDIO_H
#define AERO_AUDIO_H

#include <cstdint>

namespace ultramodern {
struct audio_callbacks_t;
}

namespace aero::audio {

// Initialise the SDL2 audio backend. Safe to call once before recomp::start();
// idempotent if called more than once. Opens the default audio device at the
// requested sample rate (48 kHz is what ultramodern::init_audio asks for, per
// ultramodern/src/ultrainit.cpp:28). The device stays paused until the runtime
// has queued enough PCM to cover host resampling and playback callback timing.
void init(uint32_t desired_sample_rate);

// Populate the three ultramodern audio callbacks (queue_samples /
// get_frames_remaining / set_frequency) into `out`. Callers must invoke
// `init(...)` first; the callback pointers are stable for the process lifetime
// of the SDL device.
void get_callbacks(ultramodern::audio_callbacks_t* out);

// Tear down the SDL device. Optional in the current boot path; provided for
// clean shutdown when the watchdog quits the process.
void shutdown();

} // namespace aero::audio

#endif // AERO_AUDIO_H
