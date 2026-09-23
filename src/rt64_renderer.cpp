// RT64 renderer context for the normal player path.
//
// The ultramodern graphics thread owns the task boundary. This module owns
// the RT64 application setup, the SDL window-handle bridge, the settings
// mapping, texture replacement paths, and the task-to-display-list call.
// RT64 owns display-list interpretation and presentation. AERO_HEADLESS is
// handled by stub_renderer.cpp instead.
//
// The live application pointer is published atomically because the game HUD
// hook reads the current output aspect from the game thread. It is cleared
// before the RT64 object is destroyed. Guest task addresses are masked and
// interpreted as N64 addresses; they are not host pointers. See
// docs/reference/renderer.md for the upstream/local boundary.

#define HLSL_CPU
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>

#include "hle/rt64_application.h"
#include "hle/rt64_state.h"
#include "render/rt64_texture_cache.h"

#include "ultramodern/ultramodern.hpp"
#include "ultramodern/config.hpp"
#include "ultramodern/renderer_context.hpp"

#include "aero_rt64.h"
#include "aero_config.h"
#include "aero_hud_widescreen.h"
#include "aero_paths.h"
#if defined(__ANDROID__)
#include "android/aero_android.h"
#endif


namespace {

// RT64 needs RSP DMEM/IMEM and MI/DPC register storage for HLE setup. These
// arrays are backing storage for the renderer context, not guest RDRAM.
uint8_t DMEM[0x1000];
uint8_t IMEM[0x1000];

uint32_t MI_INTR_REG = 0;
uint32_t DPC_START_REG = 0;
uint32_t DPC_END_REG = 0;
uint32_t DPC_CURRENT_REG = 0;
uint32_t DPC_STATUS_REG = 0;
uint32_t DPC_CLOCK_REG = 0;
uint32_t DPC_BUFBUSY_REG = 0;
uint32_t DPC_PIPEBUSY_REG = 0;
uint32_t DPC_TMEM_REG = 0;

void dummy_check_interrupts() {}

// The USA race DL draws eight craft shadow quads between the depth-writing
// course and the car meshes. Its shadow setup disables both G_ZBUFFER and
// Z_CMP, so shadows behind tunnel walls blend over those walls. On the graphics
// thread, rewrite only this setup in the game's per-frame root DL before RT64
// reads it. RDRAM holds host-order 32-bit DL words at an 8-byte-aligned task
// address; the scan stays within 8 MiB and fails closed if the pattern changes.
// A named game-source patch can replace this bridge once the shadow DL builder
// is identified. Later 2D HUD passes use the same mode and must stay untouched.
void enable_racer_shadow_depth(uint8_t* rdram, const OSTask* task) {
    if (rdram == nullptr || task == nullptr) return;
    uint32_t phys = (uint32_t)task->t.data_ptr & 0x3FFFFFFu;
    if ((phys & 7u) != 0) return;
    constexpr uint32_t kRdramBytes = 0x00800000u;
    constexpr uint32_t kSetRenderMode = 0xB900031Du;
    constexpr uint32_t kCourseOpaque = 0xC8112078u;
    constexpr uint32_t kCourseEdge = 0xC8110038u;
    constexpr uint32_t kCarMode = 0x00552078u;
    constexpr uint32_t kShadowMode = 0x00504240u;
    bool saw_depth_course = false;
    for (uint32_t cmd = 0; cmd < 200000 && phys + 8 <= kRdramBytes; ++cmd, phys += 8) {
        uint32_t w0 = *(const uint32_t*)(rdram + phys);
        uint32_t w1 = *(const uint32_t*)(rdram + phys + 4);
        if ((w0 >> 24) == 0xB8u) return; // G_ENDDL
        if (w0 != kSetRenderMode) continue;
        if (w1 == kCourseOpaque || w1 == kCourseEdge) saw_depth_course = true;
        if (w1 == kCarMode) return; // later 2D passes are not shadows
        if (!saw_depth_course || w1 != kShadowMode || phys < 16) continue;

        // Match the race shadow setup exactly before changing guest RDRAM.
        uint32_t* geom = (uint32_t*)(rdram + phys - 16);
        uint32_t* other_hi = (uint32_t*)(rdram + phys - 8);
        if (geom[0] != 0xB7000000u || geom[1] != 0x00002000u ||
            other_hi[0] != 0xBA001402u || other_hi[1] != 0) continue;
        geom[1] |= 0x00000001u; // G_ZBUFFER
        // Projected racer shadows are coplanar with the course. RT64 maps an
        // ordinary Z_CMP to strict LESS, which can flicker on equal-depth road
        // pixels. ZMODE_DEC uses RT64's coplanar-depth tolerance instead.
        // A nearer wall still fails that depth match; no depth writes are needed.
        *(uint32_t*)(rdram + phys + 4) |= 0xC10u; // ZMODE_DEC + Z_CMP; keep Z_UPD off
        return;
    }
}

// Live swapchain handle for the widescreen HUD rect-aspect helper. The
// game-space 2D HUD geometry shifts key off the effective rect-pin aspect, which depends
// on the live output size and hr_option -- see aero_ws_get_hud_rect_aspect_bits() below.
// Sky backdrop handling is owned by RT64's local patch; this pointer is only
// used for the HUD aspect calculation.
//
// Written on the gfx thread (RT64Context ctor/dtor), read every frame on the CPU/
// game-logic thread inside aero_ws_get_hud_rect_aspect_bits() below -- unlike
// get_resolution_scale() elsewhere in this file, which is only ever called from the
// gfx thread itself. atomic (not a plain pointer) so the CPU thread can't observe a
// torn or stale value; the dtor nulls it before `app` is torn down so a load racing
// shutdown sees either the fully-live pointer or nullptr, never a dangling one.
std::atomic<RT64::Application*> g_aero_active_app{nullptr};

RT64::UserConfiguration::AspectRatio to_rt64(ultramodern::renderer::AspectRatio option) {
    switch (option) {
        case ultramodern::renderer::AspectRatio::Original:    return RT64::UserConfiguration::AspectRatio::Original;
        case ultramodern::renderer::AspectRatio::Expand:      return RT64::UserConfiguration::AspectRatio::Expand;
        case ultramodern::renderer::AspectRatio::Manual:      return RT64::UserConfiguration::AspectRatio::Manual;
        case ultramodern::renderer::AspectRatio::OptionCount: return RT64::UserConfiguration::AspectRatio::OptionCount;
    }
    return RT64::UserConfiguration::AspectRatio::Original;
}

RT64::UserConfiguration::Antialiasing to_rt64(ultramodern::renderer::Antialiasing option) {
    switch (option) {
        case ultramodern::renderer::Antialiasing::None:        return RT64::UserConfiguration::Antialiasing::None;
        case ultramodern::renderer::Antialiasing::MSAA2X:      return RT64::UserConfiguration::Antialiasing::MSAA2X;
        case ultramodern::renderer::Antialiasing::MSAA4X:      return RT64::UserConfiguration::Antialiasing::MSAA4X;
        case ultramodern::renderer::Antialiasing::MSAA8X:      return RT64::UserConfiguration::Antialiasing::MSAA8X;
        case ultramodern::renderer::Antialiasing::OptionCount: return RT64::UserConfiguration::Antialiasing::OptionCount;
    }
    return RT64::UserConfiguration::Antialiasing::None;
}

RT64::UserConfiguration::RefreshRate to_rt64(ultramodern::renderer::RefreshRate option) {
    switch (option) {
        case ultramodern::renderer::RefreshRate::Original:    return RT64::UserConfiguration::RefreshRate::Original;
        case ultramodern::renderer::RefreshRate::Display:     return RT64::UserConfiguration::RefreshRate::Display;
        case ultramodern::renderer::RefreshRate::Manual:      return RT64::UserConfiguration::RefreshRate::Manual;
        case ultramodern::renderer::RefreshRate::OptionCount: return RT64::UserConfiguration::RefreshRate::OptionCount;
    }
    return RT64::UserConfiguration::RefreshRate::Original;
}

RT64::UserConfiguration::InternalColorFormat to_rt64(ultramodern::renderer::HighPrecisionFramebuffer option) {
    switch (option) {
        case ultramodern::renderer::HighPrecisionFramebuffer::Off:         return RT64::UserConfiguration::InternalColorFormat::Standard;
        case ultramodern::renderer::HighPrecisionFramebuffer::On:          return RT64::UserConfiguration::InternalColorFormat::High;
        case ultramodern::renderer::HighPrecisionFramebuffer::Auto:        return RT64::UserConfiguration::InternalColorFormat::Automatic;
        case ultramodern::renderer::HighPrecisionFramebuffer::OptionCount: return RT64::UserConfiguration::InternalColorFormat::OptionCount;
    }
    return RT64::UserConfiguration::InternalColorFormat::Automatic;
}

void set_application_user_config(RT64::Application* application, const ultramodern::renderer::GraphicsConfig& config) {
    switch (config.res_option) {
        default:
        case ultramodern::renderer::Resolution::Auto:
            application->userConfig.resolution = RT64::UserConfiguration::Resolution::WindowIntegerScale;
            application->userConfig.downsampleMultiplier = 1;
            break;
        case ultramodern::renderer::Resolution::Original:
            application->userConfig.resolution = RT64::UserConfiguration::Resolution::Manual;
            application->userConfig.resolutionMultiplier = std::max(config.ds_option, 1);
            application->userConfig.downsampleMultiplier = std::max(config.ds_option, 1);
            break;
        case ultramodern::renderer::Resolution::Original2x:
            application->userConfig.resolution = RT64::UserConfiguration::Resolution::Manual;
            application->userConfig.resolutionMultiplier = 2.0 * std::max(config.ds_option, 1);
            application->userConfig.downsampleMultiplier = std::max(config.ds_option, 1);
            break;
    }

    switch (config.hr_option) {
        default:
        case ultramodern::renderer::HUDRatioMode::Original:
            application->userConfig.extAspectRatio = RT64::UserConfiguration::AspectRatio::Original;
            break;
        case ultramodern::renderer::HUDRatioMode::Clamp16x9:
            application->userConfig.extAspectRatio = RT64::UserConfiguration::AspectRatio::Manual;
            application->userConfig.extAspectTarget = 16.0 / 9.0;
            break;
        case ultramodern::renderer::HUDRatioMode::Full:
            application->userConfig.extAspectRatio = RT64::UserConfiguration::AspectRatio::Expand;
            break;
    }

    application->userConfig.aspectRatio = to_rt64(config.ar_option);
    application->userConfig.antialiasing = to_rt64(config.msaa_option);
    application->userConfig.refreshRate = to_rt64(config.rr_option);
    application->userConfig.refreshRateTarget = config.rr_manual_value;
    application->userConfig.internalColorFormat = to_rt64(config.hpfb_option);
    application->userConfig.displayBuffering = RT64::UserConfiguration::DisplayBuffering::Triple;
}

ultramodern::renderer::SetupResult map_setup_result(RT64::Application::SetupResult rt64_result) {
    switch (rt64_result) {
        case RT64::Application::SetupResult::Success:                  return ultramodern::renderer::SetupResult::Success;
        case RT64::Application::SetupResult::DynamicLibrariesNotFound: return ultramodern::renderer::SetupResult::DynamicLibrariesNotFound;
        case RT64::Application::SetupResult::InvalidGraphicsAPI:       return ultramodern::renderer::SetupResult::InvalidGraphicsAPI;
        case RT64::Application::SetupResult::GraphicsAPINotFound:      return ultramodern::renderer::SetupResult::GraphicsAPINotFound;
        case RT64::Application::SetupResult::GraphicsDeviceNotFound:   return ultramodern::renderer::SetupResult::GraphicsDeviceNotFound;
    }
    return ultramodern::renderer::SetupResult::GraphicsDeviceNotFound;
}

ultramodern::renderer::GraphicsApi map_graphics_api(RT64::UserConfiguration::GraphicsAPI api) {
    switch (api) {
        case RT64::UserConfiguration::GraphicsAPI::D3D12:     return ultramodern::renderer::GraphicsApi::D3D12;
        case RT64::UserConfiguration::GraphicsAPI::Vulkan:    return ultramodern::renderer::GraphicsApi::Vulkan;
        case RT64::UserConfiguration::GraphicsAPI::Metal:     return ultramodern::renderer::GraphicsApi::Metal;
        case RT64::UserConfiguration::GraphicsAPI::Automatic: return ultramodern::renderer::GraphicsApi::Auto;
    }
    return ultramodern::renderer::GraphicsApi::Auto;
}

class RT64Context final : public ultramodern::renderer::RendererContext {
public:
    RT64Context(uint8_t* rdram, ultramodern::renderer::WindowHandle window_handle, bool debug) {
        static unsigned char dummy_rom_header[0x40];

        // Wire the RT64 application core to the runtime's state.
        RT64::Application::Core appCore{};
#if defined(_WIN32)
        appCore.window = window_handle.window;
#elif defined(__linux__) || defined(__ANDROID__)
        appCore.window = window_handle;
#elif defined(__APPLE__)
        appCore.window.window = window_handle.window;
        appCore.window.view = window_handle.view;
#endif

        appCore.checkInterrupts = dummy_check_interrupts;

        appCore.HEADER = dummy_rom_header;
        appCore.RDRAM = rdram; // N64Recomp/mupen byte-order convention -- RT64's native diet
        appCore.DMEM = DMEM;
        appCore.IMEM = IMEM;

        appCore.MI_INTR_REG = &MI_INTR_REG;

        appCore.DPC_START_REG = &DPC_START_REG;
        appCore.DPC_END_REG = &DPC_END_REG;
        appCore.DPC_CURRENT_REG = &DPC_CURRENT_REG;
        appCore.DPC_STATUS_REG = &DPC_STATUS_REG;
        appCore.DPC_CLOCK_REG = &DPC_CLOCK_REG;
        appCore.DPC_BUFBUSY_REG = &DPC_BUFBUSY_REG;
        appCore.DPC_PIPEBUSY_REG = &DPC_PIPEBUSY_REG;
        appCore.DPC_TMEM_REG = &DPC_TMEM_REG;

        // VI registers come from ultramodern's live VI state (the game's osViSwapBuffer
        // path writes these), so RT64 can locate the color framebuffer origin.
        ultramodern::renderer::ViRegs* vi_regs = ultramodern::renderer::get_vi_regs();
        appCore.VI_STATUS_REG = &vi_regs->VI_STATUS_REG;
        appCore.VI_ORIGIN_REG = &vi_regs->VI_ORIGIN_REG;
        appCore.VI_WIDTH_REG = &vi_regs->VI_WIDTH_REG;
        appCore.VI_INTR_REG = &vi_regs->VI_INTR_REG;
        appCore.VI_V_CURRENT_LINE_REG = &vi_regs->VI_V_CURRENT_LINE_REG;
        appCore.VI_TIMING_REG = &vi_regs->VI_TIMING_REG;
        appCore.VI_V_SYNC_REG = &vi_regs->VI_V_SYNC_REG;
        appCore.VI_H_SYNC_REG = &vi_regs->VI_H_SYNC_REG;
        appCore.VI_LEAP_REG = &vi_regs->VI_LEAP_REG;
        appCore.VI_H_START_REG = &vi_regs->VI_H_START_REG;
        appCore.VI_V_START_REG = &vi_regs->VI_V_START_REG;
        appCore.VI_V_BURST_REG = &vi_regs->VI_V_BURST_REG;
        appCore.VI_X_SCALE_REG = &vi_regs->VI_X_SCALE_REG;
        appCore.VI_Y_SCALE_REG = &vi_regs->VI_Y_SCALE_REG;

        RT64::ApplicationConfiguration appConfig;
        // Share AeroGaugeRecomp with graphics.json / saves. RT64's default
        // detectDataPath(appId) would create a second folder (Windows:
        // %LOCALAPPDATA%\aerogauge-recomp; Linux: ~/.config/.aerogauge-recomp).
        const auto storage = aero::paths::renderer_storage();
        appConfig.appId = storage.app_id;
        appConfig.detectDataPath = storage.detect_data_path;
        appConfig.dataPath = storage.data_path;
        appConfig.useConfigurationFile = false;

        app = std::make_unique<RT64::Application>(appCore, appConfig);

        auto cur_config = ultramodern::renderer::get_graphics_config();
        set_application_user_config(app.get(), cur_config);
        app->userConfig.developerMode = debug;
        // Keep display-list branch behavior stable for the current texture path.
        app->enhancementConfig.f3dex.forceBranch = true;
        // Scale LODs based on the output resolution.
        app->enhancementConfig.textureLOD.scale = true;

        switch (cur_config.api_option) {
            case ultramodern::renderer::GraphicsApi::D3D12:  app->userConfig.graphicsAPI = RT64::UserConfiguration::GraphicsAPI::D3D12; break;
            case ultramodern::renderer::GraphicsApi::Vulkan: app->userConfig.graphicsAPI = RT64::UserConfiguration::GraphicsAPI::Vulkan; break;
            case ultramodern::renderer::GraphicsApi::Metal:  app->userConfig.graphicsAPI = RT64::UserConfiguration::GraphicsAPI::Metal; break;
            default:                                         app->userConfig.graphicsAPI = RT64::UserConfiguration::GraphicsAPI::Automatic; break;
        }

        uint32_t thread_id = 0;
#ifdef _WIN32
        thread_id = window_handle.thread_id;
#endif
        setup_result = map_setup_result(app->setup(thread_id));
        chosen_api = map_graphics_api(app->chosenGraphicsAPI);
        if (setup_result != ultramodern::renderer::SetupResult::Success) {
            std::fprintf(stderr, "[rt64] RT64::Application::setup FAILED (SetupResult=%d)\n",
                         (int)setup_result);
#if defined(__ANDROID__)
            // RT64's partially initialized teardown is not safe on every driver.
            // Report the failure before destroying the incomplete application.
            aero::android::startup_error("This GPU driver cannot start AeroGauge. On Adreno devices, import a compatible Mesa Turnip ZIP in Graphics & storage. You can also select the system driver or export diagnostics below.");
#endif
            app = nullptr;
            return;
        }

        std::fprintf(stderr, "[rt64] RT64 renderer initialised (api=%d)\n", (int)chosen_api);

        // Publish the app pointer for the widescreen HUD rect-aspect helper
        // (aero_ws_get_hud_rect_aspect_bits reads this on the game-logic thread),
        // then wire texture replacement. Order matters: register first so the
        // dtor's reverse-order unpublish still fires while `app` is alive.
        g_aero_active_app = app.get();

        // Texture replacement wiring. RT64 owns the replacement machinery;
        // dump/hash/replace machinery; the port just points it at directories. Both
        // are opt-in (empty path = off) and independent of developerMode, so an
        // end-user pack loads without the F1 developer overlay.
        const std::string dump_dir = aero::config::texture_dump_dir();
        if (!dump_dir.empty()) {
            // Setting this non-empty makes TextureManager::dumpTexture write every
            // uploaded texture (raw TMEM + RDRAM + tile JSON) to the directory.
            app->state->dumpingTexturesDirectory = std::filesystem::path(dump_dir);
            std::fprintf(stderr, "[rt64] texture dump enabled -> %s\n", dump_dir.c_str());
        }

        const std::string pack = aero::config::texture_pack_path();
        if (!pack.empty()) {
            const bool ok = app->textureCache->loadReplacementDirectory(
                RT64::ReplacementDirectory(std::filesystem::path(pack)));
            std::fprintf(stderr, "[rt64] texture pack %s: %s\n",
                         ok ? "loaded" : "FAILED to load", pack.c_str());
        }
    }

