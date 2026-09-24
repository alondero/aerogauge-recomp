# Modding

Status: experimental. AeroGauge uses the package loader in N64ModernRuntime and
the Mods manager in RecompFrontend. Code packages must target the selected ROM:
`aerogauge` for USA or `aerogauge.jp.rev_a` for Japan Rev A. Regional guest
addresses differ, so code packages are not interchangeable.

## Install and manage packages

Open **Mods** from the AeroGauge launcher, or open **Settings** and choose
**Mods**. Install and scan packages before starting the game. RecompFrontend
keeps those file operations disabled during play because refreshing the list
closes the package handles used by the running game. The in-game Mods tab still
shows installed packages and supports runtime-toggleable content such as
texture packs.

Packages and settings use the same per-user or portable directory as the rest
of the port:

- `mods/` contains installed packages;
- `mods.json` stores enabled state and order; and
- `mod_config/` stores per-package settings.

Supported package types are:

- `.nrm` code packages with a manifest targeting the selected region's game ID;
- `.rtz` RT64 texture archives containing `rt64.json` at the archive root.

Code packages load when the game starts. Enable them before choosing **Start
Game**; changing code-mod selection requires restarting AeroGauge. Texture
packs can be enabled, disabled, and reordered during play. Earlier packages in
the manager's order have higher texture priority. The existing Graphics
texture path, including `AERO_TEXTURE_PACK`, has priority over managed texture
packs.

If a code package fails to load, the runtime error opens over Settings > Mods.
Disable the incompatible package, close Settings, and choose **Start Game**
from the launcher again. A failed load does not automatically retry the game.

## Code package boundary

Use the runtime's mod packaging tool with this repository's
`aerogauge.syms.toml` function references for USA, or `aerogauge.jp.syms.toml`
for Japan Rev A. USA's mod game ID is `aerogauge` and ROM selection ID is
`aerogauge.us`; Japan uses `aerogauge.jp.rev_a` for both. The launcher and Mods
tab use the validated ROM's IDs. Symbols are an experimental interface that
can change.

The runtime supports function hooks and replacements. CMake aligns translated
function entries and reserves room for runtime replacement jumps. The build
also derives regional guard lists from `aerogauge.us.toml` and
`aerogauge.jp.toml`: functions with port-injected
hooks, stubs, or instruction patches are registered as base patches so ordinary
mod replacements cannot silently discard port behavior.

This integration does not expose a general game-data API. Track Lab files and
port configuration are not mod package types. Android includes the manager UI,
but its file picker is not connected to package installation and code package
loading has not been verified on an Android device. Installing packages through
the manager is currently a desktop workflow.

Texture-only packs do not need a code-mod manifest. RT64 owns texture archive
parsing and replacement behavior; the port sends the enabled package paths to
the renderer thread.
