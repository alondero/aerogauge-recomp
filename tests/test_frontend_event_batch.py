"""Execute the frontend batch dispatcher without pumping SDL under the UI lock."""
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from tests.patched_source import ROOT, method, patched_file


@unittest.skipUnless(shutil.which("g++"), "g++ needed for event dispatch fixture")
@unittest.skipUnless((ROOT / "lib/RecompFrontend/.git").exists(), "frontend not initialized")
class EventBatchTests(unittest.TestCase):
    def test_batch_dispatch_preserves_order_and_finishes_binding_and_startup(self):
        with tempfile.TemporaryDirectory(prefix="aero-events-") as temporary:
            tree = Path(temporary)
            source = patched_file(ROOT / "lib/RecompFrontend", "recompinput/src/input_events.cpp", tree)
            dispatch = method(source, "void handle_events(std::span<const SDL_Event>")
            # Polling here would reintroduce the draw-lock stall, even if the host
            # still collects a batch beforehand.
            self.assertNotIn("SDL_PollEvent", dispatch)
            fixture = r'''
#include <span>
#include <vector>
#include <cassert>
struct SDL_Event { int type; };
constexpr int SDL_ENABLE = 1, SDL_DISABLE = 0;
constexpr bool SDL_TRUE = true, SDL_FALSE = false;
static int starts = 0, bindings = 0, cursors = 0;
static bool started = false, locked = false;
static std::vector<int> seen;
void SDL_ShowCursor(int) { assert(locked); ++cursors; }
void SDL_SetRelativeMouseMode(bool) { assert(locked); }
namespace ultramodern { bool is_game_started() { return started; } }
namespace recompui {
namespace config::general {
bool has_mouse_sensitivity_option() { return true; }
int get_mouse_sensitivity() { return 0; }
}
bool get_cursor_visible() { return true; }
void process_game_started() { assert(locked); ++starts; }
}
namespace recompinput {
bool game_input_disabled() { return false; }
namespace binding { void stop_skipping_events() { assert(locked); ++bindings; } }
bool sdl_event_filter(void*, SDL_Event* event) {
    assert(locked); seen.push_back(event->type); return event->type == 3;
}
// DISPATCH
}
int main() {
    locked = true;
    recompinput::handle_events({});
    assert(bindings == 1 && starts == 0);
    started = true;
    const std::vector<SDL_Event> batch{{1}, {2}};
    recompinput::handle_events(batch);
    recompinput::handle_events({});
    assert(seen == std::vector<int>({1, 2}) && starts == 1 && bindings == 3);
    assert(batch[0].type == 1 && batch[1].type == 2 && cursors == 2);
    const std::vector<SDL_Event> quit{{3}, {4}};
    recompinput::handle_events(quit);
    assert(seen == std::vector<int>({1, 2, 3}) && starts == 1 && bindings == 4);
}
'''
            cpp = tree / "fixture.cpp"
            cpp.write_text(fixture.replace("// DISPATCH", dispatch), encoding="utf-8")
            exe = tree / "fixture.exe"
            result = subprocess.run(["g++", "-std=c++20", str(cpp), "-o", str(exe)],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            subprocess.run([str(exe)], check=True, timeout=10)

    def test_host_collects_before_taking_the_frontend_lock(self):
        source = (ROOT / "src/main.cpp").read_text(encoding="utf-8")
        pump = method(source, "static void update_gfx_stub(")
        collect = pump.index("while (SDL_PollEvent(&event))")
        lock = pump.index("aero::menu::run_input_update(")
        dispatch = pump.index("recompinput::handle_events(events)")
        self.assertLess(collect, lock)
        self.assertLess(lock, dispatch)


if __name__ == "__main__":
    unittest.main()