    ~RT64Context() override {
        if (g_aero_active_app == app.get()) {
            g_aero_active_app = nullptr;
        }
    }

    bool valid() override { return static_cast<bool>(app); }

    bool update_config(const ultramodern::renderer::GraphicsConfig& old_config,
                       const ultramodern::renderer::GraphicsConfig& new_config) override {
        if (old_config == new_config) {
            return false;
        }
        // wm_option (fullscreen) is deliberately NOT handled here: the SDL window is
        // owned by the main thread (toggle_fullscreen in main.cpp), and RT64's
        // setFullScreen would fight SDL over the same HWND.
        set_application_user_config(app.get(), new_config);
        app->updateUserConfig(true);
        if (new_config.msaa_option != old_config.msaa_option) {
            app->updateMultisampling();
        }
        return true;
    }

    void enable_instant_present() override {
        app->enhancementConfig.presentation.mode = RT64::EnhancementConfiguration::Presentation::Mode::PresentEarly;
        app->updateEnhancementConfig();
    }

    // The port starts gameplay directly; no launcher workload is needed.
    void send_dummy_workload(uint32_t) override {}

    // Frame-pacing probe (PERMANENT harness instrumentation, same class as pace_probe):
    // AERO_FRAME_LOG=<path> logs gfx-thread anomalies -- gaps between update_screen calls
    // (the present cadence) and slow send_dl/update_screen bodies -- with ms timestamps.
    // This is what root-caused the 1 Hz console-write hitch (the gated heartbeat above):
    // spikes tagged hb=1 land exactly on heartbeat frames. Writes only on anomaly (few
    // lines/sec worst case, to a buffered FILE*, never the console), so the probe itself
    // cannot produce a periodic stall. Default off = one null-pointer check per frame.
    // See the companion vi_pace_probe() in src/main.cpp for the VI-thread probe; they
    // share aero::config::open_frame_log() so the env var behaviour stays in lock-step.
    FILE* frame_log() {
        static FILE* f = aero::config::open_frame_log("");
        return f;
    }
    double log_now_ms() {
        static const auto t0 = std::chrono::steady_clock::now();
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    }

