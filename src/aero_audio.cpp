// SPDX-License-Identifier: GPL-3.0-or-later
// SDL2 push-audio backend for the ultramodern pivot. See aero_audio.h for the contract.
// NOTE (2026-07-12, #53): the game submits real M_AUDTASKs (task type 2) with an ACMD list every
// audio frame; PCM is synthesised by the RSP aspMain microcode at AeroGauge ROM 0x7F330 -- byte-
// identical to the Automobili Lamborghini port's audio-SDK mixer blob -- now RSPRecomp'd into
// src/aspMain.cpp and routed from the M_AUDTASK path in main.cpp (see aspMain.us.toml for the
// static ROM derivation). osAiSetNextBuffer (native) then queues the finished buffer into this sink.
//
// Design notes:
//  * Format: int16 stereo at 48 kHz initially. SDL is asked for AUDIO_S16LSB
//    and 2 channels. The actual obtained spec may differ; queue_samples builds
//    an SDL_AudioCVT when the obtained spec does not match the game's output
//    (rate/format) and runs SDL_ConvertAudio on every submit.
//  * Thread model: the game's audio thread calls queue_samples (via the
//    ultramodern shim). SDL_QueueAudio and SDL_GetQueuedAudioSize are
//    thread-safe (per SDL2 docs) -- no extra lock needed.
//  * First-AICall tripwire: submit() logs once the first time it sees a NON-SILENT
//    buffer. With real aspMain synthesis wired (#53) it fires once the game starts
//    mixing -- the headless boot-smoke's "first NON-SILENT buffer" line is the
//    end-to-end proof that PCM is reaching the sink.

#include "aero_audio.h"

#include <SDL.h>
#include <ultramodern/ultramodern.hpp>
#include "recomp.h" // recomp_context + MEM_W for the func_80079720 native override below

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <vector>

