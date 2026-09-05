#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

#include "aero_config.h"
#include "json/json.hpp"

namespace {

int failures = 0;

void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void set_environment(const char* name, const char* value) {
#if defined(_WIN32)
    _putenv_s(name, value != nullptr ? value : "");
#else
    if (value != nullptr) setenv(name, value, 1);
    else unsetenv(name);
#endif
}

nlohmann::json read_json(const std::filesystem::path& path) {
    std::ifstream input(path);
    nlohmann::json result;
    input >> result;
    return result;
}

} // anonymous namespace

// aero_config.cpp normally queues its graphics changes through ultramodern. This
// focused test verifies the port-owned snapshot and persistent representation.
namespace ultramodern::renderer {
void set_graphics_config(const GraphicsConfig&) {}
}

int main() {
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto directory = std::filesystem::temp_directory_path() /
                           ("aero-live-config-test-" + std::to_string(unique));
    const auto config_path = directory / "graphics.json";
    const auto enhancements_path = directory / "enhancements.json";
    set_environment("AERO_GRAPHICS_CONFIG", config_path.string().c_str());
    set_environment("AERO_ENHANCEMENTS_CONFIG", enhancements_path.string().c_str());
    set_environment("AERO_FOG_MATCH_1P", nullptr);
    set_environment("AERO_SKY_MATCH_1P", nullptr);
    set_environment("AERO_DRAW_DISTANCE_SCALE", nullptr);
    set_environment("AERO_FULL_TRACK", nullptr);
    set_environment("AERO_EASY_TURBO", nullptr);

    auto cfg = aero::config::load_and_apply_graphics();
    expect(std::filesystem::exists(config_path), "first load creates graphics.json");
    expect(std::filesystem::exists(enhancements_path), "first load creates enhancements.json");
    expect(cfg.ar_option == ultramodern::renderer::AspectRatio::Expand,
           "enhancement-oriented aspect default is preserved");

    // A menu action must not clobber an unrelated graphics.json hand edit made
    // while the game is running.
    auto hand_edited = read_json(config_path);
    hand_edited["texture_pack"] = "manual-texture-pack";
    {
        std::ofstream output(config_path);
        output << hand_edited.dump(4) << '\n';
    }

    cfg.msaa_option = ultramodern::renderer::Antialiasing::MSAA4X;
    cfg.rr_option = ultramodern::renderer::RefreshRate::Manual;
    cfg.rr_manual_value = 120;
    cfg.developer_mode = true;
    aero::config::apply_graphics(cfg);
    expect(aero::config::current_graphics().msaa_option ==
               ultramodern::renderer::Antialiasing::MSAA4X,
           "graphics menu changes update the main-thread snapshot");
    expect(read_json(config_path).at("texture_pack") == "manual-texture-pack",
           "graphics menu changes preserve unrelated hand edits");

    aero::config::set_widescreen_fog_match(false);
    aero::config::set_widescreen_sky_match(false);
    aero::config::set_draw_distance_scale(10.0f);
    aero::config::set_full_track(false);
    aero::config::set_easy_turbo_boost(true);
    aero::config::set_window_size({1920, 1080});
    aero::config::set_texture_pack_path("menu-texture-pack");
    aero::config::set_texture_dump_dir("menu-texture-dump");
    expect(!aero::config::widescreen_fog_match(), "fog menu toggle updates live");
    expect(!aero::config::widescreen_sky_match(), "sky menu toggle updates live");
    expect(aero::config::draw_distance_scale() == 10.0f,
           "draw-distance menu selection updates live");
    expect(!aero::config::full_track(), "full-track menu toggle updates live");
    expect(aero::config::easy_turbo_boost(), "turbo assist menu toggle updates live");
    expect(aero::config::window_size().width == 1920 && aero::config::window_size().height == 1080,
           "window-size menu selection updates live");
    expect(aero::config::texture_pack_path() == "menu-texture-pack" &&
               aero::config::texture_dump_dir() == "menu-texture-dump",
           "texture-path menu selections update live");

    aero::config::update_saved_window_mode(ultramodern::renderer::WindowMode::Fullscreen);
    const nlohmann::json persisted = read_json(config_path);
    const nlohmann::json persisted_enhancements = read_json(enhancements_path);
    expect(persisted.at("wm_option") == "Fullscreen", "fullscreen selection persists");
    expect(persisted.at("msaa_option") == "MSAA4X", "graphics selection persists");
    expect(persisted.at("developer_mode") == true, "developer-mode menu selection persists");
    expect(persisted.at("texture_pack") == "menu-texture-pack",
           "graphics actions preserve then texture selection updates a hand edit");
    expect(persisted.at("rr_option") == "Manual" && persisted.at("rr_manual_value") == 120,
           "manual frame rate persists");
    expect(persisted.at("widescreen_fog_match") == false, "fog menu toggle persists");
    expect(persisted.at("widescreen_sky_match") == false, "sky menu toggle persists");
    expect(persisted.at("draw_distance_scale") == 10.0f,
           "draw-distance menu selection persists");
    expect(persisted.at("full_track") == false, "full-track menu toggle persists");
    expect(!persisted.contains("easy_turbo_boost"),
           "turbo assist is not serialized into graphics.json");
    expect(persisted_enhancements.at("easy_turbo_boost") == true,
           "turbo assist selection persists in enhancements.json");
    expect(persisted.at("window_width") == 1920 && persisted.at("window_height") == 1080,
           "window-size menu selection persists");
    expect(persisted.at("texture_dump") == "menu-texture-dump",
           "texture-dump menu selection persists");

    aero::config::load_and_apply_graphics();
    expect(aero::config::current_graphics().wm_option ==
               ultramodern::renderer::WindowMode::Fullscreen,
           "menu selections survive a reload through graphics.json");
    expect(aero::config::current_graphics().developer_mode,
           "developer-mode selection survives a reload through graphics.json");
    expect(!aero::config::widescreen_fog_match() && !aero::config::widescreen_sky_match(),
           "widescreen toggles survive a reload through graphics.json");
    expect(aero::config::draw_distance_scale() == 10.0f && !aero::config::full_track(),
           "draw-distance and full-track selections survive a reload");
    expect(aero::config::easy_turbo_boost(),
           "turbo assist selection survives a reload through enhancements.json");
    expect(aero::config::window_size().width == 1920 && aero::config::window_size().height == 1080,
           "window size survives a reload through graphics.json");

    std::error_code error;
    std::filesystem::remove_all(directory, error);
    set_environment("AERO_GRAPHICS_CONFIG", nullptr);
    set_environment("AERO_ENHANCEMENTS_CONFIG", nullptr);
    return failures == 0 ? 0 : 1;
}
