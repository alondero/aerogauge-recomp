#include "aero_config.h"
#include "aero_region.h"
#include "recompui/recompui.h"
#include "ui/aero_frontend_settings.h"
#include "recompui/config.h"
#include "recompinput/profiles.h"
#include "librecomp/game.hpp"
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <vector>

SDL_Window* window = nullptr;
int aero_japan = 0;
std::vector<recomp::GameEntry> supported_games;
namespace {
std::vector<std::function<void()>> pending;
int window_updates = 0;
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
void flush() {
    auto actions = std::move(pending);
    pending.clear();
    for (auto& action : actions) action();
}

void set_environment(const char* name, const char* value) {
#ifdef _WIN32
    _putenv_s(name, value != nullptr ? value : "");
#else
    if (value != nullptr) setenv(name, value, 1);
    else unsetenv(name);
#endif
}

struct IsolatedConfig {
    std::filesystem::path root;
    std::string previous_root;
    bool had_previous_root = false;
    std::string previous_graphics_config;
    std::string previous_enhancements_config;
    std::string previous_debounce;
    std::string previous_force_full_lod;
    bool had_previous_graphics_config = false;
    bool had_previous_enhancements_config = false;
    bool had_previous_debounce = false;
    bool had_previous_force_full_lod = false;

    IsolatedConfig() {
        const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
        root = std::filesystem::temp_directory_path() /
               ("aero-frontend-settings-test-" + std::to_string(unique));
#ifdef _WIN32
        if (const char* previous = std::getenv("LOCALAPPDATA")) {
            previous_root = previous;
            had_previous_root = true;
        }
        set_environment("LOCALAPPDATA", root.string().c_str());
#else
        if (const char* previous = std::getenv("XDG_CONFIG_HOME")) {
            previous_root = previous;
            had_previous_root = true;
        }
        set_environment("XDG_CONFIG_HOME", root.string().c_str());
#endif
        if (const char* previous = std::getenv("AERO_GRAPHICS_CONFIG")) {
            previous_graphics_config = previous;
            had_previous_graphics_config = true;
        }
        if (const char* previous = std::getenv("AERO_ENHANCEMENTS_CONFIG")) {
            previous_enhancements_config = previous;
            had_previous_enhancements_config = true;
        }
        if (const char* previous = std::getenv("AERO_CONFIG_WRITE_DEBOUNCE_MS")) {
            previous_debounce = previous;
            had_previous_debounce = true;
        }
        if (const char* previous = std::getenv("AERO_FORCE_FULL_LOD")) {
            previous_force_full_lod = previous;
            had_previous_force_full_lod = true;
        }
        set_environment("AERO_GRAPHICS_CONFIG", nullptr);
        set_environment("AERO_ENHANCEMENTS_CONFIG", nullptr);
        set_environment("AERO_FORCE_FULL_LOD", nullptr);
        // Park the background writer: the persistence checks below flush
        // explicitly, so a file read can never race the debounce window.
        set_environment("AERO_CONFIG_WRITE_DEBOUNCE_MS", "60000");
    }

    ~IsolatedConfig() {
        std::error_code error;
        std::filesystem::remove_all(root, error);
#ifdef _WIN32
        set_environment("LOCALAPPDATA", had_previous_root ? previous_root.c_str() : nullptr);
#else
        set_environment("XDG_CONFIG_HOME", had_previous_root ? previous_root.c_str() : nullptr);
#endif
        set_environment("AERO_GRAPHICS_CONFIG",
                        had_previous_graphics_config ? previous_graphics_config.c_str() : nullptr);
        set_environment("AERO_ENHANCEMENTS_CONFIG",
                        had_previous_enhancements_config ? previous_enhancements_config.c_str() : nullptr);
        set_environment("AERO_CONFIG_WRITE_DEBOUNCE_MS",
                        had_previous_debounce ? previous_debounce.c_str() : nullptr);
        set_environment("AERO_FORCE_FULL_LOD",
                        had_previous_force_full_lod ? previous_force_full_lod.c_str() : nullptr);
    }
};

nlohmann::json read(const std::filesystem::path& path) {
    nlohmann::json value;
    std::ifstream(path) >> value;
    return value;
}
}
namespace aero::menu {
void enqueue(std::function<void()> action) { pending.push_back(std::move(action)); }
void apply_window_settings() { ++window_updates; }
}

