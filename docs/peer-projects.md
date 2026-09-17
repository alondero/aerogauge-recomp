# Reference project comparison

This page compares the public player and developer workflows of established N64
projects. It does not claim that this repository installed, played, or
acceptance-tested those projects.

## Sources

- [Banjo: Recompiled](https://github.com/BanjoRecomp/BanjoRecomp) and its
  [official mod template](https://github.com/BanjoRecomp/BKRecompModTemplate) separate
  release use, source builds, settings, saves, and native mod work.
- [Shipwright / Ship of Harkinian](https://github.com/HarbourMasters/Shipwright)
  provides a task-first player guide, a separate
  [build guide](https://github.com/HarbourMasters/Shipwright/blob/develop/docs/BUILDING.md),
  and a separate
  [modding guide](https://github.com/HarbourMasters/Shipwright/blob/develop/docs/MODDING.md).
- [Spaghetti Kart](https://github.com/HarbourMasters/SpaghettiKart) provides a
  documentation map, build instructions, and an explicit mod metadata format
  in its [modding guide](https://github.com/HarbourMasters/SpaghettiKart/blob/main/docs/modding.md).
  Its [mods.toml reference](https://github.com/HarbourMasters/SpaghettiKart/blob/main/docs/mods-toml.md)
  makes identity, dependencies, and load order explicit.
- The [Banjo-Kazooie decompilation project](https://github.com/n64decomp/banjo-kazooie)
  shows the source-first workflow: ROM identity, dependencies, build targets,
  and visible progress.

## Comparison by task

| Project | Player path | Settings and saves | Platforms and build | Modding and developer workflow |
| --- | --- | --- | --- | --- |
| Banjo-Kazooie decompilation | Source-first README with supported ROM hashes, not a player release path. | Source reconstruction, not a finished player settings contract. | Ubuntu and Docker paths, with visible progress and module boundaries. | Strong source ownership and progress signals. |
| Banjo: Recompiled | Releases are separate from building; the README describes North American 1.0 ROM import. | In-game gameplay, graphics, input, and audio settings; save and portable locations are documented. | Windows, Linux, and macOS, with Linux binaries, Flatpak, Steam Deck, and a build guide. | Drag-and-drop or menu-based mods; native patch workflow in the official template. |
| Shipwright / Ship of Harkinian | Quick Start verifies the ROM and gives platform launch steps. | Keyboard defaults, menu shortcuts, save states, fullscreen, and backend selection are documented. | Windows, Linux, macOS, Switch, and Wii U build paths. | Fork-and-branch code workflow plus OTR asset mods. |
| Spaghetti Kart | Documentation is primarily a build and modding map; release UX is not verified here. | Asset and track workflows are clearer than a finished player settings contract. | Windows, Linux, and macOS build and packaging paths. | Folder or archive mods with identity, version, dependencies, and load order in mods.toml. |

The standards adopted here are concrete: a short player path, settings and
save locations in one place, platform claims tied to evidence, a repeatable
developer build, and clear limits before code mods are promised. This page is
based on public project documentation. It is not a record of hands-on testing.

## Where AeroGauge differs today

- The supported player targets are 64-bit Windows with Direct3D 12 and
  64-bit Linux with Vulkan. macOS is not a supported target in this branch.
- A matching USA ROM is required. The project cannot distribute it.
- The shared settings screen is present on the Windows and Linux build paths,
  but there is no new control-binding page.
- The port still carries ROM-specific guest-memory hooks and a local RT64
  patch stack. These are transitional maintenance boundaries, not a private
  renderer fork or a stable code-mod interface.
- Texture replacement and texture dumping are developer features. The port
  does not currently define a versioned general mod format.

These differences are implementation facts, not a reason to copy another
project's architecture. New features should meet the same clarity standard
while fitting AeroGauge's generated-code and ROM evidence boundaries.
