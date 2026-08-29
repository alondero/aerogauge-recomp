#include "aero_menu.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <optional>
#include <string>

#include <SDL.h>

#include "aero_config.h"

#if defined(_WIN32)
#include <SDL_syswm.h>
#include <commdlg.h>
#include <shlobj.h>

namespace {

SDL_Window* g_window = nullptr;
HWND g_hwnd = nullptr;
HMENU g_menu_bar = nullptr;
HMENU g_game_menu = nullptr;
HMENU g_resolution_menu = nullptr;
HMENU g_supersampling_menu = nullptr;
HMENU g_aspect_menu = nullptr;
HMENU g_hud_menu = nullptr;
HMENU g_rate_menu = nullptr;
HMENU g_aa_menu = nullptr;
HMENU g_hpfb_menu = nullptr;
HMENU g_api_menu = nullptr;
HMENU g_window_size_menu = nullptr;
HMENU g_graphics_menu = nullptr;
HMENU g_enhancements_menu = nullptr;
HMENU g_draw_distance_menu = nullptr;

constexpr std::array<int, 7> kManualRefreshRates{30, 60, 90, 120, 144, 165, 240};

enum Command : UINT {
    CMD_FULLSCREEN = 1000,
    CMD_QUIT,

    CMD_RES_AUTO = 1100,
    CMD_RES_ORIGINAL,
    CMD_RES_ORIGINAL_2X,
    CMD_SS_1X,
    CMD_SS_2X,
    CMD_SS_3X,
    CMD_SS_4X,
    CMD_ASPECT_ORIGINAL,
    CMD_ASPECT_EXPAND,
    CMD_HUD_ORIGINAL,
    CMD_HUD_CLAMP_16X9,
    CMD_HUD_FULL,
    CMD_RATE_ORIGINAL,
    CMD_RATE_DISPLAY,
    CMD_RATE_30,
    CMD_RATE_60,
    CMD_RATE_90,
    CMD_RATE_120,
    CMD_RATE_144,
    CMD_RATE_165,
    CMD_RATE_240,
    CMD_AA_NONE,
    CMD_AA_2X,
    CMD_AA_4X,
    CMD_AA_8X,
    CMD_HPFB_AUTO,
    CMD_HPFB_ON,
    CMD_HPFB_OFF,
    CMD_API_AUTO,
    CMD_API_D3D12,
    CMD_API_VULKAN,
    CMD_WINDOW_1280X720,
    CMD_WINDOW_1600X900,
    CMD_WINDOW_1920X1080,

