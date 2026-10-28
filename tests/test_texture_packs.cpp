#include "aero_mods.h"
#include "librecomp/game.hpp"
#include "librecomp/mods.hpp"

#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string_view>

// The runtime exposes startup load/unload only on ModContext. Use its real
// context to exercise that boundary without starting a ROM or game threads.
extern std::unique_ptr<recomp::mods::ModContext> mod_context;
extern recomp::Version project_version;

namespace {
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

void expect_paths(std::initializer_list<std::filesystem::path> expected) {
    const auto update = aero::mods::take_texture_pack_update();
    require(update.has_value(), "missing renderer update");
    require(*update == std::vector<std::filesystem::path>(expected), "incorrect texture priority or paths");
    require(!aero::mods::take_texture_pack_update(), "renderer update was not consumed");
}
}

int main(int argc, char** argv) {
    try {
        require(argc == 3, "expected fixture root and region");
        const auto root = std::filesystem::path(argv[1]);
        const auto packs = root / u8"packs-\u65e5\u672c with spaces";
        const auto high = packs / "high.rtz";
        const auto low = packs / "low.rtz";

        recomp::register_config_path(root);
        project_version = recomp::Version{0, 0, 1};
        recomp::mods::initialize_mods();
        recomp::GameEntry game{};
        game.display_name = "AeroGauge texture test";
        game.game_id = u8"aerogauge.us";
        game.mod_game_id = aero::mods::game_id;
        recomp::register_game(game);
        auto japan = game;
        japan.game_id = u8"aerogauge.jp.rev_a";
        japan.mod_game_id = aero::mods::japan_game_id;
        recomp::register_game(japan);
        if (std::string_view(argv[2]) == "jp") game = japan;

        aero::mods::register_content();
        const auto errors = mod_context->scan_mod_folder(packs);
        require(errors.size() == 2, "expected invalid ZIP and manifest-less NRM errors");
        bool bad_zip = false, missing_manifest = false;
        for (const auto& error : errors) {
            bad_zip |= error.mod_path == packs / "broken.rtz" &&
                       error.error == recomp::mods::ModOpenError::InvalidZip;
            missing_manifest |= error.mod_path == packs / "missing-manifest.nrm" &&
                                error.error == recomp::mods::ModOpenError::NoManifest;
        }
        require(bad_zip && missing_manifest, "container validation changed");
        require(mod_context->num_opened_mods() == 2, "RTZ archives were not discovered");
        for (const char* id : {"high", "low"}) {
            const auto details = recomp::mods::get_details_for_mod(id);
            require(details && details->runtime_toggleable, "RTZ missing or not runtime-toggleable");
        }
        require(recomp::mods::get_all_mod_details(aero::mods::game_id).size() == 2 &&
                recomp::mods::get_all_mod_details(aero::mods::japan_game_id).size() == 2,
                "manifest-less textures must work with either regional game ID");

        recomp::mods::set_mod_index(game.mod_game_id, "high", 0);
        // Reordering before startup may publish an empty selection.
        aero::mods::take_texture_pack_update();
        recomp::mods::enable_mod("high", true);
        recomp::mods::enable_mod("low", true);
        require(!aero::mods::take_texture_pack_update(), "packs loaded before game startup");
        uint32_t ram_used = 0;
        require(mod_context->load_mods(game, "", nullptr, 0, ram_used).empty(), "texture startup failed");
        expect_paths({low, high}); // RT64 gives the last path priority.

        recomp::mods::enable_mod("high", false);
        expect_paths({low});
        recomp::mods::enable_mod("high", true);
        expect_paths({low, high});
        recomp::mods::set_mod_index(game.mod_game_id, "low", 0);
        expect_paths({high, low});
        recomp::mods::enable_mod("low", false);
        expect_paths({high});
        recomp::mods::enable_mod("high", false);
        expect_paths({});

        // Multiple menu actions between VIs coalesce to the latest selection.
        recomp::mods::enable_mod("high", true);
        recomp::mods::enable_mod("low", true);
        expect_paths({high, low});
        aero::mods::discard_failed_load();
        expect_paths({});
        mod_context->unload_mods();
        recomp::mods::close_mods();
        // Stop the persistence worker before Python removes its fixture tree.
        mod_context.reset();
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
