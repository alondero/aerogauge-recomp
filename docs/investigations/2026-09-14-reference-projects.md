# Reference-project comparison — 2026-09-14

## Scope and limit

This is a review of public release pages, user documentation, build
documentation, and mod documentation for three established human-driven N64
projects. I did not install, launch, or play Banjo: Recompiled,
Ship of Harkinian/Shipwright, or Spaghetti Kart during this task. The
comparison therefore describes their published workflow, not personal
acceptance testing.

## What their public documentation makes clear

| Project | Player-facing standard | Developer and modding standard |
| --- | --- | --- |
| [Banjo: Recompiled](https://www.banjorecomp.com/) | The [download page](https://www.banjorecomp.com/download) presents Windows, Linux, and macOS packages, an extract-and-run flow, a matching USA ROM requirement, settings, saves, and a simple mod installation path | The related [decompilation project](https://github.com/n64decomp/banjo-kazooie) documents ROM checksums, dependencies, and a reproducible build boundary |
| [Shipwright](https://github.com/HarbourMasters/Shipwright) | The README names supported platforms, checks the user's ROM, lists controls and shortcuts, describes graphics choices, and points to releases | The project publishes [build](https://github.com/HarbourMasters/Shipwright/blob/develop/docs/BUILDING.md) and [modding](https://github.com/HarbourMasters/Shipwright/blob/develop/docs/MODDING.md) documentation, including its asset/mod format and workflow |
| [Spaghetti Kart](https://github.com/HarbourMasters/SpaghettiKart) | The README states the ROM identity, controls, graphics API choices, settings, and the fact that game data is not distributed | Its [modding guide](https://github.com/HarbourMasters/SpaghettiKart/blob/main/docs/modding.md), [mods.toml reference](https://harbourmasters.github.io/SpaghettiKart/md_docs_2mods-toml.html), [build guide](https://github.com/HarbourMasters/SpaghettiKart/blob/main/docs/BUILDING.md), and tutorials expose the developer path |

The useful pattern is not a particular menu design. It is that a new player
can find the package, know what personal game data is required, see controls
and settings, and understand where saves or mods go. A new developer can find
the dependency setup, build command, test or validation path, and the mod
boundary without reading private project history.

## Standards adopted here

This repository now follows these principles:

- The README speaks to a player first. It does not require knowledge of
  recompiled code, guest memory, RT64, or N64 addresses.
- Platform support is a matrix, not an implication. A feature is not called
  cross-platform when it exists only behind a Win32 menu or a Windows test.
- The ROM requirement names an identity and says what the player must do. It
  does not ask a player to build the port.
- Controls, settings, saves, and known limitations have a short public path.
- Build documentation names dependencies and separates generated output from
  hand-written source.
- Mod support is described only where the project has a real contract.
  Texture replacement is not presented as code modding.
- A source or runtime claim must point to evidence, a test, or a maintainer
  decision. Historical notes remain available but are labelled as such.

## Where AeroGauge currently diverges

AeroGauge is earlier in its recompilation journey:

- Its CPU output is produced from a symbol scan and handwritten TOML hooks.
  It does not yet have a complete decompilation or readable source tree for
  the game.
- Several features still manipulate guest RDRAM directly. This includes
  full-track display-list construction, HUD display-list rewriting, and
  save-state loading. These are transitional techniques with ROM-specific
  invariants.
- Dependency behavior is carried as local patches. The renderer audit has not
  yet reduced every patch to a public upstream issue or a minimal portable
  test.
- The current release has a Windows native settings menu and JSON editing on
  Linux. The shared frontend is still a pending pull request.
- The repository has no versioned general code-mod interface. RT64 texture
  replacement is the closest current asset workflow.

The comparison projects set a quality bar for clarity and workflow. They do
not prove that AeroGauge should copy their architecture. The maintainer must
choose which next step gives this project the best evidence: better
decompilation, stable code-mod seams, renderer upstreaming, or player-facing
frontend work.

## Post-merge status note - 2026-09-17

The shared settings frontend is now part of the current main line. The
historical statements above describe the branch state observed on 2026-09-14
and are kept as an audit record. The current implementation and its limits are
described in [the settings frontend guide](../frontend.md) and the
[settings frontend investigation](2026-09-17-settings-frontend-review.md).
