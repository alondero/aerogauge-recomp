// Host master gain: the audio module must scale the host copy of the submitted
// PCM, leave the guest buffer untouched, and keep draining the virtual AI FIFO
// while muted.
//
// The device stub captures the queued bytes so the test can inspect the exact
// samples the host would play back.
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>

static SDL_AudioSpec device;
static std::vector<uint8_t> queued_bytes;
static bool paused = true;
static int open_count = 0;

static SDL_AudioDeviceID open_device(const char*, int, const SDL_AudioSpec*, SDL_AudioSpec* obtained, int) {
    device = {};
    device.freq = 48000;
    device.format = AUDIO_S16LSB;
    device.channels = 2;
    device.samples = 480;
    *obtained = device;
    ++open_count;
    return 1;
}
static void pause_device(SDL_AudioDeviceID, int pause) { paused = pause != 0; }
static int queue_audio(SDL_AudioDeviceID, const void* data, Uint32 bytes) {
    const uint8_t* source = static_cast<const uint8_t*>(data);
    queued_bytes.insert(queued_bytes.end(), source, source + bytes);
    return 0;
}
static Uint32 queued_audio(SDL_AudioDeviceID) { return (Uint32)queued_bytes.size(); }
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

// The audio module reads AERO_HEADLESS with the C runtime's getenv, so the
// switch has to go through the C runtime. SDL_setenv only updates the Win32
// environment block, which getenv does not see.
static void set_environment(const char* name, const char* value) {
#if defined(_WIN32)
    _putenv_s(name, value != nullptr ? value : "");
#else
    if (value != nullptr) setenv(name, value, 1);
    else unsetenv(name);
#endif
}

static ultramodern::audio_callbacks_t callbacks{};

// The queued bytes are int16 stereo samples in host order.
static std::vector<int16_t> queued_samples(size_t from = 0) {
    std::vector<int16_t> out((queued_bytes.size() - from) / sizeof(int16_t));
    if (!out.empty()) {
        std::memcpy(out.data(), queued_bytes.data() + from, out.size() * sizeof(int16_t));
    }
    return out;
}

// The guest submits a byte-swapped stereo buffer: the audio path un-swizzles
// each pair before the host conversion.
static int16_t host_sample(const std::vector<int16_t>& pcm, size_t index) {
    return pcm[index ^ 1];
}

static void unity_is_exact() {
    aero::audio::set_master_gain(1.0f);
    std::vector<int16_t> pcm = {0, 1234, -1, 32767, -32768, 7};
    const std::vector<int16_t> original = pcm;
    const size_t before = queued_bytes.size();
    callbacks.queue_samples(pcm.data(), pcm.size());
    const auto queued = queued_samples(before);
    require(queued.size() == pcm.size(), "unity did not queue every sample");
    for (size_t i = 0; i < pcm.size(); ++i) {
        require(queued[i] == host_sample(original, i), "unity changed a sample");
    }
    require(pcm == original, "unity modified the guest buffer");
}

static void zero_is_silent_but_still_queued() {
    aero::audio::set_master_gain(0.0f);
    std::vector<int16_t> pcm(64, 1000);
    const size_t before = queued_bytes.size();
    callbacks.queue_samples(pcm.data(), pcm.size());
    const auto queued = queued_samples(before);
    require(queued.size() == pcm.size(), "mute did not queue the buffer");
    for (int16_t sample : queued) {
        require(sample == 0, "mute left a non-zero sample");
    }
}

static void intermediate_gain_rounds_away_from_zero() {
    aero::audio::set_master_gain(0.5f);
    std::vector<int16_t> pcm = {3, -3, 1000, -1000, 32767, -32768, 1, -1};
    const size_t before = queued_bytes.size();
    callbacks.queue_samples(pcm.data(), pcm.size());
    const auto queued = queued_samples(before);
    require(queued.size() == pcm.size(), "half gain did not queue every sample");
    // The host sees the guest's pair in the reversed order the audio path
    // restores, so the expectations follow host_sample().
    // Half of three is 1.5: rounded away from zero on both signs.
    require(queued[0] == -2 && queued[1] == 2, "half gain did not round away from zero");
    require(queued[2] == -500 && queued[3] == 500, "half gain scaled incorrectly");
    require(queued[4] == -16384 && queued[5] == 16384, "half gain clipped a full-scale sample");
    require(queued[6] == -1 && queued[7] == 1, "half gain rounded 0.5 incorrectly");
}

