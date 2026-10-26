#include "aero_rom_desktop.h"
#include "librecomp/game.hpp"
#include <cstdio>
#include <vector>
#include "aero_log.h"
#if !defined(__ANDROID__)
#include <SDL.h>
#include <nfd.h>
#endif

namespace aero::rom {
namespace {
constexpr uintmax_t RomSize = 8u * 1024u * 1024u;

const char* validation_error(recomp::RomValidationError error) {
    using Error = recomp::RomValidationError;
    switch (error) {
        case Error::FailedToOpen: return "Cannot read the selected ROM file.";
        case Error::NotARom: return "The selected file is not an N64 ROM.";
        case Error::IncorrectVersion: return "This AeroGauge ROM revision is not supported.";
        case Error::IncorrectRom: return "The selected ROM is not AeroGauge.";
        default: return "Unable to validate the selected ROM.";
    }
}

std::string supported_message(bool japan) {
    return japan ? "Use an unmodified 8 MiB USA or Japan Rev A dump (.z64, .n64, or .v64)."
                 : "This build requires an unmodified 8 MiB USA dump (.z64, .n64, or .v64).";
}
}

Selection select_startup_rom(const std::optional<std::filesystem::path>& explicit_path,
                             const recomp::GameEntry& usa,
                             const recomp::GameEntry* japan,
                             bool interactive) {
    const auto support = supported_message(japan != nullptr);
    const auto validate = [&](const std::filesystem::path& path) -> Validation {
        // Check the size before librecomp reads/inspects the file. In particular,
        // short files must not reach its ROM header/name reads. This is a size
        // preflight, not ROM identity validation; the full hash remains mandatory.
        std::error_code error;
        const auto size = std::filesystem::file_size(path, error);
        if (error) return {{}, "Cannot read the ROM file. " + support};
        if (size != RomSize) return {{}, "The ROM must be exactly 8 MiB. " + support};
        // Cached candidates are already normalized. Load/hash them directly,
        // avoiding a redundant 8 MiB rewrite on every launch. The runtime may
        // remove an invalid cache; it never changes an external source here.
        const auto load_cache = [&](const recomp::GameEntry& entry, Region region) -> Validation {
            if (!recomp::load_stored_rom(entry.game_id))
                return {{}, "The imported ROM copy is invalid. Select the original dump again."};
            return {region, {}};
        };
        if (path == recomp::get_config_path() / usa.stored_filename())
            return load_cache(usa, Region::USA);
        if (japan && path == recomp::get_config_path() / japan->stored_filename())
            return load_cache(*japan, Region::JapanRevA);
        auto status = recomp::select_rom(path, usa.game_id);
        const recomp::GameEntry* selected = &usa;
        auto region = Region::USA;
        if (japan && status == recomp::RomValidationError::IncorrectVersion) {
            status = recomp::select_rom(path, japan->game_id);
            selected = japan;
            region = Region::JapanRevA;
        }
        if (status != recomp::RomValidationError::Good)
            return {{}, std::string(validation_error(status)) + " " + support};
        // select_rom's Good result does not report cache-write failures. Verify
        // the stored bytes through the runtime loader before allowing startup;
        // start_game will consume this same stored copy, not the external path.
        const auto stored_size = std::filesystem::file_size(
            recomp::get_config_path() / selected->stored_filename(), error);
        if (error || stored_size != RomSize || !recomp::load_stored_rom(selected->game_id))
            return {{}, "Cannot read a valid imported ROM from the save directory. Check free space and permissions."};
        AERO_LOG_INFO("rom", "validated %s\n", region == Region::USA ? "USA" : "Japan Rev A");
        return {region, {}};
    };

    std::vector<std::filesystem::path> candidates;
    const auto append_existing = [&](const std::filesystem::path& path) {
        std::error_code error;
        if (std::filesystem::exists(path, error) && !error) candidates.push_back(path);
    };
    for (const auto* name : {"AeroGauge (USA).z64", "AeroGauge (USA).n64", "AeroGauge (USA).v64"})
        append_existing(name);
    if (japan) {
        for (const auto* name : {"AeroGauge (Japan) (Rev A).z64", "AeroGauge (Japan) (Rev A).n64",
                                 "AeroGauge (Japan) (Rev A).v64"}) append_existing(name);
    }
    append_existing(recomp::get_config_path() / usa.stored_filename());
    if (japan) append_existing(recomp::get_config_path() / japan->stored_filename());

    std::function<std::optional<std::filesystem::path>(const std::string&)> pick;
    bool picker_failed = false;
#if !defined(__ANDROID__)
    if (interactive) {
        pick = [&](const std::string& error) -> std::optional<std::filesystem::path> {
            const auto message = error + (error.find(support) == std::string::npos ? "\n\n" + support : "") +
                                 "\n\nChoose your ROM in the next dialog, or Cancel to exit.";
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "AeroGauge ROM setup", message.c_str(), nullptr);
            if (NFD_Init() != NFD_OKAY) {
                picker_failed = true;
                SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Cannot open ROM picker",
                    "The native file picker could not start. Pass your ROM path as the program's first argument.", nullptr);
                return {};
            }
            nfdu8char_t* raw_path = nullptr;
            const nfdu8filteritem_t filter{"N64 ROM", "z64,n64,v64"};
            const auto status = NFD_OpenDialogU8(&raw_path, &filter, 1, nullptr);
            std::optional<std::filesystem::path> path;
            if (status == NFD_OKAY) {
                path = std::filesystem::path(reinterpret_cast<const char8_t*>(raw_path));
                NFD_FreePathU8(raw_path);
            } else if (status == NFD_ERROR) {
                picker_failed = true;
                SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Cannot open ROM picker",
                    "The native file picker failed. Pass your ROM path as the program's first argument.", nullptr);
            }
            NFD_Quit();
            return path;
        };
    }
#else
    (void)interactive; // Android's Java launcher owns document selection.
#endif
    auto result = resolve(explicit_path, candidates, validate, pick);
    if (picker_failed) result = {{}, false, "Native ROM picker failed; pass a ROM path as the first argument."};
    if (!result.region && !result.cancelled) {
        AERO_LOG_WARN("rom", "%s\n", result.error.c_str());
#if !defined(__ANDROID__)
        if (interactive && !picker_failed) SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Unable to load AeroGauge ROM",
                                                 result.error.c_str(), nullptr);
#endif
    }
    return result;
}
} // namespace aero::rom
