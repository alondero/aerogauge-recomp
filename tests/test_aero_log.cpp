// Host test for the logging policy: argument parsing, the two-tier threshold
// (console vs session file), and what actually reaches the file.
//
// The port ships as a GUI-subsystem executable on Windows, so the session log
// file is the only diagnostic channel for a launch from Explorer. These
// assertions pin that contract.

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "aero_log.h"

namespace {

void set_env(const char* name, const std::string& value) {
#if defined(_WIN32)
    _putenv_s(name, value.c_str());
#else
    setenv(name, value.c_str(), 1);
#endif
}

void clear_env(const char* name) {
#if defined(_WIN32)
    _putenv_s(name, "");
#else
    unsetenv(name);
#endif
}

} // namespace

int main() {
    int failures = 0;
    auto expect = [&](bool condition, const char* message) {
        if (!condition) {
            std::fprintf(stderr, "FAIL: %s\n", message);
            ++failures;
        }
    };

    // --- argument parsing -------------------------------------------------
    {
        char a0[] = "game";
        char a1[] = "--log-level=bogus";
        char* args[] = {a0, a1};
        expect(!aero_log_parse_args(2, args), "invalid log level is rejected");
        expect(std::string(aero_log_last_error()).find("must be") != std::string::npos,
               "invalid log level reports a useful error");
    }

    {
        char a0[] = "game";
        char a1[] = "--log-level=bogus";
        char a2[] = "--console";
        char* args[] = {a0, a1, a2};
        expect(!aero_log_parse_args(3, args), "invalid log level remains an error");
        expect(aero_log_console_requested(),
               "console switch is still honored when another switch is invalid");
    }

    {
        char a0[] = "game";
        char a1[] = "--log-level";
        char* args[] = {a0, a1};
        expect(!aero_log_parse_args(2, args), "--log-level without a value is rejected");
    }

    // --- thresholds --------------------------------------------------------
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() / "aero-log-test";
    std::error_code ec;
    std::filesystem::remove_all(directory, ec);
    set_env("AERO_LOG_DIR", directory.string());

    // The console shows exactly the requested level.
    {
        char a0[] = "game";
        char a1[] = "--verbose";
        char a2[] = "--log-level=error";
        char* args[] = {a0, a1, a2};
        expect(aero_log_parse_args(3, args), "valid log arguments are accepted");
        expect(aero_log_level() == AERO_LEVEL_ERROR,
               "last log level argument wins deterministically");
    }

    // The session file keeps INFO and above even when the console is narrowed:
    // the file is the diagnostic channel for a player reporting a bug.
    {
        char a0[] = "game";
        char a1[] = "--log-level=error";
        char* args[] = {a0, a1};
        expect(aero_log_parse_args(2, args), "log level argument is accepted");
        expect(aero_log_level() == AERO_LEVEL_ERROR, "console threshold follows the request");
        expect(aero_log_would_emit(AERO_LEVEL_INFO),
               "session file threshold floors at INFO");
        expect(aero_log_would_emit(AERO_LEVEL_WARN),
               "session file keeps warnings while the console is narrowed");
        expect(!aero_log_would_emit(AERO_LEVEL_DEBUG),
               "debug records stay out of a default session file");
    }

    // --verbose raises both sinks.
    {
        char a0[] = "game";
        char a1[] = "--verbose";
        char* args[] = {a0, a1};
        expect(aero_log_parse_args(2, args), "verbose argument is accepted");
        expect(aero_log_level() == AERO_LEVEL_DEBUG, "verbose raises the console threshold");
        expect(aero_log_would_emit(AERO_LEVEL_DEBUG), "verbose raises the session file threshold");
    }

    // --- what reaches the file --------------------------------------------
    {
        char a0[] = "game";
        char a1[] = "--log-level=error";
        char* args[] = {a0, a1};
        expect(aero_log_parse_args(2, args), "log level argument is accepted");
        expect(aero_log_initialize(), "logger creates the session file");
        expect(std::string(aero_log_path()).find("aerogauge-") != std::string::npos,
               "logger exposes the session file path");

        aero_log_write(AERO_LEVEL_DEBUG, "test", "hidden debug\n");
        aero_log_write(AERO_LEVEL_WARN, "test", "kept warning\n");
        aero_log_write(AERO_LEVEL_ERROR, "test", "visible error %d\n", 7);
        aero_log_write(AERO_LEVEL_INFO, "test", "visible info\n");
        aero_log_flush();
        std::ifstream input(aero_log_path());
        std::stringstream contents;
        contents << input.rdbuf();
        const std::string text = contents.str();
        expect(text.find("hidden debug") == std::string::npos,
               "records below the file threshold do not reach the file");
        expect(text.find("[error] [test] visible error 7") != std::string::npos,
               "enabled messages include level and tag");
        expect(text.find("[warn] [test] kept warning") != std::string::npos,
               "the session file keeps warnings while the console is narrowed");
        expect(text.find("[info] [test] visible info") != std::string::npos,
               "the session file keeps info records while the console is narrowed");
        input.close();
    }

    // --- lifecycle ---------------------------------------------------------
    aero_log_shutdown();
    expect(aero_log_initialize(), "logger can be initialized again after shutdown");
    aero_log_shutdown();
    clear_env("AERO_LOG_DIR");
    std::filesystem::remove_all(directory, ec);
    return failures == 0 ? 0 : 1;
}