static void scaling_saturates_and_never_amplifies() {
    require(aero::audio::scale_sample(32767, 1.0f) == 32767, "unity changed a full-scale sample");
    require(aero::audio::scale_sample(-32768, 1.0f) == -32768, "unity changed the most negative sample");
    require(aero::audio::scale_sample(32767, 0.0f) == 0, "silence was not exact");
    require(aero::audio::scale_sample(-32768, 0.0f) == 0, "silence was not exact for the most negative sample");
    require(aero::audio::scale_sample(32767, 2.0f) == 32767, "a positive overshoot did not saturate");
    require(aero::audio::scale_sample(-32768, 2.0f) == -32768, "a negative overshoot did not saturate");
    require(aero::audio::scale_sample(1000, -1.0f) == 0, "a negative gain was not muted");
    // The published gain is clamped, so the port can never amplify the source.
    aero::audio::set_master_gain(4.0f);
    require(aero::audio::master_gain() == 1.0f, "the published gain was not clamped");
    aero::audio::set_master_gain(-1.0f);
    require(aero::audio::master_gain() == 0.0f, "a negative published gain was not muted");
    aero::audio::set_master_gain(0.25f);
    require(aero::audio::master_gain() == 0.25f, "the published gain was not stored");
}

static void reused_guest_buffer_is_not_attenuated_twice() {
    aero::audio::set_master_gain(0.5f);
    std::vector<int16_t> pcm(32, 800);
    const std::vector<int16_t> original = pcm;
    size_t before = queued_bytes.size();
    callbacks.queue_samples(pcm.data(), pcm.size());
    const auto first = queued_samples(before);
    before = queued_bytes.size();
    callbacks.queue_samples(pcm.data(), pcm.size());
    const auto second = queued_samples(before);
    require(pcm == original, "the guest buffer was modified");
    require(first == second, "a reused guest buffer was attenuated twice");
    for (int16_t sample : first) {
        require(sample == 400, "the reused guest buffer was not scaled by the gain");
    }
}

static void concurrent_changes_never_mix_gains() {
    std::atomic<bool> running{true};
    std::thread flipper([&running] {
        while (running.load(std::memory_order_relaxed)) {
            aero::audio::set_master_gain(1.0f);
            aero::audio::set_master_gain(0.5f);
        }
    });
    // A constant guest buffer makes each queued block either fully scaled or
    // fully unscaled; a torn snapshot would show a value in between.
    std::vector<int16_t> pcm(64, 1000);
    for (int round = 0; round < 400; ++round) {
        const size_t before = queued_bytes.size();
        callbacks.queue_samples(pcm.data(), pcm.size());
        const auto queued = queued_samples(before);
        for (int16_t sample : queued) {
            require(sample == 1000 || sample == 500, "a buffer mixed two gain snapshots");
        }
    }
    running.store(false, std::memory_order_relaxed);
    flipper.join();
}

// The virtual AI FIFO models the guest-side drain when there is no host
// device. Muting changes what the host plays, not the drain model, so the
// backlog the guest sees must stay the same.
static void mute_keeps_the_virtual_drain_advancing() {
    std::vector<int16_t> pcm(64, 1000);  // 32 stereo frames
    const auto drain = [&] {
        while (callbacks.get_frames_remaining() > 0) {
            // Spin until the modelled backlog is empty.
        }
    };
    drain();
    aero::audio::set_master_gain(1.0f);
    callbacks.queue_samples(pcm.data(), pcm.size());
    const size_t unmuted = callbacks.get_frames_remaining();
    drain();
    aero::audio::set_master_gain(0.0f);
    callbacks.queue_samples(pcm.data(), pcm.size());
    const size_t muted = callbacks.get_frames_remaining();
    require(unmuted > 0, "the guest drain stopped while unmuted");
    require(muted > 0, "muting stopped the virtual AI drain");
    require(std::abs((long)muted - (long)unmuted) <= 2,
            "muting changed the virtual AI backlog");
}

int main() {
    set_environment("AERO_HEADLESS", "0");
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    aero::audio::init(48000);
    aero::audio::get_callbacks(&callbacks);
    require(open_count == 1, "the audio device was not opened");

    unity_is_exact();
    zero_is_silent_but_still_queued();
    intermediate_gain_rounds_away_from_zero();
    scaling_saturates_and_never_amplifies();
    reused_guest_buffer_is_not_attenuated_twice();
    concurrent_changes_never_mix_gains();

    // The device is never reopened for a gain change.
    require(open_count == 1, "a gain change reopened the audio device");

    // Headless: no device, so the submission is dropped, but the guest buffer
    // still must not be touched and the call must not fault.
    aero::audio::shutdown();
    set_environment("AERO_HEADLESS", "1");
    aero::audio::init(48000);
    require(open_count == 1, "the headless run opened a device");
    std::vector<int16_t> headless_pcm(64, 1234);
    const std::vector<int16_t> original = headless_pcm;
    callbacks.queue_samples(headless_pcm.data(), headless_pcm.size());
    require(headless_pcm == original, "the headless path modified the guest buffer");
    require(callbacks.get_frames_remaining() <= 48000 / 60,
            "the headless feedback exceeded one VI");
    mute_keeps_the_virtual_drain_advancing();

    std::puts("PASS audio gain");
}