    void send_dl(const OSTask* task) override {
        const auto fp_start = std::chrono::steady_clock::now();
        static int count = 0;
        if (++count == 1) {
            std::fprintf(stderr,
                         "[rt64] first send_dl: ucode=0x%08x ucode_data=0x%08x dl=0x%08x\n",
                         (uint32_t)task->t.ucode, (uint32_t)task->t.ucode_data,
                         (uint32_t)task->t.data_ptr);
        }
        app->state->rsp->reset();
        enable_racer_shadow_depth(app->core.RDRAM, task);
        // AERO_DL_SKIP_DL=<hex,hex,...>: track-artefact-diagnosis Phase 3. At send_dl
        // time, walk the frame DL and rewrite any G_DL command whose target is in the
        // skip set to G_SPNOOP (0x00000000). A clean re-render with the DL skipped is
        // the definitive "is this the cause?" evidence; env unset = no-op, parse
        // amortised. KSEG0 target form (0x80xxxxxx), the address attribute tool
        // (tools/rom/full_track_attr.py) returns.
        //
        // Safety: the RSP has already finished walking this DL by the time send_dl
        // runs, and the game builds a fresh DL in RDRAM each frame from its course
        // table -- mutating the in-memory command stream here cannot affect game
        // logic or future frames. (TODO: this walker duplicates dlinspect::geom_walk
        // from stub_renderer.cpp; lift the F3DEX command walk into a shared header
        // when a third caller appears.)
        if (task && app->core.RDRAM) {
            static const char* s_skip_env = std::getenv("AERO_DL_SKIP_DL");
            if (s_skip_env) {
                static uint32_t s_skip[16] = {};
                static int s_skip_n = 0;
                static bool s_skip_init = false;
                if (!s_skip_init) {
                    const char* p = s_skip_env;
                    while (*p && s_skip_n < 16) {
                        while (*p == ',' || *p == ' ') ++p;
                        if (!*p) break;
                        char* end = nullptr;
                        unsigned long v = std::strtoul(p, &end, 16);
                        if (end == p) break;
                        s_skip[s_skip_n++] = (uint32_t)v;
                        p = end;
                    }
                    s_skip_init = true;
                    std::fprintf(stderr, "[dl-skip] %d DL(s) marked for skip: ", s_skip_n);
                    for (int i = 0; i < s_skip_n; ++i)
                        std::fprintf(stderr, "0x%08X%s", s_skip[i], i + 1 == s_skip_n ? "\n" : ",");
                }
                uint8_t* rdram = app->core.RDRAM;
                uint32_t phys = (uint32_t)task->t.data_ptr & 0x3FFFFFFu;
                uint32_t seg[16] = {};
                seg[0] = 0;
                int hits = 0;
                for (uint32_t cmd = 0; cmd < 200000; ++cmd) {
                    if (phys + 8 > 0x00800000u) break;
                    uint32_t w0 = *(const uint32_t*)(rdram + phys);
                    uint32_t w1 = *(const uint32_t*)(rdram + phys + 4);
                    uint8_t  op = (w0 >> 24) & 0xFFu;
                    if (op == 0xB8u) break;                       // G_ENDDL
                    if (op == 0x06u) {                              // G_DL
                        for (int i = 0; i < s_skip_n; ++i) {
                            if (w1 == s_skip[i]) {
                                *(uint32_t*)(rdram + phys) = 0u; // G_SPNOOP, preserves target
                                ++hits;
                                std::fprintf(stderr, "[dl-skip] hit 0x%08X send_dl #%d\n",
                                             s_skip[i], count);
                                break;
                            }
                        }
                    } else if (op == 0xBCu && (w0 & 0xFFu) == 0x06u) {
                        // gsSPSegment(seg, base) -> seg[(w0>>8)&0xF] = w1
                        uint32_t segnum = ((w0 >> 8) & 0xFFFFu) >> 2;
                        if (segnum < 16) seg[segnum] = w1;
                    }
                    phys += 8;
                }
                if (hits) std::fflush(stderr);
            }
        }
        // Match the swrender's KSEG0 call-site convention (stub_renderer.cpp send_dl)
        // so resolve()'s hi>=0x80 branch handles both call sites; the seg[0]={0} default
        // would silently mis-resolve in RT64 if the root DL ever set segment 0.
        // Game-specific fog policy is not part of this generic task bridge.
        app->interpreter->loadUCodeGBI(task->t.ucode & 0x3FFFFFF, task->t.ucode_data & 0x3FFFFFF, true);
        app->processDisplayLists(app->core.RDRAM, task->t.data_ptr & 0x3FFFFFF, 0, true);
        // Same sustained-pipeline heartbeat as the headless context, so RT64 runs are
        // comparable against headless logs. VI_ORIGIN/STATUS prove the present path is
        // scanning out the game's framebuffer, not the renderer's dummy setup store.
        //
        // GATED (AERO_HARNESS_LOG=1, default off): this fires exactly once per second
        // (30 send_dls at this title's 30 fps) ON THE GFX THREAD, and when stderr is a
        // live console the synchronous console write measured 10-77 ms -- a visible
        // hitch every second of play. Diagnostic runs opt back in via the env var.
        if (aero::config::harness_log() && count % 30 == 0) {
            const ultramodern::renderer::ViRegs* vr = ultramodern::renderer::get_vi_regs();
            // Interpolation health: viOriginalRate is the game's
            // detected update rate (30 for this title), targetRate the present pace RT64 aims
            // for (display Hz when RefreshRate::Display), and interp count/presented the
            // per-workload synthesized-frame counters -- count ~= targetRate/viOriginalRate
            // when interpolation is live; 0 means RT64 is presenting game frames raw.
            //
            // DIAGNOSTICS-GRADE SAMPLING: interpolatedMutex synchronises with the present
            // queue's counter updates, but RT64's workload thread writes the index/rate
            // fields WITHOUT any lock (rt64_workload_queue.cpp:1020-1044, :237), so a
            // fully synchronised read is impossible without patching the submodule. These
            // are aligned word loads sampled once per second for a log line -- treat a
            // single odd line as sampling noise, only a sustained pattern as signal.
            RT64::SharedQueueResources* sq = app->sharedQueueResources.get();
            uint32_t vi_rate, target_rate, swap_hz, interp_count, interp_presented;
            float res_scale_x, res_scale_y;
            {
                std::lock_guard<std::mutex> lock(sq->interpolatedMutex);
                const RT64::InterpolatedFrameCounters& fc =
                    sq->interpolatedFrames[sq->interpolatedFramesIndex];
                vi_rate = sq->viOriginalRate;
                target_rate = sq->targetRate;
                swap_hz = sq->swapChainRate;
                interp_count = fc.count;
                interp_presented = fc.presented;
        // Widescreen health probe: resolutionScale.x carries the
                // aspectRatioScale factor (rt64_workload_queue.cpp:211) -- x > y means the
                // Expand config reached the workload layer; x == y means the target aspect
                // never derived (swapchain size unknown or config lost).
                res_scale_x = sq->resolutionScale.x;
                res_scale_y = sq->resolutionScale.y;
            }
            std::fprintf(stderr,
                         "[rt64] send_dl count=%d VI_ORIGIN=0x%08x VI_STATUS=0x%04x VI_WIDTH=%u"
                         " | viRate=%u targetRate=%u swapHz=%u interp count=%u presented=%u"
                         " | resScale=%.3fx%.3f\n",
                         count, vr->VI_ORIGIN_REG, vr->VI_STATUS_REG, vr->VI_WIDTH_REG,
                         vi_rate, target_rate, swap_hz, interp_count, interp_presented,
                         res_scale_x, res_scale_y);
        }
        // Flag slow send_dl bodies; hb=1 marks heartbeat frames (count%30==0) so a
        // correlation between hitches and the harness heartbeat is directly visible.
        if (FILE* f = frame_log()) {
            double dur = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - fp_start).count();
            if (dur > 10.0) {
                std::fprintf(f, "[gfx-pace] t=%.1f send_dl dur=%.2fms count=%d hb=%d\n",
                             log_now_ms(), dur, count, (count % 30 == 0) ? 1 : 0);
                std::fflush(f);
            }
        }
    }