namespace {

// File-static device state. The struct is intentionally not in a singleton --
// the public API in aero_audio.h is the only entry point. Mutating the static
// vars is safe because all three callbacks plus init/shutdown are called from
// the same OS process and SDL2's queue API is internally serialised.
SDL_AudioDeviceID g_dev = 0;
SDL_AudioSpec     g_obtained{};
uint32_t         g_desired_rate = 0;
// Persistent stream converter (W137, #53): resampling 22050->48000 needs filter STATE carried
// across submits. The old per-submit SDL_AudioCVT path reset that state every ~21 ms buffer
// (SDL_ConvertAudio is a one-shot API that pads each chunk's edges with silence), which garbled
// the whole mix at chunk rate — Adam's "each chunk sounds played backwards" report. Guarded by
// g_state_mtx; recreated when the game changes the AI frequency.
SDL_AudioStream*  g_stream = nullptr;
uint32_t          g_stream_src_rate = 0;
std::atomic<bool> g_device_opened{false};
std::atomic<bool> g_init_logged{false};
std::atomic<bool> g_first_hit_logged{false};
std::atomic<bool> g_first_nonsilent_logged{false};

// Tiny guard for the rare case the runtime calls set_frequency before init
// (init_audio does this). We accept whatever was last set; if init never ran,
// we just store into a dead local and the device path is never taken (the
// runtime reports get_remaining_audio_bytes=100 -- see ultramodern/src/audio.cpp:52).
std::mutex g_state_mtx;

// Real AI hardware masks the length register to 18 bits (max DMA 256 KB); anything above
// that ceiling is not a real audio frame. Early boot submits one garbage-sized buffer
// (measured: byte_count 0xFFFF5000 = -0xB000 as a signed AI length) which must be dropped
// BEFORE any write touches the payload (tests/test_audio_oversize_guard.cpp).
constexpr size_t kMaxAiSamples = (256u * 1024u) / sizeof(int16_t);

void ai_fifo_queue_locked(size_t stereo_frames);  // virtual AI FIFO, defined below

bool drop_oversize(size_t sample_count) {
    if (sample_count <= kMaxAiSamples) {
        return false;
    }
    static std::atomic<bool> s_oversize_logged{false};
    bool exp = false;
    if (s_oversize_logged.compare_exchange_strong(exp, true)) {
        std::fprintf(stderr, "[probe] audio: dropped oversize submit (%zu samples > AI max)\n",
                     sample_count);
    }
    return true;
}

void log_opened_once() {
    bool expected = false;
    if (g_init_logged.compare_exchange_strong(expected, true)) {
        std::fprintf(stderr,
                     "[probe] audio: opened SDL2 device freq=%u fmt=%d ch=%u samples=%u\n",
                     (unsigned)g_obtained.freq, (int)g_obtained.format,
                     (unsigned)g_obtained.channels, (unsigned)g_obtained.samples);
    }
}

void submit(const int16_t* pcm, size_t sample_count) {
    if (pcm == nullptr || sample_count == 0) {
        return;
    }
    // One-shot content tripwire (PERMANENT harness instrumentation): distinguishes "sink receives
    // buffers" from "sink receives AUDIBLE PCM" in headless logs. Runs BEFORE the device check so
    // a headless run without a drainable audio device (e.g. SDL_AUDIODRIVER=dummy under WSL, where
    // an undrained Pulse queue makes the game's backpressure stop synthesis) still reports whether
    // the game produced real PCM.
    if (!g_first_nonsilent_logged.load() && sample_count <= kMaxAiSamples) {
        for (size_t i = 0; i < sample_count; i++) {
            if (pcm[i] != 0) {
                bool exp2 = false;
                if (g_first_nonsilent_logged.compare_exchange_strong(exp2, true)) {
                    std::fprintf(stderr,
                                 "[probe] audio: first NON-SILENT buffer (sample[%zu]=%d of %zu)\n",
                                 i, (int)pcm[i], sample_count);
                }
                break;
            }
        }
    }
    if (!g_device_opened.load() || g_dev == 0) {
        // Graceful degradation: drop. This is the same shape peer projects use
        // for headless builds where no audio device is available.
        return;
    }
    // sample_count is total int16 samples (stereo: 2 per frame). Bytes =
    // sample_count * sizeof(int16_t).
    const uint32_t byte_count = (uint32_t)(sample_count * sizeof(int16_t));

    // Un-swizzle the guest sample order (W137, #53). N64Recomp stores RDRAM as byte-swapped
    // 32-bit words (guest byte A lives at host A^3), and the RSP DMA writes the finished PCM
    // through that convention. A raw int16 view of the buffer therefore yields each aligned
    // word's two samples in REVERSED order (values intact) — i.e. the L/R channels swapped.
    // Swapping each pair restores the guest (hardware) L,R interleave; peer ports do the same
    // in their queue_samples callbacks. AI buffers are 8-byte aligned, so pairs line up with
    // guest words.
    static std::vector<int16_t> swapped;
    swapped.resize(sample_count);
    for (size_t i = 0; i + 1 < sample_count; i += 2) {
        swapped[i + 0] = pcm[i + 1];
        swapped[i + 1] = pcm[i + 0];
    }
    if (sample_count & 1) {
        swapped[sample_count - 1] = pcm[sample_count - 1];
    }

    std::lock_guard<std::mutex> lock(g_state_mtx);
    // Cheap passthrough: native format + native channels + native rate.
    const bool native_rate  = (uint32_t)g_obtained.freq == g_desired_rate;
    const bool native_fmt   = g_obtained.format == AUDIO_S16LSB;
    const bool native_chan  = g_obtained.channels == 2;
    if (native_rate && native_fmt && native_chan) {
        if (SDL_QueueAudio(g_dev, swapped.data(), byte_count) != 0) {
            std::fprintf(stderr, "[probe] audio: SDL_QueueAudio failed: %s\n", SDL_GetError());
        }
    } else {
        // Convert via a PERSISTENT SDL_AudioStream (stateful resampler — see the note at
        // g_stream). Recreate only when the game's AI frequency changes (rare: once at boot).
        if (g_stream == nullptr || g_stream_src_rate != g_desired_rate) {
            if (g_stream != nullptr) {
                SDL_FreeAudioStream(g_stream);
            }
            g_stream = SDL_NewAudioStream(AUDIO_S16LSB, 2, (int)g_desired_rate,
                                          g_obtained.format, g_obtained.channels,
                                          g_obtained.freq);
            g_stream_src_rate = g_desired_rate;
            if (g_stream == nullptr) {
                std::fprintf(stderr, "[probe] audio: SDL_NewAudioStream failed: %s\n",
                             SDL_GetError());
            }
        }
        if (g_stream == nullptr) {
            // Degraded fallback: queue unconverted (wrong rate beats silence).
            if (SDL_QueueAudio(g_dev, swapped.data(), byte_count) != 0) {
                std::fprintf(stderr, "[probe] audio: SDL_QueueAudio (fallback) failed: %s\n",
                             SDL_GetError());
            }
            return;
        }
        if (SDL_AudioStreamPut(g_stream, swapped.data(), (int)byte_count) != 0) {
            std::fprintf(stderr, "[probe] audio: SDL_AudioStreamPut failed: %s\n", SDL_GetError());
            return;
        }
        const int avail = SDL_AudioStreamAvailable(g_stream);
        if (avail > 0) {
            static std::vector<uint8_t> out;
            out.resize((size_t)avail);
            const int got = SDL_AudioStreamGet(g_stream, out.data(), avail);
            if (got > 0) {
                if (SDL_QueueAudio(g_dev, out.data(), (Uint32)got) != 0) {
                    std::fprintf(stderr, "[probe] audio: SDL_QueueAudio (stream) failed: %s\n",
                                 SDL_GetError());
                }
            }
        }
    }

    bool expected = false;
    if (g_first_hit_logged.compare_exchange_strong(expected, true)) {
        const uint32_t frames = (uint32_t)(sample_count / 2);
        std::fprintf(stderr,
                     "[probe] audio: first osAiSetNextBuffer routed (%u samples, %u frames)\n",
                     (unsigned)sample_count, (unsigned)frames);
    }
}

void queue_samples(int16_t* pcm, size_t sample_count) {
    // Oversize guard up-front (test_audio_oversize_guard.cpp): the extents behind any garbage AI
    // length are garbage, so reject before touching the payload. submit() guards again (defence in
    // depth) for non-queue_samples callers.
    if (pcm == nullptr || drop_oversize(sample_count)) {
        return;
    }
    // Feed the virtual AI FIFO for every accepted buffer, BEFORE the device check in
    // submit(): headless runs (no SDL device) must model console drain too, or the game's
    // backpressure sees a permanently-empty AI and requests crash-inducing oversized frames
    // (see get_frames_remaining).
    {
        std::lock_guard<std::mutex> lock(g_state_mtx);
        ai_fifo_queue_locked(sample_count / 2);
    }
    // AERO_AUDIO_RMS=1: print a per-second RMS of the submitted PCM. Headless smoke
    // runs use this to tell "music/SFX playing" from "silence" without capturing a
    // WAV (e.g. the music-engine regression gate: boot shows the ~3 s jingle burst,
    // then sustained non-zero RMS once the sequenced title music starts).
    static int rms_probe = -1;
    if (rms_probe < 0) rms_probe = (std::getenv("AERO_AUDIO_RMS") != nullptr) ? 1 : 0;
    if (rms_probe == 1) {
        static uint64_t acc = 0, n = 0, block = 0;
        for (size_t i = 0; i < sample_count; i++) {
            int64_t s = pcm[i];
            acc += (uint64_t)(s * s);
            n++;
        }
        if (n >= 44100) {  // ~1 s of 22050 Hz stereo
            double rms = n ? __builtin_sqrt((double)acc / (double)n) : 0.0;
            std::fprintf(stderr, "[rms] t=%llus rms=%.0f\n", (unsigned long long)block, rms);
            block++;
            acc = 0;
            n = 0;
        }
    }
    submit(pcm, sample_count);
}

// Virtual AI FIFO (issue #7 follow-up, aspMain unhandled-jump crash, 2026-07-17). The N64
// AI drains queued PCM at exactly the game's AI rate, so osAiGetLength declines smoothly and
// only reaches 0 when the game genuinely stops feeding audio. The previous implementation
// reported the SDL device queue, which (a) is pulled in whole callback-sized bursts (~10 ms
// sawtooth, momentary zeros at pull boundaries) and (b) is absent entirely in AERO_HEADLESS
// runs, where the old ideal-drain model reported a PERMANENT zero backlog. A zero report
// makes the game's mixer request a maximum-length frame (target - 0 + 0x60 samples => 3+
// subframes per task), and oversized frames expose a latent bug in the game's own command
// builder: a voice whose pull produces zero samples emits its envmixer with a stale SETBUFF
// count (0x240 from the preceding interleave/save block), whose wet-buffer writes wrap past
// DMEM 0x1000 and shred the ACMD dispatch table -> "Unhandled jump target" crash (RSP DMEM
// wraps identically on real hardware; console timing just never produces the degenerate
// frame). Modeling the console drain keeps frame requests console-sized in windowed AND
// headless runs. Guarded by g_state_mtx.
std::chrono::steady_clock::time_point g_ai_fifo_end{};

void ai_fifo_queue_locked(size_t stereo_frames) {
    if (g_desired_rate == 0) {
        return;
    }
    const auto now = std::chrono::steady_clock::now();
    if (g_ai_fifo_end < now) {
        g_ai_fifo_end = now;
    }
    g_ai_fifo_end += std::chrono::nanoseconds(
        (uint64_t)stereo_frames * 1'000'000'000ull / g_desired_rate);
}

size_t get_frames_remaining() {
    std::lock_guard<std::mutex> lock(g_state_mtx);
    if (g_desired_rate == 0) {
        return 0;
    }
    const auto now = std::chrono::steady_clock::now();
    if (g_ai_fifo_end <= now) {
        return 0;
    }
    const uint64_t ns = (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(
        g_ai_fifo_end - now).count();
    return (size_t)(ns * g_desired_rate / 1'000'000'000ull);
}

void set_frequency(uint32_t freq) {
    std::lock_guard<std::mutex> lock(g_state_mtx);
    if (freq != g_desired_rate) {
        std::fprintf(stderr, "[probe] audio: set_frequency %u -> %u\n", g_desired_rate, freq);
    }
    g_desired_rate = freq;
    // We do not reopen the device on every set_frequency. SDL honours the
    // requested rate via SDL_AUDIO_ALLOW_ANY_CHANGE at open time. If the
    // obtained spec rate does not match what the game asks for, the CVT path
    // in submit() handles the conversion. This keeps the audio path light --
    // a reopen is heavy and would stall the game thread for tens of ms.
}

} // anonymous namespace

namespace aero::audio {

void init(uint32_t desired_sample_rate) {
    std::lock_guard<std::mutex> lock(g_state_mtx);
    if (g_device_opened.load()) {
        return;  // idempotent
    }
    g_desired_rate = desired_sample_rate ? desired_sample_rate : 48000;

    // HEADLESS harness runs get NO audio device (W135, #53). Rationale: in a headless/WSL
    // environment the SDL queue never drains (Pulse has no real sink; SDL's dummy driver buffers
    // forever), so a queue-based get_frames_remaining would report a full queue and the game's
    // backpressure would stop synthesising -- silently masking whether the audio pipeline works.
    // Backpressure now comes from the virtual AI FIFO (see get_frames_remaining), which drains
    // at the console rate with or without a device, so headless runs synthesise real PCM at the
    // console cadence (NON-SILENT tripwire in submit() still reports it).
    {
        const char* headless = std::getenv("AERO_HEADLESS");
        if (headless && headless[0] && headless[0] != '0') {
            std::fprintf(stderr, "[probe] audio: headless -- no SDL device (ideal-drain sink)\n");
            return;
        }
    }

    // Windows driver hint: bypass DirectSound for the lower-latency WASAPI
    // backend. Mirrors the peer pattern in Zelda64Recomp/SnowboardKids2/
    // BM64Recomp. No-op on Linux/macOS.
#if defined(_WIN32)
    SDL_setenv("SDL_AUDIODRIVER", "wasapi", true);
#endif

    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        std::fprintf(stderr, "[probe] audio: SDL_InitSubSystem(SDL_INIT_AUDIO) failed: %s\n",
                     SDL_GetError());
        return;
    }

    SDL_AudioSpec want{};
    want.freq     = (int)g_desired_rate;
    want.format   = AUDIO_S16LSB;  // int16 little-endian, host-native on x86
    want.channels = 2;            // stereo, matching the N64 AI output
    want.samples  = 0x100;        // 256 frames ~= 5.3 ms at 48 kHz; the value
                                  // ultramodern's buffer_offset_frames heuristic
                                  // (ultramodern/src/audio.cpp:41) plays nicely with
    want.callback = nullptr;      // use SDL_QueueAudio, not a callback

    g_dev = SDL_OpenAudioDevice(/*device=*/nullptr, /*iscapture=*/0,
                                &want, &g_obtained,
                                SDL_AUDIO_ALLOW_ANY_CHANGE);
    if (g_dev == 0) {
        std::fprintf(stderr, "[probe] audio: SDL_OpenAudioDevice failed: %s\n", SDL_GetError());
        return;
    }
    SDL_PauseAudioDevice(g_dev, 0);  // start playback immediately
    g_device_opened.store(true);
    log_opened_once();
}

void get_callbacks(ultramodern::audio_callbacks_t* out) {
    out->queue_samples        = &queue_samples;
    out->get_frames_remaining = &get_frames_remaining;
    out->set_frequency        = &set_frequency;
}

void shutdown() {
    std::lock_guard<std::mutex> lock(g_state_mtx);
    if (g_stream != nullptr) {
        SDL_FreeAudioStream(g_stream);
        g_stream = nullptr;
        g_stream_src_rate = 0;
    }
    if (g_dev != 0) {
        SDL_CloseAudioDevice(g_dev);
        g_dev = 0;
    }
    g_device_opened.store(false);
}

} // namespace aero::audio

// NOTE(aerogauge): the Lamborghini port carried a native override here for that ROM's
// sound-player status getter (a busy-spin the cooperative scheduler needed a dispatch
// point in). Dropped with the stack port — AeroGauge equivalents get added when its
// audio library is mapped.
