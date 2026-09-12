#undef NDEBUG
#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <fstream>
#include <thread>
#include "librecomp/game.hpp"
#include "ultramodern/ultramodern.hpp"

std::filesystem::path config_path;
std::atomic_bool exited{false};
namespace recomp {
bool sram_allowed() { std::abort(); }
SaveType get_save_type() { return SaveType::Eep4k; }
std::u8string current_game_id() { return u8"aerogauge.us"; }
}
namespace ultramodern::error_handling {
void message_box(const char*) { std::abort(); }
[[noreturn]] void quick_exit(const char*, int, const char*, int) { std::abort(); }
}
namespace ultramodern {
void enqueue_external_message_src(PTR(OSMesgQueue), OSMesg, bool, EventMessageSource) { std::abort(); }
}
extern "C" void aero_flush_eeprom();
void save_write_ptr(const void*, uint32_t, uint32_t);

int main() {
    // Closing a window before saving starts must not wait for a nonexistent worker.
    aero_flush_eeprom();
    config_path = std::filesystem::temp_directory_path() /
        ("aero-eeprom-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    ultramodern::init_saving(nullptr);
    std::array<uint8_t, 512> expected{}, actual{};
    for (unsigned run = 0; run < 16; ++run) {
        for (unsigned i = 0; i < expected.size(); ++i) expected[i] = i * 11 + run;
        // Match the game's burst of 8-byte EEPROM writes, then immediately exit/flush.
        for (unsigned i = 0; i < expected.size(); i += 8) save_write_ptr(expected.data() + i, i, 8);
        // VI watchdog and window close may race; both callers must complete.
        std::thread other(aero_flush_eeprom);
        aero_flush_eeprom();
        other.join();
        std::ifstream saved(ultramodern::get_save_file_path(), std::ios::binary);
        saved.read(reinterpret_cast<char*>(actual.data()), actual.size());
        assert(saved && expected == actual);
    }
    std::thread closing(aero_flush_eeprom);
    exited.store(true);
    closing.join();
    ultramodern::join_saving_thread();
    aero_flush_eeprom(); // the former post-teardown deadlock
    std::filesystem::remove_all(config_path);
}
