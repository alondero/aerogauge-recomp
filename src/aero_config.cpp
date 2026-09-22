// Persistent graphics configuration (see aero_config.h).
//
// This module owns the two on-disk JSON files. It publishes a main-thread
// snapshot for the RecompFrontend settings page. Missing keys use defaults;
// malformed files remain in place so the original text can be recovered.
#include "aero_config.h"
#include "aero_paths.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <system_error>
#include <thread>
#include <utility>

#include "json/json.hpp"

namespace {

constexpr const char* kGraphicsFile = "graphics.json";
constexpr const char* kEnhancementsFile = "enhancements.json";

// Window-size keys live in the same graphics.json (extra keys alongside the
// GraphicsConfig fields); 16:9 default so AspectRatio::Expand widens on first run.
constexpr int kDefaultWindowWidth = 1600;
constexpr int kDefaultWindowHeight = 900;

aero::config::WindowSize g_window_size{kDefaultWindowWidth, kDefaultWindowHeight};

// RT64 texture-replacement paths, persisted as extra graphics.json string keys
// alongside the GraphicsConfig fields (like the window size). Empty = feature off.
std::string g_texture_pack;
std::string g_texture_dump;
std::mutex g_texture_mutex;

// Widen the dense 3P/4P split-screen fog to the 1P window and colour.
// The enhancement defaults on, but the rewrite self-gates on player count so
// 1P and 2P are unaffected.
std::atomic_bool g_widescreen_fog_match{true};

// Draw the sky panorama in 3P/4P split screen like 1P/2P. The 1P and 2P
// paths already take this route, so the change only affects split-screen.
std::atomic_bool g_widescreen_sky_match{true};

// Far-clip-plane multiplier applied in the native guPerspectiveF (the game's
// universal far plane is 500 units -- the pop-in). 1.0 = original game.
//   0.0 = infinite far plane (m22=-1, m32=-2*n -- no clip whatsoever)
//   1.0 = the unmodified game's 500-unit far plane
//   >1  = finite scaled far plane; the default 100 is an enhancement.
// Clamped to {0} U [1, 10000]: below 1 (non-zero) would SHRINK the frustum and
// is almost certainly a typo, and beyond 10000 the s15.16 fixed-point matrix
// loses precision faster than the geometry extends (the (n-f) divisor converges
// to -f, the (n+f) to +f, and ULP noise in each term lands in screen space).
std::atomic<float> g_draw_distance_scale{100.0f};

// Register EVERY course zone's geometry each frame instead of the game's 3-zone
// visibility window (the large-scale pop-in that the extended far plane exposed;
// see src/aero_full_track.cpp). Enhancement default-on like the far plane.
std::atomic_bool g_full_track{true};
std::atomic_bool g_force_full_lod{false};

// Accelerator-only Boost Start and player-directed Turbo assist. This changes
// handling, so it is explicitly opt-in and defaults to the original game.
std::atomic_bool g_easy_turbo_boost{false};

// The menu queues config operations onto the main SDL thread. This snapshot
// avoids reading ultramodern's reference-returning getter while RT64 applies a
// setting on another thread.
ultramodern::renderer::GraphicsConfig g_current_graphics{};

float clamp_draw_distance(float v) {
    if (!(v >= 0.0f)) return 1.0f;      // also catches NaN; <0 is meaningless
    if (v == 0.0f) return 0.0f;        // 0 is the explicit "infinite" sentinel
    if (v < 1.0f) return 1.0f;          // (0,1) would shrink the frustum
    if (v > 10000.0f) return 10000.0f;
    return v;
}

aero::config::WindowSize clamp_window_size(aero::config::WindowSize size) {
    // Match the load-time bound: below the N64 framebuffer is useless, above 8K
    // is a typo that can make SDL window creation fail.
    if (size.width < 320 || size.width > 7680 || size.height < 240 || size.height > 4320) {
        std::fprintf(stderr, "[config] window %dx%d out of range -- using %dx%d\n",
                     size.width, size.height, kDefaultWindowWidth, kDefaultWindowHeight);
        return {kDefaultWindowWidth, kDefaultWindowHeight};
    }
    return size;
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

nlohmann::json graphics_config_json(const ultramodern::renderer::GraphicsConfig& c) {
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
    };
}

nlohmann::json to_json(const ultramodern::renderer::GraphicsConfig& c) {
    std::lock_guard<std::mutex> lock(g_texture_mutex);
    nlohmann::json result = graphics_config_json(c);
    result.update({
        {"window_width", g_window_size.width},
        {"window_height", g_window_size.height},
        {"texture_pack", g_texture_pack},
        {"texture_dump", g_texture_dump},
        {"widescreen_fog_match", g_widescreen_fog_match.load()},
        {"widescreen_sky_match", g_widescreen_sky_match.load()},
        {"draw_distance_scale", g_draw_distance_scale.load()},
        {"full_track", g_full_track.load()},
        {"force_full_lod", g_force_full_lod.load()},
    });
    return result;
}

void from_json(const nlohmann::json& j, ultramodern::renderer::GraphicsConfig& c) {
    std::lock_guard<std::mutex> lock(g_texture_mutex);
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
    bool widescreen_fog_match = g_widescreen_fog_match.load();
    bool widescreen_sky_match = g_widescreen_sky_match.load();
    float draw_distance_scale = g_draw_distance_scale.load();
    bool full_track = g_full_track.load();
    bool force_full_lod = g_force_full_lod.load();
    from_or_default(j, "widescreen_fog_match", widescreen_fog_match);
    from_or_default(j, "widescreen_sky_match", widescreen_sky_match);
    from_or_default(j, "draw_distance_scale", draw_distance_scale);
    from_or_default(j, "full_track", full_track);
    from_or_default(j, "force_full_lod", force_full_lod);
    g_widescreen_fog_match.store(widescreen_fog_match);
    g_widescreen_sky_match.store(widescreen_sky_match);
    g_draw_distance_scale.store(clamp_draw_distance(draw_distance_scale));
    g_full_track.store(full_track);
    g_force_full_lod.store(force_full_lod);
    g_window_size = clamp_window_size(g_window_size);
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

std::filesystem::path enhancements_json_path() {
    if (const char* p = std::getenv("AERO_ENHANCEMENTS_CONFIG")) {
        return std::filesystem::path{p};
    }
    return aero::config::app_config_dir() / kEnhancementsFile;
}

ReadResult read_enhancements_file(const std::filesystem::path& path, bool& easy_turbo) {
    std::ifstream in{path};
    if (!in.good()) return ReadResult::Missing;
    try {
        nlohmann::json j;
        in >> j;
        if (!j.is_object()) return ReadResult::Unparseable;
        from_or_default(j, "easy_turbo_boost", easy_turbo);
        return ReadResult::Ok;
    } catch (const nlohmann::json::exception& e) {
        std::fprintf(stderr, "[config] %s unparseable (%s); using defaults IN MEMORY\n",
                     path.string().c_str(), e.what());
        return ReadResult::Unparseable;
    }
}

bool write_graphics_json(const std::filesystem::path& path, const nlohmann::json& j) {
    // Serialize before touching the file. dump() throws on a string that is not
    // UTF-8, and opening the stream first would truncate the user's settings and
    // then leave them empty when the throw escaped.
    const std::string payload = j.dump(4) + "\n";
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream out{path};
    if (!out.good()) {
        std::fprintf(stderr, "[config] cannot write %s\n", path.string().c_str());
        return false;
    }
    out << payload;
    out.flush();
    if (!out.good()) {
        std::fprintf(stderr, "[config] write to %s FAILED (disk full / permissions?)"
                     " -- settings may not persist\n", path.string().c_str());
        return false;
    }
    return true;
}

// --- deferred persistence ---------------------------------------------------
//
// Menu actions and hotkeys used to run a full read-parse-merge-write of the JSON
// files inline on the SDL main thread: one disk round trip per click, and one per
// step of a slider drag. Writes now leave callers with only in-memory work.
// Updates accumulate here and a dedicated worker writes them once the edits stop
// for a short window, so a run of changes on one thread driving frames costs a
// single file round trip.
//
// Setters never throw and never block on the filesystem -- they run inside the
// SDL event pump. flush_config_writes() is the synchronous barrier the shutdown
// path uses.
//
// One instance serves the whole process, which is safe because the config
// directory is fixed for a process's lifetime (app_config_dir() plus the
// AERO_*_CONFIG launch overrides). Reloading from a different directory mid-run
// would need the state below keyed by path.

constexpr auto kWriteDebounce = std::chrono::milliseconds(250);
constexpr long kMaxDebounceMs = 60000;
// Wait before retrying a batch that failed, so a repeating failure cannot spin.
constexpr auto kWriteRetryDelay = std::chrono::milliseconds(1000);

// How long the worker waits for the edits to stop before touching the file.
// AERO_CONFIG_WRITE_DEBOUNCE_MS overrides it for tests. Parsed per batch rather
// than cached so a test that sets the variable before its first change sees it.
std::chrono::milliseconds write_debounce() {
    const char* v = std::getenv("AERO_CONFIG_WRITE_DEBOUNCE_MS");
    if (v == nullptr || v[0] == '\0') return kWriteDebounce;
    // Reject trailing garbage and overflow: a mistyped knob should fall back to
    // the default rather than silently mean a different window.
    errno = 0;
    char* end = nullptr;
    const long ms = std::strtol(v, &end, 10);
    if (end == v || (end != nullptr && *end != '\0')) return kWriteDebounce;
    if (errno == ERANGE || ms < 0) return kWriteDebounce;
    return std::chrono::milliseconds{std::min<long>(ms, kMaxDebounceMs)};
}

struct PersistenceState {
    std::mutex mutex;
    std::condition_variable cv;
    bool worker_started = false;
    bool flush_requested = false;
    bool writing = false;
    // End of the current quiet window; pushed out by every queued change.
    std::chrono::steady_clock::time_point deadline;
    // Values a menu action changed in graphics.json (empty = nothing pending).
    nlohmann::json graphics_updates = nlohmann::json::object();
    // Complete enhancements.json document (null = nothing pending).
    nlohmann::json enhancements_document;
    // Last complete graphics.json this module wrote, refreshed after every
    // successful write. Only used as the merge base if the file disappears while
    // the game is running.
    nlohmann::json base_document = nlohmann::json::object();
    // Files successfully written. Diagnostics and the coalescing test.
    uint64_t completed_writes = 0;
};

bool has_pending_work(const PersistenceState& state) {
    return !state.graphics_updates.empty() || !state.enhancements_document.is_null();
}

// Heap-allocated and deliberately never destroyed: the worker is detached and
// outlives every static in this translation unit, so process exit cannot race a
// destructor. Durability comes from flush_config_writes() on the quit path (see
// boot_summary_and_exit in src/main.cpp), not from destruction.
PersistenceState& persistence() {
    static PersistenceState* state = new PersistenceState();
    return *state;
}

// Merge the values a menu action changed into graphics.json, re-reading the file
// first so a hand edit made while the game was running is preserved, including
// settings that have no fixed-choice native menu item. A malformed or non-object
// file remains untouched so its original text is recoverable.
//
// Returns the complete document that reached the file, or a null JSON when the
// file was deliberately left alone or the write failed.
nlohmann::json merge_graphics_updates(const nlohmann::json& updates,
                                      const nlohmann::json& fallback_base) {
    const std::filesystem::path path = graphics_json_path();
    nlohmann::json current;
    std::ifstream in{path};
    if (!in.good()) {
        // The file was deleted behind us: rebuild from the last document we wrote
        // so this batch does not drop the settings that are still live in memory.
        current = fallback_base;
    } else {
        try {
            in >> current;
        } catch (const nlohmann::json::exception& e) {
            std::fprintf(stderr, "[config] %s unparseable (%s); leaving it untouched\n",
                         path.string().c_str(), e.what());
            return nlohmann::json();
        }
        if (!current.is_object()) {
            std::fprintf(stderr, "[config] %s is not a JSON object; leaving it untouched\n",
                         path.string().c_str());
            return nlohmann::json();
        }
    }
    for (auto it = updates.begin(); it != updates.end(); ++it) {
        current[it.key()] = it.value();
    }
    if (!write_graphics_json(path, current)) return nlohmann::json();
    return current;
}

// Write one drained batch. `written_base` receives the document that reached
// graphics.json, or stays null when that file was deliberately left alone or the
// write failed. Returns the number of files successfully written.
//
// Only the JSON and stream operations here can throw, and only for allocation or
// stream-construction failure; a bad path degrades to the usual "may not persist".
uint64_t write_batch(const nlohmann::json& updates, const nlohmann::json& enhancements,
                     const nlohmann::json& base, nlohmann::json& written_base) {
    uint64_t writes = 0;
    if (!enhancements.is_null() &&
        write_graphics_json(enhancements_json_path(), enhancements)) {
        ++writes;
    }
    if (!updates.empty()) {
        written_base = merge_graphics_updates(updates, base);
        if (!written_base.is_null()) ++writes;
    }
    return writes;
}

// Keep the worker alive after a failure. This matters because the thread is
// detached: an escaping exception would call std::terminate, and leaving
// `writing` set would hang flush_config_writes() forever.
//
// Pending work is deliberately left alone. A failure while writing happens after
// the batch was taken, so anything in these slots was queued by another thread in
// the meantime and is not ours to discard. The deadline is pushed back so a
// failure that repeats cannot spin: without it the next pass would find the wait
// non-blocking and retry immediately, forever.
//
// (A failure before the batch is taken leaves that batch in place and it is
// retried for the same reason. A batch lost inside the drain itself, for example
// to a bad_alloc copying the base document, is not recoverable and is dropped.)
void recover_worker(std::unique_lock<std::mutex>& lock, PersistenceState& state,
                    const char* what) {
    if (!lock.owns_lock()) lock.lock();
    state.writing = false;
    state.deadline = std::max(state.deadline,
                              std::chrono::steady_clock::now() + kWriteRetryDelay);
    std::fprintf(stderr, "[config] background settings write failed (%s)"
                 " -- will retry; settings may not be current on disk\n", what);
    state.cv.notify_all();
}

void persistence_worker() {
    PersistenceState& state = persistence();
    std::unique_lock lock(state.mutex);
    for (;;) {
        try {
            state.cv.wait(lock, [&state] { return has_pending_work(state); });

            // Trailing debounce: every queued change pushes the deadline out, so a
            // run of edits produces one write once they stop. wait_until captures
            // the deadline when it is called, so a change that arrives during the
            // wait needs another pass with the moved deadline. A flush skips all
            // of this.
            while (!state.flush_requested) {
                if (state.cv.wait_until(lock, state.deadline,
                                        [&state] { return state.flush_requested; })) {
                    break;
                }
                if (std::chrono::steady_clock::now() >= state.deadline) break;
            }

            state.flush_requested = false;
            nlohmann::json updates = std::move(state.graphics_updates);
            nlohmann::json enhancements = std::move(state.enhancements_document);
            const nlohmann::json base = state.base_document;
            state.graphics_updates = nlohmann::json::object();
            state.enhancements_document = nullptr;
            state.writing = true;
            lock.unlock();

            nlohmann::json written_base; // null = graphics.json left alone
            const uint64_t writes = write_batch(updates, enhancements, base, written_base);

            lock.lock();
            if (!written_base.is_null()) state.base_document = std::move(written_base);
            state.completed_writes += writes;
            state.writing = false;
            state.cv.notify_all();
        } catch (const std::exception& e) {
            recover_worker(lock, state, e.what());
        } catch (...) {
            recover_worker(lock, state, "unknown error");
        }
    }
}

// Caller holds state.mutex. A setter runs inside the SDL event pump, so this must
// not throw: a failed creation leaves the change queued in memory (a later batch
// retries) and flush_config_writes() finds no worker to wait for.
void start_worker(PersistenceState& state) {
    if (state.worker_started) return;
    try {
        std::thread(persistence_worker).detach();
        state.worker_started = true;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[config] cannot start the settings writer (%s);"
                     " settings changes will not persist\n", e.what());
    }
}

// Caller holds state.mutex and must call cv.notify_all() after recording the work.
// Pushing the deadline out per change is what makes this a debounce rather than a
// fixed-rate throttle.
void note_pending_change(PersistenceState& state) {
    state.deadline = std::chrono::steady_clock::now() + write_debounce();
    start_worker(state);
}

void queue_graphics_updates(nlohmann::json updates) {
    if (updates.empty()) return;
    PersistenceState& state = persistence();
    std::lock_guard lock(state.mutex);
    note_pending_change(state);
    for (auto it = updates.begin(); it != updates.end(); ++it) {
        state.graphics_updates[it.key()] = it.value();
    }
    state.cv.notify_all();
}

void queue_enhancements_document(nlohmann::json document) {
    PersistenceState& state = persistence();
    std::lock_guard lock(state.mutex);
    note_pending_change(state);
    state.enhancements_document = std::move(document);
    state.cv.notify_all();
}

nlohmann::json enhancements_json(bool easy_turbo) {
    return nlohmann::json{{"easy_turbo_boost", easy_turbo}};
}

// Startup: inline so enhancements.json exists before the game starts (matches
// save_graphics()). A live toggle goes through save_enhancements() instead.
void write_enhancements(bool easy_turbo) {
    write_graphics_json(enhancements_json_path(), enhancements_json(easy_turbo));
}

void save_enhancements(bool easy_turbo) {
    queue_enhancements_document(enhancements_json(easy_turbo));
}

} // anonymous namespace

