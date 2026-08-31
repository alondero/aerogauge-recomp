# AeroGauge: Recompiled

A native PC port of **AeroGauge** (Nintendo 64, USA) built with
[N64Recomp](https://github.com/N64Recomp/N64Recomp) static recompilation, running on the
[N64ModernRuntime](https://github.com/N64Recomp/N64ModernRuntime) (`ultramodern` +
`librecomp`) with the [RT64](https://github.com/rt64/rt64) renderer.

**Status: renders and plays.** The whole-ROM recompile, libultra routing, RT64
rendering (widescreen + display-rate interpolation), input, and native configuration
menu are live; races render and run. Audio ucode and HUD polish are in progress. See
the issue tracker for the porting roadmap.

## What you need

- Your **own legally dumped ROM**: `AeroGauge (USA).z64` (8 MiB, big-endian,
  XXH3-64 `0x89ea0690f3e22201`), placed in the repository root. This repository
  contains **no game assets** and never will.

## Building

See [BUILDING.md](BUILDING.md), or just run the end-to-end script:

```powershell
.\build.ps1        # Windows (MinGW GCC + Ninja)
```

```bash
./build.sh         # Linux
```

Then run from the repo root:

```
./build/aerogauge_modern
```

## In-game configuration (Windows)

In windowed mode, the native menu bar exposes **Graphics** and **Enhancements** settings.
Changes to rendering, widescreen presentation, draw distance, and window size apply
in-game and are saved to `graphics.json`. The graphics API and texture pack/dump paths
are saved for the next launch; texture paths are chosen from native file/folder dialogs.
Press **F11** (or
Alt+Enter) to switch fullscreen; return to windowed mode to access the menu bar again.

## Developer warp menu

Jump straight into a 1-player race on any track without driving the menus
(issue #3; the launch path is the ROM's own — see `src/aero_warp.c`):

- **F1–F6** — warp to that track any time after boot, including mid-race
  (the current race exits through the game's own teardown first).
- **`AERO_WARP=track[:craft]`** (track 1–6, craft 1–10) — one-shot warp at boot,
  for scripted/headless runs.
- **`AERO_WARP_AT=vi:track[:craft]`** — scripted warp at a given VI (harness runs).

Tracks: 1 CANYON RUSH, 2 BIKINI ISLAND, 3 CHINATOWN, 4 NEO ARENA,
5 CHINATOWN JAM, 6 NEO SPEED WAY.

## How it works

1. `scripts/gen_syms_toml.py` scans the ROM for function boundaries (jal targets +
   IDO stack-frame prologues) and emits `aerogauge.syms.toml` + `aerogauge.us.toml`.
2. The bundled N64Recomp CLI translates every function to C (`RecompiledFuncs/`,
   git-ignored — regenerated from *your* ROM).
3. The C is compiled and linked against `librecomp`/`ultramodern`, which replace the
   N64's OS kernel with native threads, and RT64, which renders the game's display
   lists at native resolution.

## License

Project code is licensed per [LICENSE](LICENSE). Submodules and the game itself carry
their own licenses/ownership; you must supply your own ROM.
