cmake_minimum_required(VERSION 3.20)

# AeroGauge: Recompiled — a static-recompilation PC port built on
# N64ModernRuntime (ultramodern + librecomp) with the RT64 renderer.
# Stack cloned from the Automobili Lamborghini port (the proven template).
#
# See BUILDING.md for full instructions. In short, from the repo root:
#
#   # 1. Recompile the game code from YOUR ROM (produces RecompiledFuncs/):
#   python3 scripts/gen_syms_toml.py
#   ./lib/N64ModernRuntime/librecomp/N64Recomp/N64Recomp aerogauge.us.toml
#
#   # 2. Configure + build (Linux example; MinGW/Windows details in BUILDING.md):
#   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
#   cmake --build build --target aerogauge_modern -j
#
# Run from the repo root: the ROM path in aerogauge.us.toml is cwd-relative.
# The RT64/plume MinGW patches (patches/0004+0005) are applied to the submodule
# working tree on Windows; DirectX-Headers v1.614.1 is fetched at configure time.
# RecompiledFuncs/ is gitignored — it is regenerated from your ROM (step 1).

project(aerogauge_modern LANGUAGES C CXX)

set(CMAKE_C_STANDARD 17)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(N64MR ${CMAKE_CURRENT_SOURCE_DIR}/lib/N64ModernRuntime)

# --- RT64 renderer (default presenter; hard build dependency) ---
# lib/rt64 is a pinned git submodule; RT64 is always compiled in and is the
# default presenter at runtime (AERO_HEADLESS=1 is the harness knob that keeps the
# headless swrender, the RDRAM-framebuffer measurement instrument for the
# port-vs-ares harness). Wiring mirrors Zelda64Recomp's CMakeLists.txt.
if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/lib/rt64/CMakeLists.txt)
    message(FATAL_ERROR "lib/rt64 submodule is missing. Initialise it first:\n"
        "  git submodule update --init --recursive")
endif()
set(RT64_STATIC TRUE)
if(WIN32)
    # Windows: RT64 uses its D3D12 backend + a plain Win32 window (HWND handed
    # over via SDL_GetWindowWMInfo in create_window_stub), so no SDL Vulkan
    # surface. SDL2 comes from rt64's own bundled Windows deps — the same
    # tree rt64's CMake points itself at (mupen64plus-win32-deps/SDL2-2.26.3).
    set(SDL2_WIN32_DEPS ${CMAKE_CURRENT_SOURCE_DIR}/lib/rt64/src/contrib/mupen64plus-win32-deps/SDL2-2.26.3)
    set(SDL2_INCLUDE_DIRS ${SDL2_WIN32_DEPS}/include)
    set(SDL2_LIBRARIES SDL2 SDL2main)
    link_directories(${SDL2_WIN32_DEPS}/lib/x64)
    # MinGW's bundled d3d12.h is years behind the D3D12 SDK: plume needs
    # D3D12_HEAP_TYPE_GPU_UPLOAD and D3D12MemAlloc needs <dxguids.h> (its own
    # MinGW support path, D3D12MA #71). Microsoft's DirectX-Headers package is
    # the official fix and supports MinGW; put its d3d12.h AHEAD of the MinGW
    # system header and link the GUID definitions library everywhere.
    include(FetchContent)
    FetchContent_Declare(DirectX-Headers
        GIT_REPOSITORY https://github.com/microsoft/DirectX-Headers
        GIT_TAG v1.614.1
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(DirectX-Headers)
    include_directories(BEFORE
        ${directx-headers_SOURCE_DIR}/include
        ${directx-headers_SOURCE_DIR}/include/directx
        ${directx-headers_SOURCE_DIR}/include/dxguids)
else()
    set(RT64_SDL_WINDOW_VULKAN TRUE) # non-Windows: RT64 expects an SDL window + Vulkan surface
    # rt64's vendored nativefiledialog-extended defaults to the GTK3 backend;
    # prefer the xdg-desktop-portal backend (dbus-1) on boxes without libgtk-3-dev.
    set(NFD_PORTAL ON CACHE BOOL "" FORCE)
endif()
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/lib/rt64 ${CMAKE_BINARY_DIR}/rt64)
# Generated shader headers land in the rt64 binary dir (Zelda64Recomp does the same).
target_include_directories(rt64 PRIVATE ${CMAKE_BINARY_DIR}/rt64/src)
if(WIN32)
    # GUID definitions for GCC's __uuidof emulation (see the DirectX-Headers
    # block above). Scoped per-target: a global link_libraries() leaks into
    # zstd's install-export set and breaks the CMake generate step.
    target_link_libraries(plume PRIVATE DirectX-Guids)
    # dwmapi (imgui_impl_win32) + d3dcompiler (imgui_impl_dx12's D3DCompile):
    # both are pulled in via #pragma comment(lib,...) which only MSVC honours;
    # MinGW needs the explicit link.
    target_link_libraries(rt64 DirectX-Guids dwmapi d3dcompiler)
    if(NOT MSVC)
        # MinGW: force-include the __mingw_uuidof compat header into every C++
        # TU of the D3D12-touching targets (why: see the header itself).
        set(DX12_UUID_COMPAT ${CMAKE_CURRENT_SOURCE_DIR}/src/mingw_dx12_uuid_compat.h)
        foreach(dx12_target plume rt64)
            target_compile_options(${dx12_target} PRIVATE
                $<$<COMPILE_LANGUAGE:CXX>:-include${DX12_UUID_COMPAT}>)
        endforeach()
    endif()
