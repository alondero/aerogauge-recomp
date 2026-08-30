// Persistent user-facing graphics configuration (#1/#2 enhancement wave).
//
// Same model as Zelda64Recomp: ultramodern owns the GraphicsConfig struct and the
// renderer reacts to set_graphics_config(); the PORT owns persistence. We persist a
// graphics.json in a per-user config directory using the NLOHMANN_JSON_SERIALIZE_ENUM
// mappings ultramodern ships in ultramodern/config.hpp, so the on-disk vocabulary
// ("Expand", "Display", "MSAA4X", ...) matches the peer ports.
#ifndef AERO_CONFIG_H
#define AERO_CONFIG_H

#include <filesystem>
#include <string>

#include "ultramodern/config.hpp"

namespace aero {
namespace config {

// Per-user persistent config directory (created on demand):
//   Windows: %LOCALAPPDATA%\AeroGaugeRecomp
//   else:    $XDG_CONFIG_HOME/AeroGaugeRecomp (or ~/.config/AeroGaugeRecomp)
std::filesystem::path app_config_dir();

// The enhancement-oriented defaults this port ships with (widescreen Expand,
// display-rate interpolated rendering, window-scaled internal resolution).
ultramodern::renderer::GraphicsConfig default_graphics_config();

// Load graphics.json (falling back to defaults for missing/invalid keys), apply it
// via ultramodern::renderer::set_graphics_config, and write the merged file back so
// users always have a complete, editable file on disk. Returns the applied config.
ultramodern::renderer::GraphicsConfig load_and_apply_graphics();

// Main-thread snapshot/apply helpers for the native in-game menu. apply_graphics()
// queues the live RT64 update and persists the same value.
ultramodern::renderer::GraphicsConfig current_graphics();
void apply_graphics(const ultramodern::renderer::GraphicsConfig& cfg, bool apply_live = true);

// Persist the given config (full overwrite of graphics.json).
void save_graphics(const ultramodern::renderer::GraphicsConfig& cfg);

// Persist a runtime window-mode change (F11/menu) in the main-thread snapshot.
void update_saved_window_mode(ultramodern::renderer::WindowMode wm);

// Requested window size for windowed mode (from graphics.json; defaults 1600x900,
// chosen 16:9 so AspectRatio::Expand actually widens on first launch).
struct WindowSize { int width; int height; };
WindowSize window_size();
void set_window_size(WindowSize size);

// RT64 texture-replacement paths (issue #9). Both are extra graphics.json string keys
// (empty = feature off), overridable by env var for headless capture/testing:
//   texture_pack  / AERO_TEXTURE_PACK  -- directory or .rtz to auto-load at startup.
//   texture_dump  / AERO_TEXTURE_DUMP  -- directory RT64 writes every used texture to
//                                          (raw TMEM/RDRAM dumps; decode with
//                                          tools/decode_dump.py). Enables headless dump
//                                          without the F1 developer overlay.
std::string texture_pack_path();
std::string texture_dump_dir();
void set_texture_pack_path(std::string path);
void set_texture_dump_dir(std::string path);

// Widen the dense 3P/4P split-screen fog to the 1P window/colour (issue #83).
// graphics.json key "widescreen_fog_match" (default true), overridable by
// AERO_FOG_MATCH_1P=1/0. The rewrite still self-gates on player count >= 3.
bool widescreen_fog_match();
void set_widescreen_fog_match(bool enabled);

// Draw the sky panorama in 3P/4P split screen like 1P/2P (issue #84).
// graphics.json key "widescreen_sky_match" (default true), overridable by
// AERO_SKY_MATCH_1P=1/0. Only flips a branch that 1P/2P already take.
bool widescreen_sky_match();
void set_widescreen_sky_match(bool enabled);

// Far-clip-plane multiplier for the native guPerspectiveF (draw-distance
// enhancement; see src/aero_draw_distance.h). graphics.json key
// "draw_distance_scale" (default 100.0; 0.0 = infinite far plane,
// 1.0 = the original game's 500-unit far plane, clamped to {0} U [1, 10000]),
// overridable by AERO_DRAW_DISTANCE_SCALE=<float> for A/B capture runs.
float draw_distance_scale();
void set_draw_distance_scale(float scale);

// Register the whole course's geometry every frame instead of the game's 3-zone
// visibility window (the residual large-scale pop-in; see src/aero_full_track.cpp).
// graphics.json key "full_track" (default true), overridable by AERO_FULL_TRACK=1/0.
bool full_track();
void set_full_track(bool enabled);

// One-button Turbo + Boost Start (see src/aero_turbo_boost.c). graphics.json key
// "easy_turbo_boost" (default true), overridable by AERO_EASY_TURBO=1/0.
bool easy_turbo_boost();
void set_easy_turbo_boost(bool enabled);

// C-linkage bridge so the plain-C hook (src/aero_turbo_boost.c) can read the
// toggle without dragging the C++ config machinery into its TU.
#ifdef __cplusplus
extern "C" {
#endif
int aero_easy_turbo_enabled(void);
#ifdef __cplusplus
}
#endif

// Periodic harness diagnostics on hot threads (the 1 Hz [rt64] send_dl heartbeat on
// the gfx thread, the ~8.5 s [probe] fb-swap line on the VI thread): AERO_HARNESS_LOG=1
// enables. Env-only, DEFAULT OFF: a Windows console write is a synchronous cross-process
// call, and the 1 Hz heartbeat measured 10-77 ms per write on the gfx thread when stderr
// was a live console -- a user-visible hitch every second of play. Event-driven logs
// (scene transitions, warp, init) are not gated; they are silent during steady play.
bool harness_log();

// Open the frame-pacing probe log (AERO_FRAME_LOG=<path>) with an optional suffix on the
// base name (e.g. ".vi" for the VI-thread probe). Returns nullptr when the env var is
// unset; callers should treat the FILE* as fire-and-forget -- the buffered stream never
// fails individual writes and fclose is intentionally skipped at shutdown (process exit
// closes it). Single shared implementation so the gfx and VI probes stay in lock-step.
std::FILE* open_frame_log(const char* suffix);

} // namespace config
} // namespace aero

#endif
