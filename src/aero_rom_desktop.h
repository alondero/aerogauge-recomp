#ifndef AERO_ROM_DESKTOP_H
#define AERO_ROM_DESKTOP_H

#include "aero_rom_startup.h"

namespace recomp { struct GameEntry; }

namespace aero::rom {
// Runs before game threads or renderer initialization. Runtime owns byte-order
// normalization, full hash validation and the imported ROM copy. Both game
// entries must already be registered; a null Japan entry is a USA-only build.
Selection select_startup_rom(const std::optional<std::filesystem::path>& explicit_path,
                             const recomp::GameEntry& usa,
                             const recomp::GameEntry* japan,
                             bool interactive);
}
#endif
