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

- The supported player targets are 64-bit Windows with Direct3D 12, 64-bit
  Linux with Vulkan, and ARM64 Android with Vulkan. macOS is not a supported
  target in this branch.
- A matching USA or Japan Rev A ROM is required. The project cannot distribute it.
- The shared settings screen is present on the Windows, Linux, and Android
  build paths (with Android hiding desktop-only controls), including two-player
  assignment, editable binding profiles, per-player rumble and gyro steering.
- The port still carries ROM-specific guest-memory hooks and a local RT64
  patch stack. These are transitional maintenance boundaries, not a private
  renderer fork or a stable code-mod interface.
- The experimental mod manager handles regional code packages and RT64 texture
  packs. Texture dumping remains a developer feature; code packages must target
  AeroGauge's current compatibility boundary.

These differences are implementation facts, not a reason to copy another
project's architecture. New features should meet the same clarity standard
while fitting AeroGauge's generated-code and ROM evidence boundaries.

## Android port comparison

The following is a source review, not a playthrough of these ports. Android
implementation decisions should preserve these ownership and player-workflow
lessons; device compatibility still needs separate evidence.

### Automobili Lamborghini

The closest reference is the maintainer's
[Lamborghini port](https://github.com/alondero/automobililamborghini-recomp),
reviewed at `c232d5c09806e8fa628cdb419ba78ec757149f79`.
Its [Android guide](https://github.com/alondero/automobililamborghini-recomp/blob/c232d5c09806e8fa628cdb419ba78ec757149f79/docs/android.md)
describes an ARM64 SDL2/RT64 APK with app-private data and optional AdrenoTools
driver import. It records a Pixel 5 reaching the title, attract race, and car
selection with Turnip; this is the reference project's report, not AeroGauge
device validation. Vulkan 1.1 alone is insufficient: descriptor indexing and
scalar block layout must also be available.

Its [launcher source](https://github.com/alondero/automobililamborghini-recomp/blob/c232d5c09806e8fa628cdb419ba78ec757149f79/android/app/src/main/java/io/github/alondero/lamborghinirecomp/LauncherActivity.java)
uses Android's document picker, bounds the import size, normalizes the three
common N64 byte orders, hashes the entire ROM, and replaces an existing import
only after validation. Import work runs off the UI thread. These are useful
defaults for an install/import/play flow without storage permissions.

The [cross-build configuration](https://github.com/alondero/automobililamborghini-recomp/blob/c232d5c09806e8fa628cdb419ba78ec757149f79/cmake/Android.cmake)
keeps shader conversion tools on the host, builds SDL/FreeType for Android,
and uses 16 KiB native library alignment. The
[RT64 patch](https://github.com/alondero/automobililamborghini-recomp/blob/c232d5c09806e8fa628cdb419ba78ec757149f79/patches/0013-rt64-android-cross-build.patch)
uses RGBA presentation on Android; the frontend target must use the same format.
The [Plume patch](https://github.com/alondero/automobililamborghini-recomp/blob/c232d5c09806e8fa628cdb419ba78ec757149f79/patches/0014-plume-android-sdl-window.patch)
obtains Vulkan entry points from SDL so both layers share the selected driver.

### Zelda64 Recompiled for Android

[Zelda64's Android build notes](https://github.com/linkzenic/Zelda64Recomp-Android/blob/android-port/android/README.md)
separate runtime APKs from lifecycle probes and require private generated inputs
for CI. They recommend release builds for performance measurements.
Its [native touch bridge](https://github.com/linkzenic/Zelda64Recomp-Android/blob/android-port/src/android/android_lifecycle.cpp)
attaches an SDL virtual gamecontroller and supplies analog axes and buttons.
This offers a useful racing-game pattern: analog touch steering can enter the
same input path as physical controllers. Lifecycle callbacks in that file only
log state, so their presence alone is not evidence of native pause handling.

### Banjo: Recompiled for Android

[Banjo's port findings](https://github.com/AurelioB/BanjoRecomp-Android/blob/android/docs/android-port-findings.md)
identify two important lifecycle boundaries: opening DocumentsUI can destroy
and recreate the SDL surface, and immersive mode must be reapplied from Java
lifecycle callbacks. Game-specific Java/JNI names belong in the app, outside
shared renderer/frontend modules.

Its [save-management design](https://github.com/AurelioB/BanjoRecomp-Android/blob/android/docs/plans/android-save-storage.md)
keeps an app-private native save mirror and uses quiesced snapshots for external
export. Invalid imports, revoked access, and failed provider writes retain the
previous internal save. Existing external saves are not silently overwritten.
For AeroGauge, this supports keeping native I/O private and offering explicit
backup/restore without making a cloud document provider part of the frame loop.

### Release and acceptance lessons

Lamborghini's [release workflow](https://github.com/alondero/automobililamborghini-recomp/blob/c232d5c09806e8fa628cdb419ba78ec757149f79/.github/workflows/build-release.yml)
verifies APK signatures and ZIP alignment, but intentionally permits desktop
publication without Android signing configured. AeroGauge's requested promise
to bundle an APK on future releases calls for a stricter release dependency:
missing signing configuration must be visible before publishing an incomplete
release. A persistent signing key is needed for updates that preserve app data.

Device acceptance should cover first import, rejected ROM recovery, multitouch
steering plus acceleration, controller connection, background/resume, renderer
failure recovery, save persistence, and installing an update over the existing
app. Source review or a successful APK build does not establish those outcomes.

## Desktop onboarding and feature comparison (2026-10-26)

This is a source audit of public project documentation and code, not a
playthrough, benchmark, or acceptance test of these projects. These projects
also use different reconstruction methods. [Super Mario 64 decompilation](https://github.com/n64decomp/sm64)
reconstructs the game's C/source and builds
a ROM; its README requires a matching ROM to extract assets and gives source
build steps. [Banjo: Recompiled](https://github.com/BanjoRecomp/BanjoRecomp),
[Zelda 64: Recompiled](https://github.com/Zelda64Recomp/Zelda64Recomp),
[Automobili Lamborghini: Recompiled](https://github.com/alondero/automobililamborghini-recomp),
and [World Driver Championship: Recompiled](https://github.com/alondero/worlddriverchampionship-recomp)
use N64 static recompilation to produce native applications. They still need
the user's matching ROM, but their player workflow is a port/launcher rather
than a source-reconstructed game build. WDC specifically states in its
[README](https://github.com/alondero/worlddriverchampionship-recomp/blob/main/README.md)
that its CPU code and custom BOSS graphics microcode are statically recompiled,
then RT64 renders the low-level RDP commands.

Banjo's [player README](https://github.com/BanjoRecomp/BanjoRecomp/blob/main/README.md)
and Zelda's [launcher source](https://github.com/Zelda64Recomp/Zelda64Recomp/blob/b65c482ed672258eb684ab94a4e8c8aa662615ee/src/ui/ui_launcher.cpp)
offer a useful desktop ROM-onboarding pattern: choose the ROM in a launcher,
validate it, and enable Start only when a supported game is ready. Zelda's
launcher distinguishes unreadable files, non-ROMs, wrong games, and incorrect
versions. Their shared [N64ModernRuntime ROM handling](https://github.com/N64Recomp/N64ModernRuntime/blob/cdf5abbd5026fef5c364c676e4667c45e42b6863/librecomp/src/recomp.cpp)
stores the validated, normalized bytes under the app configuration directory,
checks that copy's hash on later launches, and loads it for gameplay; an
invalid cached copy is removed. At the audit baseline, AeroGauge asked for a specifically
named ROM before opening its app and exited when that path failed. A desktop
picker with a validated persistent copy is therefore a distinct onboarding
improvement. The matching ROM remains user-supplied; selection does not change
the ROM distribution boundary.

The feature examples below separate shipped behavior from possible transfer.
Game-specific addresses, input semantics, and renderer work do not transfer
without AeroGauge ROM evidence.

The peers also mark feature maturity differently. Banjo and Zelda describe
high-framerate presentation and broad settings as available player features;
their documentation calls out driver or presentation limits. Lamborghini's
auto-accelerate and car information are implemented options, but are opt-in.
WDC documents a first playable route and says rendering glitches remain; its
presentation-rate setting is experimental, and motion interpolation is
explicitly future work. The source-first SM64 project is a useful reference
for ROM identity, build inputs, and source ownership. Its native-port descendants
are a separate comparison: [sm64ex](https://github.com/sm64pc/sm64ex/blob/nightly/README.md)
documents remapping, texture packs, alternative renderers for older hardware,
and optional unlimited draw distance. [sm64coopdx](https://github.com/coop-deluxe/sm64coopdx)
continues the cooperative native-port lineage with a documented Lua mod API;
[Render96ex](https://github.com/Render96/Render96ex) builds on sm64ex for model
and texture enhancements. These are decompilation-based native ports, not
N64Recomp static recomps. AeroGauge already has remapping, texture packs and
draw-distance controls; SM64's renderer choices or model replacements are not
drop-in RT64 upgrades. Conversely, static recompilation does not mean game behavior has
been decompiled into readable original source. These distinctions keep the
comparison useful without promoting a peer's roadmap claim to a shipped
capability or treating a successful compile as gameplay acceptance.

| Priority | Peer pattern and AeroGauge opportunity | Current status |
| --- | --- | --- |
| 1 | First-launch ROM picker, clear validation errors, and a validated cached copy, following Banjo/Zelda and the shared runtime. | [#113](https://github.com/alondero/aerogauge-recomp/issues/113) implemented in this change; platform acceptance remains separate. |
| 2 | Coordinated shutdown should close frontend, renderer, audio, and runtime workers in an explicit order. Treat peer lifecycle patterns as prompts to review ownership, not proof of AeroGauge's behavior. | [#114](https://github.com/alondero/aerogauge-recomp/issues/114) tracked follow-up. |
| 3 | [Banjo](https://github.com/BanjoRecomp/BanjoRecomp/blob/main/README.md) and [Zelda](https://github.com/Zelda64Recomp/Zelda64Recomp/blob/dev/README.md) expose audio controls; a player-facing master volume and mute path would make AeroGauge's sound easier to adjust. | [#115](https://github.com/alondero/aerogauge-recomp/issues/115) tracked follow-up. |
| 4 | [Banjo](https://github.com/BanjoRecomp/BanjoRecomp/blob/main/README.md#high-framerate-support) and [Zelda](https://github.com/Zelda64Recomp/Zelda64Recomp/blob/dev/README.md#high-framerate-support) document high-frame-rate controls and driver troubleshooting. [WDC's graphics review](https://github.com/alondero/worlddriverchampionship-recomp/blob/main/docs/graphics-capabilities.md) describes its experimental presentation-rate settings and why interpolation needs a game-specific transform bridge; its README marks interpolation future work and limits validation to an early Windows/D3D12 build. Transfer the acceptance discipline: distinguish presentation rate from game timing, name backend/driver limitations, and document verified troubleshooting. | [#116](https://github.com/alondero/aerogauge-recomp/issues/116) tracked follow-up. |
| 5 | [Lamborghini's controller guide](https://github.com/alondero/automobililamborghini-recomp/blob/1b12f3005a96c69e86dcd4b18cbd77a2002faf12/docs/controllers.md) and [assist implementation](https://github.com/alondero/automobililamborghini-recomp/blob/1b12f3005a96c69e86dcd4b18cbd77a2002faf12/src/lambo_driving_assists.cpp) show optional auto-accelerate: during active driving it supplies throttle until braking; braking pauses it, and it is gated off for menus, pause, attract, finish, and replay. This could reduce sustained-button effort in AeroGauge. | [#117](https://github.com/alondero/aerogauge-recomp/issues/117) tracked follow-up; AeroGauge's Easy Turbo + Boost Start is a different feature. |
| 6 | [Banjo's README](https://github.com/BanjoRecomp/BanjoRecomp/blob/main/README.md#mod-support) documents installing mods by dragging files onto the window before gameplay or through its mod menu. AeroGauge's experimental manager currently installs packages through its UI; drag-and-drop could shorten the pre-game path. | [#118](https://github.com/alondero/aerogauge-recomp/issues/118) tracked follow-up. |

These ranked entries are already tracked; they are not new issue proposals.
The existing performance follow-ups (#80–86), presentation/game-rate diagnostics (#94), per-view
HUD work (#96), Controller Pak transfer (#93), and save profiles (#99)
remain the owners for those areas and should not be duplicated here. AeroGauge
already has runtime EEPROM backup, so save-backup work is not a new gap. Its
current controls, graphics settings, save locations, and mod limitations are
documented in the [README](../README.md); this comparison identifies remaining
workflow opportunities rather than suggesting wholesale adoption of another
project's architecture.

The cited README claims and code paths describe their own projects. They do
not establish hands-on behavior on AeroGauge's supported platforms, nor do
they prove that a peer's optional or experimental feature is broadly tested.
