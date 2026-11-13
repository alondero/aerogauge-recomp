// Hold slow SDL operations at a recording boundary. AI feedback and lifecycle
// publication must remain available while open, conversion or close is stalled.
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <future>
#include <mutex>
#include <vector>

using namespace std::chrono_literals;
static void require(bool value, const char* message) {
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

struct Gate {
    std::mutex mutex;
    std::condition_variable cv;
    bool entered = false, released = false;
    void block() {
        std::unique_lock lock(mutex);
        entered = true;
        cv.notify_all();
        require(cv.wait_for(lock, 5s, [&] { return released; }), "SDL gate timed out");
    }
    void wait() {
        std::unique_lock lock(mutex);
        require(cv.wait_for(lock, 5s, [&] { return entered; }), "SDL operation never entered");
    }
    void release() {
        std::lock_guard lock(mutex);
        released = true;
        cv.notify_all();
    }
};

static Gate open_gate, stream_gate, close_gate;
static std::atomic<Uint32> queued{0};
static std::atomic<bool> block_stream{true};
static SDL_AudioDeviceID open_device(const char*, int, const SDL_AudioSpec* want,
                                     SDL_AudioSpec* obtained, int) {
    open_gate.block();
    *obtained = *want;
    return 1;
}
static void close_device(SDL_AudioDeviceID) { close_gate.block(); }
static void pause_device(SDL_AudioDeviceID, int) { }
static void clear_audio(SDL_AudioDeviceID) { queued.store(0); }
static Uint32 queued_audio(SDL_AudioDeviceID) { return queued.load(); }
static int queue_audio(SDL_AudioDeviceID, const void*, Uint32 bytes) { queued += bytes; return 0; }
static int stream_put(SDL_AudioStream* stream, const void* data, int bytes) {
    if (block_stream.exchange(false)) stream_gate.block();
    return SDL_AudioStreamPut(stream, data, bytes);
}
#define SDL_OpenAudioDevice open_device
#define SDL_CloseAudioDevice close_device
#define SDL_PauseAudioDevice pause_device
#define SDL_ClearQueuedAudio clear_audio
#define SDL_GetQueuedAudioSize queued_audio
#define SDL_QueueAudio queue_audio
#define SDL_AudioStreamPut stream_put
#include "../src/aero_audio.cpp"

template <typename Function>
static void responds(Function function, const char* message) {
    auto work = std::async(std::launch::async, function);
    require(work.wait_for(1s) == std::future_status::ready, message);
    work.get();
}

int main() {
#if defined(_WIN32)
    _putenv_s("AERO_HEADLESS", "0");
#else
    setenv("AERO_HEADLESS", "0", 1);
#endif
    ultramodern::audio_callbacks_t callbacks{};
    aero::audio::get_callbacks(&callbacks);
    auto opening = std::async(std::launch::async, [] { aero::audio::init(48000); });
    open_gate.wait();
    responds([&] { callbacks.get_frames_remaining(); callbacks.set_frequency(22050); },
             "device open held the AI state lock");
    open_gate.release();
    opening.get();

    std::vector<int16_t> pcm(4096, 1234);
    auto converting = std::async(std::launch::async, [&] { callbacks.queue_samples(pcm.data(), pcm.size()); });
    stream_gate.wait();
    responds([&] {
        callbacks.get_frames_remaining();
        aero::audio::set_suspended(true);
        require(callbacks.get_frames_remaining() == 0, "suspended AI reported a backlog");
        aero::audio::set_suspended(false);
    }, "conversion held the AI state lock or blocked suspension");
    stream_gate.release();
    converting.get();
    require(queued == 0, "a buffer converted before suspension reached the resumed device");
    callbacks.queue_samples(pcm.data(), pcm.size());
    require(queued > 0, "resumed conversion did not queue new PCM");
    aero::audio::set_suspended(true);
    require(queued == 0, "suspension did not clear host playback");
    callbacks.queue_samples(pcm.data(), pcm.size());
    require(queued == 0, "suspension accepted PCM");
    aero::audio::set_suspended(false);

    auto closing = std::async(std::launch::async, [] { aero::audio::shutdown(); });
    close_gate.wait();
    responds([&] { callbacks.get_frames_remaining(); callbacks.set_frequency(32000); },
             "device close held the AI state lock");
    close_gate.release();
    closing.get();
    std::puts("PASS audio lock scopes and suspension");
}
