"""JNI action sheets and SDL surface pauses retain independent ownership."""
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from tests.patched_source import ROOT, method


@unittest.skipUnless(shutil.which("g++"), "g++ needed for lifecycle fixture")
class ActivityLifecycleTests(unittest.TestCase):
    def test_pause_flushes_before_blocking_sdl_and_resume_respects_both_owners(self):
        source = (ROOT / "src/android/aero_android.cpp").read_text(encoding="utf-8")
        functions = method(source, "void publish_background(")
        functions += "\n" + method(source, "void flush_stores(").replace("void flush_stores(", "void aero::android::flush_stores(")
        functions += "\n" + method(source, "void notify_event(").replace("flush_stores();", "aero::android::flush_stores();")
        background = "Java_io_github_alondero_aerogaugerecomp_GameActivity_nativeBackground"
        quit_game = "Java_io_github_alondero_aerogaugerecomp_GameActivity_nativeRequestQuit"
        functions += "\nvoid " + method(source, background + "(")
        functions += "\nvoid " + method(source, quit_game + "(")
        fixture = r'''
#include <atomic>
#include <cassert>
#include <mutex>
using JNIEnv = void;
using jclass = void*;
using jboolean = bool;
static bool audio_paused, clock_paused, gyro_paused, quit_posted;
static int eeprom_flushes = 0, config_flushes = 0;
namespace aero::android { void flush_stores(); }
namespace aero::config { void flush_config_writes() { ++config_flushes; } }
namespace aero::audio { void set_suspended(bool value) { audio_paused = value; } }
namespace aero::gyro { void suspend(bool value) { gyro_paused = value; } }
namespace ultramodern { void set_app_suspended(bool value) { clock_paused = value; } }
constexpr int SDL_APP_WILLENTERBACKGROUND = 1, SDL_APP_DIDENTERFOREGROUND = 2;
constexpr int SDL_APP_TERMINATING = 3, SDL_QUIT = 4;
struct SDL_Event { int type; };
void SDL_PushEvent(SDL_Event* event) { quit_posted = event->type == SDL_QUIT; }
std::atomic<unsigned> touch{0};
std::mutex lifecycle_mutex;
bool activity_background = false, surface_background = false;
void aero_flush_eeprom() {
    // A pause publisher must release its own lock before waiting on storage.
    assert(lifecycle_mutex.try_lock());
    lifecycle_mutex.unlock();
    ++eeprom_flushes;
}
// PRODUCTION
void check(bool paused) {
    assert(audio_paused == paused && clock_paused == paused && gyro_paused == paused);
}
int main() {
    touch = 42;
    // JNI action sheet pauses without requiring any surface change.
    BACKGROUND(nullptr, nullptr, true);
    check(true);
    assert(touch == 0 && eeprom_flushes == 1 && config_flushes == 1);
    // The SDL pump can now block until resume; there is no deferred flush.
    notify_event({SDL_APP_DIDENTERFOREGROUND});
    check(true);
    assert(eeprom_flushes == 1 && config_flushes == 1);
    BACKGROUND(nullptr, nullptr, false);
    check(false);
    notify_event({SDL_APP_WILLENTERBACKGROUND});
    check(true);
    assert(eeprom_flushes == 2 && config_flushes == 2);
    BACKGROUND(nullptr, nullptr, false);
    check(true);
    notify_event({SDL_APP_DIDENTERFOREGROUND});
    check(false);
    BACKGROUND(nullptr, nullptr, true);
    notify_event({SDL_APP_WILLENTERBACKGROUND});
    QUIT(nullptr, nullptr);
    check(false);
    assert(quit_posted);
}
'''
        fixture = fixture.replace("// PRODUCTION", functions).replace("BACKGROUND(", background + "(")
        fixture = fixture.replace("QUIT(", quit_game + "(")
        with tempfile.TemporaryDirectory(prefix="aero-activity-") as temporary:
            cpp = Path(temporary) / "fixture.cpp"
            cpp.write_text(fixture, encoding="utf-8")
            exe = cpp.with_suffix(".exe")
            result = subprocess.run(["g++", "-std=c++17", str(cpp), "-o", str(exe)],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            subprocess.run([str(exe)], check=True, timeout=10)


if __name__ == "__main__":
    unittest.main()
