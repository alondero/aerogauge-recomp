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
// Last-known preset for the live window size (seed bookkeeping only; the
// picker applies at Apply time, never at pick time).
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
    seeded = aero::config::current_graphics();
#define ENUM(field) sync(page, #field, uint32_t(seeded.field))
    ENUM(res_option); ENUM(wm_option); ENUM(hr_option); ENUM(api_option);
    ENUM(ar_option); ENUM(msaa_option); ENUM(rr_option); ENUM(hpfb_option); ENUM(ds_option);
#undef ENUM
    sync(page, "rr_manual_value", double(seeded.rr_manual_value));
    seeded_pack = aero::config::texture_pack_path();
    seeded_dump = aero::config::texture_dump_dir();
    sync(page, "texture_pack", seeded_pack);
    sync(page, "texture_dump", seeded_dump);
    // Picker state for the (possibly JSON- or preset-edited) live size. The
    // picker itself applies with the page's Apply button; seeding only updates
    // the displayed value, so no change callback fires here.
    const uint32_t live_preset = preset_from_size(aero::config::window_size().width,
                                                  aero::config::window_size().height);
    sync(page, "window_size", live_preset);
    // Selecting the no-op "Custom" entry is only meaningful when Custom IS the
    // current size; otherwise grey it out so a click cannot silently do nothing.
    page.update_enum_option_disabled("window_size", kWindowPresetCustom,
                                     live_preset != kWindowPresetCustom);
    page.revert_temp_config();
}

void seed_enhancements() {
    auto& page = recompui::config::get_config("enhancements");
    sync(page, "full_track", aero::config::full_track());
    sync(page, "easy_turbo", aero::config::easy_turbo_boost());
    // "Unlimited" is a first-class menu choice mapping to the internal 0
    // sentinel (infinite far plane); the multiplier only applies when unticked.
    const bool unlimited = aero::config::draw_distance_scale() == 0.0f;
    sync(page, "draw_distance_unlimited", unlimited);
    // The slider's schema minimum is 1, so never seed the internal 0 sentinel
    // into it; while Unlimited is engaged the slider shows the restore default.
    sync(page, "draw_distance", unlimited ? 100.0 : double(aero::config::draw_distance_scale()));
    page.revert_temp_config();
}

void seed_debug() {
    auto& page = recompui::config::get_config("debug");
    sync(page, "developer_mode", aero::config::current_graphics().developer_mode);
    page.revert_temp_config();
}

