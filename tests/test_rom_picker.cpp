// Exercise the real startup adapter with failed native dialogs, without a
// desktop, ROM or runtime threads. CTest also requires its stderr diagnostic.
#undef NDEBUG
#include "aero_rom_desktop.h"
#include "librecomp/game.hpp"
#include <SDL.h>
#include <nfd.h>
#include <cassert>
#include <chrono>
#include <string_view>

namespace {
std::filesystem::path config_path;
bool fail_init;
int messages = 0;
int opens = 0;
int quits = 0;
}

int SDLCALL SDL_ShowSimpleMessageBox(Uint32, const char*, const char*, SDL_Window*) {
    ++messages;
    return 0;
}
nfdresult_t NFD_Init() { return fail_init ? NFD_ERROR : NFD_OKAY; }
void NFD_Quit() { ++quits; }
nfdresult_t NFD_OpenDialogU8(nfdu8char_t**, const nfdu8filteritem_t*, nfdfiltersize_t, const nfdu8char_t*) {
    ++opens;
    return NFD_ERROR;
}
void NFD_FreePathU8(nfdu8char_t*) { assert(false && "failed dialog has no allocated path"); }

// Only filesystem discovery runs in these scenarios. Calling validation here
// means the isolated no-ROM fixture no longer represents a picker failure.
std::filesystem::path recomp::get_config_path() { return config_path; }
std::u8string recomp::GameEntry::stored_filename() const { return game_id + u8".z64"; }
recomp::RomValidationError recomp::select_rom(const std::filesystem::path&, const std::u8string&) {
    assert(false && "no ROM should be present");
    return recomp::RomValidationError::OtherError;
}
bool recomp::load_stored_rom(const std::u8string&) {
    assert(false && "no cache should be present");
    return false;
}

int main(int argc, char** argv) {
    assert(argc == 2);
    fail_init = std::string_view(argv[1]) == "init";
    assert(fail_init || std::string_view(argv[1]) == "open");
    config_path = std::filesystem::temp_directory_path() /
        ("aero-picker-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(config_path);
    const auto previous = std::filesystem::current_path();
    std::filesystem::current_path(config_path);
    recomp::GameEntry usa{};
    usa.game_id = u8"aerogauge.us";
    const auto result = aero::rom::select_startup_rom({}, usa, nullptr, true);
    assert(!result.region && !result.cancelled);
    assert(result.error == "Native ROM picker failed; pass a ROM path as the first argument.");
    assert(messages == 2); // setup guidance, then one error (not a duplicate)
    assert(opens == (fail_init ? 0 : 1));
    assert(quits == (fail_init ? 0 : 1));
    std::filesystem::current_path(previous);
    std::filesystem::remove(config_path);
}