endif()
if(NOT WIN32)
    find_package(SDL2 REQUIRED)
endif()

# ultramodern + librecomp, which cascades N64Recomp / LiveRecomp / miniz.
# The runtime root CMake (lib/N64ModernRuntime/CMakeLists.txt) deliberately
# omits RT64 — it is pulled in only by a game's own CMake.
add_subdirectory(${N64MR})

# --- RecompiledFuncs: the whole-ROM recompiled image as a static library -----
# The generated funcs #include "recomp.h" (N64Recomp/include), "funcs.h"
# (resolved relative to each TU in RecompiledFuncs/), and lookup.cpp is picked
# up by the *.cpp glob. recomp_overlays.inl is an include (NOT a TU) and is
# intentionally excluded by the *.c/*.cpp globs.
#
# Skipped when empty so the first configure (which builds the recompiler
# tools) succeeds before N64Recomp populates this directory.
file(GLOB FUNC_C_SOURCES   CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/RecompiledFuncs/*.c)
file(GLOB FUNC_CXX_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/RecompiledFuncs/*.cpp)
if(FUNC_C_SOURCES OR FUNC_CXX_SOURCES)
    add_library(RecompiledFuncs STATIC)
    target_sources(RecompiledFuncs PRIVATE ${FUNC_C_SOURCES} ${FUNC_CXX_SOURCES})

    target_compile_options(RecompiledFuncs PRIVATE
        -fno-strict-aliasing
        -ggdb
        -Wno-unused-variable
        # implicit-function-declaration is a C-only diagnostic; lookup.cpp is C++.
        $<$<COMPILE_LANGUAGE:C>:-Wno-implicit-function-declaration>
    )

    target_include_directories(RecompiledFuncs PRIVATE
        ${N64MR}/ultramodern/include
        ${N64MR}/librecomp/include
        ${N64MR}/N64Recomp/include
    )
endif()

# --- Game executable: added once src/ exists ---------------------------------
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp)
    # src/aspMain.cpp is RSPRecomp output (see BUILDING.md step 3); appended
    # only when present so the first configure (which builds the recompilers)
    # can succeed before RSPRecomp runs.
    set(AERO_SOURCES
        src/main.cpp
        src/register_overlays.cpp
        src/stub_renderer.cpp
        src/aero_audio.cpp     # SDL2 push-audio sink for the AI buffer
        src/aero_config.cpp    # persistent graphics.json -> ultramodern GraphicsConfig
        src/aero_crash.cpp     # native crash backtrace: Win32 EXCEPTION_POINTERS +
                               # POSIX sigaction handler printing KSEG0/RDRAM offset + a 32-entry
                               # function-lookup trace ring. No deps beyond libc + DbgHelp (Win) /
                               # libc backtrace(3) (POSIX).
        src/libultra_stubs.c   # no-op symbols for ignored libultra CP0 helpers (e.g. __osSetSR)
        src/aero_draw_distance.cpp  # native guPerspectiveF: far-clip-plane scale (pop-in fix);
                                    # routed via `[patches] ignored` in aerogauge.us.toml
        src/aero_full_track.cpp     # native course-geometry registrars: full-track rendering
                                    # (large-scale pop-in fix); same `ignored` routing
        src/aero_warp.c        # developer warp menu (#3): F1-F6 / AERO_WARP race warp;
                               # tick hooked at func_80015FD0 via [[patches.hook]]
        src/aero_hud_widescreen.c  # widescreen 1P-HUD pinning (#1): whole-frame texrect
                                   # retag pass + needle matrix shift, hooked around the
                                   # 2D dispatcher func_80022408 via [[patches.hook]]
        src/aero_savestate.c   # developer save-state (#17): F7/F8 / AERO_STATE_LOAD RDRAM
                               # snapshot; tick hooked one instruction into func_80015C8C
    )
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/src/aspMain.cpp)
        list(APPEND AERO_SOURCES src/aspMain.cpp)
    endif()

    add_executable(aerogauge_modern ${AERO_SOURCES})
    target_compile_options(aerogauge_modern PRIVATE
        -fno-strict-aliasing
        -fms-extensions
        -ggdb
    )
    if(CMAKE_SIZEOF_VOID_P EQUAL 8 AND CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|amd64|AMD64")
        target_compile_options(aerogauge_modern PRIVATE -march=nehalem)
    endif()
    target_include_directories(aerogauge_modern PRIVATE
        ${N64MR}/ultramodern/include
        ${N64MR}/librecomp/include
        ${N64MR}/N64Recomp/include
        ${CMAKE_CURRENT_SOURCE_DIR}/RecompiledFuncs
    )
    target_link_libraries(aerogauge_modern PRIVATE
        librecomp
        ultramodern
    )
    # Host unit tests (pure math/classification, no ROM or runtime needed). Run with
    # `ctest --test-dir build` or execute build/test_hud_shift_scale directly.
    enable_testing()
    add_executable(test_hud_shift_scale tests/test_hud_shift_scale.c)
    target_include_directories(test_hud_shift_scale PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    if(NOT WIN32)
        target_link_libraries(test_hud_shift_scale PRIVATE m)
    endif()
    add_test(NAME hud_shift_scale COMMAND test_hud_shift_scale)
    if(WIN32)
        # Full-game audio regression for the aspMain "Unhandled jump target" crash
        # (2026-07-17): 60 s headless warp race with music; pre-fix this crashed on the
        # first run. Needs the ROM + built exe; the script self-skips when missing.
        add_test(NAME audio_task_crash COMMAND powershell -NoProfile -ExecutionPolicy Bypass
                 -File ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_audio_task_crash.ps1)
    endif()

    # E2E regression: a default play run must not emit periodic stderr lines from hot
    # threads (the 1 Hz gfx-thread heartbeat / VI-thread fb-swap line caused a visible
    # hitch every second when stderr was a console -- see tests/test_play_logging_quiet.ps1).
    # Runs the real binary ~15 s, so it needs the ROM (same precondition as the build).
    if(WIN32 AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/AeroGauge (USA).z64")
        add_test(NAME play_logging_quiet
                 COMMAND powershell -NoProfile -ExecutionPolicy Bypass
                         -File "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_play_logging_quiet.ps1"
                         -Exe $<TARGET_FILE:aerogauge_modern>
                         -RepoRoot "${CMAKE_CURRENT_SOURCE_DIR}")
        set_tests_properties(play_logging_quiet PROPERTIES TIMEOUT 180)
    endif()

    if(TARGET RecompiledFuncs)
        target_link_libraries(aerogauge_modern PRIVATE RecompiledFuncs)
    endif()

    # RT64 renderer (default presenter; see the RT64 block above).
    target_sources(aerogauge_modern PRIVATE src/rt64_renderer.cpp)
    # rt64's include_directories() are directory-scoped and do not propagate;
    # mirror Zelda64Recomp's include list for the game target.
    set(RT64_DIR ${CMAKE_CURRENT_SOURCE_DIR}/lib/rt64)
    target_include_directories(aerogauge_modern PRIVATE
        ${RT64_DIR}/include
        ${RT64_DIR}/src
        ${RT64_DIR}/src/rhi
        ${RT64_DIR}/src/render
        ${RT64_DIR}/src/contrib
        ${RT64_DIR}/src/contrib/plume
        ${RT64_DIR}/src/contrib/imgui
        ${RT64_DIR}/src/contrib/hlslpp/include
        ${RT64_DIR}/src/contrib/nativefiledialog-extended/src/include
        ${SDL2_INCLUDE_DIRS}
    )
    target_link_libraries(aerogauge_modern PRIVATE rt64 ${SDL2_LIBRARIES})

    if(WIN32)
        # Native crash backtrace: CaptureStackBackTrace lives in dbghelp.dll.
        # MinGW64 ships the import lib (libdbghelp.a) so an explicit
        # target_link_libraries entry is enough; we don't ship the DLL — the
        # system one is fine.
        target_link_libraries(aerogauge_modern PRIVATE dbghelp)
        # Self-contained exe dir: RT64 dlopen()s dxcompiler/dxil at runtime and SDL2 is
        # a dynamic dep; MinGW's own runtime is linked statically so the exe runs
        # outside an MSYS/MinGW shell.
        target_link_options(aerogauge_modern PRIVATE -static-libgcc -static-libstdc++
            -Wl,-Bstatic,--whole-archive -lwinpthread -Wl,--no-whole-archive,-Bdynamic)
        set(RT64_DXC_BIN ${CMAKE_CURRENT_SOURCE_DIR}/lib/rt64/src/contrib/dxc/bin/x64)
        add_custom_command(TARGET aerogauge_modern POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${SDL2_WIN32_DEPS}/lib/x64/SDL2.dll
                ${RT64_DXC_BIN}/dxcompiler.dll
                ${RT64_DXC_BIN}/dxil.dll
                $<TARGET_FILE_DIR:aerogauge_modern>)
    endif()
endif()
