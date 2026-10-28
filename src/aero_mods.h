#ifndef AERO_MODS_H
#define AERO_MODS_H

#include <filesystem>
#include <optional>
#include <vector>

namespace aero::mods {

inline constexpr char game_id[] = "aerogauge";
// Code packages contain regional guest addresses and must not cross regions.
inline constexpr char japan_game_id[] = "aerogauge.jp.rev_a";
inline constexpr char texture_pack_container_extension[] = "rtz";

void register_content();
void discard_failed_load();

// Texture paths are handed to RT64 on its render thread, never while the
// runtime's mod-context lock is held.
std::optional<std::vector<std::filesystem::path>> take_texture_pack_update();

} // namespace aero::mods

#endif
