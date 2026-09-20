# Android

The ARM64 APK runs the same recompiled game and RT64 renderer as the desktop
port. It contains no ROM. Android 9 or newer and a Vulkan driver supporting
descriptor indexing and scalar block layout are required; a Vulkan version
number alone does not guarantee compatibility.

## Install and play

1. Download `aerogauge-recomp-android-arm64.apk` from
   [Releases](https://github.com/alondero/aerogauge-recomp/releases).
2. Open it and allow installation from that source if Android asks.
3. Open **AeroGauge Recompiled**, tap **Import ROM**, and choose your own USA
   cartridge dump. Uncompressed `.z64`, `.v64`, and `.n64` files are accepted.
4. Tap **Play AeroGauge**.

Import checks the complete 8 MiB ROM after normalizing its byte order. Invalid,
modified, or other-region ROMs produce an explanation without replacing an
existing import or saves. The app copies the ROM into its private storage;
you do not need to rename files or grant broad storage access.

The left touch stick steers and navigates game menus. A confirms/accelerates,
B cancels/brakes, Z drifts, and Start pauses/advances. L, R and the four C
buttons are also available. Multiple fingers can steer, accelerate and drift
together. Connect a Bluetooth or USB gamepad to use the desktop mappings.
The launcher offers automatic, always-visible and hidden touch controls.

**Menu** opens Resume, Graphics & enhancements, and Exit to launcher.
Android Back opens the same menu. The settings screen accepts touch and
controller input. Android owns screen size and fullscreen mode; those desktop
settings are disabled. Initial mobile settings use original resolution and
frame rate without MSAA. Raise them in settings if your device has headroom.

## GPU drivers

The Pixel 5's system Adreno driver lacks features RT64 requires, as documented
by the [Lamborghini port](https://github.com/alondero/automobililamborghini-recomp/blob/main/docs/android.md).

A modern Adreno system driver can run the game. The Adreno 750 system driver
(v0762.41) used to return `VK_ERROR_UNKNOWN` from `vkCreateComputePipelines`
for the RT64 framebuffer compute shaders that contain the 16-bit byte swap.
[Patch 0023](../patches/0023-rt64-adreno-endian-swap.patch) spells that swap in
a form the driver accepts; the value is unchanged. The
[renderer reference](reference/renderer.md#adreno-compute-pipeline-rejection)
has the reduction. Prefer the system driver and import Turnip when graphics
initialization fails.

The launcher can import an AdrenoTools-compatible Mesa Turnip ZIP. Driver code
stays private to AeroGauge; importing it does not modify Android or other apps.
Use a trusted driver compatible with your Qualcomm GPU. Turnip is not a Mali
compatibility solution. **Use system driver** restores the default for the
next game launch. Exit the running game before changing drivers.

If graphics initialization fails, return to the launcher and check the selected
driver. **Export diagnostics** saves the native log through Android's file picker.
This does not export the ROM or saves.

## Saves and updates

Saves, configuration and imported files live in app-private storage. APK updates
signed with the same release key retain them. Uninstalling or clearing app data
removes them. Exit to the launcher and select **Back up saves** to export the
EEPROM and Controller Pak files as a ZIP. Keep that ZIP independently of the
phone. **Restore saves** accepts that bounded ZIP format and replaces the
current images only after validating their names and exact sizes. Both actions
use a process lock so a running game cannot copy or replace its files.

## Build

Install Python 3, Git, Ninja, a native CMake 3.22+, JDK 17 or 21, a host GCC/G++
toolchain, Android SDK platform 35, build tools 35.0.0, and NDK 28.2.13676358.
Windows requires MinGW-w64 on PATH and native Windows CMake, not MSYS CMake.

```sh
sdkmanager 'platforms;android-35' 'build-tools;35.0.0' 'ndk;28.2.13676358'
export ANDROID_HOME=/path/to/Android/Sdk
export JAVA_HOME=/path/to/jdk-17
python3 scripts/build_android.py --install
```

PowerShell, after setting JAVA_HOME and adding MinGW-w64 to PATH:

```powershell
$env:ANDROID_HOME = "$env:LOCALAPPDATA/Android/Sdk"
python scripts/build_android.py --cmake "$env:ANDROID_HOME/cmake/3.22.1/bin/cmake.exe" --install
```

Supply `AeroGauge (USA).z64` at the repository root. The script initializes
submodules, applies the shared patch series without discarding local changes,
fetches pinned SDL2/FreeType/AdrenoTools sources, builds host recompilers and
shader tools, translates the ROM, cross-compiles native ARM64 libraries, and
packages the APK. Host DXC compiles the SPIR-V shaders. NDK 28 and linker settings
provide 16 KiB native-library alignment.

The debug result is `dist/aerogauge-recomp-android-arm64-debug.apk`.
`--package-only` packages an existing native build; `--jobs N` sets build
parallelism. `--install` installs using ADB and requires one authorized device.
Generated native/host files live under `build-android/`; Gradle output is under
`android/app/build/`. These directories, APKs and signing material are ignored.

## Release signing

The release workflow requires all three platforms to build successfully and
attaches the APK alongside the desktop archives. Android failure or missing
signing configuration prevents publication. It never substitutes a debug key.
All platforms check out the requested immutable tag.

Configure these repository secrets once:

| Secret | Value |
| --- | --- |
| ANDROID_KEYSTORE_BASE64 | Base64-encoded persistent release keystore |
| ANDROID_KEYSTORE_PASSWORD | Keystore password |
| ANDROID_KEY_ALIAS | Signing key alias |
| ANDROID_KEY_PASSWORD | Signing key password |

For a new release identity on Windows, `scripts/setup_android_signing.ps1`
creates a dedicated key outside the repository, protects its directory, backs
up the password with Windows DPAPI, and configures the GitHub secrets. It refuses
to overwrite an existing backup. The encrypted credential backup is tied to
the Windows account and machine; retain the key and password in a separate
secure backup for disaster recovery. Never regenerate a key for an existing app.

For local release builds set ANDROID_KEYSTORE_PATH plus the password/alias
variables above, set AERO_VERSION to the release tag, and run:

```sh
python3 scripts/build_android.py --release
```

Tags must be semantic versions, e.g. `v0.2.0`. Gradle computes versionCode as
`major * 1000000 + minor * 1000 + patch`; minor and patch must be below 1000.
Bump the numeric version for each published APK, including prereleases.
Development builds default to 0.1.0. The workflow verifies APK signatures and
16 KiB ZIP alignment, checks the payload for forbidden game-file extensions,
and removes private build inputs from the runner.

## Acceptance checks

Build/import regression commands are in [Testing](testing.md). On hardware,
check fresh install and cancelled import; invalid ROM preservation; all ROM
byte orders; system-driver failure messaging; valid/invalid driver ZIPs; analog
steering with A and Z held; physical-controller hotplug; touch settings; Home,
screen lock and resume; exit/relaunch; save backup; and signed APK update retention.
Record the phone, OS, GPU driver and observed screens. A successful native build
alone does not establish device compatibility.

The checked-in acceptance run used a Pixel 5 on Android 13 (API 33). The
launcher imported the verified USA ROM and a Mesa Turnip driver ZIP, the game
rendered menus and a race, touch steering and simultaneous A/B/Z input worked,
the secondary-finger Menu action opened while steering and accelerating, Android
Back and Exit to launcher worked, and the nearby-devices permission prompt was
shown. The signed APK installed cleanly.

A second acceptance run used the debug APK on a Galaxy Z Fold 6 (Android 16,
API 36, Adreno 750, system driver v0762.41) with patch 0023. The build without
that patch killed `:game` within two seconds of the first display list, logging
five `vkCreateComputePipelines` failures and faulting inside `vkCmdBindPipeline`.
With the patch the same run logged no pipeline failures, `:game` held one PID
across a 60-second hold with touch input, `/data/tombstones` gained no entry, and
the title screen and touch overlay rendered correctly while the log showed scene
transitions and a course load.

A physical Bluetooth controller, stock-driver failure messaging after the final
install, Home/screen-lock resume, and an in-place signed update with existing
saves still need a device run before claiming universal hardware coverage.

The Android platform boundary lives in `src/android/`, the launcher/input UI
in `android/app/`, and dependency changes in patches 0019–0023. The game process
uses the normal input snapshot and save paths. Java publishes touch samples
atomically; SDL and game threads retain their existing ownership. Backgrounding
releases touch input and waits at the VI callback boundary; SDL owns audio and
surface lifecycle. The runtime's process-exit shutdown is confined to a separate
`:game` process so the launcher remains available.