    CMD_DRAW_ORIGINAL = 1200,
    CMD_DRAW_2X,
    CMD_DRAW_10X,
    CMD_DRAW_100X,
    CMD_DRAW_UNLIMITED,
    CMD_TEXTURE_PACK_DIRECTORY,
    CMD_TEXTURE_PACK_ARCHIVE,
    CMD_TEXTURE_PACK_CLEAR,
    CMD_TEXTURE_DUMP_DIRECTORY,
    CMD_TEXTURE_DUMP_CLEAR,
};

void append_item(HMENU menu, UINT id, const char* label) {
    AppendMenuA(menu, MF_STRING, id, label);
}

HMENU append_submenu(HMENU parent, const char* label) {
    HMENU child = CreatePopupMenu();
    AppendMenuA(parent, MF_POPUP, reinterpret_cast<UINT_PTR>(child), label);
    return child;
}

void check(HMENU menu, UINT id, bool checked) {
    CheckMenuItem(menu, id, MF_BYCOMMAND | (checked ? MF_CHECKED : MF_UNCHECKED));
}

void radio(HMENU menu, UINT first, UINT last, UINT selected) {
    CheckMenuRadioItem(menu, first, last, selected, MF_BYCOMMAND);
}

bool close(float a, float b) {
    return std::abs(a - b) < 0.0001f;
}

std::optional<std::string> select_directory(const char* title) {
    BROWSEINFOA dialog{};
    dialog.hwndOwner = g_hwnd;
    dialog.lpszTitle = title;
    dialog.ulFlags = BIF_RETURNONLYFSDIRS;
    PIDLIST_ABSOLUTE item = SHBrowseForFolderA(&dialog);
    if (item == nullptr) return std::nullopt;

    std::array<char, MAX_PATH> path{};
    const bool selected = SHGetPathFromIDListA(item, path.data()) == TRUE;
    CoTaskMemFree(item);
    if (!selected) return std::nullopt;
    return std::string{path.data()};
}

std::optional<std::string> select_texture_archive() {
    std::array<char, 32768> path{};
    OPENFILENAMEA dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = g_hwnd;
    dialog.lpstrFilter = "RT64 texture archives (*.rtz)\0*.rtz\0All files (*.*)\0*.*\0";
    dialog.lpstrFile = path.data();
    dialog.nMaxFile = static_cast<DWORD>(path.size());
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    if (GetOpenFileNameA(&dialog) == FALSE) return std::nullopt;
    return std::string{path.data()};
}

void refresh() {
    using namespace ultramodern::renderer;
    const GraphicsConfig cfg = aero::config::current_graphics();
    check(g_game_menu, CMD_FULLSCREEN, g_window != nullptr &&
          (SDL_GetWindowFlags(g_window) & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0);

    radio(g_resolution_menu, CMD_RES_AUTO, CMD_RES_ORIGINAL_2X,
          cfg.res_option == Resolution::Auto ? CMD_RES_AUTO :
          cfg.res_option == Resolution::Original2x ? CMD_RES_ORIGINAL_2X : CMD_RES_ORIGINAL);
    radio(g_supersampling_menu, CMD_SS_1X, CMD_SS_4X,
          cfg.ds_option >= 1 && cfg.ds_option <= 4 ? CMD_SS_1X + cfg.ds_option - 1 : 0);
    radio(g_aspect_menu, CMD_ASPECT_ORIGINAL, CMD_ASPECT_EXPAND,
          cfg.ar_option == AspectRatio::Expand ? CMD_ASPECT_EXPAND :
          cfg.ar_option == AspectRatio::Original ? CMD_ASPECT_ORIGINAL : 0);
    radio(g_hud_menu, CMD_HUD_ORIGINAL, CMD_HUD_FULL,
          cfg.hr_option == HUDRatioMode::Full ? CMD_HUD_FULL :
          cfg.hr_option == HUDRatioMode::Clamp16x9 ? CMD_HUD_CLAMP_16X9 : CMD_HUD_ORIGINAL);

    UINT rate = CMD_RATE_ORIGINAL;
    if (cfg.rr_option == RefreshRate::Display) rate = CMD_RATE_DISPLAY;
    else if (cfg.rr_option == RefreshRate::Manual) {
        const auto it = std::find(kManualRefreshRates.begin(), kManualRefreshRates.end(),
                                  cfg.rr_manual_value);
        rate = it == kManualRefreshRates.end() ? 0 :
               CMD_RATE_30 + UINT(it - kManualRefreshRates.begin());
    }
    radio(g_rate_menu, CMD_RATE_ORIGINAL, CMD_RATE_240, rate);
    radio(g_aa_menu, CMD_AA_NONE, CMD_AA_8X,
          cfg.msaa_option == Antialiasing::MSAA8X ? CMD_AA_8X :
          cfg.msaa_option == Antialiasing::MSAA4X ? CMD_AA_4X :
          cfg.msaa_option == Antialiasing::MSAA2X ? CMD_AA_2X : CMD_AA_NONE);
    radio(g_hpfb_menu, CMD_HPFB_AUTO, CMD_HPFB_OFF,
          cfg.hpfb_option == HighPrecisionFramebuffer::On ? CMD_HPFB_ON :
          cfg.hpfb_option == HighPrecisionFramebuffer::Off ? CMD_HPFB_OFF : CMD_HPFB_AUTO);
    radio(g_api_menu, CMD_API_AUTO, CMD_API_VULKAN,
          cfg.api_option == GraphicsApi::D3D12 ? CMD_API_D3D12 :
          cfg.api_option == GraphicsApi::Vulkan ? CMD_API_VULKAN :
          cfg.api_option == GraphicsApi::Auto ? CMD_API_AUTO : 0);
    const auto window_size = aero::config::window_size();
    radio(g_window_size_menu, CMD_WINDOW_1280X720, CMD_WINDOW_1920X1080,
          window_size.width == 1920 && window_size.height == 1080 ? CMD_WINDOW_1920X1080 :
          window_size.width == 1600 && window_size.height == 900 ? CMD_WINDOW_1600X900 :
          window_size.width == 1280 && window_size.height == 720 ? CMD_WINDOW_1280X720 : 0);

    const float draw_distance = aero::config::draw_distance_scale();
    radio(g_draw_distance_menu, CMD_DRAW_ORIGINAL, CMD_DRAW_UNLIMITED,
          draw_distance == 0.0f ? CMD_DRAW_UNLIMITED : close(draw_distance, 100.0f) ? CMD_DRAW_100X :
          close(draw_distance, 10.0f) ? CMD_DRAW_10X : close(draw_distance, 2.0f) ? CMD_DRAW_2X :
          close(draw_distance, 1.0f) ? CMD_DRAW_ORIGINAL : 0);
    if (g_hwnd != nullptr) DrawMenuBar(g_hwnd);
}

void apply_graphics_command(UINT command) {
    using namespace ultramodern::renderer;
    GraphicsConfig cfg = aero::config::current_graphics();
    switch (command) {
        case CMD_RES_AUTO: cfg.res_option = Resolution::Auto; break;
        case CMD_RES_ORIGINAL: cfg.res_option = Resolution::Original; break;
        case CMD_RES_ORIGINAL_2X: cfg.res_option = Resolution::Original2x; break;
        case CMD_SS_1X: case CMD_SS_2X: case CMD_SS_3X: case CMD_SS_4X:
            cfg.ds_option = int(command - CMD_SS_1X) + 1; break;
        case CMD_ASPECT_ORIGINAL: cfg.ar_option = AspectRatio::Original; break;
        case CMD_ASPECT_EXPAND: cfg.ar_option = AspectRatio::Expand; break;
        case CMD_HUD_ORIGINAL: cfg.hr_option = HUDRatioMode::Original; break;
        case CMD_HUD_CLAMP_16X9: cfg.hr_option = HUDRatioMode::Clamp16x9; break;
        case CMD_HUD_FULL: cfg.hr_option = HUDRatioMode::Full; break;
        case CMD_RATE_ORIGINAL: cfg.rr_option = RefreshRate::Original; break;
        case CMD_RATE_DISPLAY: cfg.rr_option = RefreshRate::Display; break;
        case CMD_RATE_30: case CMD_RATE_60: case CMD_RATE_90: case CMD_RATE_120:
        case CMD_RATE_144: case CMD_RATE_165: case CMD_RATE_240: {
            cfg.rr_option = RefreshRate::Manual;
            cfg.rr_manual_value = kManualRefreshRates[command - CMD_RATE_30];
            break;
        }
        case CMD_AA_NONE: cfg.msaa_option = Antialiasing::None; break;
        case CMD_AA_2X: cfg.msaa_option = Antialiasing::MSAA2X; break;
        case CMD_AA_4X: cfg.msaa_option = Antialiasing::MSAA4X; break;
        case CMD_AA_8X: cfg.msaa_option = Antialiasing::MSAA8X; break;
        case CMD_HPFB_AUTO: cfg.hpfb_option = HighPrecisionFramebuffer::Auto; break;
        case CMD_HPFB_ON: cfg.hpfb_option = HighPrecisionFramebuffer::On; break;
        case CMD_HPFB_OFF: cfg.hpfb_option = HighPrecisionFramebuffer::Off; break;
        // RT64 selects its backend while constructing the renderer. Save the
        // selection now; it takes effect on the next launch.
        case CMD_API_AUTO: cfg.api_option = GraphicsApi::Auto; break;
        case CMD_API_D3D12: cfg.api_option = GraphicsApi::D3D12; break;
        case CMD_API_VULKAN: cfg.api_option = GraphicsApi::Vulkan; break;
        default: return;
    }
    aero::config::apply_graphics(cfg);
}

void dispatch(UINT command) {
    switch (command) {
        case CMD_FULLSCREEN: aero::menu::toggle_fullscreen(); break;
        case CMD_QUIT: {
            SDL_Event quit{};
            quit.type = SDL_QUIT;
            SDL_PushEvent(&quit);
            break;
        }
        case CMD_DRAW_ORIGINAL: aero::config::set_draw_distance_scale(1.0f); break;
        case CMD_DRAW_2X: aero::config::set_draw_distance_scale(2.0f); break;
        case CMD_DRAW_10X: aero::config::set_draw_distance_scale(10.0f); break;
        case CMD_DRAW_100X: aero::config::set_draw_distance_scale(100.0f); break;
        case CMD_DRAW_UNLIMITED: aero::config::set_draw_distance_scale(0.0f); break;
        case CMD_WINDOW_1280X720:
            aero::config::set_window_size({1280, 720});
            SDL_SetWindowSize(g_window, 1280, 720);
            break;
        case CMD_WINDOW_1600X900:
            aero::config::set_window_size({1600, 900});
            SDL_SetWindowSize(g_window, 1600, 900);
            break;
        case CMD_WINDOW_1920X1080:
            aero::config::set_window_size({1920, 1080});
            SDL_SetWindowSize(g_window, 1920, 1080);
            break;
        case CMD_TEXTURE_PACK_DIRECTORY: {
            const auto path = select_directory("Select RT64 texture-pack directory");
            if (path.has_value()) aero::config::set_texture_pack_path(*path);
            break;
        }
        case CMD_TEXTURE_PACK_ARCHIVE: {
            const auto path = select_texture_archive();
            if (path.has_value()) aero::config::set_texture_pack_path(*path);
            break;
        }
        case CMD_TEXTURE_PACK_CLEAR: aero::config::set_texture_pack_path({}); break;
        case CMD_TEXTURE_DUMP_DIRECTORY: {
            const auto path = select_directory("Select RT64 texture-dump directory");
            if (path.has_value()) aero::config::set_texture_dump_dir(*path);
            break;
        }
        case CMD_TEXTURE_DUMP_CLEAR: aero::config::set_texture_dump_dir({}); break;
        default: apply_graphics_command(command); break;
    }
    refresh();
}

} // anonymous namespace

namespace aero::menu {

void attach(SDL_Window* window) {
    g_window = window;
    SDL_SysWMinfo info{};
    SDL_VERSION(&info.version);
    if (SDL_GetWindowWMInfo(window, &info) != SDL_TRUE) return;
    g_hwnd = info.info.win.window;

    g_menu_bar = CreateMenu();
    HMENU game = g_game_menu = append_submenu(g_menu_bar, "&Game");
    append_item(game, CMD_FULLSCREEN, "&Fullscreen\tF11");
    AppendMenuA(game, MF_SEPARATOR, 0, nullptr);
    append_item(game, CMD_QUIT, "E&xit");

    HMENU graphics = g_graphics_menu = append_submenu(g_menu_bar, "&Graphics");
    HMENU resolution = g_resolution_menu = append_submenu(graphics, "Internal resolution");
    append_item(resolution, CMD_RES_AUTO, "Automatic (window scale)");
    append_item(resolution, CMD_RES_ORIGINAL, "Original");
    append_item(resolution, CMD_RES_ORIGINAL_2X, "Original 2x");
    HMENU supersampling = g_supersampling_menu = append_submenu(graphics, "Supersampling");
    append_item(supersampling, CMD_SS_1X, "1x"); append_item(supersampling, CMD_SS_2X, "2x");
    append_item(supersampling, CMD_SS_3X, "3x"); append_item(supersampling, CMD_SS_4X, "4x");
    HMENU aspect = g_aspect_menu = append_submenu(graphics, "Aspect ratio");
    append_item(aspect, CMD_ASPECT_ORIGINAL, "Original (4:3)");
    append_item(aspect, CMD_ASPECT_EXPAND, "Expand (widescreen)");
    HMENU hud = g_hud_menu = append_submenu(graphics, "HUD placement");
    append_item(hud, CMD_HUD_ORIGINAL, "Original (4:3)");
    append_item(hud, CMD_HUD_CLAMP_16X9, "Clamp to 16:9");
    append_item(hud, CMD_HUD_FULL, "Full width");
    HMENU rate = g_rate_menu = append_submenu(graphics, "Presentation rate");
    append_item(rate, CMD_RATE_ORIGINAL, "Original");
    append_item(rate, CMD_RATE_DISPLAY, "Display refresh rate");
    AppendMenuA(rate, MF_SEPARATOR, 0, nullptr);
    append_item(rate, CMD_RATE_30, "30 Hz"); append_item(rate, CMD_RATE_60, "60 Hz");
    append_item(rate, CMD_RATE_90, "90 Hz"); append_item(rate, CMD_RATE_120, "120 Hz");
    append_item(rate, CMD_RATE_144, "144 Hz"); append_item(rate, CMD_RATE_165, "165 Hz");
    append_item(rate, CMD_RATE_240, "240 Hz");
    HMENU aa = g_aa_menu = append_submenu(graphics, "Anti-aliasing");
    append_item(aa, CMD_AA_NONE, "Off"); append_item(aa, CMD_AA_2X, "MSAA 2x");
    append_item(aa, CMD_AA_4X, "MSAA 4x"); append_item(aa, CMD_AA_8X, "MSAA 8x");
    HMENU hpfb = g_hpfb_menu = append_submenu(graphics, "High-precision framebuffer");
    append_item(hpfb, CMD_HPFB_AUTO, "Automatic"); append_item(hpfb, CMD_HPFB_ON, "On");
    append_item(hpfb, CMD_HPFB_OFF, "Off");
    HMENU api = g_api_menu = append_submenu(graphics, "Graphics API (restart required)");
    append_item(api, CMD_API_AUTO, "Automatic"); append_item(api, CMD_API_D3D12, "Direct3D 12");
    append_item(api, CMD_API_VULKAN, "Vulkan");
    HMENU window_size = g_window_size_menu = append_submenu(graphics, "Window size");
    append_item(window_size, CMD_WINDOW_1280X720, "1280 x 720");
    append_item(window_size, CMD_WINDOW_1600X900, "1600 x 900");
    append_item(window_size, CMD_WINDOW_1920X1080, "1920 x 1080");

    HMENU enhancements = g_enhancements_menu = append_submenu(g_menu_bar, "&Enhancements");
    HMENU draw_distance = g_draw_distance_menu = append_submenu(enhancements, "Draw distance");
    append_item(draw_distance, CMD_DRAW_ORIGINAL, "Original (1x)");
    append_item(draw_distance, CMD_DRAW_2X, "2x");
    append_item(draw_distance, CMD_DRAW_10X, "10x");
    append_item(draw_distance, CMD_DRAW_100X, "100x");
    append_item(draw_distance, CMD_DRAW_UNLIMITED, "Unlimited");
    HMENU texture_pack = append_submenu(enhancements, "Texture pack (restart required)");
    append_item(texture_pack, CMD_TEXTURE_PACK_DIRECTORY, "Choose directory...");
    append_item(texture_pack, CMD_TEXTURE_PACK_ARCHIVE, "Choose .rtz archive...");
    append_item(texture_pack, CMD_TEXTURE_PACK_CLEAR, "Clear selection");
    HMENU texture_dump = append_submenu(enhancements, "Texture dump (restart required)");
    append_item(texture_dump, CMD_TEXTURE_DUMP_DIRECTORY, "Choose output directory...");
    append_item(texture_dump, CMD_TEXTURE_DUMP_CLEAR, "Disable texture dump");

    if ((SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN_DESKTOP) == 0) {
        SetMenu(g_hwnd, g_menu_bar);
    }
    SDL_EventState(SDL_SYSWMEVENT, SDL_ENABLE);
    refresh();
}

bool handle_event(const SDL_Event& event) {
    if (event.type != SDL_SYSWMEVENT || event.syswm.msg == nullptr) return false;
    const SDL_SysWMmsg& msg = *event.syswm.msg;
    if (msg.subsystem != SDL_SYSWM_WINDOWS || msg.msg.win.msg != WM_COMMAND) return false;
    dispatch(LOWORD(msg.msg.win.wParam));
    return true;
}

void toggle_fullscreen() {
    if (g_window == nullptr) return;
    const bool fullscreen = (SDL_GetWindowFlags(g_window) & SDL_WINDOW_FULLSCREEN_DESKTOP) == 0;
    if (SDL_SetWindowFullscreen(g_window, fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0) != 0) {
        std::fprintf(stderr, "[config] fullscreen toggle FAILED: %s\n", SDL_GetError());
        return;
    }
    SetMenu(g_hwnd, fullscreen ? nullptr : g_menu_bar);
    aero::config::update_saved_window_mode(
        fullscreen ? ultramodern::renderer::WindowMode::Fullscreen
                   : ultramodern::renderer::WindowMode::Windowed);
    std::fprintf(stderr, "[config] fullscreen %s (menu / F11 / Alt+Enter)\n",
                 fullscreen ? "ON" : "OFF");
    refresh();
}

} // namespace aero::menu

#else

namespace {
SDL_Window* g_window = nullptr;
}

namespace aero::menu {

void attach(SDL_Window* window) { g_window = window; }
bool handle_event(const SDL_Event&) { return false; }

void toggle_fullscreen() {
    if (g_window == nullptr) return;
    const bool fullscreen = (SDL_GetWindowFlags(g_window) & SDL_WINDOW_FULLSCREEN_DESKTOP) == 0;
    if (SDL_SetWindowFullscreen(g_window, fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0) != 0) {
        std::fprintf(stderr, "[config] fullscreen toggle FAILED: %s\n", SDL_GetError());
        return;
    }
    aero::config::update_saved_window_mode(
        fullscreen ? ultramodern::renderer::WindowMode::Fullscreen
                   : ultramodern::renderer::WindowMode::Windowed);
    std::fprintf(stderr, "[config] fullscreen %s (F11 / Alt+Enter)\n", fullscreen ? "ON" : "OFF");
}

} // namespace aero::menu

#endif
