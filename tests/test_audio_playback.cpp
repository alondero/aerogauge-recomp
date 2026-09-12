// Exercise the real sink and SDL resampler with a deterministic host device.
// The device consumes 10 ms blocks, independently of 60 Hz guest submissions.
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <cstdio>
#include <cstdlib>
#include <vector>

static SDL_AudioSpec device;
static Uint32 queued;
static bool paused = true;
static unsigned underruns;
static unsigned starts;
static SDL_AudioFormat test_format = AUDIO_F32SYS;
static int test_rate = 48000;
static bool test_device_available = true;
static SDL_AudioDeviceID open_device(const char*, int, const SDL_AudioSpec*, SDL_AudioSpec* obtained, int) {
    device = {};
    device.freq = test_rate;
    device.format = test_format;
    device.channels = 2;
    device.samples = test_rate / 100;
    *obtained = device;
    return test_device_available ? 1 : 0;
}
static void pause_device(SDL_AudioDeviceID, int pause) {
    paused = pause != 0;
    if (!paused) ++starts;
}
static int queue_audio(SDL_AudioDeviceID, const void*, Uint32 bytes) { queued += bytes; return 0; }
static Uint32 queued_audio(SDL_AudioDeviceID) { return queued; }
static void close_device(SDL_AudioDeviceID) {}
#define SDL_OpenAudioDevice open_device
#define SDL_PauseAudioDevice pause_device
#define SDL_QueueAudio queue_audio
#define SDL_GetQueuedAudioSize queued_audio
#define SDL_CloseAudioDevice close_device
#include "../src/aero_audio.cpp"

static void require(bool condition, const char* message) {
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

static void playback(bool adaptive) {
    queued = underruns = starts = 0;
    paused = true;
    aero::audio::init(48000);
    ultramodern::audio_callbacks_t cb{};
    aero::audio::get_callbacks(&cb);
    cb.set_frequency(22050);
    std::vector<int16_t> pcm(928, 1234);
    unsigned submits = 0;
    unsigned next_frames = 464;
    for (unsigned ms = 0; ms < 20000; ++ms) {
        if (ms * 60 >= submits * 1000) {
            cb.queue_samples(pcm.data(), (adaptive ? next_frames : 367 + (submits & 1)) * 2);
            if (adaptive) {
                // ROM func_80001CA0: submit the preceding task, read AI length,
                // then size the NEXT task (target 368, minimum 352, align 16).
                // Include ultramodern's 0.5 * sizeof(int16_t) * (22050 / 60)
                // byte lookahead before the ROM converts bytes to stereo frames.
                const unsigned bytes = cb.get_frames_remaining() * 4;
                const unsigned remaining = bytes > 367 ? (bytes - 367) / 4 : 0;
                require(remaining < 464, "feedback grew beyond the mixer target");
                next_frames = std::max(352u, (368 - remaining + 96) & ~15u);
            }
            ++submits;
        }
        if (ms % 10 == 0 && !paused) {
            const Uint32 bytes = device.samples * device.channels * (SDL_AUDIO_BITSIZE(device.format) / 8);
            if (queued < bytes) { ++underruns; queued = 0; }
            else queued -= bytes;
        }
    }
    require(starts != 0, "audio never started");
    std::printf("playback: %u underruns, %u starts\n", underruns, starts);
    require(underruns == 0, "steady intro playback runs out of samples");
    require(queued < (unsigned)device.freq * device.channels *
                    (SDL_AUDIO_BITSIZE(device.format) / 8) / 5,
            "playback latency grows beyond 200 ms");
    // Recover from a long host stall by buffering again, not immediately playing
    // the first short resampler output into another underrun.
    queued = 0;
    cb.queue_samples(pcm.data(), 928);
    require(paused, "starved device restarted before rebuilding the buffer");
    for (unsigned i = 0; i < 10 && paused; ++i) cb.queue_samples(pcm.data(), 928);
    require(!paused && starts == 2, "starved device did not resume");
    aero::audio::shutdown();
}

int main() {
    SDL_setenv("AERO_HEADLESS", "0", 1);
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    playback(false);
    playback(true);
    test_format = AUDIO_S16SYS;
    test_rate = 22050;
    playback(true);
    test_device_available = false;
    aero::audio::init(48000);
    ultramodern::audio_callbacks_t headless{};
    aero::audio::get_callbacks(&headless);
    headless.set_frequency(22050);
    std::vector<int16_t> headless_pcm(928, 1234);
    headless.queue_samples(headless_pcm.data(), headless_pcm.size());
    require(headless.get_frames_remaining() <= 22050 / 60,
            "headless feedback exceeds one VI");
    aero::audio::shutdown();
    std::puts("PASS audio playback");
}
