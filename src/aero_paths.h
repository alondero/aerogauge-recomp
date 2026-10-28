#ifndef AERO_PATHS_H
#define AERO_PATHS_H

#include <cstdlib>
#include <exception>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

// One per-user directory for every host file the port writes: graphics.json,
// enhancements.json, EEPROM/Pak saves, and RT64's log/imgui files.
//
// RT64's default detectDataPath(appId) is a second folder:
//   Windows: %LOCALAPPDATA%\<appId>
//   Linux:   ~/.config/.<appId>   (dotted, so not even the XDG config name)
// Point RT64 at app_data_dir() instead of letting it invent aerogauge-recomp.
namespace aero {
namespace paths {

inline constexpr const char* kAppFolderName = "AeroGaugeRecomp";

// User-supplied UTF-8 paths can fail conversion to the host's native encoding.
// An absent path tells the caller to skip the input, never to open the cwd.
inline std::optional<std::filesystem::path> from_utf8(std::string_view path) {
    if (path.empty()) return std::nullopt;
    try {
        return std::filesystem::path(std::u8string(
            reinterpret_cast<const char8_t*>(path.data()), path.size()));
    }
    catch (const std::exception&) {
        return std::nullopt;
    }
}

inline std::filesystem::path app_data_dir() {
#if defined(__ANDROID__)
    if (const char* storage = std::getenv("AERO_ANDROID_DATA_DIR")) {
        return std::filesystem::path{storage} / kAppFolderName;
    }
#endif
    std::error_code ec;
    if (std::filesystem::exists("portable.txt", ec)) {
        return std::filesystem::current_path();
    }
#if defined(_WIN32)
    if (const char* localappdata = std::getenv("LOCALAPPDATA")) {
        return std::filesystem::path{localappdata} / kAppFolderName;
    }
#else
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME")) {
        return std::filesystem::path{xdg} / kAppFolderName;
    }
    if (const char* home = std::getenv("HOME")) {
        return std::filesystem::path{home} / ".config" / kAppFolderName;
    }
#endif
    return std::filesystem::current_path();
}

// Per-session log files. The Windows build ships as a GUI-subsystem
// executable, so a launch from Explorer has no console to write to: the log
// file is where diagnostics survive for a player reporting a bug.
inline std::filesystem::path log_dir() {
    return app_data_dir() / "logs";
}

// Native crash reports. Written on the crash path, so the directory is
// created only when a report is actually produced.
inline std::filesystem::path crash_dir() {
    return app_data_dir() / "crashes";
}

// Values to copy onto RT64::ApplicationConfiguration. detect_data_path must
// stay false so RT64 does not rebuild a path from app_id.
struct RendererStorage {
    const char* app_id;
    bool detect_data_path;
    std::filesystem::path data_path;
};

inline RendererStorage renderer_storage() {
    return {kAppFolderName, false, app_data_dir()};
}

} // namespace paths
} // namespace aero

#endif