    void update_screen() override {
        // Present cadence: update_screen is called once per VI tick (60 Hz);
        // a gap >25 ms means the display visibly stalled -- log gap and body duration.
        FILE* f = frame_log();
        if (f == nullptr) {
            app->updateScreen();
            return;
        }
        static std::chrono::steady_clock::time_point last{};
        const auto start = std::chrono::steady_clock::now();
        if (last.time_since_epoch().count() != 0) {
            double gap = std::chrono::duration<double, std::milli>(start - last).count();
            if (gap > 25.0) {
                std::fprintf(f, "[gfx-pace] t=%.1f GAP between update_screen calls: %.2fms\n", log_now_ms(), gap);
                std::fflush(f);
            }
        }
        last = start;
        app->updateScreen();
        double dur = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        if (dur > 10.0) {
            std::fprintf(f, "[gfx-pace] t=%.1f update_screen dur=%.2fms\n", log_now_ms(), dur);
            std::fflush(f);
        }
    }

    void shutdown() override {
        if (app != nullptr) {
            app->end();
        }
    }

    uint32_t get_display_framerate() const override {
        return app->presentQueue->ext.sharedResources->swapChainRate;
    }

    float get_resolution_scale() const override {
        constexpr int ReferenceHeight = 240;
        switch (app->userConfig.resolution) {
            case RT64::UserConfiguration::Resolution::WindowIntegerScale:
                if (app->sharedQueueResources->swapChainHeight > 0) {
                    return std::max(float((app->sharedQueueResources->swapChainHeight + ReferenceHeight - 1) / ReferenceHeight), 1.0f);
                }
                return 1.0f;
            case RT64::UserConfiguration::Resolution::Manual:
                return float(app->userConfig.resolutionMultiplier);
            case RT64::UserConfiguration::Resolution::Original:
            default:
                return 1.0f;
        }
    }

private:
    std::unique_ptr<RT64::Application> app;
};

} // anonymous namespace

