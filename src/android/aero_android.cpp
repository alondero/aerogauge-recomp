#include "aero_android.h"
#include "aero_input.h"
#include "aero_config.h"
#include "aero_menu.h"
#include "aero_gyro_sdl.h"
#include "aero_audio.h"
#include <ultramodern/ultramodern.hpp>
#include <jni.h>
#include <SDL_system.h>
#include <atomic>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <mutex>
#include <unistd.h>
#include "aero_log.h"

extern "C" void aero_flush_eeprom();
namespace {
// The Java UI thread publishes a complete touch sample. Only the SDL thread
// merges it with keyboard/gamepad input; game threads retain their usual snapshot.
std::atomic<uint32_t> touch{0};
std::mutex lifecycle_mutex;
bool activity_background = false;
bool surface_background = false;

// JNI and SDL can report distinct pause reasons. A focus/surface notification
// must not resume the game underneath the Android action sheet.
void publish_background() {
    const bool suspended = activity_background || surface_background;
    aero::gyro::suspend(suspended);
    aero::audio::set_suspended(suspended);
    ultramodern::set_app_suspended(suspended);
    if (suspended) {
        touch.store(0);
    }
}
}

extern "C" JNIEXPORT void JNICALL
Java_io_github_alondero_aerogaugerecomp_GameActivity_nativeBackground(JNIEnv*, jclass, jboolean value) {
    {
        std::lock_guard lock(lifecycle_mutex);
        activity_background = value;
        publish_background();
    }
    // JNI runs this before SDLActivity enters nativePause. Do not defer to a
    // main-thread pump: SDL_PollEvent can already be waiting for resume there.
    if (value) aero::android::flush_stores();
}

extern "C" JNIEXPORT void JNICALL
Java_io_github_alondero_aerogaugerecomp_GameActivity_nativeRequestQuit(JNIEnv*, jclass) {
    {
        std::lock_guard lock(lifecycle_mutex);
        activity_background = surface_background = false;
        publish_background();
    }
    SDL_Event event{}; event.type = SDL_QUIT; SDL_PushEvent(&event);
}

extern "C" JNIEXPORT void JNICALL
Java_io_github_alondero_aerogaugerecomp_GameActivity_nativeTouch(JNIEnv*, jclass, jint buttons, jint x, jint y) {
    touch.store(aero_input_pack(uint16_t(buttons), aero_touch_axis_to_n64(x), aero_touch_axis_to_n64(y)));
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
    AERO_LOG_INFO("android", "%s\n", message);
    std::_Exit(1); // Confined to the game process; the launcher displays the error.
}

void flush_stores() {
    // Both store barriers are thread-safe and use their existing writer threads.
    // Never wait for them with the lifecycle or frontend mutex held.
    aero_flush_eeprom();
    aero::config::flush_config_writes();
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

void notify_event(const SDL_Event& event) {
    if (event.type != SDL_APP_WILLENTERBACKGROUND && event.type != SDL_APP_TERMINATING &&
        event.type != SDL_APP_DIDENTERFOREGROUND) return;
    {
        std::lock_guard lock(lifecycle_mutex);
        surface_background = event.type != SDL_APP_DIDENTERFOREGROUND;
        publish_background();
    }
    // SDL invokes its event filter before parking the Android event pump.
    if (event.type != SDL_APP_DIDENTERFOREGROUND) flush_stores();
}

void controller_back() {
    // Back closes the settings page first, then opens the Android action sheet.
    // This gives controller-only players a reliable route back to the launcher.
    if (aero::menu::captures_input()) {
        aero::menu::toggle();
        return;
    }
    JNIEnv* environment = static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
    if (!environment) return;
    jobject activity = static_cast<jobject>(SDL_AndroidGetActivity());
    if (!activity) return;
    jclass type = environment->GetObjectClass(activity);
    jmethodID show_actions = type ? environment->GetMethodID(type, "showActions", "()V") : nullptr;
    if (show_actions) environment->CallVoidMethod(activity, show_actions);
    if (environment->ExceptionCheck()) environment->ExceptionClear();
    if (type) environment->DeleteLocalRef(type);
    environment->DeleteLocalRef(activity);
}

void sample_touch(uint16_t& buttons, int& x, int& y) {
    const auto sample = touch.load();
    buttons |= uint16_t(sample);
    const int tx = int8_t(sample >> 16), ty = int8_t(sample >> 24);
    if (x == 0) x = tx;
    if (y == 0) y = ty;
}
}
