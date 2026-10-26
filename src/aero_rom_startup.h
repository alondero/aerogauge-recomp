#ifndef AERO_ROM_STARTUP_H
#define AERO_ROM_STARTUP_H

#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <utility>

namespace aero::rom {

enum class Region { USA, JapanRevA };

struct Validation {
    std::optional<Region> region;
    std::string error;
};

struct Selection {
    std::optional<Region> region;
    bool cancelled = false;
    std::string error;
};

// Main-thread startup policy. An explicit argument is authoritative even when
// invalid. Automatic candidates are ordered local files, then runtime caches.
// A missing picker means noninteractive startup (headless/Android). Its null
// result means cancellation; validation failures allow another choice.
inline Selection resolve(
    const std::optional<std::filesystem::path>& explicit_path,
    std::span<const std::filesystem::path> candidates,
    const std::function<Validation(const std::filesystem::path&)>& validate,
    const std::function<std::optional<std::filesystem::path>(const std::string&)>& pick = {}) {
    if (explicit_path) {
        auto result = validate(*explicit_path);
        return {result.region, false, result.error};
    }
    std::string error = "Select your own AeroGauge ROM to play.";
    for (const auto& path : candidates) {
        auto result = validate(path);
        if (result.region) return {result.region, false, {}};
        error = std::move(result.error);
    }
    while (pick) {
        auto path = pick(error);
        if (!path) return {{}, true, {}};
        auto result = validate(*path);
        if (result.region) return {result.region, false, {}};
        error = std::move(result.error);
    }
    return {{}, false, error};
}

} // namespace aero::rom
#endif
