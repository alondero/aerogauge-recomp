# AeroGauge: Recompiled

A native PC port of **AeroGauge** (Nintendo 64, USA) built with
[N64Recomp](https://github.com/N64Recomp/N64Recomp) static recompilation, running on the
[N64ModernRuntime](https://github.com/N64Recomp/N64ModernRuntime) (`ultramodern` +
`librecomp`) with the [RT64](https://github.com/rt64/rt64) renderer.

The stack is cloned from the
[Automobili Lamborghini: Recompiled](https://github.com/alondero/automobililamborghini-recomp)
port — same submodule pins, same runtime patches, same runtime glue — and the same
approach as [Zelda64Recomp](https://github.com/Zelda64Recomp/Zelda64Recomp) and friends.

**Status: early bring-up.** The whole-ROM recompile (1118 functions), build, and boot
harness are wired; the libultra routing layer (which OS primitives the ROM uses and
where) has not been mapped yet, so the game does not reach gameplay. See the issue
tracker for the porting roadmap.

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
