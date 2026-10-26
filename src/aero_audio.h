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

#include <cstddef>
#include <cstdint>

namespace ultramodern {
struct audio_callbacks_t;
}

namespace aero::audio {

// Initialise the SDL2 audio backend. Safe to call once before recomp::start();
// idempotent if called more than once. Opens the default audio device at the
// requested sample rate (the current runtime requests 48 kHz). The device
// stays paused until the runtime has queued enough PCM to cover host
// resampling and playback callback timing.
void init(uint32_t desired_sample_rate);

// Populate the three ultramodern audio callbacks (queue_samples /
// get_frames_remaining / set_frequency) into `out`. Callers must invoke
// `init(...)` first; the callback pointers are stable for the process lifetime
// of the SDL device.
void get_callbacks(ultramodern::audio_callbacks_t* out);

// Tear down the SDL device. Optional in the current boot path; provided for
// clean shutdown when the watchdog quits the process.
void shutdown();

// --- Master gain ------------------------------------------------------------
//
// The host-side master gain for the generated audio. 1.0 keeps the guest PCM
// unmodified, 0.0 mutes the host output. It is a host setting only: the
// guest's own music/effects/announcer balance is untouched, and the guest's
// source buffers are never written to.
//
// The value is a cross-thread snapshot. The settings layer publishes it from
// the SDL main thread and the audio path reads it once per submitted buffer,
// so a change lands on the next buffer without a lock, without reopening the
// device, and without resetting the conversion stream. Values outside
// [0, 1] are clamped; this port never amplifies the source.
void set_master_gain(float gain);
float master_gain();

// Scale one signed 16-bit sample by `gain`. Rounding is half away from zero
// and the result saturates to the int16 range, so a gain of 1.0 is an exact
// pass-through and a gain of 0.0 is exact silence.
int16_t scale_sample(int16_t sample, float gain);

// Apply `gain` to `count` signed 16-bit samples in place. The buffer must be
// the host's own copy: the guest's source PCM stays untouched, so a guest
// buffer the game reuses is never attenuated twice.
void apply_master_gain(int16_t* samples, size_t count, float gain);

} // namespace aero::audio

#endif // AERO_AUDIO_H
