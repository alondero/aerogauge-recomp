#undef NDEBUG
// Real SDL2 regression for user-supplied gamecontrollerdb.txt loading:
// verifies that an already-connected, previously unmapped virtual joystick
// starts unrecognized, ignores untagged lines without a matching platform:<OS>,
// field, loads exe_dir mappings, lets app_data_dir override exe_dir for
// equivalent keys, queues exactly one startup ADDED event per device, and
// releases the controller handle on a single close.
#include <SDL.h>

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include "aero_controller_db.h"

namespace {

const char* current_sdl_platform() {
#if defined(_WIN32)
    return "Windows";
#elif defined(__APPLE__)
    return "Mac OS X";
#elif defined(__ANDROID__)
    return "Android";
#else
    return "Linux";
#endif
}

void write_file(const std::filesystem::path& path, const std::string& content) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    assert(out && "failed to write test gamecontrollerdb.txt");
    out << content;
}

void assert_button_bindings(int device_index, int expected_a, int expected_b) {
    SDL_GameController* gc = SDL_GameControllerOpen(device_index);
    assert(gc != nullptr);
    const SDL_GameControllerButtonBind bind_a =
        SDL_GameControllerGetBindForButton(gc, SDL_CONTROLLER_BUTTON_A);
    const SDL_GameControllerButtonBind bind_b =
        SDL_GameControllerGetBindForButton(gc, SDL_CONTROLLER_BUTTON_B);
    assert(bind_a.bindType == SDL_CONTROLLER_BINDTYPE_BUTTON && bind_a.value.button == expected_a);
    assert(bind_b.bindType == SDL_CONTROLLER_BINDTYPE_BUTTON && bind_b.value.button == expected_b);
    SDL_GameControllerClose(gc);
}

} // namespace

int main() {
    SDL_SetHint(SDL_HINT_VIDEODRIVER, "dummy");
    assert(SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) == 0);

    // Use SDL_JOYSTICK_TYPE_UNKNOWN with a synthetic VID/PID so SDL 2.26.3+ does
    // not auto-assign a default controller mapping before user files are loaded.
    SDL_VirtualJoystickDesc desc{};
    desc.version = SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
    desc.type = SDL_JOYSTICK_TYPE_UNKNOWN;
    desc.naxes = 4;
    desc.nbuttons = 12;
    desc.nhats = 1;
    desc.vendor_id = 0x1234;
    desc.product_id = 0x5678;
    desc.name = "Virtual Unmapped Test Pad";
    const int virt_index = SDL_JoystickAttachVirtualEx(&desc);
    assert(virt_index >= 0);

    assert(SDL_IsGameController(virt_index) == SDL_FALSE);

    char guid_str[64]{};
    SDL_JoystickGetGUIDString(SDL_JoystickGetDeviceGUID(virt_index), guid_str, sizeof(guid_str));

    const auto tmp = std::filesystem::temp_directory_path() /
                     ("aero-controller-db-sdl-" + std::to_string(
                          std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto exe_dir = tmp / "exe";
    const auto data_dir = tmp / "data";
    std::filesystem::create_directories(exe_dir);
    std::filesystem::create_directories(data_dir);

    const auto exe_db = exe_dir / aero::controller_db::kDatabaseFilename;
    const auto data_db = data_dir / aero::controller_db::kDatabaseFilename;

    // 1. A mapping line missing the comma-terminated platform:<OS>, field is
    //    ignored by SDL_GameControllerAddMappingsFromFile.
    write_file(exe_db, std::string(guid_str) + ",Virtual Test Pad,a:b5,b:b4,\n");
    aero::controller_db::load_user_mappings_from({}, exe_dir);
    assert(SDL_IsGameController(virt_index) == SDL_FALSE);

    // 2. A properly platform-tagged mapping in exe_dir makes the already-attached
    //    virtual joystick recognizable with exe_dir's bindings.
    const std::string plat = std::string("platform:") + current_sdl_platform() + ",";
    write_file(exe_db, std::string(guid_str) + ",Virtual Test Pad,a:b5,b:b4," + plat + "\n");
    aero::controller_db::load_user_mappings_from({}, exe_dir);
    assert(SDL_IsGameController(virt_index) == SDL_TRUE);
    assert_button_bindings(virt_index, 5, 4);

    // 3. Adding an equivalent entry in app_data_dir overrides exe_dir.
    write_file(data_db, std::string(guid_str) + ",Virtual Test Pad,a:b4,b:b5," + plat + "\n");
    aero::controller_db::load_user_mappings_from(data_dir, exe_dir);
    assert_button_bindings(virt_index, 4, 5);

    // 4. Seed a pre-existing ADDED event (as SDL2-compat/SDL3 or a hotplug
    //    during startup can emit), then verify startup registration queues
    //    exactly one ADDED event for the device so one open + one close
    //    releases the handle.
    SDL_Event pre_added{};
    pre_added.type = SDL_CONTROLLERDEVICEADDED;
    pre_added.cdevice.which = virt_index;
    assert(SDL_PushEvent(&pre_added) == 1);

    aero::controller_db::queue_startup_controllers();
    int added_events = 0;
    SDL_Event event{};
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_CONTROLLERDEVICEADDED && event.cdevice.which == virt_index) {
            ++added_events;
        }
    }
    assert(added_events == 1);

    SDL_GameController* gc = SDL_GameControllerOpen(virt_index);
    assert(gc != nullptr);
    const SDL_JoystickID id = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(gc));
    SDL_GameControllerClose(gc);
    assert(SDL_GameControllerFromInstanceID(id) == nullptr);

    SDL_JoystickDetachVirtual(virt_index);
    SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);

    std::error_code ec;
    std::filesystem::remove_all(tmp, ec);
    return 0;
}
