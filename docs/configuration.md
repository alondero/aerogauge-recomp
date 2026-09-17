# Configuration

The port has two JSON files. It creates them with defaults on the first run.

- graphics.json stores renderer, window, and visual enhancement settings.
- enhancements.json stores gameplay assists.

The files live in the application directory:

| Host | Directory |
| --- | --- |
| Windows | %LOCALAPPDATA%/AeroGaugeRecomp |
| Linux | $XDG_CONFIG_HOME/AeroGaugeRecomp, or ~/.config/AeroGaugeRecomp |
| Portable mode | The current working directory, when portable.txt exists there |

Portable mode is enabled by creating an empty file named portable.txt in the
current working directory before launch. The file only needs to exist. Its
contents are ignored. Run the executable from that directory.

## graphics.json

The normal file contains the following keys. Enum values are case-sensitive.

| Key | Values or type | Default | Meaning |
| --- | --- | --- | --- |
| res_option | Auto, Original, Original2x | Auto | Internal resolution |
| wm_option | Windowed, Fullscreen | Windowed | Startup window mode |
| hr_option | Original, Clamp16x9, Full | Clamp16x9 | HUD aspect behavior |
| api_option | Auto, D3D12, Vulkan, Metal | Auto | Graphics API request |
| ar_option | Original, Expand, Manual | Expand | 3D aspect behavior |
| msaa_option | None, MSAA2X, MSAA4X, MSAA8X | MSAA2X | Anti-aliasing |
| rr_option | Original, Display, Manual | Display | Presentation rate |
| hpfb_option | Auto, On, Off | Auto | High-precision framebuffer |
| rr_manual_value | integer | 60 | Target when rr_option is Manual |
| ds_option | integer | 1 | Resolution multiplier used by RT64 |
| developer_mode | true or false | false | RT64 developer tools |
| window_width | integer from 320 to 7680 | 1600 | Window width |
| window_height | integer from 240 to 4320 | 900 | Window height |
| texture_pack | path or empty string | empty | RT64 texture replacement input |
| texture_dump | directory or empty string | empty | RT64 texture dump destination |
| widescreen_fog_match | true or false | true | 3P/4P fog matching enhancement |
| widescreen_sky_match | true or false | true | 3P/4P sky matching enhancement |
| draw_distance_scale | 0, or a number from 1 to 10000 | 100 | Far-plane multiplier |
| full_track | true or false | true | Register all course geometry |

The current settings menu exposes the most common graphics settings on the
supported Windows and Linux paths. Some keys, including the 3P/4P fog and sky
options, are currently easiest to edit by hand.

The draw-distance scale has two special values:

- 1 is the original game's far plane.
- 0 means no finite far-plane clip.

The default of 100 is an enhancement, not a measured promise that every
course is correct at every distance. Full-course geometry is also
experimental. Use false for both settings when comparing the port with the
original visibility behavior.

