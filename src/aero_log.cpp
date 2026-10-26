
#include "aero_paths.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>
#include "aero_log.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <process.h>
#else
#include <unistd.h>
#endif

namespace {

std::atomic_bool g_console_requested{false};
std::atomic_bool g_stderr_sink{false};
bool g_initialized = false;
int g_first_free_argument = -1;
std::mutex g_mutex;
std::ofstream g_file;
std::string g_path;
std::string g_error;

const char* level_name(AeroLogLevel level) {
    switch (level) {
        case AERO_LEVEL_ERROR: return "error";
        case AERO_LEVEL_WARN:  return "warn";
        case AERO_LEVEL_INFO:  return "info";
        case AERO_LEVEL_DEBUG: return "debug";
        case AERO_LEVEL_TRACE: return "trace";
    }
    return "unknown";
}

bool parse_level(const char* value, AeroLogLevel* out) {
    if (value == nullptr || out == nullptr) return false;
    struct Name { const char* text; AeroLogLevel level; };
    constexpr Name names[] = {
        {"error", AERO_LEVEL_ERROR}, {"warn", AERO_LEVEL_WARN},
        {"warning", AERO_LEVEL_WARN}, {"info", AERO_LEVEL_INFO},
        {"debug", AERO_LEVEL_DEBUG}, {"trace", AERO_LEVEL_TRACE}
    };
    for (const Name& name : names) {
        if (std::strcmp(value, name.text) == 0) {
            *out = name.level;
            return true;
        }
    }
    return false;
}

std::filesystem::path log_directory() {
    if (const char* override_dir = std::getenv("AERO_LOG_DIR");
        override_dir != nullptr && override_dir[0] != '\0') {
        return std::filesystem::path{override_dir};
    }
    return aero::paths::log_dir();
}

unsigned long process_id() {
#if defined(_WIN32)
    return static_cast<unsigned long>(_getpid());
#else
    return static_cast<unsigned long>(getpid());
#endif
}

// True when the process has a usable stderr handle. A GUI-subsystem launch
// from Explorer has none, which is why the console is opt-in.
bool stderr_available() {
#if defined(_WIN32)
    HANDLE error_handle = GetStdHandle(STD_ERROR_HANDLE);
    return error_handle != nullptr && error_handle != INVALID_HANDLE_VALUE &&
        GetFileType(error_handle) != FILE_TYPE_UNKNOWN;
#else
    return true;
#endif
}

void open_console() {
#if defined(_WIN32)
    // A test harness or terminal may already have supplied a pipe/console
    // handle even when GetConsoleWindow() is null. Preserve that inherited
    // sink; only create a new window for an Explorer-style launch.
    HANDLE error_handle = GetStdHandle(STD_ERROR_HANDLE);
    const bool inherited_sink = error_handle != nullptr &&
        error_handle != INVALID_HANDLE_VALUE && GetFileType(error_handle) != FILE_TYPE_UNKNOWN;
    // An existing console already has correctly inherited CRT handles. This
    // also preserves deliberate stdout/stderr redirection from a shell.
    if (GetConsoleWindow() != nullptr || inherited_sink) {
        g_stderr_sink.store(true, std::memory_order_release);
        return;
    }
    // A GUI process started from cmd/PowerShell often has no inherited
    // standard handles. Attach to that parent before falling back to a new
    // console, so --console behaves like a normal CLI switch.
    if (AttachConsole(ATTACH_PARENT_PROCESS) == 0 && AllocConsole() == 0) return;
    g_stderr_sink.store(true, std::memory_order_release);
    // freopen() rather than freopen_s(): the two compilers disagree on the
    // secure variant's fourth parameter, and the plain form is standard C.
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
    (void)freopen("CONIN$", "r", stdin);
    (void)freopen("CONOUT$", "w", stdout);
    (void)freopen("CONOUT$", "w", stderr);
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
    clearerr(stdin);
    clearerr(stdout);
    clearerr(stderr);
#endif
}

void prune_old_logs(const std::filesystem::path& directory) {
    std::vector<std::filesystem::directory_entry> files;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(directory, ec)) {
        if (ec) break;
        const auto filename = entry.path().filename();
        if (entry.is_regular_file(ec) && filename.extension() == ".log" &&
            filename.stem().string().rfind("aerogauge-", 0) == 0)
            files.push_back(entry);
    }
    // The name starts with a sortable timestamp, so sorting descending puts
    // the newest first. Keep the ten newest and drop the rest.
    std::sort(files.begin(), files.end(), [](const auto& a, const auto& b) {
        return a.path().filename() > b.path().filename();
    });
    for (size_t i = 10; i < files.size(); ++i) (void)std::filesystem::remove(files[i], ec);
}