void save_graphics() {
    auto& page = recompui::config::get_graphics_config();
    auto edited = seeded;
#define ENUM(field) edited.field = static_cast<decltype(edited.field)>(std::get<uint32_t>(page.get_option_value(#field)))
    ENUM(res_option); ENUM(wm_option); ENUM(hr_option); ENUM(api_option);
    ENUM(ar_option); ENUM(msaa_option); ENUM(rr_option); ENUM(hpfb_option); ENUM(ds_option);
#undef ENUM
    edited.rr_manual_value = int(std::get<double>(page.get_option_value("rr_manual_value")));
    const auto pack = std::get<std::string>(page.get_option_value("texture_pack"));
    const auto dump = std::get<std::string>(page.get_option_value("texture_dump"));
    // The window-size picker obeys this page's confirmation flow: the picked
    // preset resolves at Apply time. Custom (or an unknown value) keeps the
    // live size, so a discarded pick can never clobber a custom resolution.
    const uint32_t picked_preset = std::get<uint32_t>(page.get_option_value("window_size"));
    // SDL window calls and the live config snapshot are main-thread operations;
    // the JSON write they trigger is queued for the background writer. The
    // current lock still means a blocking action can delay rendering; see
    // docs/frontend.md (## Ownership and threads) before changing this boundary.
    enqueue([edited, before = seeded, pack, dump, picked_preset] {
        auto cfg = aero::config::current_graphics();
        // Merge only edited fields: F11 may have changed the window mode since
        // this confirmation-backed page was opened.
#define MERGE(field) if (edited.field != before.field) cfg.field = edited.field
        MERGE(res_option); MERGE(wm_option); MERGE(hr_option); MERGE(api_option);
        MERGE(ar_option); MERGE(msaa_option); MERGE(rr_option); MERGE(hpfb_option);
        MERGE(ds_option); MERGE(rr_manual_value);
#undef MERGE
        const auto live = aero::config::window_size();
        auto size = live;
        for (const auto& preset : kWindowPresets) {
            if (preset.value == picked_preset) {
                size = {preset.width, preset.height};
                break;
            }
        }
        const bool resized = size.width != live.width || size.height != live.height;
        aero::config::apply_graphics_settings(cfg, size, pack, dump);
        if (resized || edited.wm_option != before.wm_option) apply_window_settings();
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
    graphics.add_string_option("texture_pack", "Texture pack path (restart)", "Directory or .rtz archive. Leave empty for original textures. AERO_TEXTURE_PACK overrides this setting.", port::texture_pack_path());
    graphics.add_string_option("texture_dump", "Texture dump directory (restart)", "Output directory for RT64 texture dumps. Leave empty to disable. AERO_TEXTURE_DUMP overrides this setting.", port::texture_dump_dir());
    graphics.update_option_disabled("texture_pack", std::getenv("AERO_TEXTURE_PACK") != nullptr);
    graphics.update_option_disabled("texture_dump", std::getenv("AERO_TEXTURE_DUMP") != nullptr);

    // Window size picker: common desktop resolutions. The picker obeys the
    // page's confirmation flow — a pick only stages the value; Apply resolves
    // it into a window resize + JSON persistence (see save_graphics).
    std::vector<recomp::config::ConfigOptionEnumOption> window_preset_options;
    char key[32];
    for (const auto& preset : kWindowPresets) {
        std::snprintf(key, sizeof(key), "%dx%d", preset.width, preset.height);
        window_preset_options.emplace_back(preset.value, key, key);
    }
    window_preset_options.emplace_back(kWindowPresetCustom, "Custom", "Custom");
    graphics.add_enum_option("window_size", "Window size",
        "Windowed size, applied with the Apply button. Pick a common resolution; a size typed directly into graphics.json shows as Custom and is kept unless you pick a preset.",
        window_preset_options, preset_from_size(port::window_size().width, port::window_size().height));
#if defined(__ANDROID__)
    // These desktop controls cannot change an Android-owned surface or open
    // a desktop filesystem dialog. Keep the shared schema for saved settings.
    for (const char* id : {"wm_option", "window_size", "api_option", "texture_pack", "texture_dump"})
        graphics.update_option_disabled(id, true);
    graphics.update_option_description("api_option", "Android uses Vulkan. Choose a GPU driver in the launcher.");
    graphics.update_option_description("wm_option", "Android manages the fullscreen display.");
#endif

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
                // slider is never left pointing at the internal 0 sentinel, and
                // the slider option is updated too (this page has no Apply step,
                // so its values must track live state immediately).
                auto& page = recompui::config::get_config("enhancements");
                page.update_option_value("draw_distance", 100.0);
                enqueue([] { aero::config::set_draw_distance_scale(100.0f); });
            }
        });
    enhancements.add_number_option("draw_distance", "Draw distance multiplier",
        "Far-clip-plane multiplier over the original game's 500-unit draw distance; 1 = original, higher values show more scenery ahead of you. Disabled while draw distance is Unlimited.",
        1, 10000, 10, 0, false, port::draw_distance_scale() == 0.0f ? 100.0 : port::draw_distance_scale());
    // Grey out the multiplier while Unlimited is engaged. Registered once at
    // schema time so the boot-time dependency derive (which runs before any UI
    // exists) sees it; re-registering per seed would duplicate map entries.
    // Explicit bool variant: a bare `true` would bind to the enum overload and
    // compare as uint32_t. When the env var pins the toggle off (forced on,
    // greying the toggle itself), the disable value also matches so the derive
    // cannot re-enable the slider behind the override.
    std::vector<ConfigValueVariant> unlimited_values = {true};
    if (std::getenv("AERO_DRAW_DISTANCE_SCALE") != nullptr) {
        unlimited_values.push_back(false);
    }
    enhancements.add_option_disable_dependency("draw_distance", "draw_distance_unlimited", unlimited_values);
    enhancements.update_option_disabled("draw_distance", std::getenv("AERO_DRAW_DISTANCE_SCALE") != nullptr);
    enhancements.add_option_change_callback("draw_distance",
        [](ConfigValueVariant value, ConfigValueVariant, OptionChangeContext context) {
            if (context == OptionChangeContext::Permanent)
                enqueue([scale = float(std::get<double>(value))] { aero::config::set_draw_distance_scale(scale); });
        });

    // Developer/debug tools get their own tab (moved out of Graphics); created
    // last so it renders as the right-most tab in the settings modal.
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
}
} // namespace aero::menu
