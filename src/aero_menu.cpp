// RecompFrontend settings-overlay integration.
//
// The SDL/main thread calls attach(), handle_event(), update(), and
// toggle_fullscreen(). RecompFrontend owns the temporary page state; the
// configuration module owns JSON persistence; the renderer and runtime own
// live graphics state.
//
// This is intentionally a narrow integration layer. The shared settings
// overlay is built on the Windows and Linux paths. No disk I/O runs under the
// frontend lock: the configuration layer records changes in memory and a
// background worker coalesces and writes them, so the queued actions drained by
// update() only touch in-memory config state and the SDL window.

#include "aero_menu.h"
#include "aero_config.h"
#include "ui/aero_frontend_settings.h"
#include "recompui/recompui.h"
#include "recompui/config.h"
#include "recompui/program_config.h"
#include "librecomp/game.hpp"
#include "rt64_render_hooks.h"
#include <SDL.h>
#include <atomic>
#include <cstdio>
#include <mutex>
#include <vector>

// RecompFrontend host contract. SDL owns the window; RT64 owns rendering.
SDL_Window* window = nullptr;
std::vector<recomp::GameEntry> supported_games;
void init_hook(plume::RenderInterface*, plume::RenderDevice*);
void draw_hook(plume::RenderCommandList*, plume::RenderFramebuffer*);
void deinit_hook();