The current port accepts the enum names supplied by
[N64ModernRuntime's configuration header](https://github.com/N64Recomp/N64ModernRuntime/blob/main/ultramodern/include/ultramodern/config.hpp).
That does not make every upstream option supported on every AeroGauge host.
For example, this branch has no supported macOS window path.

## enhancements.json

The file currently contains one key:

| Key | Values | Default | Meaning |
| --- | --- | --- | --- |
| easy_turbo_boost | true or false | false | Enables the port's Boost Start and R-button Turbo assist |

The assist preserves the game's own boost timer, heat, and overheat logic. It
only adds the input path described in the [README](../README.md).

## Environment variables

Environment variables are useful for tests and repeatable investigations.
They are not a second settings UI. When a variable corresponds to a JSON
setting, its value wins for that run.

### Player and normal-launch settings

| Variable | Values | Effect |
| --- | --- | --- |
| AERO_PAK_PATH | file path | Uses this raw Controller Pak image instead of the default save path |
| AERO_CONTROLLER_PAK | 0 or another value | 0 disables the virtual Controller Pak |
| AERO_RUMBLE | 0 or another value | 0 disables gamepad vibration |
| AERO_RUMBLE_TURBO | 0 or another value | 0 disables the Turbo part of vibration |
| AERO_TEXTURE_PACK | directory or RT64 pack path | Loads texture replacements at startup |
| AERO_TEXTURE_DUMP | directory path | Asks RT64 to write used texture data there |
| AERO_FOG_MATCH_1P | 0 or 1 | Overrides the 3P/4P fog matching option |
| AERO_SKY_MATCH_1P | 0 or 1 | Overrides the 3P/4P sky matching option |
| AERO_DRAW_DISTANCE_SCALE | number | Overrides draw_distance_scale |
| AERO_FULL_TRACK | 0 or 1 | Overrides full_track |
| AERO_EASY_TURBO | 0 or 1 | Overrides easy_turbo_boost |
| AERO_HEADLESS | 1 | Skips the window and RT64 and uses the software test renderer |

AERO_GRAPHICS_CONFIG and AERO_ENHANCEMENTS_CONFIG point the two JSON loaders
at alternate files. They are intended for tests. They are useful when a test
must not modify a developer's normal settings.

The current code accepts AERO_CONTROLLER_PAK and the rumble variables using a
simple rule: exactly 0 disables the feature; any other value leaves it on.
The option variables that end in 1 use exactly 1 for true when they are set.

### Developer and test settings

These options may change across investigations. They are documented so a
future developer can identify them, not because they are a supported player
interface.

| Variable | Form | Effect |
| --- | --- | --- |
| AERO_WARP | track or track:craft | One-shot race warp; track is 1-6 and craft is 1-10 |
| AERO_WARP_AT | vi:track or vi:track:craft | Schedules a warp at a VI count |
| AERO_STATE_FILE | file path | F7/F8 save-state slot |
| AERO_STATE_SAVE | file path | One-shot settled-frame save |
| AERO_STATE_SAVE_SCENE | scene number | Scene for the automatic save; default 5 |
| AERO_STATE_SAVE_DELAY | VI ticks | Extra wait after the save scene becomes eligible |
| AERO_STATE_LOAD | file path | One-shot load during startup |
| AERO_STATE_LOAD_SCENE | scene number | Scene for the automatic load; default 3 |
| AERO_STATE_LOAD_DELAY | VI ticks | Extra wait after the load scene becomes eligible |
| AERO_MODERN_MAX_VIS | positive integer | Exit after this many VI ticks |
| AERO_MODERN_INPUT | button hex[:stick x[:stick y]] | Held N64 input for a headless run |
| AERO_MODERN_INPUT_AFTER | vi:button hex:stick x:stick y | Replaces the held test input at a VI |
| AERO_INPUT_PULSE | button hex:period:duty[:start vi[:count]] | Repeated button pulses for menu tests |
| AERO_AUDIO_STATS | present | Prints audio device and queue statistics |
| AERO_AUDIO_RMS | present | Prints a periodic RMS value for submitted PCM |
| AERO_HARNESS_LOG | 1 | Enables low-rate thread diagnostics |
| AERO_FRAME_LOG | file path | Logs slow graphics and VI timing events; the VI log adds .vi |
| AERO_CRASH_SYMBOLS | file path | Adds a symbol-table search path for crash reports |
| AERO_CRASH_TEST | text | Injects a deliberate crash for crash-report testing |
| AERO_LIGHTING_SELFTEST | present | Runs the no-ROM software-renderer lighting test |
| LAMBO_THREAD_TRACE | present | Enables the legacy thread-message trace in local runtime patch 0001 |
| RT64_MATCH_DEBUG | 1 or 2 | Enables local interpolation-match diagnostics; 2 is more verbose |

### Renderer and display-list investigations

The following variables are for ROM and renderer investigations. They can
write large dumps to the current directory, so set an explicit output name
and remove the files after a run.

| Variable | Form | Effect |
| --- | --- | --- |
| AERO_WS_RETAG | 0 or another value | Disables or enables the widescreen HUD retag pass |
| AERO_WS_TRACE | 1 or 2 | Logs HUD gate and rectangle classification |
| AERO_WS_NEEDLE_DX | number | Replaces the measured widescreen needle shift |
| AERO_WS_INTRO | 0 or another value | Disables or enables the race-intro widescreen pass |
| AERO_FT_SECTIONS | 0 or another value | Selects full-track section registration |
| AERO_FT_OBJECTS | 0 or another value | Selects full-track object registration |
| AERO_FT_ZONE_MASK | hexadecimal mask | Limits full-track registration to selected zones |
| AERO_FT_TRACE | present | Logs full-track course rebuilds |
| AERO_DL_SKIP_DL | comma-separated hex addresses | Replaces selected RT64 display-list calls with no-ops |
| AERO_SWRENDER_NO_FOG | 1 | Omits fog in a software-renderer capture |
| AERO_PROJ_PROBE | present | Logs software-renderer projection observations |
| AERO_DL_INSPECT | present | Enables one-shot display-list summary logic |
| AERO_DL_INSPECT_STATE | number | Display-list inspect threshold |
| AERO_RACE_DL_DUMP | base name | Writes a race display-list and RDRAM dump |
| AERO_DL_DUMP_AT | send_dl count | Uses a send count instead of the unmapped state probe |
| AERO_DL_GEOMSET | positive integer | Logs a display-list geometry set at that stride |
| AERO_MENU_DL_TRACE | present | Logs menu display-list command counts |
| AERO_MENU_DL_DUMP | screen number | Writes one menu display-list and RDRAM dump |
| AERO_DL_RENDER_STATE | number | Software-renderer capture threshold |
| AERO_DL_RENDER_OUT | base name | Software-renderer BMP output base name |
| AERO_DL_RENDER_EVERY | positive integer | Repeats software-renderer captures at that stride |

At present, some display-list probes still use an unmapped game-state
sentinel. Check [Debugging](debugging.md) before relying on a state-based
trigger.

LAMBO_THREAD_TRACE and RT64_MATCH_DEBUG are dependency-patch diagnostics, not
player settings. Their names are retained for compatibility with the local
patches. They are off by default and may produce timing-sensitive logs.

## File errors and live writes

If a JSON file is missing, the port writes a complete file using defaults. If
an individual key is missing or has the wrong type, that key keeps its
default. A malformed JSON file is left untouched and defaults are used in
memory. Fix or delete the malformed file yourself.

The current settings menu writes graphics changes synchronously on the main
thread. This can make a settings change briefly block the event loop. The
[deferred graphics write issue](https://github.com/alondero/aerogauge-recomp/issues/26)
tracks changing that behavior.

The runtime's live graphics configuration also has an open
[thread-safety issue](https://github.com/alondero/aerogauge-recomp/issues/25).
Until that is resolved, treat live configuration changes as a main-branch
engineering feature, not as a safe place to add cross-thread reads.
