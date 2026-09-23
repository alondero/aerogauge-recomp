#include "aero_mods.h"

#include "librecomp/mods.hpp"

#include <algorithm>
#include <mutex>
#include <string>
#include <unordered_set>
#include <utility>

namespace aero::mods {
namespace {

// Runtime callbacks run under the mod-context lock. Store an ordered snapshot
// here so RT64 never has to call back into that context from its render thread.
std::unordered_set<std::string> enabled_texture_packs;
std::mutex update_mutex;
std::optional<std::vector<std::filesystem::path>> pending_texture_paths;

void publish_texture_paths(recomp::mods::ModContext& context) {
    std::vector<std::string> mod_ids;
    {
        std::lock_guard lock(update_mutex);
        mod_ids.assign(enabled_texture_packs.begin(), enabled_texture_packs.end());
    }

    // RT64 applies later directories first, so send the manager's package
    // order from lowest priority to highest priority.
    std::sort(mod_ids.begin(), mod_ids.end(), [&](const auto& left, const auto& right) {
        return context.get_mod_order_index(left) > context.get_mod_order_index(right);
    });

    std::vector<std::filesystem::path> paths;
    paths.reserve(mod_ids.size());
    for (const auto& id : mod_ids) paths.push_back(context.get_mod_filename(id));

    std::lock_guard lock(update_mutex);
    pending_texture_paths = std::move(paths);
}

void texture_pack_enabled(recomp::mods::ModContext& context,
                          const recomp::mods::ModHandle& mod) {
    {
        std::lock_guard lock(update_mutex);
        enabled_texture_packs.insert(mod.manifest.mod_id);
    }
    publish_texture_paths(context);
}

void texture_pack_disabled(recomp::mods::ModContext& context,
                           const recomp::mods::ModHandle& mod) {
    {
        std::lock_guard lock(update_mutex);
        enabled_texture_packs.erase(mod.manifest.mod_id);
    }
    publish_texture_paths(context);
}

} // namespace

void register_content() {
    const recomp::mods::ModContentTypeId texture_packs =
        recomp::mods::register_mod_content_type({
            .content_filename = "rt64.json",
            .allow_runtime_toggle = true,
            .on_enabled = texture_pack_enabled,
            .on_disabled = texture_pack_disabled,
            .on_reordered = publish_texture_paths,
        });

    // RT64 packs are ZIP archives without a code-mod manifest. NRM packages
    // retain the runtime's normal manifest and content checks.
    recomp::mods::register_mod_container_type(".rtz", {texture_packs}, false);
}

void discard_failed_load() {
    std::lock_guard lock(update_mutex);
    enabled_texture_packs.clear();
    pending_texture_paths = std::vector<std::filesystem::path>{};
}

std::optional<std::vector<std::filesystem::path>> take_texture_pack_update() {
    std::lock_guard lock(update_mutex);
    auto paths = std::move(pending_texture_paths);
    pending_texture_paths.reset();
    return paths;
}

} // namespace aero::mods
