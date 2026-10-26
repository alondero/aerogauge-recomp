#ifndef AERO_LOG_H
#define AERO_LOG_H

#include <stdbool.h>

// First-party logging and the Windows console policy. C linkage keeps this API
// usable from the small C translation units (aero_savestate.c, aero_warp.c,
// aero_hud_widescreen.c) as well as the C++ ones.
//
// The port ships as a GUI-subsystem executable on Windows, so a launch from
// Explorer has no console to write to. Diagnostics therefore go to a
// per-session log file first; the console is opt-in via --console.

// The threshold is read on hot paths, so the read must not be a data race.
// GCC and Clang provide the __atomic builtins; MSVC does not, so its
// interlocked operations stand in. The Windows host-test job builds this file
// with MSVC, so both paths have to compile. MSVC's underscore-prefixed
// intrinsic forms are the ones available without pulling in windows.h.
#if defined(_MSC_VER)
    #include <intrin.h>
    #define AERO_LOG_LOAD(ptr) \
        ((int)_InterlockedCompareExchange((volatile long*)(ptr), 0, 0))
    #define AERO_LOG_STORE(ptr, value) \
        _InterlockedExchange((volatile long*)(ptr), (long)(value))
#else
    #define AERO_LOG_LOAD(ptr) __atomic_load_n((ptr), __ATOMIC_ACQUIRE)
    #define AERO_LOG_STORE(ptr, value) __atomic_store_n((ptr), (value), __ATOMIC_RELEASE)
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Severity levels. Lower is more severe; a threshold admits every message
// whose level number is less than or equal to the threshold.
typedef enum AeroLogLevel {
    AERO_LEVEL_ERROR = 0,
    AERO_LEVEL_WARN  = 1,
    AERO_LEVEL_INFO  = 2,
    AERO_LEVEL_DEBUG = 3,
    AERO_LEVEL_TRACE = 4
} AeroLogLevel;

// Threshold for the session log file. The file always records INFO and
// above, so a field report still has boot context when the console is quiet.
extern volatile int g_aero_log_file_threshold;
// Threshold for the console sink. This is exactly the level the user asked
// for, so --log-level=warn narrows the console without emptying the log.
extern volatile int g_aero_log_console_threshold;

// Parses --console, --log-level and --verbose. Returns false when a switch is
// malformed; aero_log_last_error() then describes the problem.
bool aero_log_parse_args(int argc, char** argv);
const char* aero_log_last_error(void);

// Opens the session log file and decides whether a console sink exists.
// Safe to call more than once; the second call is a no-op.
bool aero_log_initialize(void);
void aero_log_shutdown(void);
// Pushes buffered records to the session file without closing it. Used on
// exit paths that call _Exit() and therefore skip stream destruction. The
// lock is taken opportunistically: a crash handler must never block.
void aero_log_flush(void);

// Path of the current session log file, or an empty string when none was
// opened. The returned pointer stays valid until the next call on this thread.
const char* aero_log_path(void);
bool aero_log_console_requested(void);
// True when the process has a usable stderr sink: either a console the user
// asked for, or one inherited from the parent. A GUI-subsystem launch from
// Explorer has neither, which is why the console output is opt-in.
bool aero_log_stderr_available(void);
AeroLogLevel aero_log_level(void);

// Writes one record to every enabled sink.
void aero_log_write(AeroLogLevel level, const char* tag, const char* format, ...);

#ifdef __cplusplus
} // extern "C"
#endif

// One relaxed atomic load keeps a disabled probe to a single comparison.
// The file threshold is the gate for both sinks: it is never lower than the
// console threshold, so anything the console would show is also in the file.
static inline bool aero_log_would_emit(AeroLogLevel level) {
    return (int)level <= AERO_LOG_LOAD(&g_aero_log_file_threshold);
}

// `tag` MUST be a string literal. Arguments stay inside the untaken branch,
// so an expensive formatting expression is not evaluated at a disabled level.
#define AERO_LOG_AT(level, tag, ...) \
    do { if (aero_log_would_emit(level)) aero_log_write((level), (tag), __VA_ARGS__); } while (0)

// Existing probe diagnostics are debug-level: they describe internal paths,
// not player-facing events.
#define AERO_LOG(tag, ...)       AERO_LOG_AT(AERO_LEVEL_DEBUG, tag, __VA_ARGS__)
#define AERO_LOG_ERROR(tag, ...) AERO_LOG_AT(AERO_LEVEL_ERROR, tag, __VA_ARGS__)
#define AERO_LOG_WARN(tag, ...)  AERO_LOG_AT(AERO_LEVEL_WARN, tag, __VA_ARGS__)
#define AERO_LOG_INFO(tag, ...)  AERO_LOG_AT(AERO_LEVEL_INFO, tag, __VA_ARGS__)
#define AERO_LOG_DEBUG(tag, ...) AERO_LOG_AT(AERO_LEVEL_DEBUG, tag, __VA_ARGS__)
#define AERO_LOG_TRACE(tag, ...) AERO_LOG_AT(AERO_LEVEL_TRACE, tag, __VA_ARGS__)

#endif // AERO_LOG_H
