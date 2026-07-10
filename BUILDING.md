# Building

The build has two stages: **(1)** recompile the game code from your ROM into C, then
**(2)** compile everything with CMake. All commands run from the repository root.

The scripted equivalents of everything below: `.\build.ps1` (Windows) / `./build.sh` (Linux).

## Prerequisites

- **Git**, **CMake ≥ 3.20**, and **Python 3**.
- A C/C++ toolchain:
  - **Linux:** `gcc`/`g++` (C17 / C++20), plus `SDL2`, Vulkan headers/loader, and the
    usual desktop build dependencies.
  - **Windows:** **MinGW-w64 GCC** (MSVC is *not* required). RT64 uses its Direct3D 12
    backend. The MinGW `bin` directory must be on `PATH`, or `gcc.exe` fails to load its
    own DLLs.
- A network connection at configure time (CMake fetches `DirectX-Headers` on Windows).
- **Your own ROM:** `AeroGauge (USA).z64`, placed in the repository root.
  Only the USA release is currently supported (XXH3-64 `0x89ea0690f3e22201`).

## 1. Clone with submodules

```bash
git clone --recurse-submodules <this-repo>
cd aerogauge-recomp
# If you already cloned without --recurse-submodules:
git submodule update --init --recursive
```

On Windows, enable long paths for the RT64 submodule's deep test files:

```bash
git -c core.longpaths=true submodule update --init --recursive
```

## 2. Apply the dependency patches

The port needs small compatibility patches applied to the submodule working trees.
The submodules are pinned to their public upstream commits; these patches carry the
runtime changes the stack was developed against (cooperative scheduler dispatch,
VI-mode fallback, 30fps pacing, save-state thread relink, RT64 frame-interpolation and
widescreen features, and — on Windows — the MinGW/D3D12 COM ABI fixes for RT64/plume).
They are inherited unchanged from the Automobili Lamborghini port (hence the 0001 patch
filename).

```bash
# ultramodern / librecomp runtime (all platforms):
git -C lib/N64ModernRuntime apply ../../patches/0001-lamborghini-runtime-scheduler-audio-vi.patch
git -C lib/N64ModernRuntime apply ../../patches/0007-ultramodern-savestate-thread-context-relink.patch

# RT64 renderer — all platforms:
git -C lib/rt64 apply "$(pwd)/patches/0006-rt64-interp-angular-velocity-matching.patch"
git -C lib/rt64 apply "$(pwd)/patches/0008-rt64-skybox-stretch-parallaxless-backdrop.patch"
git -C lib/rt64 apply "$(pwd)/patches/0009-rt64-widescreen-split-subviewport.patch"

# RT64 renderer — Windows / MinGW only (absolute paths avoid depth confusion):
git -C lib/rt64 apply "$(pwd)/patches/0005-rt64-mingw-gcc-compat.patch"
git -C lib/rt64/src/contrib/plume apply "$(pwd)/patches/0004-plume-d3d12-mingw-com-abi-struct-return.patch"
```

## 3. Recompile the game code from your ROM

This reads your ROM and generates `RecompiledFuncs/` (git-ignored). First build the
N64Recomp CLI (bundled in the runtime submodule), then run it against the config:

```bash
# Regenerate the symbol map + config (optional; committed copies are provided):
python3 scripts/gen_syms_toml.py

# Build the recompiler CLIs, then recompile the game code:
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target N64RecompCLI RSPRecomp
./build/lib/N64ModernRuntime/librecomp/N64Recomp/N64Recomp aerogauge.us.toml
```

The recompiler reads `rom_file_path` from the `.toml` (defaults to
`AeroGauge (USA).z64` in the repo root). This produces the git-ignored, ROM-derived
translation `RecompiledFuncs/` — never committed to the repository.

> The RSP audio microcode step (`RSPRecomp aspMain.us.toml` in the Lamborghini
> template) does not exist here yet: AeroGauge's aspMain ucode location has not been
> derived. Audio tasks currently hit a no-op scaffold (see `src/main.cpp`).

## 4. Build the port

### Linux

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++
cmake --build build --target aerogauge_modern -j
```

### Windows (MinGW GCC)

```bash
export PATH="/c/ProgramData/mingw64/mingw64/bin:$PATH"   # adjust to your MinGW path
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER=gcc.exe -DCMAKE_CXX_COMPILER=g++.exe
cmake --build build --target aerogauge_modern -j
```

## 5. Run

Run from the repository root so the ROM path resolves:

```bash
./build/aerogauge_modern
```

`AERO_HEADLESS=1` skips the window/RT64 and runs the headless software-render probe
(120-VI boot smoke, prints a `[probe] boot summary;` line).

## Notes

- `lib/N64ModernRuntime`'s root CMake deliberately omits RT64; it is pulled in only by
  this project's `CMakeLists.txt`.
- `RecompiledFuncs/` is regenerated from your ROM and is never committed. Re-run step 3
  after changing the symbol map, `force_stub.txt`, or the config.
- `force_stub.txt` is the recompiler-error iteration loop: when N64Recomp fails on a
  function (unhandled instruction, mis-derived boundary), add the name there, re-run
  `gen_syms_toml.py`, and re-run the recompiler.