// Full rendered aspect for race-intro coverage, independent of the HUD clamp.
// Read swapchain dimensions under the same lock as the HUD helper below.
extern "C" uint32_t aero_ws_get_output_aspect_bits(void) {
    float aspect = 4.0f / 3.0f;
    const auto cfg = ultramodern::renderer::get_graphics_config();
    auto* active_app = g_aero_active_app.load(std::memory_order_acquire);
    if (cfg.ar_option == ultramodern::renderer::AspectRatio::Expand &&
        active_app && active_app->sharedQueueResources) {
        auto& shared = *active_app->sharedQueueResources;
        std::scoped_lock<std::mutex> lock(shared.configurationMutex);
        if (shared.swapChainWidth > 0 && shared.swapChainHeight > 0)
            aspect = std::max(aspect, float(shared.swapChainWidth) / float(shared.swapChainHeight));
    }
    uint32_t bits;
    std::memcpy(&bits, &aspect, sizeof(bits));
    return bits;
}

// Effective aspect the extended-GBI HUD rect pins travel to, returned as raw
// float bits. Full reaches the real edges, Clamp16x9 stops at 16:9, and
// Original does not move. Geometry must match these rect pins rather than the
// raw output aspect. The shared-state read uses RT64's configuration lock.
extern "C" uint32_t aero_ws_get_hud_rect_aspect_bits(void) {
    const float source = 4.0f / 3.0f;
    float aspect = source;
    const auto cfg = ultramodern::renderer::get_graphics_config();
    RT64::Application* active_app = g_aero_active_app.load(std::memory_order_acquire);
    if (cfg.ar_option == ultramodern::renderer::AspectRatio::Expand &&
        active_app != nullptr && active_app->sharedQueueResources) {
        auto& shared = *active_app->sharedQueueResources;
        std::scoped_lock<std::mutex> configuration_lock(shared.configurationMutex);
        uint32_t w = shared.swapChainWidth;
        uint32_t h = shared.swapChainHeight;
        if (w > 0 && h > 0) {
            float display = float(w) / float(h);
            if (display > source) {
                float ext_percentage;
                switch (cfg.hr_option) {
                    case ultramodern::renderer::HUDRatioMode::Full:
                        ext_percentage = 1.0f;
                        break;
                    case ultramodern::renderer::HUDRatioMode::Clamp16x9:
                        ext_percentage = aero_ws_hud_clamp_ext_percentage(
                            display, source, 16.0f / 9.0f);
                        break;
                    case ultramodern::renderer::HUDRatioMode::Original:
                    default:
                        ext_percentage = 0.0f;
                        break;
                }
                aspect = aero_ws_hud_effective_rect_aspect(display, source, ext_percentage);
            }
        }
    }
    uint32_t bits;
    std::memcpy(&bits, &aspect, sizeof(bits));
    return bits;
}

namespace aero_rt64 {

bool enabled() {
    const char* v = std::getenv("AERO_HEADLESS");
    return !(v != nullptr && v[0] == '1');
}

std::unique_ptr<ultramodern::renderer::RendererContext>
create_render_context(uint8_t* rdram, ultramodern::renderer::WindowHandle window_handle,
                      bool developer_mode) {
    auto ctx = std::make_unique<RT64Context>(rdram, window_handle, developer_mode);
    if (!ctx->valid()) {
#if defined(__ANDROID__)
        aero::android::startup_error("This GPU driver cannot start AeroGauge. On Adreno devices, try importing a compatible Mesa Turnip ZIP in Graphics & storage. You can also select the system driver or export diagnostics below.");
#endif
        return nullptr;
    }
    return ctx;
}

} // namespace aero_rt64
