// Persistent graphics configuration (see aero_config.h).
//
// Schema and behaviour mirror Zelda64Recomp's src/game/config.cpp graphics.json
// (same key names, same per-key fall-back-to-default on missing/corrupt values,
// and a "portable.txt in the LAUNCH directory -> keep config there" escape hatch),
// minus the RmlUi menu: this port's UI is the JSON file itself plus hotkeys.
#include "aero_config.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>

#include "json/json.hpp"

namespace {

constexpr const char* kAppFolderName = "AeroGaugeRecomp";
constexpr const char* kGraphicsFile = "graphics.json";

// Window-size keys live in the same graphics.json (extra keys alongside the
// GraphicsConfig fields); 16:9 default so AspectRatio::Expand widens on first run.
constexpr int kDefaultWindowWidth = 1600;
constexpr int kDefaultWindowHeight = 900;

aero::config::WindowSize g_window_size{kDefaultWindowWidth, kDefaultWindowHeight};

// RT64 texture-replacement paths (issue #9), persisted as extra graphics.json string
// keys alongside the GraphicsConfig fields (like the window size). Empty = feature off.
std::string g_texture_pack;
std::string g_texture_dump;

// Widen the dense 3P/4P split-screen fog to the open 1P window/colour (issue #83).
// Enhancement default-on, consistent with the widescreen wave; 1P/2P are unaffected
// regardless (the rewrite self-gates on player count).
bool g_widescreen_fog_match = true;

// Draw the sky panorama in 3P/4P split screen like 1P/2P (issue #84). Same
// enhancement family as the fog match; 1P/2P take the sky path natively anyway.
bool g_widescreen_sky_match = true;

// Far-clip-plane multiplier applied in the native guPerspectiveF (the game's
// universal far plane is 500 units -- the pop-in). 1.0 = original game.
//   0.0 = infinite far plane (m22=-1, m32=-2*n -- no clip whatsoever)
//   1.0 = the unmodified game's 500-unit far plane
//   >1  = finite scaled far plane; default 100 -> 50,000 units (the user's
//         "we should no longer need to compromise on this" with margin)
// Clamped to {0} U [1, 10000]: below 1 (non-zero) would SHRINK the frustum and
// is almost certainly a typo, and beyond 10000 the s15.16 fixed-point matrix
// loses precision faster than the geometry extends (the (n-f) divisor converges
// to -f, the (n+f) to +f, and ULP noise in each term lands in screen space).
float g_draw_distance_scale = 100.0f;

// Register EVERY course zone's geometry each frame instead of the game's 3-zone
// visibility window (the large-scale pop-in that the extended far plane exposed;
// see src/aero_full_track.cpp). Enhancement default-on like the far plane.
bool g_full_track = true;

float clamp_draw_distance(float v) {
    if (!(v >= 0.0f)) return 1.0f;      // also catches NaN; <0 is meaningless
    if (v == 0.0f) return 0.0f;        // 0 is the explicit "infinite" sentinel
    if (v < 1.0f) return 1.0f;          // (0,1) would shrink the frustum
    if (v > 10000.0f) return 10000.0f;
    return v;
}

// Read a key into `out`, keeping the existing (default) value when the key is
// missing or invalid. NLOHMANN_JSON_SERIALIZE_ENUM does NOT throw on an
// unrecognised string -- it silently maps it to the FIRST enumerator, which for
// several options (res/ar/rr/msaa) is not this port's default. Round-tripping the
// parsed value back to JSON detects that: a value that doesn't re-serialise to
// what we read was invalid, so the default is kept (and the user warned).
template <typename T>
void from_or_default(const nlohmann::json& j, const char* key, T& out) {
    auto it = j.find(key);
    if (it == j.end()) return;
    try {
        T parsed = it->get<T>();
        if (nlohmann::json(parsed) != *it) {
            std::fprintf(stderr, "[config] %s: invalid value %s -- keeping default\n",
                         key, it->dump().c_str());
            return;
        }
        out = parsed;
    } catch (const nlohmann::json::exception&) {
        std::fprintf(stderr, "[config] %s: wrong type -- keeping default\n", key);
    }
}

nlohmann::json to_json(const ultramodern::renderer::GraphicsConfig& c) {
    return nlohmann::json{
        {"res_option", c.res_option},
        {"wm_option", c.wm_option},
        {"hr_option", c.hr_option},
        {"api_option", c.api_option},
        {"ar_option", c.ar_option},
        {"msaa_option", c.msaa_option},
        {"rr_option", c.rr_option},
        {"hpfb_option", c.hpfb_option},
        {"rr_manual_value", c.rr_manual_value},
        {"ds_option", c.ds_option},
        {"developer_mode", c.developer_mode},
        {"window_width", g_window_size.width},
        {"window_height", g_window_size.height},
        {"texture_pack", g_texture_pack},
        {"texture_dump", g_texture_dump},
        {"widescreen_fog_match", g_widescreen_fog_match},
        {"widescreen_sky_match", g_widescreen_sky_match},
        {"draw_distance_scale", g_draw_distance_scale},
        {"full_track", g_full_track},
    };
}

void from_json(const nlohmann::json& j, ultramodern::renderer::GraphicsConfig& c) {
    from_or_default(j, "res_option", c.res_option);
    from_or_default(j, "wm_option", c.wm_option);
    from_or_default(j, "hr_option", c.hr_option);
    from_or_default(j, "api_option", c.api_option);
    from_or_default(j, "ar_option", c.ar_option);
    from_or_default(j, "msaa_option", c.msaa_option);
    from_or_default(j, "rr_option", c.rr_option);
    from_or_default(j, "hpfb_option", c.hpfb_option);
    from_or_default(j, "rr_manual_value", c.rr_manual_value);
    from_or_default(j, "ds_option", c.ds_option);
    from_or_default(j, "developer_mode", c.developer_mode);
    from_or_default(j, "window_width", g_window_size.width);
    from_or_default(j, "window_height", g_window_size.height);
    from_or_default(j, "texture_pack", g_texture_pack);
    from_or_default(j, "texture_dump", g_texture_dump);
    from_or_default(j, "widescreen_fog_match", g_widescreen_fog_match);
    from_or_default(j, "widescreen_sky_match", g_widescreen_sky_match);
    from_or_default(j, "draw_distance_scale", g_draw_distance_scale);
    from_or_default(j, "full_track", g_full_track);
    g_draw_distance_scale = clamp_draw_distance(g_draw_distance_scale);
    // Sanity-bound the window size: below the N64 framebuffer is useless, above 8K
    // is a typo -- either way SDL_CreateWindow would fail and the port would run
    // permanently headless, so reset to defaults instead.
    if (g_window_size.width < 320 || g_window_size.width > 7680 ||
        g_window_size.height < 240 || g_window_size.height > 4320) {
        std::fprintf(stderr, "[config] window %dx%d out of range -- using %dx%d\n",
                     g_window_size.width, g_window_size.height,
                     kDefaultWindowWidth, kDefaultWindowHeight);
        g_window_size = {kDefaultWindowWidth, kDefaultWindowHeight};
    }
}

// Read graphics.json into cfg (merging over whatever cfg already holds).
enum class ReadResult { Missing, Ok, Unparseable };

ReadResult read_graphics_file(const std::filesystem::path& path,
                              ultramodern::renderer::GraphicsConfig& cfg) {
    std::ifstream in{path};
    if (!in.good()) return ReadResult::Missing;
    try {
        nlohmann::json j;
        in >> j;
        from_json(j, cfg);
        return ReadResult::Ok;
    } catch (const nlohmann::json::exception& e) {
        std::fprintf(stderr, "[config] %s unparseable (%s); using defaults IN MEMORY"
                     " -- file left untouched, fix or delete it\n",
                     path.string().c_str(), e.what());
        return ReadResult::Unparseable;
    }
}

std::filesystem::path graphics_json_path() {
    // Test/harness override: point at (or isolate to) an explicit file.
    if (const char* p = std::getenv("AERO_GRAPHICS_CONFIG")) {
        return std::filesystem::path{p};
    }
    return aero::config::app_config_dir() / kGraphicsFile;
}

} // anonymous namespace

