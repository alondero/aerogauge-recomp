#ifndef AERO_PATHS_H
#define AERO_PATHS_H

#include <cstdlib>
#include <filesystem>

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