void write_threshold(AeroLogLevel requested) {
    // The console shows exactly what was asked for. The session file never
    // drops below INFO: the file is the main diagnostic channel for a player
    // reporting a bug, and it costs no frame time to write there.
    const int console = static_cast<int>(requested);
    const int file = console > static_cast<int>(AERO_LEVEL_INFO)
        ? console
        : static_cast<int>(AERO_LEVEL_INFO);
    AERO_LOG_STORE(&g_aero_log_console_threshold, console);
    AERO_LOG_STORE(&g_aero_log_file_threshold, file);
}

} // namespace

extern "C" {
volatile int g_aero_log_file_threshold = AERO_LEVEL_INFO;
volatile int g_aero_log_console_threshold = AERO_LEVEL_INFO;
}

extern "C" bool aero_log_parse_args(int argc, char** argv) {
    g_error.clear();
    g_console_requested.store(false, std::memory_order_release);
    g_first_free_argument = -1;
    AeroLogLevel requested = AERO_LEVEL_INFO;
    bool valid = true;
    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        if (arg == nullptr) continue;
        if (std::strcmp(arg, "--console") == 0) {
            g_console_requested.store(true, std::memory_order_release);
        } else if (std::strcmp(arg, "--verbose") == 0) {
            requested = AERO_LEVEL_DEBUG;
        } else if (std::strncmp(arg, "--log-level=", 12) == 0) {
            if (!parse_level(arg + 12, &requested)) {
                g_error = "--log-level must be error, warn, info, debug, or trace";
                valid = false;
            }
        } else if (std::strcmp(arg, "--log-level") == 0) {
            if (i + 1 >= argc) {
                g_error = "--log-level requires error, warn, info, debug, or trace";
                valid = false;
            } else if (!parse_level(argv[++i], &requested)) {
                g_error = "--log-level requires error, warn, info, debug, or trace";
                valid = false;
            }
        } else if (arg[0] != '-') {
            // Not a switch and not switch-shaped: this argument belongs to
            // the port. The first one is the ROM path.
            if (g_first_free_argument < 0) g_first_free_argument = i;
        }
    }
    // AERO_HARNESS_LOG predates the level system and means "show the
    // low-rate hot-thread diagnostics". Treat it as an alias for --verbose so
    // existing diagnostic runs keep working.
    if (const char* harness = std::getenv("AERO_HARNESS_LOG");
        harness != nullptr && harness[0] != '\0' && std::strcmp(harness, "0") != 0) {
        if (requested < AERO_LEVEL_DEBUG) requested = AERO_LEVEL_DEBUG;
    }
    write_threshold(requested);
    return valid;
}

extern "C" int aero_log_first_free_argument(void) { return g_first_free_argument; }

extern "C" const char* aero_log_last_error(void) { return g_error.c_str(); }

extern "C" bool aero_log_initialize(void) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_initialized) {
        return g_file.good() || g_stderr_sink.load(std::memory_order_acquire);
    }
    if (g_console_requested.load(std::memory_order_acquire)) open_console();