namespace aero {
namespace config {

std::filesystem::path app_config_dir() {
    // Portable mode: a portable.txt in the working directory keeps everything local.
    std::error_code ec;
    if (std::filesystem::exists("portable.txt", ec)) {
        return std::filesystem::current_path();
    }
#if defined(_WIN32)
    if (const char* localappdata = std::getenv("LOCALAPPDATA")) {
        return std::filesystem::path{localappdata} / kAppFolderName;
    }
#else
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME")) {
        return std::filesystem::path{xdg} / kAppFolderName;
    }
    if (const char* home = std::getenv("HOME")) {
        return std::filesystem::path{home} / ".config" / kAppFolderName;
    }
#endif
    return std::filesystem::current_path();
}

ultramodern::renderer::GraphicsConfig default_graphics_config() {
    // Zelda64Recomp's shipped defaults (config.cpp:26-35), which are the
    // enhancement goals of issue wave 1: window-scaled internal resolution,
    // widescreen Expand with the HUD clamped to 16:9, and RT64 frame
    // interpolation up to the display refresh rate (game logic stays at its
    // native 30Hz tick either way -- ultramodern's VI clock is fixed).
    ultramodern::renderer::GraphicsConfig cfg{};
    cfg.res_option = ultramodern::renderer::Resolution::Auto;
    cfg.wm_option = ultramodern::renderer::WindowMode::Windowed;
    cfg.hr_option = ultramodern::renderer::HUDRatioMode::Clamp16x9;
    cfg.api_option = ultramodern::renderer::GraphicsApi::Auto;
    cfg.ar_option = ultramodern::renderer::AspectRatio::Expand;
    cfg.msaa_option = ultramodern::renderer::Antialiasing::MSAA2X;
    cfg.rr_option = ultramodern::renderer::RefreshRate::Display;
    cfg.hpfb_option = ultramodern::renderer::HighPrecisionFramebuffer::Auto;
    cfg.rr_manual_value = 60;
    cfg.ds_option = 1;
    cfg.developer_mode = false;
    return cfg;
}

// (issue #67 carry-over) The widescreen-HUD rect-pin scaling (see aero_hud_widescreen.h)
// tracks runtime window resizes and the hr_option clamp via aero_ws_hud_effective_rect_aspect,
// so it needs no config-time gate. The constants remain unverified for AeroGauge's HUD;
// TODO(aerogauge): re-measure once the HUD renders.

ultramodern::renderer::GraphicsConfig load_and_apply_graphics() {
    ultramodern::renderer::GraphicsConfig cfg = default_graphics_config();
    const std::filesystem::path path = graphics_json_path();
    const ReadResult r = read_graphics_file(path, cfg);

    ultramodern::renderer::set_graphics_config(cfg);
    // Write the merged config back so the on-disk file is always complete and
    // editable (new keys appear with their defaults after an upgrade) -- but NEVER
    // overwrite a file that failed to parse: a hand-edit typo must stay recoverable,
    // not be replaced by defaults.
    if (r != ReadResult::Unparseable) {
        save_graphics(cfg);
    }
    std::fprintf(stderr, "[config] graphics config: %s\n", path.string().c_str());
    return cfg;
}

void update_saved_window_mode(ultramodern::renderer::WindowMode wm) {
    // Persist a runtime window-mode change by re-reading the on-disk file and
    // updating ONLY wm_option -- a user hand-editing other keys while the game runs
    // must not have those edits clobbered by an F11 press.
    ultramodern::renderer::GraphicsConfig cfg = default_graphics_config();
    const std::filesystem::path path = graphics_json_path();
    if (read_graphics_file(path, cfg) == ReadResult::Unparseable) {
        return; // don't destroy a recoverable (broken) file just to save a toggle
    }
    cfg.wm_option = wm;
    save_graphics(cfg);
}

void save_graphics(const ultramodern::renderer::GraphicsConfig& cfg) {
    const std::filesystem::path path = graphics_json_path();
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream out{path};
    if (!out.good()) {
        std::fprintf(stderr, "[config] cannot write %s\n", path.string().c_str());
        return;
    }
    out << to_json(cfg).dump(4) << "\n";
    out.flush();
    if (!out.good()) {
        std::fprintf(stderr, "[config] write to %s FAILED (disk full / permissions?)"
                     " -- settings may not persist\n", path.string().c_str());
    }
}

WindowSize window_size() {
    return g_window_size;
}

// Env var wins over the JSON key so a headless capture run can point at a scratch
// directory without editing (and re-saving) the user's graphics.json.
static std::string path_from_env_or(const char* env, const std::string& fallback) {
    if (const char* v = std::getenv(env)) {
        return v;
    }
    return fallback;
}

std::string texture_pack_path() {
    return path_from_env_or("AERO_TEXTURE_PACK", g_texture_pack);
}

std::string texture_dump_dir() {
    return path_from_env_or("AERO_TEXTURE_DUMP", g_texture_dump);
}

// AERO_FOG_MATCH_1P=1/0 overrides the JSON key for headless capture/testing.
bool widescreen_fog_match() {
    if (const char* v = std::getenv("AERO_FOG_MATCH_1P")) {
        return v[0] == '1';
    }
    return g_widescreen_fog_match;
}

// AERO_DRAW_DISTANCE_SCALE=<float> overrides the JSON key for A/B capture runs
// (1 = original game's 500-unit far plane). Parsed once; called every frame from
// the game thread via the native guPerspectiveF.
float draw_distance_scale() {
    if (const char* v = std::getenv("AERO_DRAW_DISTANCE_SCALE")) {
        return clamp_draw_distance(std::strtof(v, nullptr));
    }
    return g_draw_distance_scale;
}

// AERO_FULL_TRACK=1/0 overrides the JSON key for A/B capture runs
// (0 = the original game's 3-zone visibility window).
bool full_track() {
    if (const char* v = std::getenv("AERO_FULL_TRACK")) {
        return v[0] == '1';
    }
    return g_full_track;
}

// AERO_SKY_MATCH_1P=1/0 overrides the JSON key for headless capture/testing.
bool widescreen_sky_match() {
    if (const char* v = std::getenv("AERO_SKY_MATCH_1P")) {
        return v[0] == '1';
    }
    return g_widescreen_sky_match;
}

} // namespace config
} // namespace aero
