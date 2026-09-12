# AeroGauge: Recompiled

![AeroGauge: Recompiled running on PC](docs/aerogaugerecomp.jfif)

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
Changes to rendering, widescreen presentation, draw distance, full course geometry,
and window size apply
in-game and are saved to `graphics.json`. The graphics API, developer overlay, and
texture pack/dump paths
are saved for the next launch; texture paths are chosen from native file/folder dialogs.
**Enhancements > Full course geometry** is experimental (higher CPU/GPU cost, possible
visual regressions) and defaults on; uncheck it to restore the original 3-zone
visibility window.
Press **F11** (or
Alt+Enter) to switch fullscreen; return to windowed mode to access the menu bar again.

Enable **Enhancements > Easy Turbo + Boost Start** for simplified boosts:

- At the start, hold accelerator for the existing automatic Boost Start.
- During a race (player 1 only), press the dedicated **Turbo** button: **R** on
  keyboard (or **E**), **right trigger** (or **right shoulder**) on gamepad.
  Drift keeps its own button (default **Z** on keyboard / **left trigger** on
  gamepad), so drifting still works while Turbo is enabled. Turbo is fixed to
  the physical R button and ignores the in-game control mapping: if you assign
  an action to R, pressing it will also trigger Turbo.
- Release and press again for another boost. Holding the button does not repeat
  boosts, and pressing during an active boost does not extend or queue one.
  The normal craft-specific boost duration, heat buildup and overheating still apply.

This option is off by default, saved in `enhancements.json`; disabling it turns
off the launch assist and race Turbo. Ordinary drift and boost mechanics are
unaffected either way.

## Saves and controller feedback

Controller 1 has a virtual **Controller Pak** by default. Use AeroGauge's own
Controller Pak / Time Attack ghost save and load options; notes survive closing
and restarting the port. Cartridge progress and settings continue to use EEPROM.

Both live in the application's `saves` folder:

- Windows: `%LOCALAPPDATA%\AeroGaugeRecomp\saves`
- Linux: `$XDG_CONFIG_HOME/AeroGaugeRecomp/saves`, or `~/.config/AeroGaugeRecomp/saves`
- Portable mode (`portable.txt` in the working directory): `./saves`

`aerogauge.us.mpk` is the 32 KiB Controller Pak image; `aerogauge.us.bin` is the
512-byte EEPROM save. Back up both to keep ghosts and cartridge progress.
A missing Pak starts formatted; existing images are never silently replaced on a
load error. Only raw 32 KiB MPK images are supported.

A rumble-capable SDL gamepad receives **impact and turbo feedback** during P1
races, alongside Controller Pak saving. Impacts give a stronger short pulse;
turbo gives a lighter vibration while active. Feedback stops on pause and expires
if gameplay stops updating. This is a port enhancement driven by the original
race physics; the ROM's unused D-pad motor test is not a race rumble implementation.

Optional launch overrides:

- `AERO_RUMBLE=0`: disable vibration.
- `AERO_RUMBLE_TURBO=0`: impacts only.
- `AERO_PAK_PATH=<path>`: use another raw MPK image.
- `AERO_CONTROLLER_PAK=0`: disable the virtual Controller Pak.

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
