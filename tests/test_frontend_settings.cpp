#include "aero_config.h"
#include "ui/aero_frontend_settings.h"
#include "recompui/config.h"
#include "librecomp/game.hpp"
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

SDL_Window* window = nullptr;
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
    bool had_previous_graphics_config = false;
    bool had_previous_enhancements_config = false;

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
        set_environment("AERO_GRAPHICS_CONFIG", nullptr);
        set_environment("AERO_ENHANCEMENTS_CONFIG", nullptr);
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
        (void)argc;
        (void)argv;
        IsolatedConfig isolated;
        const auto path = aero::config::app_config_dir();
        std::filesystem::create_directories(path);
        recomp::register_config_path(path);
        const nlohmann::json initial = {
            {"ds_option", 3}, {"msaa_option", "MSAA8X"},
            {"window_width", 1920}, {"window_height", 1080},
            {"texture_pack", "seed-pack"}, {"texture_dump", "seed-dump"},
            {"future_option", "preserve me"}, {"full_track", true}};
        { std::ofstream file(path / "graphics.json"); file << initial; }
        { std::ofstream file(path / "enhancements.json"); file << R"({"easy_turbo_boost":false})"; }
        aero::config::load_and_apply_graphics();
        // Match live_config_updates: preserve unrelated edits made while the
        // game is running, after its existing load-time defaults are written.
        auto hand_edit = read(path / "graphics.json");
        hand_edit["future_option"] = "preserve me";
        { std::ofstream file(path / "graphics.json"); file << hand_edit; }
        aero::menu::create_settings();
        recompui::config::finalize();
        auto& graphics = recompui::config::get_graphics_config();
        using namespace ultramodern::renderer;
        require(std::get<uint32_t>(graphics.get_option_value("ds_option")) == 3, "supersampling import");
        require(std::get<uint32_t>(graphics.get_option_value("msaa_option")) == uint32_t(Antialiasing::MSAA8X), "MSAA import");
        for (const char* key : {"api_option", "hpfb_option", "texture_pack",
                               "texture_dump", "window_width", "window_height", "window_size"}) {
            require(graphics.has_option(key), "missing graphics option");
        }
        // developer_mode moved to the Debug tab; the per-axis window size
        // options are JSON round-trip storage behind the preset picker.
        require(graphics.is_config_option_hidden(graphics.get_config_schema().options_by_id.at("developer_mode")),
                "developer mode still exposed on graphics");
        require(graphics.is_config_option_hidden(graphics.get_config_schema().options_by_id.at("window_width")) &&
                graphics.is_config_option_hidden(graphics.get_config_schema().options_by_id.at("window_height")),
                "window size sliders not hidden");
        auto& debug = recompui::config::get_config("debug");
        require(debug.has_option("developer_mode"), "missing debug option");
        require(!debug.is_config_option_hidden(debug.get_config_schema().options_by_id.at("developer_mode")),
                "debug developer mode hidden");
        require(std::get<uint32_t>(graphics.get_option_value("window_size")) == 3, // 1920x1080
                "window size preset not derived from graphics.json");
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
        graphics.set_option_value("window_width", 2560.0);
        graphics.set_option_value("window_height", 1440.0);
        // The preset picker applies through the page's Apply; an unapplied pick
        // must not reach the config (the per-axis page values are hidden storage).
        graphics.set_option_value("window_size", uint32_t(4)); // 2560x1440
        require(aero::config::window_size().width != 2560, "unapplied window preset leaked");
        graphics.revert_temp_config();
        graphics.set_option_value("window_size", uint32_t(4));
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
        aero::menu::refresh_settings();
        require(std::get<bool>(graphics.get_temp_option_value("developer_mode")), "graphics mirror seed");
        std::cout << "Frontend settings integration passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
