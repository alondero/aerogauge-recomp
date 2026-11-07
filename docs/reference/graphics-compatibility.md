# Graphics API compatibility

AeroGauge's current renderer has no Direct3D 11 backend. Adding one is
technically possible, but it requires renderer development and shader/resource
binding changes, rather than a new settings option. Running another native
port successfully establishes neither RT64 compatibility nor a hardware
performance limit. API features and performance are separate requirements;
Microsoft explicitly distinguishes them in its
[feature-level documentation](https://learn.microsoft.com/en-us/windows/win32/direct3d12/hardware-feature-levels).

This assessment is based on source inspection on 2026-11-07. It does not
establish compatibility with a particular GPU, reproduce the reported startup
failure, or promise a future release. See the [renderer reference](renderer.md)
for renderer ownership and the [build targets](../../BUILDING.md#supported-build-targets)
for supported platform paths.

## Where the requirement comes from

The [port's renderer adapter](../../src/rt64_renderer.cpp) sends N64 display
lists to RT64. The pinned
[RT64 feature list](https://github.com/rt64/rt64/blob/f0728a2520d5aa735886240de3fee75cc805f6d6/README.md#features-available)
names D3D12, Vulkan and Metal. Its graphics abstraction dependency is
[Plume at 51b1ad4](https://github.com/renderbag/plume/tree/51b1ad443b9f202c5cfc930ae25345d3f2ba7716),
whose [build manifest](https://github.com/renderbag/plume/blob/51b1ad443b9f202c5cfc930ae25345d3f2ba7716/CMakeLists.txt)
contains Vulkan, D3D12 and Metal implementations, with no D3D11 backend.

The CPU translation/runtime is a different boundary.
[N64ModernRuntime's renderer interface](https://github.com/N64Recomp/N64ModernRuntime/blob/cdf5abbd5026fef5c364c676e4667c45e42b6863/ultramodern/include/ultramodern/renderer_context.hpp)
accepts a host-supplied renderer callback. A different renderer could therefore
be integrated without replacing the CPU recompiler. However,
[RecompFrontend's UI renderer](https://github.com/N64Recomp/RecompFrontend/blob/b1a1477c6556aeb7ed45defbfb5924f721efebc1/recompui/src/renderer/ui_renderer.cpp)
also uses RT64/Plume resources and compiled shaders. Its integration would need
to be preserved or replaced as part of that work.

## Why a D3D11 port is substantial

The following are concrete differences in the existing implementation:

| Area | Current implementation | Consequence for D3D11 |
| --- | --- | --- |
| GPU backend | Plume provides D3D12 command queues, resource management and presentation. | A new implementation must translate the abstraction to D3D11's device/context model. |
| Shader pipeline | [RT64's build](https://github.com/rt64/rt64/blob/f0728a2520d5aa735886240de3fee75cc805f6d6/CMakeLists.txt) uses DXC and Shader Model 6.3 profiles for Windows shader binaries. | A D3D11-compatible shader compilation path and shader audit are needed. |
| Resource binding | [Framebuffer shader declarations](https://github.com/rt64/rt64/blob/f0728a2520d5aa735886240de3fee75cc805f6d6/src/shaders/FbRendererCommon.hlsli) contain 8,192-entry texture resource arrays and 18 samplers. [Sampling code](https://github.com/rt64/rt64/blob/f0728a2520d5aa735886240de3fee75cc805f6d6/src/shaders/TextureSampler.hlsli) uses nonuniform resource indices. | D3D11 feature level 11 has 128 shader input-resource slots and 16 sampler slots per stage. The existing layout cannot be copied directly. |

The binding limits come from Microsoft's
[D3D11 resource limits](https://learn.microsoft.com/en-us/windows/win32/direct3d11/overviews-direct3d-11-resources-limits).
The engineering implication is that a port needs a different binding strategy,
such as smaller resource sets with rebinding or batching, as well as shader
changes. Lowering the shader target alone does not resolve those limits.
RT64's [boundless texture descriptor set](https://github.com/rt64/rt64/blob/f0728a2520d5aa735886240de3fee75cc805f6d6/src/render/rt64_descriptor_sets.h)
also makes the resource model explicit on the host side.

Two implementation routes follow from these boundaries: develop a D3D11
compatibility path in RT64/Plume, or integrate another N64 renderer and adapt
the port and frontend. Both require real-device validation of gameplay,
framebuffer effects, HUD/widescreen handling, presentation, and supported
enhancements. The [headless renderer](renderer.md#headless-renderer) is a test
instrument, not an existing player-ready compatibility backend.

Direct3D API version, hardware feature level and shader model must not be
conflated. Plume's [D3D12 device creation](https://github.com/renderbag/plume/blob/51b1ad443b9f202c5cfc930ae25345d3f2ba7716/plume_d3d12.cpp)
requests `D3D_FEATURE_LEVEL_11_0`; this still calls `D3D12CreateDevice` and
requires a compatible D3D12 driver. It does not enable the D3D11 API on an
otherwise incompatible GPU. Microsoft documents these numbering systems
[separately](https://learn.microsoft.com/en-us/windows/win32/direct3d12/hardware-feature-levels#numbering-systems).

## An existing alternative to investigate: Windows Vulkan

Vulkan already exists in the Windows code path. The
[Plume build](https://github.com/renderbag/plume/blob/51b1ad443b9f202c5cfc930ae25345d3f2ba7716/CMakeLists.txt)
always includes its Vulkan backend, and
[the Vulkan implementation](https://github.com/renderbag/plume/blob/51b1ad443b9f202c5cfc930ae25345d3f2ba7716/plume_vulkan.cpp)
supports Win32 surfaces. AeroGauge passes its Windows window handle and maps
`api_option` to RT64's Vulkan selection in
[rt64_renderer.cpp](../../src/rt64_renderer.cpp).

RT64's [application setup](https://github.com/rt64/rt64/blob/f0728a2520d5aa735886240de3fee75cc805f6d6/src/hle/rt64_application.cpp)
tries D3D12 and then Vulkan on Windows when the API is Automatic and device
creation fails. That loop does not provide a general retry for later shader
or pipeline failures after a device has been created. Explicitly selecting
Vulkan is therefore a useful diagnostic even when Automatic did not work.

For a machine-specific check:

1. Record the exact GPU model, Windows version, driver version, game release
   and startup error or log. A report that Windows has DirectX 12 installed
   does not establish the GPU/driver capabilities.
2. With the game closed, change the existing `api_option` entry to `"Vulkan"`
   in `%LOCALAPPDATA%\AeroGaugeRecomp\graphics.json`, or the portable-mode
   configuration file. Preserve the other settings. See
   [configuration](../configuration.md#graphicsjson).
3. Relaunch and inspect the [session log](../debugging.md#logs). Restore
   `"Auto"` if the forced backend fails.

This is a source-supported diagnostic path, not a newly validated Windows
support claim. Plume requests Vulkan 1.2 and negotiates descriptor-indexing
and scalar-block-layout features. RT64's large, nonuniform texture arrays and
Plume's variable/partially-bound descriptor bindings mean that merely having
some Vulkan support does not establish compatibility. The Android dependency
patch adds an explicit feature rejection there; that check must not be
described as a universal upstream desktop check. See the
[Vulkan implementation](https://github.com/renderbag/plume/blob/51b1ad443b9f202c5cfc930ae25345d3f2ba7716/plume_vulkan.cpp)
and [Android patch](../../patches/0019-rt64-android-cross-build.patch).

## Why Twilight Princess can run on the same PC

The report does not identify the Twilight Princess project. If it means
Dusklight, that project's [README at 77705f8](https://github.com/TwilitRealm/dusklight/blob/77705f8bb95ef7b7dca4675b149c4bcb6a48da34/README.md)
describes a source reimplementation based on the Twilight Princess
decompilation, credits Aurora, and explicitly provides best-effort D3D11
support with accuracy/performance limitations. Its
[dependency manifest](https://github.com/TwilitRealm/dusklight/blob/77705f8bb95ef7b7dca4675b149c4bcb6a48da34/.gitmodules)
includes Aurora. [Aurora](https://github.com/encounter/aurora#graphics) implements
GameCube/Wii GX through WebGPU/Dawn, rather than RT64's N64 rendering path.
That is a different graphics stack with a different compatibility path.

A successful run there is consistent with a PC having adequate speed while
lacking the API features needed by this renderer. The next useful step is to
check that machine's Vulkan capability; the source does not justify promising
DX11 in a future AeroGauge release or telling the player to buy hardware
before identifying the GPU and driver.
