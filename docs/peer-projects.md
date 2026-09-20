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
- A matching USA ROM is required. The project cannot distribute it.
- The shared settings screen is present on the Windows, Linux, and Android
  build paths (with Android hiding desktop-only controls), but there is no new
  control-binding page.
- The port still carries ROM-specific guest-memory hooks and a local RT64
  patch stack. These are transitional maintenance boundaries, not a private
  renderer fork or a stable code-mod interface.
- Texture replacement and texture dumping are developer features. The port
  does not currently define a versioned general mod format.

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
