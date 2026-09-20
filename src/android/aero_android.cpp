#include "aero_android.h"
#include "aero_input.h"
#include "aero_config.h"
#include "aero_menu.h"
#include <jni.h>
#include <atomic>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <condition_variable>
#include <mutex>
#include <unistd.h>

extern "C" void aero_flush_eeprom();
namespace {
// The Java UI thread publishes a complete touch sample. Only the SDL thread
// merges it with keyboard/gamepad input; game threads retain their usual snapshot.
std::atomic<uint32_t> touch{0};
std::mutex lifecycle_mutex;
std::condition_variable foreground;
bool background = false;
}

extern "C" JNIEXPORT void JNICALL
Java_io_github_alondero_aerogaugerecomp_GameActivity_nativeBackground(JNIEnv*, jclass, jboolean value) {
    { std::lock_guard lock(lifecycle_mutex); background = value; }
    if (value) touch.store(0);
    foreground.notify_all();
}

extern "C" JNIEXPORT void JNICALL
Java_io_github_alondero_aerogaugerecomp_GameActivity_nativeRequestQuit(JNIEnv*, jclass) {
    { std::lock_guard lock(lifecycle_mutex); background = false; }
    foreground.notify_all();
    SDL_Event event{}; event.type = SDL_QUIT; SDL_PushEvent(&event);
}

extern "C" JNIEXPORT void JNICALL
Java_io_github_alondero_aerogaugerecomp_GameActivity_nativeTouch(JNIEnv*, jclass, jint buttons, jint x, jint y) {
    touch.store(aero_input_pack(uint16_t(buttons), std::clamp(int(x), -N64_STICK_MAX, N64_STICK_MAX), std::clamp(int(y), -N64_STICK_MAX, N64_STICK_MAX)));
}

extern "C" JNIEXPORT jboolean JNICALL
Java_io_github_alondero_aerogaugerecomp_GameActivity_nativeMenuOpen(JNIEnv*, jclass) {
    return aero::menu::captures_input();
}

namespace aero::android {
[[noreturn]] void startup_error(const char* message) {
    if (FILE* file = std::fopen("startup-error.txt", "w")) {
        std::fputs(message, file);
        std::fclose(file);
    }
    std::fprintf(stderr, "[android] %s\n", message);
    std::_Exit(1); // Confined to the game process; the launcher displays the error.
}

void wait_foreground() {
    std::unique_lock lock(lifecycle_mutex);
    if (!background) return;
    lock.unlock();
    aero_flush_eeprom();
    aero::config::flush_config_writes();
    lock.lock();
    foreground.wait(lock, [] { return !background; });
}

bool initialize() {
    const char* storage = SDL_AndroidGetInternalStoragePath();
    if (!storage) return false;
    setenv("AERO_ANDROID_DATA_DIR", storage, 1);
    const auto config = (std::filesystem::path(storage) / "AeroGaugeRecomp").string();
    setenv("APP_FOLDER_PATH", config.c_str(), 1);
    std::error_code error;
    std::filesystem::current_path(storage, error);
    if (error) return false;
    // Keep native diagnostics available for the launcher's Share diagnostics action.
    FILE* output = std::freopen("native.log", "w", stdout);
    if (output) { dup2(fileno(stdout), fileno(stderr)); setvbuf(stdout, nullptr, _IONBF, 0); }
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
    return true;
}

void handle_event(const SDL_Event& event) {
    if (event.type == SDL_APP_WILLENTERBACKGROUND || event.type == SDL_APP_TERMINATING) {
        touch.store(0);
        aero_flush_eeprom();
        aero::config::flush_config_writes();
    }
}

void sample_touch(uint16_t& buttons, int& x, int& y) {
    const auto sample = touch.load();
    buttons |= uint16_t(sample);
    const int tx = int8_t(sample >> 16), ty = int8_t(sample >> 24);
    if (x == 0) x = tx;
    if (y == 0) y = ty;
}
}
