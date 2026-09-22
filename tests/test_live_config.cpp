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
    set_environment("AERO_FORCE_FULL_LOD", nullptr);
    set_environment("AERO_EASY_TURBO", nullptr);
    // Park the background writer for the whole test (a flush still writes
    // immediately), so "not written yet" and "written" are both deterministic
    // instead of racing the debounce window.
    set_environment("AERO_CONFIG_WRITE_DEBOUNCE_MS", "60000");

    auto cfg = aero::config::load_and_apply_graphics();
    expect(std::filesystem::exists(config_path), "first load creates graphics.json");
    expect(std::filesystem::exists(enhancements_path), "first load creates enhancements.json");
    expect(cfg.ar_option == ultramodern::renderer::AspectRatio::Expand,
           "enhancement-oriented aspect default is preserved");
    // Shipped defaults for the menu-exposed enhancement knobs. Verify and
    // document the default rather than assuming it.
    expect(!cfg.developer_mode, "developer overlay defaults off");
    expect(aero::config::full_track(), "full course geometry defaults on");
    expect(aero::config::draw_distance_scale() == 100.0f,
           "draw distance defaults to 100x");
    expect(!aero::config::force_full_lod(), "full LOD defaults off");
    expect(!aero::config::easy_turbo_boost(), "turbo assist defaults off");

    // Boolean environment overrides follow the documented 0/1 contract. In
    // particular, an empty value or words such as "false" must not enable it.
    set_environment("AERO_FORCE_FULL_LOD", "");
    expect(!aero::config::force_full_lod(), "empty full-LOD override stays off");
    set_environment("AERO_FORCE_FULL_LOD", "false");
    expect(!aero::config::force_full_lod(), "false full-LOD override stays off");
    set_environment("AERO_FORCE_FULL_LOD", "off");
    expect(!aero::config::force_full_lod(), "off full-LOD override stays off");
    set_environment("AERO_FORCE_FULL_LOD", "1");
    expect(aero::config::force_full_lod(), "one full-LOD override enables it");
    set_environment("AERO_FORCE_FULL_LOD", "0");
    expect(!aero::config::force_full_lod(), "zero full-LOD override disables it");
    set_environment("AERO_FORCE_FULL_LOD", nullptr);

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
    // The menu path must not touch the file itself: the change is live in memory
    // but still queued, so a menu click never blocks the event loop on disk I/O.
    expect(read_json(config_path).at("msaa_option") == "MSAA2X",
           "graphics menu change is not written synchronously");
    aero::config::flush_config_writes();
    expect(read_json(config_path).at("msaa_option") == "MSAA4X",
           "flush persists the queued graphics menu change");
    expect(read_json(config_path).at("texture_pack") == "manual-texture-pack",
           "graphics menu changes preserve unrelated hand edits");

    aero::config::set_widescreen_fog_match(false);
    aero::config::set_widescreen_sky_match(false);
    aero::config::set_draw_distance_scale(10.0f);
    aero::config::set_full_track(false);
    aero::config::set_force_full_lod(true);
    aero::config::set_easy_turbo_boost(true);
    aero::config::set_window_size({1920, 1080});
    aero::config::set_texture_pack_path("menu-texture-pack");
    aero::config::set_texture_dump_dir("menu-texture-dump");
    expect(!aero::config::widescreen_fog_match(), "fog menu toggle updates live");
    expect(!aero::config::widescreen_sky_match(), "sky menu toggle updates live");
    expect(aero::config::draw_distance_scale() == 10.0f,
           "draw-distance menu selection updates live");
    expect(!aero::config::full_track(), "full-track menu toggle updates live");
    expect(aero::config::force_full_lod(), "full-LOD menu toggle updates live");
    expect(aero::config::easy_turbo_boost(), "turbo assist menu toggle updates live");
    expect(aero::config::window_size().width == 1920 && aero::config::window_size().height == 1080,
           "window-size menu selection updates live");
    expect(aero::config::texture_pack_path() == "menu-texture-pack" &&
               aero::config::texture_dump_dir() == "menu-texture-dump",
           "texture-path menu selections update live");

    aero::config::update_saved_window_mode(ultramodern::renderer::WindowMode::Fullscreen);
    // Every edit above (a burst of menu/hotkey actions) is still in memory only;
    // a single flush has to coalesce and persist all of it.
    expect(read_json(config_path).at("full_track") == true &&
               read_json(enhancements_path).at("easy_turbo_boost") == false,
           "live setters do not write synchronously");
    aero::config::flush_config_writes();
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
    expect(persisted.at("force_full_lod") == true, "full-LOD menu toggle persists");
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
    expect(aero::config::force_full_lod(),
           "full-LOD selection survives a reload through graphics.json");
    expect(aero::config::easy_turbo_boost(),
           "turbo assist selection survives a reload through enhancements.json");
    expect(aero::config::window_size().width == 1920 && aero::config::window_size().height == 1080,
           "window size survives a reload through graphics.json");

    // If graphics.json disappears while the game runs, the next write rebuilds from
    // the last complete document this module wrote plus the change that triggered it.
    // Writing only the changed keys would silently drop every other live setting --
    // and, because only the startup load writes a whole document, a base captured at
    // startup would resurrect a value the user has since changed.
    aero::config::set_draw_distance_scale(5.0f);
    aero::config::flush_config_writes();
    std::error_code removed;
    std::filesystem::remove(config_path, removed);
    aero::config::set_widescreen_fog_match(true);
    aero::config::flush_config_writes();
    const nlohmann::json rebuilt = read_json(config_path);
    expect(rebuilt.at("widescreen_fog_match") == true,
           "deleted-file rebuild applies the queued change");
    expect(rebuilt.at("draw_distance_scale") == 5.0f && rebuilt.at("wm_option") == "Fullscreen",
           "deleted-file rebuild keeps settings the queued change did not mention");

    // Coalescing: a rapid run of edits must reach disk as one write per file, not
    // one write per edit. The counter is the only way to observe that; the file
    // contents look the same either way.
    const uint64_t writes_before_burst = aero::config::config_write_count();
    for (int i = 1; i <= 20; ++i) {
        aero::config::set_draw_distance_scale(float(i));
        aero::config::set_full_track(i % 2 == 0);
    }
    aero::config::set_easy_turbo_boost(true);
    aero::config::flush_config_writes();
    expect(aero::config::config_write_count() == writes_before_burst + 2,
           "a burst of edits coalesces into one write per file");
    expect(aero::config::draw_distance_scale() == 20.0f && aero::config::full_track(),
           "the coalesced burst settles on its final values");
    expect(read_json(config_path).at("draw_distance_scale") == 20.0f,
           "the coalesced burst persists its final values");

    // A batch that throws while serializing must not kill the writer. The thread is
    // detached, so an escaping exception would terminate the process; nlohmann's
    // dump() raises on a string that is not UTF-8, which the texture path can carry
    // through from the caller. The failed batch is dropped, but the file must not be
    // left truncated and later changes must still land.
    const uint64_t writes_before_failure = aero::config::config_write_count();
    aero::config::set_texture_pack_path(std::string("\xff\xfe not utf-8", 14));
    aero::config::flush_config_writes();
    expect(aero::config::config_write_count() == writes_before_failure,
           "a batch that cannot be serialized is not counted as written");
    expect(read_json(config_path).at("draw_distance_scale") == 20.0f,
           "a failed serialization leaves the existing file intact");
    aero::config::set_window_size({1280, 720});
    aero::config::flush_config_writes();
    expect(read_json(config_path).at("window_width") == 1280,
           "the writer still persists changes after a failed batch");

    aero::config::flush_config_writes();
    std::error_code error;
    std::filesystem::remove_all(directory, error);
    set_environment("AERO_GRAPHICS_CONFIG", nullptr);
    set_environment("AERO_ENHANCEMENTS_CONFIG", nullptr);
    set_environment("AERO_FORCE_FULL_LOD", nullptr);
    set_environment("AERO_CONFIG_WRITE_DEBOUNCE_MS", nullptr);
    return failures == 0 ? 0 : 1;
}