namespace aero::menu {
namespace {
std::recursive_mutex frontend_mutex;
std::atomic<bool> capture{false};
bool ready = false;
enum class Request { None, Open, Close };
Request request = Request::None;
std::vector<std::function<void()>> actions;

void initialize(plume::RenderInterface* interface, plume::RenderDevice* device) {
    std::lock_guard lock(frontend_mutex);
    auto& graphics = recompui::config::get_graphics_config();
    const bool samples = device->getCapabilities().sampleLocations;
    graphics.update_option_disabled("msaa_option", !samples);
    if (samples) {
        const auto counts = device->getSampleCountsSupported(plume::RenderFormat::R8G8B8A8_UNORM) &
                            device->getSampleCountsSupported(plume::RenderFormat::D32_FLOAT);
        using AA = ultramodern::renderer::Antialiasing;
        graphics.update_enum_option_disabled("msaa_option", uint32_t(AA::MSAA2X), !(counts & plume::RenderSampleCount::Bits::COUNT_2));
        graphics.update_enum_option_disabled("msaa_option", uint32_t(AA::MSAA4X), !(counts & plume::RenderSampleCount::Bits::COUNT_4));
        graphics.update_enum_option_disabled("msaa_option", uint32_t(AA::MSAA8X), !(counts & plume::RenderSampleCount::Bits::COUNT_8));
    }
    init_hook(interface, device);
    ready = true;
}

void render(plume::RenderCommandList* commands, plume::RenderFramebuffer* framebuffer) {
    std::lock_guard lock(frontend_mutex);
    if (request == Request::Open) {
        recompui::config::open();
        recompui::config::set_tab("graphics");
    } else if (request == Request::Close) {
        // Preserve the Apply/Discard prompt when a confirmation-backed page is dirty.
        if (recompui::config::close()) recompui::hide_all_contexts();
    }
    request = Request::None;
    // This port starts the game directly, with no pre-game launcher.
    if (ultramodern::is_game_started() || capture.load()) draw_hook(commands, framebuffer);
    capture.store(recompui::is_context_capturing_input(), std::memory_order_release);
}

void deinitialize() {
    std::lock_guard lock(frontend_mutex);
    ready = false;
    capture.store(false, std::memory_order_release);
    deinit_hook();
}
}

void enqueue(std::function<void()> action) {
    // Most calls happen while the frontend lock is held by the presentation
    // callback. Keep this safe for callbacks or tests that enqueue elsewhere.
    std::lock_guard lock(frontend_mutex);
    actions.push_back(std::move(action));
}

void update() {
    std::lock_guard lock(frontend_mutex);
    // Persistence, config snapshots and SDL window changes belong to the main
    // thread. Never execute these from RT64's presentation callback.
    auto pending = std::move(actions);
    actions.clear();
    for (auto& action : pending) action();
}

void attach(SDL_Window* value) {
    window = value;
    recompui::programconfig::set_program_name("AeroGauge Recompiled");
    recompui::programconfig::set_program_id(u8"AeroGaugeRecomp");
    recompui::register_primary_font("LatoLatin-Regular.ttf", "LatoLatin");
    recompui::register_extra_font("LatoLatin-Bold.ttf");
    // The stock launcher dereferences supported_games[0]. This direct-boot
    // port only uses the config modal, so it supplies an empty launcher.
    recompui::register_launcher_init_callback([](recompui::LauncherMenu*) {});
    create_settings();
    recompui::config::finalize();
    SDL_DisplayMode display{};
    if (SDL_GetCurrentDisplayMode(SDL_GetWindowDisplayIndex(window), &display) == 0)
        recompui::config::graphics::update_refresh_rate(display.refresh_rate);
    RT64::SetRenderHooks(initialize, render, deinitialize);
    std::fprintf(stderr, "[menu] Settings: Escape / F10 / controller Back\n");
}

bool captures_input() { return capture.load(std::memory_order_acquire); }

bool handle_event(const SDL_Event& event) {
    std::lock_guard lock(frontend_mutex);
    if (event.type == SDL_KEYDOWN && !event.key.repeat &&
        (event.key.keysym.sym == SDLK_F11 ||
         (event.key.keysym.sym == SDLK_RETURN && (event.key.keysym.mod & KMOD_ALT)))) {
        toggle_fullscreen();
        return true;
    }
    const bool toggle = (event.type == SDL_KEYDOWN && !event.key.repeat &&
                         (event.key.keysym.sym == SDLK_ESCAPE || event.key.keysym.sym == SDLK_F10 || event.key.keysym.sym == SDLK_AC_BACK)) ||
                        (event.type == SDL_CONTROLLERBUTTONDOWN && event.cbutton.button == SDL_CONTROLLER_BUTTON_BACK);
    if (toggle && ready) {
        if (captures_input()) request = Request::Close;
        else {
            refresh_settings();
            capture.store(true, std::memory_order_release);
            request = Request::Open;
        }
        return true;
    }
    if (event.type == SDL_DROPFILE || event.type == SDL_DROPTEXT) {
        SDL_free(event.drop.file);
        return true;
    }
    // Hotplug must still reach the game's existing controller owner.
    if (event.type == SDL_CONTROLLERDEVICEADDED || event.type == SDL_CONTROLLERDEVICEREMOVED) {
        recompui::queue_event(event);
        return false;
    }
    if (!ready || !captures_input()) return false;
    // Pass controller events through unchanged. RecompFrontend maps the active
    // controller profile, tracks controller-vs-keyboard focus, and selects the
    // matching button hints. Synthesizing keyboard events would lose that state.
    recompui::queue_event(event);
    return true;
}

void apply_window_settings() {
#if defined(__ANDROID__)
    return; // Android owns the full-screen surface and physical dimensions.
#endif
    if (!window) return;
    const auto cfg = aero::config::current_graphics();
    const bool fullscreen = cfg.wm_option == ultramodern::renderer::WindowMode::Fullscreen;
    if (SDL_SetWindowFullscreen(window, fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0) != 0) {
        std::fprintf(stderr, "[config] fullscreen failed: %s\n", SDL_GetError());
        aero::config::update_saved_window_mode((SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN_DESKTOP)
            ? ultramodern::renderer::WindowMode::Fullscreen : ultramodern::renderer::WindowMode::Windowed);
    }
    const auto size = aero::config::window_size();
    SDL_SetWindowSize(window, size.width, size.height);
}

void toggle_fullscreen() {
    std::lock_guard lock(frontend_mutex);
    if (!window) return;
    const bool fullscreen = (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN_DESKTOP) == 0;
    if (SDL_SetWindowFullscreen(window, fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0) != 0) {
        std::fprintf(stderr, "[config] fullscreen failed: %s\n", SDL_GetError());
        return;
    }
    aero::config::update_saved_window_mode(fullscreen ? ultramodern::renderer::WindowMode::Fullscreen
                                                    : ultramodern::renderer::WindowMode::Windowed);
    refresh_settings();
}
} // namespace aero::menu