namespace aero {
namespace config {

std::filesystem::path app_config_dir() {
    return aero::paths::app_data_dir();
}

ultramodern::renderer::GraphicsConfig default_graphics_config() {
    // These are the port's current display defaults: window-scaled internal
    // resolution, widescreen Expand with the HUD clamped to 16:9, and RT64
    // frame interpolation up to the display refresh rate. Game logic stays at
    // its native 30 Hz tick; interpolation does not add game updates.
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
#if defined(__ANDROID__)
    // A conservative first launch avoids full-resolution MSAA on mobile GPUs.
    cfg.res_option = ultramodern::renderer::Resolution::Original;
    cfg.wm_option = ultramodern::renderer::WindowMode::Fullscreen;
    cfg.api_option = ultramodern::renderer::GraphicsApi::Vulkan;
    cfg.msaa_option = ultramodern::renderer::Antialiasing::None;
    cfg.rr_option = ultramodern::renderer::RefreshRate::Original;
#endif
    return cfg;
}

// The widescreen-HUD rect-pin scaling (see aero_hud_widescreen.h) tracks
// runtime window resizes and the hr_option clamp through
// aero_ws_hud_effective_rect_aspect, so it needs no config-time gate. The
// constants remain unverified for this game's HUD; re-measure them when a
// capture shows that the current gate is wrong.

ultramodern::renderer::GraphicsConfig load_and_apply_graphics() {
    ultramodern::renderer::GraphicsConfig cfg = default_graphics_config();
    const std::filesystem::path path = graphics_json_path();
    const ReadResult r = read_graphics_file(path, cfg);

    ultramodern::renderer::set_graphics_config(cfg);
    g_current_graphics = cfg;
    // Write the merged config back so the on-disk file is always complete and
    // editable (new keys appear with their defaults after an upgrade) -- but NEVER
    // overwrite a file that failed to parse: a hand-edit typo must stay recoverable,
    // not be replaced by defaults.
    if (r != ReadResult::Unparseable) {
        save_graphics(cfg);
    }
    std::fprintf(stderr, "[config] graphics config: %s\n", path.string().c_str());

    bool easy_turbo = false;
    const std::filesystem::path enhancements_path = enhancements_json_path();
    const ReadResult enhancements_result = read_enhancements_file(enhancements_path, easy_turbo);
    g_easy_turbo_boost.store(easy_turbo);
    if (enhancements_result != ReadResult::Unparseable) write_enhancements(easy_turbo);
    std::fprintf(stderr, "[config] enhancements config: %s\n", enhancements_path.string().c_str());
    return cfg;
}

ultramodern::renderer::GraphicsConfig current_graphics() {
    return g_current_graphics;
}

void apply_graphics(const ultramodern::renderer::GraphicsConfig& cfg, bool apply_live) {
    const auto before = g_current_graphics;
    g_current_graphics = cfg;
    nlohmann::json updates = nlohmann::json::object();
    if (before.res_option != cfg.res_option) updates["res_option"] = cfg.res_option;
    if (before.wm_option != cfg.wm_option) updates["wm_option"] = cfg.wm_option;
    if (before.hr_option != cfg.hr_option) updates["hr_option"] = cfg.hr_option;
    if (before.api_option != cfg.api_option) updates["api_option"] = cfg.api_option;
    if (before.ar_option != cfg.ar_option) updates["ar_option"] = cfg.ar_option;
    if (before.msaa_option != cfg.msaa_option) updates["msaa_option"] = cfg.msaa_option;
    if (before.rr_option != cfg.rr_option) updates["rr_option"] = cfg.rr_option;
    if (before.hpfb_option != cfg.hpfb_option) updates["hpfb_option"] = cfg.hpfb_option;
    if (before.rr_manual_value != cfg.rr_manual_value) updates["rr_manual_value"] = cfg.rr_manual_value;
    if (before.ds_option != cfg.ds_option) updates["ds_option"] = cfg.ds_option;
    if (before.developer_mode != cfg.developer_mode) updates["developer_mode"] = cfg.developer_mode;
    if (apply_live) ultramodern::renderer::set_graphics_config(cfg);
    queue_graphics_updates(updates);
}

void apply_graphics_settings(const ultramodern::renderer::GraphicsConfig& cfg,
                             WindowSize size,
                             std::string texture_pack,
                             std::string texture_dump,
                             bool apply_live,
                             std::optional<bool> force_full_lod) {
    const auto before = g_current_graphics;
    g_current_graphics = cfg;

    nlohmann::json updates = nlohmann::json::object();
    if (before.res_option != cfg.res_option) updates["res_option"] = cfg.res_option;
    if (before.wm_option != cfg.wm_option) updates["wm_option"] = cfg.wm_option;
    if (before.hr_option != cfg.hr_option) updates["hr_option"] = cfg.hr_option;
    if (before.api_option != cfg.api_option) updates["api_option"] = cfg.api_option;
    if (before.ar_option != cfg.ar_option) updates["ar_option"] = cfg.ar_option;
    if (before.msaa_option != cfg.msaa_option) updates["msaa_option"] = cfg.msaa_option;
    if (before.rr_option != cfg.rr_option) updates["rr_option"] = cfg.rr_option;
    if (before.hpfb_option != cfg.hpfb_option) updates["hpfb_option"] = cfg.hpfb_option;
    if (before.rr_manual_value != cfg.rr_manual_value) updates["rr_manual_value"] = cfg.rr_manual_value;
    if (before.ds_option != cfg.ds_option) updates["ds_option"] = cfg.ds_option;
    if (before.developer_mode != cfg.developer_mode) updates["developer_mode"] = cfg.developer_mode;

    const auto clamped_size = clamp_window_size(size);
    {
        std::lock_guard<std::mutex> lock(g_texture_mutex);
        if (g_window_size.width != clamped_size.width)
            updates["window_width"] = clamped_size.width;
        if (g_window_size.height != clamped_size.height)
            updates["window_height"] = clamped_size.height;
        g_window_size = clamped_size;

        // Environment overrides are read-only in the menu and must not be
        // copied into the user's JSON file by an Apply action.
        if (!std::getenv("AERO_TEXTURE_PACK") && g_texture_pack != texture_pack) {
            g_texture_pack = std::move(texture_pack);
            updates["texture_pack"] = g_texture_pack;
        }
        if (!std::getenv("AERO_TEXTURE_DUMP") && g_texture_dump != texture_dump) {
            g_texture_dump = std::move(texture_dump);
            updates["texture_dump"] = g_texture_dump;
        }
    }

    // The Graphics page commits all of its options as one transaction. An
    // environment override is read-only, and an unchanged value must not
    // create a spurious dirty write when the user presses Apply.
    if (force_full_lod && std::getenv("AERO_FORCE_FULL_LOD") == nullptr &&
        g_force_full_lod.load() != *force_full_lod) {
        g_force_full_lod.store(*force_full_lod);
        updates["force_full_lod"] = *force_full_lod;
    }

    if (apply_live) ultramodern::renderer::set_graphics_config(cfg);
    queue_graphics_updates(updates);
}

void update_saved_window_mode(ultramodern::renderer::WindowMode wm) {
    g_current_graphics.wm_option = wm;
    queue_graphics_updates({{"wm_option", wm}});
}

void save_graphics(const ultramodern::renderer::GraphicsConfig& cfg) {
    // Drain first. This writes a whole document, so a queued update left behind
    // would be clobbered here and then merged back from a stale base by the worker.
    flush_config_writes();
    const std::filesystem::path path = graphics_json_path();
    const nlohmann::json document = to_json(cfg);
    write_graphics_json(path, document);
    PersistenceState& state = persistence();
    std::lock_guard lock(state.mutex);
    state.base_document = document;
}

void flush_config_writes() {
    PersistenceState& state = persistence();
    std::unique_lock lock(state.mutex);
    if (!has_pending_work(state) && !state.writing) return;
    if (!state.worker_started) {
        // No worker exists, so nothing will ever drain this: thread creation failed
        // earlier and the setters left their changes queued. Writing here blocks, but
        // returning with the queue unwritten would break this function's contract and
        // silently drop the user's settings on the way out.
        const nlohmann::json updates = std::move(state.graphics_updates);
        const nlohmann::json enhancements = std::move(state.enhancements_document);
        const nlohmann::json base = state.base_document;
        state.graphics_updates = nlohmann::json::object();
        state.enhancements_document = nullptr;
        nlohmann::json written_base;
        uint64_t writes = 0;
        try {
            writes = write_batch(updates, enhancements, base, written_base);
        } catch (const std::exception& e) {
            std::fprintf(stderr, "[config] inline settings write failed (%s)\n", e.what());
        }
        if (!written_base.is_null()) state.base_document = std::move(written_base);
        state.completed_writes += writes;
        return;
    }
    state.flush_requested = true;
    state.cv.notify_all();
    state.cv.wait(lock, [&state] { return !has_pending_work(state) && !state.writing; });
    // A flush that arrives while the worker is already writing is never consumed by
    // the worker's own reset; clear it here so the next batch keeps its debounce.
    state.flush_requested = false;
}

uint64_t config_write_count() {
    PersistenceState& state = persistence();
    std::lock_guard lock(state.mutex);
    return state.completed_writes;
}

WindowSize window_size() {
    return g_window_size;
}

void set_window_size(WindowSize size) {
    g_window_size = clamp_window_size(size);
    queue_graphics_updates({
        {"window_width", g_window_size.width},
        {"window_height", g_window_size.height},
    });
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
    std::lock_guard<std::mutex> lock(g_texture_mutex);
    return path_from_env_or("AERO_TEXTURE_PACK", g_texture_pack);
}

std::string texture_dump_dir() {
    std::lock_guard<std::mutex> lock(g_texture_mutex);
    return path_from_env_or("AERO_TEXTURE_DUMP", g_texture_dump);
}

void set_texture_pack_path(std::string path) {
    {
        std::lock_guard<std::mutex> lock(g_texture_mutex);
        g_texture_pack = std::move(path);
    }
    queue_graphics_updates({{"texture_pack", texture_pack_path()}});
}

void set_texture_dump_dir(std::string path) {
    {
        std::lock_guard<std::mutex> lock(g_texture_mutex);
        g_texture_dump = std::move(path);
    }
    queue_graphics_updates({{"texture_dump", texture_dump_dir()}});
}

// AERO_FOG_MATCH_1P=1/0 overrides the JSON key for headless capture/testing.
bool widescreen_fog_match() {
    if (const char* v = std::getenv("AERO_FOG_MATCH_1P")) {
        return v[0] == '1';
    }
    return g_widescreen_fog_match.load();
}

void set_widescreen_fog_match(bool enabled) {
    g_widescreen_fog_match.store(enabled);
    queue_graphics_updates({{"widescreen_fog_match", enabled}});
}

// AERO_DRAW_DISTANCE_SCALE=<float> overrides the JSON key for A/B capture runs
// (1 = original game's 500-unit far plane). Parsed once; called every frame from
// the game thread via the native guPerspectiveF.
float draw_distance_scale() {
    if (const char* v = std::getenv("AERO_DRAW_DISTANCE_SCALE")) {
        return clamp_draw_distance(std::strtof(v, nullptr));
    }
    return g_draw_distance_scale.load();
}

void set_draw_distance_scale(float scale) {
    const float clamped = clamp_draw_distance(scale);
    g_draw_distance_scale.store(clamped);
    queue_graphics_updates({{"draw_distance_scale", clamped}});
}

// AERO_FULL_TRACK=1/0 overrides the JSON key for A/B capture runs
// (0 = the original game's 3-zone visibility window).
bool full_track() {
    if (const char* v = std::getenv("AERO_FULL_TRACK")) {
        return v[0] == '1';
    }
    return g_full_track.load();
}

void set_full_track(bool enabled) {
    g_full_track.store(enabled);
    queue_graphics_updates({{"full_track", enabled}});
}

bool force_full_lod() {
    if (const char* v = std::getenv("AERO_FORCE_FULL_LOD")) {
        return v[0] == '1';
    }
    return g_force_full_lod.load();
}

void set_force_full_lod(bool enabled) {
    g_force_full_lod.store(enabled);
    queue_graphics_updates({{"force_full_lod", enabled}});
}

extern "C" int aero_force_full_lod_enabled(void) {
    return force_full_lod();
}

// AERO_EASY_TURBO=1/0 overrides the JSON key for A/B capture runs (0 = original
// button sequences only). Read from the P1 semantic-control hook.
bool easy_turbo_boost() {
    static const int env_override = []() {
        const char* v = std::getenv("AERO_EASY_TURBO");
        if (v == nullptr || v[0] == '\0') return -1;
        return v[0] == '1' ? 1 : 0;
    }();
    if (env_override >= 0) return env_override != 0;
    return g_easy_turbo_boost.load();
}

void set_easy_turbo_boost(bool enabled) {
    g_easy_turbo_boost.store(enabled);
    save_enhancements(enabled);
}

// C-linkage bridge for src/aero_turbo_boost.c (plain C TU).
extern "C" int aero_easy_turbo_enabled(void) {
    return easy_turbo_boost() ? 1 : 0;
}

// AERO_HARNESS_LOG=1 enables the low-rate hot-thread diagnostics (see aero_config.h).
// The value is cached because these queries run on the graphics and VI threads.
bool harness_log() {
    static const bool enabled = []() {
        const char* v = std::getenv("AERO_HARNESS_LOG");
        return v != nullptr && v[0] == '1';
    }();
    return enabled;
}

std::FILE* open_frame_log(const char* suffix) {
    const char* p = std::getenv("AERO_FRAME_LOG");
    if (p == nullptr || p[0] == '\0') return nullptr;
    std::string path = p;
    if (suffix != nullptr && suffix[0] != '\0') path += suffix;
    return std::fopen(path.c_str(), "w");
}

// AERO_SKY_MATCH_1P=1/0 overrides the JSON key for headless capture/testing.
bool widescreen_sky_match() {
    if (const char* v = std::getenv("AERO_SKY_MATCH_1P")) {
        return v[0] == '1';
    }
    return g_widescreen_sky_match.load();
}

void set_widescreen_sky_match(bool enabled) {
    g_widescreen_sky_match.store(enabled);
    queue_graphics_updates({{"widescreen_sky_match", enabled}});
}

} // namespace config
} // namespace aero
