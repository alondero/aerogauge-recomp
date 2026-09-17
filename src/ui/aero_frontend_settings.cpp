#include "aero_frontend_settings.h"
#include "aero_config.h"
#include "aero_menu.h"
#include "recompui/config.h"
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace aero::menu {
namespace {
using recomp::config::Config;
using recomp::config::ConfigValueVariant;
using recomp::config::OptionChangeContext;
using namespace ultramodern::renderer;
GraphicsConfig seeded;
std::string seeded_pack, seeded_dump;

// Common desktop resolutions for the windowed-mode size picker (16:9 plus
// legacy 16:10/4:3 favourites). The resolved size stays in graphics.json
// (window_width/window_height) exactly as before; the picker only writes it.
struct WindowPreset { uint32_t value; int width; int height; };
constexpr WindowPreset kWindowPresets[] = {
    {0, 640, 360}, {1, 1280, 720}, {2, 1600, 900}, {3, 1920, 1080},
    {4, 2560, 1440}, {5, 3840, 2160}, {6, 1280, 800}, {7, 1920, 1200},
    {8, 1280, 960}, {9, 1600, 1200}, {10, 1920, 1440},
};
constexpr uint32_t kWindowPresetCustom = 11;
// Last-known preset for the live window size; lets the change callback tell
// seed/echo events apart from real user picks.
uint32_t seeded_window_preset = 0;
// Set while seed_graphics() writes values into the confirmation page. The
// picker's change callback must not apply those (seed/echo) events, or every
// refresh would re-run the last pick; only a real user pick may apply.
bool seeding_graphics = false;

uint32_t preset_from_size(int width, int height) {
    for (const auto& preset : kWindowPresets) {
        if (preset.width == width && preset.height == height) return preset.value;
    }
    return kWindowPresetCustom;
}

void sync(Config& page, const char* id, ConfigValueVariant value) {
    if (page.get_option_value(id) == value) return;
    page.update_option_value(id, value);
    if (page.requires_confirmation) page.apply_option_value(id);
}

void seed_graphics() {
    auto& page = recompui::config::get_graphics_config();
    seeding_graphics = true;
    seeded = aero::config::current_graphics();
#define ENUM(field) sync(page, #field, uint32_t(seeded.field))
    ENUM(res_option); ENUM(wm_option); ENUM(hr_option); ENUM(api_option);
    ENUM(ar_option); ENUM(msaa_option); ENUM(rr_option); ENUM(hpfb_option); ENUM(ds_option);
#undef ENUM
    sync(page, "rr_manual_value", double(seeded.rr_manual_value));
    // Mirrors the Debug tab's toggle; the Graphics entry itself stays hidden.
    sync(page, "developer_mode", seeded.developer_mode);
    seeded_pack = aero::config::texture_pack_path();
    seeded_dump = aero::config::texture_dump_dir();
    sync(page, "window_width", double(aero::config::window_size().width));
    sync(page, "window_height", double(aero::config::window_size().height));
    sync(page, "texture_pack", seeded_pack);
    sync(page, "texture_dump", seeded_dump);
    // Picker state for the (possibly JSON- or preset-edited) live size; the
    // callback skips this echo because the value matches seeded_window_preset.
    seeded_window_preset = preset_from_size(aero::config::window_size().width,
                                             aero::config::window_size().height);
    sync(page, "window_size", seeded_window_preset);
    // Selecting the no-op "Custom" entry is only meaningful when Custom IS the
    // current size; otherwise grey it out so a click cannot silently do nothing.
    page.update_enum_option_disabled("window_size", kWindowPresetCustom,
                                     seeded_window_preset != kWindowPresetCustom);
    page.revert_temp_config();
    seeding_graphics = false;
}

void seed_enhancements() {
    auto& page = recompui::config::get_config("enhancements");
    sync(page, "full_track", aero::config::full_track());
    sync(page, "easy_turbo", aero::config::easy_turbo_boost());
    // "Unlimited" is a first-class menu choice mapping to the internal 0
    // sentinel (infinite far plane); the multiplier only applies when unticked.
    sync(page, "draw_distance_unlimited", aero::config::draw_distance_scale() == 0.0f);
    sync(page, "draw_distance", double(aero::config::draw_distance_scale()));
    // Grey out the multiplier while Unlimited is engaged. Explicit bool variant:
    // a bare `true` would bind to the enum overload and compare as uint32_t.
    std::vector<ConfigValueVariant> unlimited_values = {true};
    page.add_option_disable_dependency("draw_distance", "draw_distance_unlimited", unlimited_values);
    page.revert_temp_config();
}

void seed_debug() {
    auto& page = recompui::config::get_config("debug");
    sync(page, "developer_mode", aero::config::current_graphics().developer_mode);
    page.revert_temp_config();
}

void apply_window_preset(uint32_t preset_value) {
    for (const auto& preset : kWindowPresets) {
        if (preset.value == preset_value) {
            aero::config::set_window_size({preset.width, preset.height});
            apply_window_settings();
            return;
        }
    }
}

void save_window_preset(uint32_t preset_value) {
    seeded_window_preset = preset_value;
    if (preset_value == kWindowPresetCustom) return;
    // SDL window changes and persistence belong to the main thread, never to
    // RT64's presentation callback (same contract as save_graphics below).
    enqueue([preset_value] { apply_window_preset(preset_value); });
}
void save_graphics() {
    auto& page = recompui::config::get_graphics_config();
    auto edited = seeded;
#define ENUM(field) edited.field = static_cast<decltype(edited.field)>(std::get<uint32_t>(page.get_option_value(#field)))
    ENUM(res_option); ENUM(wm_option); ENUM(hr_option); ENUM(api_option);
    ENUM(ar_option); ENUM(msaa_option); ENUM(rr_option); ENUM(hpfb_option); ENUM(ds_option);
#undef ENUM
    edited.rr_manual_value = int(std::get<double>(page.get_option_value("rr_manual_value")));
    edited.developer_mode = std::get<bool>(page.get_option_value("developer_mode"));
    const auto pack = std::get<std::string>(page.get_option_value("texture_pack"));
    const auto dump = std::get<std::string>(page.get_option_value("texture_dump"));
    // Window size is owned by the preset picker (a separate option), so Apply
    // persists the live size instead of the hidden per-axis page values.
    enqueue([edited, before = seeded, pack, dump] {
        auto cfg = aero::config::current_graphics();
        // Merge only edited fields: F11 may have changed the window mode since
        // this confirmation-backed page was opened.
#define MERGE(field) if (edited.field != before.field) cfg.field = edited.field
        MERGE(res_option); MERGE(wm_option); MERGE(hr_option); MERGE(api_option);
        MERGE(ar_option); MERGE(msaa_option); MERGE(rr_option); MERGE(hpfb_option);
        MERGE(ds_option); MERGE(rr_manual_value); MERGE(developer_mode);
#undef MERGE
        aero::config::apply_graphics_settings(cfg, aero::config::window_size(), pack, dump);
        if (edited.wm_option != before.wm_option) apply_window_settings();
        refresh_settings();
    });
    seeded = edited;
    seeded_pack = pack;
    seeded_dump = dump;
}

void boolean(Config& page, const char* id, const char* label, const char* description,
             bool initial, void (*setter)(bool), const char* env = nullptr) {
    page.add_bool_option(id, label, description, initial);
    if (env != nullptr) page.update_option_disabled(id, std::getenv(env) != nullptr);
    page.add_option_change_callback(id, [setter](ConfigValueVariant value, ConfigValueVariant, OptionChangeContext context) {
        if (context == OptionChangeContext::Permanent)
            enqueue([setter, enabled = std::get<bool>(value)] { setter(enabled); });
    });
}
}

void refresh_settings() {
    auto& graphics = recompui::config::get_graphics_config();
    auto& enhancements = recompui::config::get_config("enhancements");
    if (!graphics.is_dirty()) seed_graphics();
    if (!enhancements.is_dirty()) seed_enhancements();
    if (!recompui::config::get_config("debug").is_dirty()) seed_debug();
}

void create_settings() {
    namespace settings = recompui::config;
    namespace port = aero::config;
    // The frontend reads these options internally. Hide the tab because the
    // game's existing input/audio layer does not implement its extra controls.
    auto& general = settings::create_general_tab({.has_rumble_strength = false,
        .has_gyro_sensitivity = false, .has_mouse_sensitivity = false});
    general.external_storage = true;
    settings::set_tab_visible("general", false);
    auto& graphics = settings::create_graphics_tab();
    graphics.external_storage = true;
    graphics.set_load_callback(seed_graphics);
    graphics.set_save_callback(save_graphics);
    graphics.update_option_description("api_option", "Graphics backend. Changes take effect after restarting the application.");
    // Window size is a preset picker now; the per-axis number options stay
    // registered (hidden) so graphics.json still round-trips through them.
    graphics.add_number_option("window_width", "", "", 320, 7680, 1, 0, false, port::window_size().width, true);
    graphics.add_number_option("window_height", "", "", 240, 4320, 1, 0, false, port::window_size().height, true);
    graphics.add_string_option("texture_pack", "Texture pack path (restart)", "Directory or .rtz archive. Leave empty for original textures. AERO_TEXTURE_PACK overrides this setting.", port::texture_pack_path());
    graphics.add_string_option("texture_dump", "Texture dump directory (restart)", "Output directory for RT64 texture dumps. Leave empty to disable. AERO_TEXTURE_DUMP overrides this setting.", port::texture_dump_dir());
    graphics.update_option_disabled("texture_pack", std::getenv("AERO_TEXTURE_PACK") != nullptr);
    graphics.update_option_disabled("texture_dump", std::getenv("AERO_TEXTURE_DUMP") != nullptr);

    // Window size picker: common desktop resolutions, applied immediately on
    // selection (the callback fires for both the immediate Temporary change and
    // the Apply confirmation; the seed echo is skipped via seeded_window_preset).
    std::vector<recomp::config::ConfigOptionEnumOption> window_preset_options;
    char key[32];
    for (const auto& preset : kWindowPresets) {
        std::snprintf(key, sizeof(key), "%dx%d", preset.width, preset.height);
        window_preset_options.emplace_back(preset.value, key, key);
    }
    window_preset_options.emplace_back(kWindowPresetCustom, "Custom", "Custom");
    seeded_window_preset = preset_from_size(port::window_size().width, port::window_size().height);
    graphics.add_enum_option("window_size", "Window size",
        "Windowed size, applied with the Apply button. Pick a common resolution; a size typed directly into graphics.json shows as Custom.",
        window_preset_options, seeded_window_preset);
    graphics.add_option_change_callback("window_size",
        [](ConfigValueVariant value, ConfigValueVariant, OptionChangeContext context) {
            if (context == OptionChangeContext::Load || seeding_graphics) return;
            const uint32_t preset = std::get<uint32_t>(value);
            if (preset == seeded_window_preset) return; // seed echo / Apply re-fire
            save_window_preset(preset);
        });

    // Developer/debug tools get their own tab (moved out of Graphics).
    auto& debug = settings::create_config_tab("Debug", "debug", false);
    debug.external_storage = true;
    debug.set_load_callback(seed_debug);
    boolean(debug, "developer_mode", "RT64 developer overlay (restart)",
        "Enables RT64's developer tools. Changes take effect after restarting the application.",
        port::current_graphics().developer_mode, [](bool enabled) {
            auto cfg = aero::config::current_graphics();
            cfg.developer_mode = enabled;
            aero::config::apply_graphics(cfg);
        });

    auto& enhancements = settings::create_config_tab("Enhancements", "enhancements", false);
    enhancements.external_storage = true;
    enhancements.set_load_callback(seed_enhancements);
    boolean(enhancements, "full_track", "Full course geometry (experimental)",
        "Draw the whole course instead of the original visibility zones. AERO_FULL_TRACK overrides this setting.",
        port::full_track(), port::set_full_track, "AERO_FULL_TRACK");
    boolean(enhancements, "easy_turbo", "Easy Turbo + Boost Start",
        "Simplified boost controls. Boost Start: hold Accelerate through the countdown for a launch boost. "
        "Turbo (player 1 races): press the dedicated Turbo button -- R (or E) on keyboard, right trigger "
        "(or right shoulder) on gamepad. Release and press again for another boost; normal heat and "
        "overheating rules apply. AERO_EASY_TURBO overrides this setting.",
        port::easy_turbo_boost(), port::set_easy_turbo_boost, "AERO_EASY_TURBO");
    enhancements.add_bool_option("draw_distance_unlimited", "Unlimited draw distance",
        "Remove the far clipping plane entirely so no scenery pops in. When off, the multiplier below is used. "
        "AERO_DRAW_DISTANCE_SCALE overrides this setting.",
        port::draw_distance_scale() == 0.0f);
    enhancements.update_option_disabled("draw_distance_unlimited", std::getenv("AERO_DRAW_DISTANCE_SCALE") != nullptr);
    enhancements.add_option_change_callback("draw_distance_unlimited",
        [](ConfigValueVariant value, ConfigValueVariant, OptionChangeContext context) {
            if (context != OptionChangeContext::Permanent) return;
            if (std::get<bool>(value)) {
                enqueue([] { aero::config::set_draw_distance_scale(0.0f); });
            } else if (aero::config::draw_distance_scale() == 0.0f) {
                // Leaving Unlimited restores the shipped default multiplier so the
                // slider below is never left pointing at the internal 0 sentinel.
                enqueue([] { aero::config::set_draw_distance_scale(100.0f); });
            }
        });
    enhancements.add_number_option("draw_distance", "Draw distance multiplier",
        "Far-clip-plane multiplier over the original game's 500-unit draw distance; 1 = original, higher values show more scenery ahead of you. Disabled while draw distance is Unlimited.",
        1, 10000, 1, 0, false, port::draw_distance_scale() == 0.0f ? 100.0 : port::draw_distance_scale());
    enhancements.update_option_disabled("draw_distance", std::getenv("AERO_DRAW_DISTANCE_SCALE") != nullptr);
    enhancements.add_option_change_callback("draw_distance",
        [](ConfigValueVariant value, ConfigValueVariant, OptionChangeContext context) {
            if (context == OptionChangeContext::Permanent)
                enqueue([scale = float(std::get<double>(value))] { aero::config::set_draw_distance_scale(scale); });
        });
}
} // namespace aero::menu