int main(int argc, char** argv) {
    try {
        aero_japan = argc > 1 && std::string_view(argv[1]) == "jp";
        IsolatedConfig isolated;
        const auto path = aero::config::app_config_dir();
        std::filesystem::create_directories(path);
        recomp::register_config_path(path);
        // draw_distance_scale: 0 seeds the first-open scenario: Unlimited is
        // engaged from disk, so the multiplier must arrive disabled.
        const nlohmann::json initial = {
            {"ds_option", 3}, {"msaa_option", "MSAA8X"},
            {"window_width", 1920}, {"window_height", 1080},
            {"texture_pack", "seed-pack"}, {"texture_dump", "seed-dump"},
            {"future_option", "preserve me"}, {"full_track", true},
            {"draw_distance_scale", 0}};
        { std::ofstream file(path / "graphics.json"); file << initial; }
        { std::ofstream file(path / "enhancements.json"); file << R"({"easy_turbo_boost":false})"; }
        aero::config::load_and_apply_graphics();
        // Match live_config_updates: preserve unrelated edits made while the
        // game is running, after its existing load-time defaults are written.
        auto hand_edit = read(path / "graphics.json");
        hand_edit["future_option"] = "preserve me";
        { std::ofstream file(path / "graphics.json"); file << hand_edit; }
        aero::menu::create_settings();
        require(recompui::get_game_mod_id() == (aero_japan ? "aerogauge.jp.rev_a" : "aerogauge"),
                "initial Mods settings must use the selected ROM region");
        bool controls_tab_registered = true;
        try {
            // This operates on the pending tab list before the modal is created.
            recompui::config::set_tab_visible("controls", true);
        } catch (const std::exception&) {
            controls_tab_registered = false;
        }
        require(controls_tab_registered, "controls tab not registered");
        recompui::config::finalize();
        // Drain the seed echoes: seeding a live value that differs from the
        // schema default enqueues a no-op live-set (the app drains this queue
        // every frame).
        flush();
        auto& graphics = recompui::config::get_graphics_config();
        require(!aero::config::force_full_lod(), "full LOD must default off");
        graphics.set_option_value("force_full_lod", true);
        flush();
        require(!aero::config::force_full_lod(), "LOD applied before Apply");
        graphics.revert_temp_config();
        require(!std::get<bool>(graphics.get_temp_option_value("force_full_lod")), "LOD discard failed");
        graphics.set_option_value("force_full_lod", true);
        graphics.save_config();
        require(!aero::config::force_full_lod(), "LOD save escaped main-thread queue");
        flush();
        require(aero::config::force_full_lod(), "LOD Apply failed");
        aero::config::flush_config_writes();
        require(read(path / "graphics.json").at("force_full_lod") == true, "LOD persistence");
        set_environment("AERO_FORCE_FULL_LOD", "0");
        require(!aero::config::force_full_lod(), "LOD environment override");
        set_environment("AERO_FORCE_FULL_LOD", nullptr);
        const uint64_t writes_before_noop_apply = aero::config::config_write_count();
        graphics.save_config();
        flush();
        aero::config::flush_config_writes();
        require(aero::config::config_write_count() == writes_before_noop_apply,
                "unchanged Graphics Apply does not dirty graphics.json");
        using namespace ultramodern::renderer;
        const int keyboard_profile = recompinput::profiles::get_sp_keyboard_profile_index();
        const int controller_profile = recompinput::profiles::get_sp_controller_profile_index();
        require(recompinput::profiles::get_input_binding(
                    keyboard_profile, recompinput::GameInput::A, 0) ==
                    recompinput::InputField::keyboard(SDL_SCANCODE_X),
                "keyboard defaults not configured");
        require(recompinput::profiles::get_input_binding(
                    controller_profile, recompinput::GameInput::A, 0) ==
                    recompinput::InputField::controller_digital(SDL_CONTROLLER_BUTTON_A),
                "controller defaults not configured");
        const auto keyboard_a = recompinput::profiles::get_input_binding(
            keyboard_profile, recompinput::GameInput::A, 0);
        recompinput::profiles::set_input_binding(
            keyboard_profile, recompinput::GameInput::A, 0, recompinput::InputField::keyboard(SDL_SCANCODE_Z));
        require(recompinput::profiles::get_input_binding(
                    keyboard_profile, recompinput::GameInput::A, 0) ==
                    recompinput::InputField::keyboard(SDL_SCANCODE_Z),
                "synchronized binding update not visible");
        recompinput::profiles::set_input_binding(
            keyboard_profile, recompinput::GameInput::A, 0, keyboard_a);
        require(recompinput::get_game_input_description(recompinput::GameInput::A) ==
                    "Accelerates during a race and confirms menu choices.",
                "control descriptions not configured");
        require(recompinput::profiles::save_controls_config(path / "controls.json"),
                "controls persistence failed");
        require(std::filesystem::exists(path / "controls.json"),
                "controls file not created");
        require(recompinput::players::is_single_player_mode(),
                "single-player input mode not configured");
        require(std::get<uint32_t>(graphics.get_option_value("ds_option")) == 3, "supersampling import");
        require(std::get<uint32_t>(graphics.get_option_value("msaa_option")) == uint32_t(Antialiasing::MSAA8X), "MSAA import");
        for (const char* key : {"api_option", "hpfb_option", "texture_pack",
                               "texture_dump", "window_size", "force_full_lod"}) {
            require(graphics.has_option(key), "missing graphics option");
        }
        // developer_mode moved to the Debug tab, which solely owns it.
        require(graphics.is_config_option_hidden(graphics.get_config_schema().options_by_id.at("developer_mode")),
                "developer mode still exposed on graphics");
        auto& debug = recompui::config::get_config("debug");
        require(debug.has_option("developer_mode"), "missing debug option");
        require(!debug.is_config_option_hidden(debug.get_config_schema().options_by_id.at("developer_mode")),
                "debug developer mode hidden");
        require(std::get<uint32_t>(graphics.get_option_value("window_size")) == 3, // 1920x1080
                "window size preset not derived from graphics.json");
        // First-open regression: the disable dependency is registered at schema
        // time and derived by finalize(), so with Unlimited engaged from disk the
        // multiplier must already be disabled before any user interaction.
        auto& enhancements_at_open = recompui::config::get_config("enhancements");
        require(enhancements_at_open.is_config_option_disabled(
                    enhancements_at_open.get_config_schema().options_by_id.at("draw_distance")),
                "multiplier enabled on first open with unlimited on");
        // The multiplier steps in coarse increments so the slider is usable.
        const auto& enh_schema = enhancements_at_open.get_config_schema();
        require(std::get<recomp::config::ConfigOptionNumber>(
                    enh_schema.options[enh_schema.options_by_id.at("draw_distance")].variant).step == 10.0,
                "multiplier step not coarsened");
        graphics.set_option_value("ds_option", uint32_t(4));
        require(aero::config::current_graphics().ds_option == 3 && pending.empty(), "unapplied edit escaped");
        graphics.revert_temp_config();
        require(std::get<uint32_t>(graphics.get_temp_option_value("ds_option")) == 3, "discard failed");
        graphics.set_option_value("ds_option", uint32_t(4));
        aero::config::update_saved_window_mode(WindowMode::Fullscreen);
        aero::menu::refresh_settings();
        require(std::get<uint32_t>(graphics.get_temp_option_value("ds_option")) == 4, "refresh discarded dirty edit");
        graphics.save_config();
        require(aero::config::current_graphics().ds_option == 3, "render-thread save was not deferred");
        flush();
        require(aero::config::current_graphics().ds_option == 4, "apply failed");
        require(aero::config::current_graphics().wm_option == WindowMode::Fullscreen, "apply undid F11");
        require(window_updates == 0, "unrelated apply changed window");
        // The window-size picker obeys the page's confirmation flow: a pick at
        // Temporary time must not enqueue anything. The queue is pumped here
        // (the real game drains it every frame), so this cannot pass vacuously.
        graphics.set_option_value("window_size", uint32_t(4)); // 2560x1440
        flush();
        require(pending.empty(), "unapplied window preset leaked into main-thread queue");
        require(aero::config::window_size().width != 2560, "unapplied window preset leaked");
        graphics.revert_temp_config(); // cancel: selection and live size untouched

        // A custom (non-preset) size survives Apply while Custom is selected.
        aero::config::set_window_size({1920, 1000});
        graphics.set_option_value("window_size", uint32_t(11)); // Custom
        graphics.save_config(); // Apply
        flush();
        require(aero::config::window_size().width == 1920 && aero::config::window_size().height == 1000,
                "custom resolution destroyed on apply");

        // Picking a real preset applies it with the Apply button.
        graphics.set_option_value("window_size", uint32_t(4)); // 2560x1440
        graphics.save_config(); // Apply
        flush();
        require(aero::config::window_size().width == 2560 && aero::config::window_size().height == 1440,
                "window preset apply");
        require(window_updates >= 1, "window preset did not resize window");
        const int updates_before = window_updates;
        graphics.set_option_value("texture_pack", std::string("new-pack"));
        graphics.set_option_value("texture_dump", std::string("new-dump"));
        graphics.set_option_value("api_option", uint32_t(GraphicsApi::Vulkan));
        graphics.save_config();
        flush();
        require(window_updates == updates_before, "unrelated apply re-resized window");
        require(aero::config::window_size().width == 2560, "apply lost the picked window size");
        require(aero::config::texture_pack_path() == "new-pack", "texture pack save");
        auto& enhancements = recompui::config::get_config("enhancements");
        enhancements.set_option_value("full_track", false);
        enhancements.set_option_value("draw_distance_unlimited", true);
        enhancements.set_option_value("easy_turbo", true);
        require(aero::config::full_track(), "enhancement escaped main-thread queue");
        flush();
        require(!aero::config::full_track() && aero::config::draw_distance_scale() == 0.0f, "live enhancements");
        require(aero::config::easy_turbo_boost(), "easy turbo update");
        // The multiplier slider is disabled while Unlimited is engaged.
        require(enhancements.is_config_option_disabled(
                    enhancements.get_config_schema().options_by_id.at("draw_distance")),
                "multiplier not disabled while unlimited");
        enhancements.set_option_value("draw_distance_unlimited", false);
        flush();
        require(aero::config::draw_distance_scale() == 100.0f, "unlimited off fallback");
        require(!enhancements.is_config_option_disabled(
                    enhancements.get_config_schema().options_by_id.at("draw_distance")),
                "multiplier still disabled after unlimited off");
        // The Apply/enhancement actions above only queued their writes.
        aero::config::flush_config_writes();
        const auto saved = read(path / "graphics.json");
        require(saved.at("future_option") == "preserve me", "unknown config key lost");
        require(saved.at("ds_option") == 4 && saved.at("api_option") == "Vulkan", "graphics persistence");
        require(saved.at("texture_dump") == "new-dump", "texture dump persistence");
        require(read(path / "enhancements.json").at("easy_turbo_boost") == true, "enhancement persistence");
        enhancements.revert_temp_config();
        aero::config::set_full_track(true);
        aero::config::set_draw_distance_scale(75.0f);
        aero::config::set_easy_turbo_boost(false);
        aero::menu::refresh_settings();
        require(std::get<bool>(enhancements.get_option_value("full_track")), "full-track refresh");
        require(!std::get<bool>(enhancements.get_option_value("easy_turbo")), "easy turbo refresh");
        require(std::get<double>(enhancements.get_option_value("draw_distance")) == 75.0,
                "draw distance refresh");
        require(!std::get<bool>(enhancements.get_option_value("draw_distance_unlimited")),
                "unlimited refresh");
        // The Debug toggle must drive the shared graphics snapshot.
        auto& debug_tab = recompui::config::get_config("debug");
        require(!std::get<bool>(debug_tab.get_option_value("developer_mode")), "debug seed");
        debug_tab.set_option_value("developer_mode", true);
        require(!aero::config::current_graphics().developer_mode, "debug change escaped queue");
        flush();
        require(aero::config::current_graphics().developer_mode, "debug toggle apply");
        // Drain the writer before IsolatedConfig tears the directory down.
        aero::config::flush_config_writes();
        std::cout << "Frontend settings integration passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