#if defined(_WIN32)
    if (!g_stderr_sink.load(std::memory_order_acquire)) {
        HANDLE error_handle = GetStdHandle(STD_ERROR_HANDLE);
        g_stderr_sink.store(error_handle != nullptr && error_handle != INVALID_HANDLE_VALUE &&
                                GetFileType(error_handle) != FILE_TYPE_UNKNOWN,
                            std::memory_order_release);
    }
#else
    g_stderr_sink.store(true, std::memory_order_release);
#endif
    const std::filesystem::path directory = log_directory();
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);
    if (!ec) {
        const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        std::tm tm{};
#if defined(_WIN32)
        localtime_s(&tm, &now);
#else
        localtime_r(&now, &tm);
#endif
        std::ostringstream name;
        name << "aerogauge-" << std::put_time(&tm, "%Y%m%d-%H%M%S")
             << "-" << process_id() << ".log";
        const std::filesystem::path path = directory / name.str();
        g_file.open(path, std::ios::out | std::ios::app);
        if (g_file.good()) {
            g_path = path.string();
            prune_old_logs(directory);
        }
    }
    g_initialized = true;
    return g_file.good() || g_stderr_sink.load(std::memory_order_acquire);
}

extern "C" const char* aero_log_path(void) {
    // Return a per-thread snapshot so callers never retain a pointer into the
    // mutable path string while shutdown/reinitialization holds g_mutex.
    thread_local std::string path_snapshot;
    std::lock_guard<std::mutex> lock(g_mutex);
    path_snapshot = g_path;
    return path_snapshot.c_str();
}

extern "C" void aero_log_shutdown(void) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_file.good()) g_file.flush();
    g_file.close();
    g_path.clear();
    g_stderr_sink.store(false, std::memory_order_release);
    g_initialized = false;
}

extern "C" void aero_log_flush(void) {
    // Opportunistic lock: an exit path that is also a crash handler must
    // never block on a mutex the crashing thread might still hold.
    std::unique_lock<std::mutex> lock(g_mutex, std::try_to_lock);
    if (!lock.owns_lock()) return;
    if (g_file.good()) g_file.flush();
}

extern "C" bool aero_log_console_requested(void) {
    return g_console_requested.load(std::memory_order_acquire);
}

extern "C" bool aero_log_stderr_available(void) {
    return g_stderr_sink.load(std::memory_order_acquire);
}

extern "C" AeroLogLevel aero_log_level(void) {
    return static_cast<AeroLogLevel>(AERO_LOG_LOAD(&g_aero_log_console_threshold));
}

extern "C" void aero_log_write(AeroLogLevel level, const char* tag, const char* format, ...) {
    if (!aero_log_would_emit(level)) return;
    std::lock_guard<std::mutex> lock(g_mutex);
    const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &now);
#else
    localtime_r(&now, &tm);
#endif
    char timestamp[32]{};
    std::strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &tm);
    char message[4096]{};
    va_list args;
    va_start(args, format);
    std::vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    std::ostringstream line;
    line << '[' << timestamp << "] [" << level_name(level) << "] ["
         << (tag != nullptr ? tag : "log") << "] " << message;
    const std::string text = line.str();
    const bool to_console = level <=
        static_cast<AeroLogLevel>(AERO_LOG_LOAD(&g_aero_log_console_threshold));
    if (!g_initialized) {
        // The logger was never started. A host test that links the port's
        // source files directly reaches this path. Fall back to stderr rather
        // than dropping the message, which is what the port did before the
        // logger existed.
        if (to_console && stderr_available())
            std::fwrite(text.data(), 1, text.size(), stderr);
        return;
    }
    if (g_file.good()) g_file << text;
    if (to_console && g_stderr_sink.load(std::memory_order_acquire))
        std::fwrite(text.data(), 1, text.size(), stderr);
    if (level == AERO_LEVEL_ERROR) {
        if (g_file.good()) g_file.flush();
        if (to_console && g_stderr_sink.load(std::memory_order_acquire)) std::fflush(stderr);
    }
}
