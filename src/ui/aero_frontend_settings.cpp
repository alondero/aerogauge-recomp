#include "aero_frontend_settings.h"
#include "aero_config.h"
#include "aero_menu.h"
#include "recompui/config.h"
#include <cstdlib>

namespace aero::menu {
namespace {
using recomp::config::Config;
using recomp::config::ConfigValueVariant;
using recomp::config::OptionChangeContext;
using namespace ultramodern::renderer;
GraphicsConfig seeded;
aero::config::WindowSize seeded_size;
std::string seeded_pack, seeded_dump;

void sync(Config& page, const char* id, ConfigValueVariant value) {
    if (page.get_option_value(id) == value) return;
    page.update_option_value(id, value);
    if (page.requires_confirmation) page.apply_option_value(id);
}

// RecompFrontend owns the temporary page values. The port owns the persistent
// JSON files and the live settings snapshot, so opening or refreshing a page
// copies from the port instead of making the frontend a second storage owner.
void seed_graphics() {
    auto& page = recompui::config::get_graphics_config();
    seeded = aero::config::current_graphics();
#define ENUM(field) sync(page, #field, uint32_t(seeded.field))
    ENUM(res_option); ENUM(wm_option); ENUM(hr_option); ENUM(api_option);
    ENUM(ar_option); ENUM(msaa_option); ENUM(rr_option); ENUM(hpfb_option); ENUM(ds_option);
#undef ENUM
    sync(page, "rr_manual_value", double(seeded.rr_manual_value));
    sync(page, "developer_mode", seeded.developer_mode);
    seeded_size = aero::config::window_size();
    seeded_pack = aero::config::texture_pack_path();
    seeded_dump = aero::config::texture_dump_dir();
    sync(page, "window_width", double(seeded_size.width));
    sync(page, "window_height", double(seeded_size.height));
    sync(page, "texture_pack", seeded_pack);
    sync(page, "texture_dump", seeded_dump);
    page.revert_temp_config();
}

void seed_enhancements() {
    auto& page = recompui::config::get_config("enhancements");
    sync(page, "full_track", aero::config::full_track());
    sync(page, "easy_turbo", aero::config::easy_turbo_boost());
    sync(page, "draw_distance", double(aero::config::draw_distance_scale()));
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
    edited.developer_mode = std::get<bool>(page.get_option_value("developer_mode"));
    const aero::config::WindowSize size{int(std::get<double>(page.get_option_value("window_width"))),
                                      int(std::get<double>(page.get_option_value("window_height")))};
    const auto pack = std::get<std::string>(page.get_option_value("texture_pack"));
    const auto dump = std::get<std::string>(page.get_option_value("texture_dump"));
    // The callback may run while the frontend render lock is held. Capture the
    // proposed values and let aero_menu::update apply them on the SDL thread.
    enqueue([edited, before = seeded, size, old_size = seeded_size, pack, dump] {
        auto cfg = aero::config::current_graphics();
        // Merge only edited fields: F11 may have changed the window mode since
        // this confirmation-backed page was opened.
#define MERGE(field) if (edited.field != before.field) cfg.field = edited.field
        MERGE(res_option); MERGE(wm_option); MERGE(hr_option); MERGE(api_option);
        MERGE(ar_option); MERGE(msaa_option); MERGE(rr_option); MERGE(hpfb_option);
        MERGE(ds_option); MERGE(rr_manual_value); MERGE(developer_mode);
#undef MERGE
        // JSON I/O and SDL window calls are main-thread operations. The current
        // lock also means a blocking action can delay rendering; see docs/frontend.md
        // (## Ownership and threads) before changing this boundary.
        aero::config::apply_graphics_settings(cfg, size, pack, dump);
        const bool resized = size.width != old_size.width || size.height != old_size.height;
        if (resized || edited.wm_option != before.wm_option) apply_window_settings();
        refresh_settings();
    });
    seeded = edited;
    seeded_size = size;
    seeded_pack = pack;
    seeded_dump = dump;
}

void boolean(Config& page, const char* id, const char* label, const char* description,
             bool initial, void (*setter)(bool), const char* env) {
    page.add_bool_option(id, label, description, initial);
    page.update_option_disabled(id, std::getenv(env) != nullptr);
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
    graphics.update_option_description("developer_mode", "RT64 developer tools. Changes take effect after restarting the application.");
    graphics.add_number_option("window_width", "Window width", "Windowed width in pixels.", 320, 7680, 1, 0, false, port::window_size().width);
    graphics.add_number_option("window_height", "Window height", "Windowed height in pixels.", 240, 4320, 1, 0, false, port::window_size().height);
    graphics.add_string_option("texture_pack", "Texture pack path (restart)", "Directory or .rtz archive. Leave empty for original textures. AERO_TEXTURE_PACK overrides this setting.", port::texture_pack_path());
    graphics.add_string_option("texture_dump", "Texture dump directory (restart)", "Output directory for RT64 texture dumps. Leave empty to disable. AERO_TEXTURE_DUMP overrides this setting.", port::texture_dump_dir());
    graphics.update_option_disabled("texture_pack", std::getenv("AERO_TEXTURE_PACK") != nullptr);
    graphics.update_option_disabled("texture_dump", std::getenv("AERO_TEXTURE_DUMP") != nullptr);

    auto& enhancements = settings::create_config_tab("Enhancements", "enhancements", false);
    enhancements.external_storage = true;
    enhancements.set_load_callback(seed_enhancements);
    boolean(enhancements, "full_track", "Full course geometry (experimental)",
        "Draw the whole course instead of the original visibility zones. AERO_FULL_TRACK overrides this setting.",
        port::full_track(), port::set_full_track, "AERO_FULL_TRACK");
    boolean(enhancements, "easy_turbo", "Easy Turbo + Boost Start",
        "Simplifies the Turbo and Boost Start button sequences. AERO_EASY_TURBO overrides this setting.",
        port::easy_turbo_boost(), port::set_easy_turbo_boost, "AERO_EASY_TURBO");
    enhancements.add_number_option("draw_distance", "Draw distance multiplier",
        "1 = original, 0 = unlimited. AERO_DRAW_DISTANCE_SCALE overrides this setting.",
        0, 10000, 1, 0, false, port::draw_distance_scale());
    enhancements.update_option_disabled("draw_distance", std::getenv("AERO_DRAW_DISTANCE_SCALE") != nullptr);
    enhancements.add_option_change_callback("draw_distance",
        [](ConfigValueVariant value, ConfigValueVariant, OptionChangeContext context) {
            if (context == OptionChangeContext::Permanent)
                enqueue([scale = float(std::get<double>(value))] { aero::config::set_draw_distance_scale(scale); });
        });
}
} // namespace aero::menu
