#include "aero_frontend_settings.h"
#include "aero_config.h"
#include "aero_menu.h"
#include "aero_mods.h"
#include "aero_region.h"
#include "recompui/recompui.h"
#include "recompui/config.h"
#include "recompinput/input_mapping.h"
#include "recompinput/players.h"
#include "recompinput/input_types.h"
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
    sync(page, "force_full_lod", aero::config::force_full_lod());
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
    const bool full_lod = std::get<bool>(page.get_option_value("force_full_lod"));
    // The window-size picker obeys this page's confirmation flow: the picked
    // preset resolves at Apply time. Custom (or an unknown value) keeps the
    // live size, so a discarded pick can never clobber a custom resolution.
    const uint32_t picked_preset = std::get<uint32_t>(page.get_option_value("window_size"));
    // SDL window calls and the live config snapshot are main-thread operations;
    // the JSON write they trigger is queued for the background writer. The
    // current lock still means a blocking action can delay rendering; see
    // docs/frontend.md (## Ownership and threads) before changing this boundary.
    enqueue([edited, before = seeded, pack, dump, picked_preset, full_lod] {
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
        aero::config::apply_graphics_settings(cfg, size, pack, dump, true, full_lod);
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

void configure_controls() {
    using recompinput::GameInput;
    using recompinput::InputField;

    // AeroGauge is a single-player port. Keep the shared frontend profile
    // model, but do not expose multiplayer assignment for a second
    // controller that the port cannot report to the game.
    recompinput::players::set_single_player_mode(true);

    recompinput::set_game_input_name(GameInput::A, "Accelerate / Confirm");
    recompinput::set_game_input_description(GameInput::A,
        "Accelerates during a race and confirms menu choices.");
    recompinput::set_game_input_name(GameInput::B, "Brake / Cancel");
    recompinput::set_game_input_description(GameInput::B,
        "Brakes during a race and cancels menu choices.");
    recompinput::set_game_input_name(GameInput::Z, "Drift");
    recompinput::set_game_input_description(GameInput::Z,
        "Activates the vehicle drift control.");
    recompinput::set_game_input_name(GameInput::START, "Pause / Advance");
    recompinput::set_game_input_description(GameInput::START,
        "Pauses a race and advances supported menu screens.");
    recompinput::set_game_input_description(GameInput::L,
        "The Nintendo 64 L button.");
    recompinput::set_game_input_name(GameInput::R, "Turbo / R");
    recompinput::set_game_input_description(GameInput::R,
        "The Nintendo 64 R button. With Easy Turbo enabled, this starts Turbo during a race.");
    recompinput::set_game_input_description(GameInput::C_UP,
        "The C Up camera control.");
    recompinput::set_game_input_description(GameInput::C_DOWN,
        "The C Down camera control.");
    recompinput::set_game_input_description(GameInput::C_LEFT,
        "The C Left camera control.");
    recompinput::set_game_input_description(GameInput::C_RIGHT,
        "The C Right camera control.");
    recompinput::set_game_input_description(GameInput::X_AXIS_NEG,
        "Steers left.");
    recompinput::set_game_input_description(GameInput::X_AXIS_POS,
        "Steers right.");
    recompinput::set_game_input_description(GameInput::Y_AXIS_POS,
        "Steers up or forward in menus.");
    recompinput::set_game_input_description(GameInput::Y_AXIS_NEG,
        "Steers down or backward in menus.");
    recompinput::set_game_input_description(GameInput::DPAD_UP,
        "The Nintendo 64 D-Pad Up button.");
    recompinput::set_game_input_description(GameInput::DPAD_DOWN,
        "The Nintendo 64 D-Pad Down button.");
    recompinput::set_game_input_description(GameInput::DPAD_LEFT,
        "The Nintendo 64 D-Pad Left button.");
    recompinput::set_game_input_description(GameInput::DPAD_RIGHT,
        "The Nintendo 64 D-Pad Right button.");
    recompinput::set_game_input_description(GameInput::TOGGLE_MENU,
        "Opens or closes the AeroGauge settings menu.");
    recompinput::set_game_input_description(GameInput::ACCEPT_MENU,
        "Confirms the selected menu action.");
    recompinput::set_game_input_description(GameInput::BACK_MENU,
        "Returns to the previous menu or cancels an action.");
    recompinput::set_game_input_description(GameInput::APPLY_MENU,
        "Applies pending settings changes.");
    recompinput::set_game_input_description(GameInput::TAB_LEFT_MENU,
        "Selects the settings tab to the left.");
    recompinput::set_game_input_description(GameInput::TAB_RIGHT_MENU,
        "Selects the settings tab to the right.");

    recompinput::set_default_mapping_for_keyboard(GameInput::A,
        {InputField::keyboard(SDL_SCANCODE_X)});
    recompinput::set_default_mapping_for_keyboard(GameInput::B,
        {InputField::keyboard(SDL_SCANCODE_C)});
    recompinput::set_default_mapping_for_keyboard(GameInput::Z,
        {InputField::keyboard(SDL_SCANCODE_Z)});
    recompinput::set_default_mapping_for_keyboard(GameInput::L,
        {InputField::keyboard(SDL_SCANCODE_Q)});
    recompinput::set_default_mapping_for_keyboard(GameInput::R,
        {InputField::keyboard(SDL_SCANCODE_E), InputField::keyboard(SDL_SCANCODE_R)});
    recompinput::set_default_mapping_for_keyboard(GameInput::START,
        {InputField::keyboard(SDL_SCANCODE_RETURN)});
    recompinput::set_default_mapping_for_keyboard(GameInput::C_UP,
        {InputField::keyboard(SDL_SCANCODE_I)});
    recompinput::set_default_mapping_for_keyboard(GameInput::C_DOWN,
        {InputField::keyboard(SDL_SCANCODE_K)});
    recompinput::set_default_mapping_for_keyboard(GameInput::C_LEFT,
        {InputField::keyboard(SDL_SCANCODE_J)});
    recompinput::set_default_mapping_for_keyboard(GameInput::C_RIGHT,
        {InputField::keyboard(SDL_SCANCODE_L)});
    recompinput::set_default_mapping_for_keyboard(GameInput::X_AXIS_NEG,
        {InputField::keyboard(SDL_SCANCODE_LEFT), InputField::keyboard(SDL_SCANCODE_A)});
    recompinput::set_default_mapping_for_keyboard(GameInput::X_AXIS_POS,
        {InputField::keyboard(SDL_SCANCODE_RIGHT), InputField::keyboard(SDL_SCANCODE_D)});
    recompinput::set_default_mapping_for_keyboard(GameInput::Y_AXIS_POS,
        {InputField::keyboard(SDL_SCANCODE_UP), InputField::keyboard(SDL_SCANCODE_W)});
    recompinput::set_default_mapping_for_keyboard(GameInput::Y_AXIS_NEG,
        {InputField::keyboard(SDL_SCANCODE_DOWN), InputField::keyboard(SDL_SCANCODE_S)});
    // AeroGauge's keyboard scheme uses these keys for the C buttons; the
    // translated game does not consume a separate keyboard D-pad.
    recompinput::set_default_mapping_for_keyboard(GameInput::DPAD_UP, {});
    recompinput::set_default_mapping_for_keyboard(GameInput::DPAD_DOWN, {});
    recompinput::set_default_mapping_for_keyboard(GameInput::DPAD_LEFT, {});
    recompinput::set_default_mapping_for_keyboard(GameInput::DPAD_RIGHT, {});

    recompinput::set_default_mapping_for_controller(GameInput::A,
        {InputField::controller_digital(SDL_CONTROLLER_BUTTON_A)});
    recompinput::set_default_mapping_for_controller(GameInput::B,
        {InputField::controller_digital(SDL_CONTROLLER_BUTTON_B)});
    recompinput::set_default_mapping_for_controller(GameInput::Z,
        {InputField::controller_analog(SDL_CONTROLLER_AXIS_TRIGGERLEFT)});
    recompinput::set_default_mapping_for_controller(GameInput::L,
        {InputField::controller_digital(SDL_CONTROLLER_BUTTON_LEFTSHOULDER)});
    recompinput::set_default_mapping_for_controller(GameInput::R,
        {InputField::controller_digital(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER),
         InputField::controller_analog(SDL_CONTROLLER_AXIS_TRIGGERRIGHT)});
    recompinput::set_default_mapping_for_controller(GameInput::START,
        {InputField::controller_digital(SDL_CONTROLLER_BUTTON_START)});
    recompinput::set_default_mapping_for_controller(GameInput::C_UP,
        {InputField::controller_analog(SDL_CONTROLLER_AXIS_RIGHTY, false),
         InputField::controller_digital(SDL_CONTROLLER_BUTTON_Y)});
    recompinput::set_default_mapping_for_controller(GameInput::C_DOWN,
        {InputField::controller_analog(SDL_CONTROLLER_AXIS_RIGHTY),
         InputField::controller_digital(SDL_CONTROLLER_BUTTON_RIGHTSTICK)});
    recompinput::set_default_mapping_for_controller(GameInput::C_LEFT,
        {InputField::controller_analog(SDL_CONTROLLER_AXIS_RIGHTX, false),
         InputField::controller_digital(SDL_CONTROLLER_BUTTON_X)});
    recompinput::set_default_mapping_for_controller(GameInput::C_RIGHT,
        {InputField::controller_analog(SDL_CONTROLLER_AXIS_RIGHTX)});
    recompinput::set_default_mapping_for_controller(GameInput::X_AXIS_NEG,
        {InputField::controller_analog(SDL_CONTROLLER_AXIS_LEFTX, false)});
    recompinput::set_default_mapping_for_controller(GameInput::X_AXIS_POS,
        {InputField::controller_analog(SDL_CONTROLLER_AXIS_LEFTX)});
    recompinput::set_default_mapping_for_controller(GameInput::Y_AXIS_POS,
        {InputField::controller_analog(SDL_CONTROLLER_AXIS_LEFTY, false)});
    recompinput::set_default_mapping_for_controller(GameInput::Y_AXIS_NEG,
        {InputField::controller_analog(SDL_CONTROLLER_AXIS_LEFTY)});
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
    recompui::update_game_mod_id(AERO_BRANCH(aero::mods::game_id, aero::mods::japan_game_id));
    configure_controls();
    // The port does not implement all of the shared General page's audio,
    // gyro, and mouse services, so keep that page hidden. Create Graphics
    // before the game-specific Controls page so Controls is the second tab.
    auto& general = settings::create_general_tab({.has_rumble_strength = false,
        .has_gyro_sensitivity = false, .has_mouse_sensitivity = false});
    general.external_storage = true;
    settings::set_tab_visible("general", false);
    auto& graphics = settings::create_graphics_tab();
    settings::create_controls_tab();
    settings::create_mods_tab();
    graphics.external_storage = true;
    graphics.set_load_callback(seed_graphics);
    graphics.set_save_callback(save_graphics);
    graphics.add_bool_option("force_full_lod", "Force Full LOD",
        "Keep cars at maximum model detail and remove their distance cutoff. "
        "The Draw distance setting still controls far clipping. May reduce performance.",
        port::force_full_lod());
    graphics.update_option_disabled("force_full_lod", std::getenv("AERO_FORCE_FULL_LOD") != nullptr);
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
