# Modding

Status: experimental. AeroGauge uses the package loader in N64ModernRuntime and
the Mods manager in RecompFrontend. Code packages must target the selected ROM:
`aerogauge` for USA or `aerogauge.jp.rev_a` for Japan Rev A. Regional guest
addresses differ, so code packages are not interchangeable.

## Install and manage packages

The in-game tab, reached with **Settings** then **Mods**, lists installed
packages, toggles runtime-toggleable content such as texture packs, and lets you
disable a code package that failed to load. It cannot install or scan packages
while the game is running: RecompFrontend keeps those file operations disabled
during play because refreshing the list closes the package handles the running
game is using.

Installing needs the pre-game launcher. Set `AERO_LAUNCHER=1` and choose **Mods**
before the game starts; that is where packages are added and scanned, and it is
also where you can enable a **code** package and start the game in one step. See
[Configuration](configuration.md) for the variable.

Packages and settings use the same per-user or portable directory as the rest
of the port:

- `mods/` contains installed packages;
- `mods.json` stores enabled state and order; and
- `mod_config/` stores per-package settings.

Supported package types are:

- `.nrm` code packages with a manifest targeting the selected region's game ID;
- `.rtz` RT64 texture archives containing `rt64.json` at the archive root.

Code packages load when the game starts, so one has to be enabled before that
start. Enable it in the launcher's Mods option, or change the selection in the
Mods tab and restart AeroGauge; the change applies at the next start. Texture
packs can be enabled, disabled, and reordered during play. Earlier packages in
the manager's order have higher texture priority. Use Mods to manage texture
packs; Graphics has no separate texture-pack path control.

For a texture pack, use **Install Mods** in the launcher to select its `.rtz`
file, or copy that file into the user-data `mods/` directory before launching.
Enable the pack in Mods, then start AeroGauge. The archive is a ZIP containing
`rt64.json` at its root and the replacement files referenced by that database.
Texture-only packs do not need `mod.json` and are listed for both supported
ROM regions; actual replacements depend on matching the textures in that ROM.
Keep the filename stable: without a manifest, the runtime uses the file stem
as the package ID, so renaming it changes its saved enablement and order.

A loose replacement directory is an authoring input, not an installable pack
by itself. The mod manager requires `mod.json` for directory packages. Use the
legacy override below while authoring a loose pack, then distribute a `.rtz`.

If a code package fails to load, the runtime error opens over Settings > Mods.
Disable the incompatible package, close Settings, and start AeroGauge again. A
failed load does not retry on its own, so a restart is the recovery step; a bad
package does not turn into a boot loop.

## Legacy texture pack setting

The `texture_pack` key in `graphics.json` and `AERO_TEXTURE_PACK` still accept
a loose RT64 directory or `.rtz` file. This compatibility override takes
priority over every managed texture pack, survives Graphics saves, and is
not editable in Settings. Startup logs `legacy texture_pack override active`
when it loads, or `legacy texture_pack override FAILED to load` on failure.
Paths that cannot be converted to the host filesystem encoding are logged and
skipped at startup and during live updates; managed packs remain available.

To migrate a configured `.rtz`, install and enable it through Mods, then set
`"texture_pack": ""` in `graphics.json`, remove `AERO_TEXTURE_PACK` from your
environment if set, and restart. Empty values disable the override. Clearing
it lets the Mods list control texture activation and priority.

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
