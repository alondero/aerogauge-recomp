# Configuration

The port has two JSON files. It creates them with defaults on the first run.

- graphics.json stores renderer, window, and visual enhancement settings.
- enhancements.json stores gameplay assists.

The files live in the application directory:

| Host | Directory |
| --- | --- |
| Windows | %LOCALAPPDATA%/AeroGaugeRecomp |
| Linux | $XDG_CONFIG_HOME/AeroGaugeRecomp, or ~/.config/AeroGaugeRecomp |
| Android | App-private `files/AeroGaugeRecomp` (use the launcher’s save backup action) |
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
| force_full_lod | true or false | false | Maximum car model detail and no car distance cutoff |

The current settings menu exposes the common graphics settings on the supported
Windows, Linux, and Android paths. Android omits desktop-only window, API, and
texture-path controls. Some keys, including the 3P/4P fog and sky options, are
currently easiest to edit by hand.

The draw-distance scale has two special values:

- 1 is the original game's far plane.
- 0 means no finite far-plane clip.

**Force Full LOD** is on the Graphics page and follows Apply/Discard. It keeps
cars at their near-detail model and removes the separate 750-unit car cutoff.
Near-camera and viewing-angle culling remain active. The projection still uses
the Draw distance setting; use extended or unlimited distance for distant cars.
This option does not change course geometry or texture mipmapping.
`AERO_FORCE_FULL_LOD=1` or `0` overrides the saved value for a run and disables
the menu control. More detailed distant cars can cost performance.

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
| AERO_FORCE_FULL_LOD | 0 or 1 | Overrides force_full_lod |
| AERO_EASY_TURBO | 0 or 1 | Overrides easy_turbo_boost |
| AERO_HEADLESS | 1 | Skips the window and RT64 and uses the software test renderer |

AERO_GRAPHICS_CONFIG and AERO_ENHANCEMENTS_CONFIG point the two JSON loaders
at alternate files. They are intended for tests. They are useful when a test
must not modify a developer's normal settings.

The current code accepts AERO_CONTROLLER_PAK and the rumble variables using a
simple rule: exactly 0 disables the feature; any other value leaves it on.
The option variables that end in 1 use exactly 1 for true when they are set.

### Developer and test settings

These options are developer and test controls. They are documented so a
developer can reproduce a run, not because they are a supported player
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

### Renderer and display-list probes

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

## Runtime variable contract

The tables above are a quick effect index. This table records the contract for
each variable named in this document. "Unset" means that the process does not
have the variable. Examples use POSIX shell syntax; in PowerShell set
$env:NAME before running the executable.

### Launch and player overrides

| Variable | Scope and accepted form | Default and precedence | Read timing | Side effect and owner | Example |
| --- | --- | --- | --- | --- | --- |
| AERO_GRAPHICS_CONFIG | Launch or test; file path | Unset uses app data graphics.json; set replaces that path | Startup config load | Reads and may write the merged JSON; src/aero_config.cpp | AERO_GRAPHICS_CONFIG=tmp/graphics.json ./build/aerogauge_modern |
| AERO_ENHANCEMENTS_CONFIG | Launch or test; file path | Unset uses app data enhancements.json; set replaces that path | Startup config load | Reads and may write the merged JSON; src/aero_config.cpp | AERO_ENHANCEMENTS_CONFIG=tmp/enhancements.json ./build/aerogauge_modern |
| AERO_CONFIG_WRITE_DEBOUNCE_MS | Launch or test; integer milliseconds | Unset uses 250; a non-numeric value, a negative value, or an overflow falls back to 250; values above 60000 are clamped | Every queued settings write | Sets how long the background writer waits for edits to stop before rewriting graphics.json or enhancements.json; src/aero_config.cpp | AERO_CONFIG_WRITE_DEBOUNCE_MS=60000 ./build/aerogauge_modern |
| AERO_PAK_PATH | Launch; file path | Unset uses the per-user saves path; set selects the Controller Pak image | Startup | Selects or creates the save-file parent; src/main.cpp and src/aero_pak.cpp | AERO_PAK_PATH=tmp/test.mpk ./build/aerogauge_modern |
| AERO_CONTROLLER_PAK | Launch; 0 or another value | Enabled when unset; exactly 0 disables it | Startup | Controls virtual Controller Pak presence; src/main.cpp and src/aero_pak.cpp | AERO_CONTROLLER_PAK=0 ./build/aerogauge_modern |
| AERO_RUMBLE | Launch; 0 or another value | Enabled when unset; exactly 0 disables rumble | Startup | Controls the normal rumble callback; src/main.cpp | AERO_RUMBLE=0 ./build/aerogauge_modern |
| AERO_RUMBLE_TURBO | Launch; 0 or another value | Enabled when unset; exactly 0 disables Turbo rumble | Startup | Controls Turbo-specific rumble; src/main.cpp | AERO_RUMBLE_TURBO=0 ./build/aerogauge_modern |
| AERO_TEXTURE_PACK | Launch or test; directory or .rtz path | Unset uses the JSON value, empty by default; set wins over JSON | Startup and menu refresh | Supplies RT64 texture replacements and disables the matching menu field; src/aero_config.cpp | AERO_TEXTURE_PACK=assets/textures ./build/aerogauge_modern |
| AERO_TEXTURE_DUMP | Launch or test; directory path | Unset uses the JSON value, empty by default; set wins over JSON | Startup and menu refresh | Gives RT64 a texture dump destination and disables the matching menu field; src/aero_config.cpp | AERO_TEXTURE_DUMP=tmp/textures ./build/aerogauge_modern |
| AERO_FOG_MATCH_1P | Launch or test; 0 or 1 | Unset uses the JSON value, true by default; set wins | Each fog-policy query | Changes the 3P/4P widescreen fog branch; src/aero_config.cpp and src/aero_hud_widescreen.c | AERO_FOG_MATCH_1P=0 ./build/aerogauge_modern |
| AERO_SKY_MATCH_1P | Launch or test; 0 or 1 | Unset uses the JSON value, true by default; set wins | Each sky-policy query | Changes the 3P/4P sky branch; src/aero_config.cpp and src/aero_hud_widescreen.c | AERO_SKY_MATCH_1P=0 ./build/aerogauge_modern |
| AERO_DRAW_DISTANCE_SCALE | Launch or test; number | Unset uses JSON, 100 by default; set wins and is clamped | Each guPerspectiveF replacement call | Changes the far-plane calculation; src/aero_config.cpp and src/aero_draw_distance.cpp | AERO_DRAW_DISTANCE_SCALE=1 ./build/aerogauge_modern |
| AERO_FULL_TRACK | Launch or test; 0 or 1 | Unset uses JSON, true by default; set wins | Each full-track policy query | Enables or disables all-course registration; src/aero_config.cpp and src/aero_full_track.cpp | AERO_FULL_TRACK=0 ./build/aerogauge_modern |
| AERO_FORCE_FULL_LOD | Launch or test; 0 or 1 | Unset uses JSON, false by default; set wins | Each car LOD hook | Forces near-detail car meshes and removes their distance cutoff; src/aero_car_lod.c | AERO_FORCE_FULL_LOD=1 ./build/aerogauge_modern |
| AERO_EASY_TURBO | Launch or test; 0 or 1 | Unset uses JSON, false by default; set wins | First semantic input query | Enables the alternate Turbo and Boost Start input path; src/aero_config.cpp and src/aero_turbo_boost.c | AERO_EASY_TURBO=1 ./build/aerogauge_modern |
| AERO_HEADLESS | Test or investigation; use 1 or any present value | Unset uses the RT64 window path when available | Startup | Skips the window and selects the software renderer; src/aero_rt64.h and src/main.cpp | AERO_HEADLESS=1 ./build/aerogauge_modern |

### Developer, headless, and crash controls

These variables have no JSON setting. Unset means no request, no probe, or no
extra output unless the row says otherwise.

| Variable | Scope and accepted form | Default and precedence | Read timing | Side effect and owner | Example |
| --- | --- | --- | --- | --- | --- |
| AERO_WARP | Developer run; track or track:craft | Unset means no warp | First game tick that can consume it | Requests a race scene through the guest hook; src/aero_warp.c | AERO_WARP=3:2 ./build/aerogauge_modern |
| AERO_WARP_AT | Headless test; vi:track[:craft] | Unset means no scheduled warp | First VI callback | Publishes one warp request at the selected VI; src/main.cpp | AERO_WARP_AT=300:3:2 ./build/aerogauge_modern |
| AERO_STATE_FILE | Developer run; file path | Unset uses aero_savestate.astate in the working directory | When F7 or F8 is used | Selects the interactive save-state slot; src/aero_savestate.c | AERO_STATE_FILE=tmp/slot.astate ./build/aerogauge_modern |
| AERO_STATE_SAVE | Headless test; file path | Unset means no automatic save | First save-state tick, then target scene | Writes one settled RDRAM snapshot; src/aero_savestate.c | AERO_STATE_SAVE=tmp/race.astate ./build/aerogauge_modern |
| AERO_STATE_SAVE_SCENE | Headless test; integer | Unset uses scene 5 | First save-state tick | Selects the scene at which AERO_STATE_SAVE may run; src/aero_savestate.c | AERO_STATE_SAVE_SCENE=5 ./build/aerogauge_modern |
| AERO_STATE_SAVE_DELAY | Headless test; integer VI ticks | Unset uses 0 | First save-state tick | Delays the automatic save after its scene gate; src/aero_savestate.c | AERO_STATE_SAVE_DELAY=30 ./build/aerogauge_modern |
| AERO_STATE_LOAD | Headless test; file path | Unset means no automatic load | First save-state tick, then target scene | Restores one RDRAM snapshot; src/aero_savestate.c | AERO_STATE_LOAD=tmp/race.astate ./build/aerogauge_modern |
| AERO_STATE_LOAD_SCENE | Headless test; integer | Unset uses scene 3 | First save-state tick | Selects the scene at which AERO_STATE_LOAD may run; src/aero_savestate.c | AERO_STATE_LOAD_SCENE=3 ./build/aerogauge_modern |
| AERO_STATE_LOAD_DELAY | Headless test; integer VI ticks | Unset uses 0 | First save-state tick | Delays the automatic load after its scene gate; src/aero_savestate.c | AERO_STATE_LOAD_DELAY=30 ./build/aerogauge_modern |
| AERO_MODERN_MAX_VIS | Headless test; positive integer | Unset uses 120 in headless mode and no cap in RT64 mode | Startup | Stops the boot harness after a VI budget; src/main.cpp | AERO_MODERN_MAX_VIS=120 ./build/aerogauge_modern |
| AERO_MODERN_INPUT | Headless test; button hex[:stick x[:stick y]] | Unset adds no held input | Startup | ORs a held N64 input into every game read; src/main.cpp | AERO_MODERN_INPUT=1000 ./build/aerogauge_modern |
| AERO_MODERN_INPUT_AFTER | Headless test; startvi:button hex:stick x:stick y | Unset makes no replacement | Startup, then selected VI | Replaces the held test input atomically at one VI; src/main.cpp | AERO_MODERN_INPUT_AFTER=300:0:53:0 ./build/aerogauge_modern |
| AERO_INPUT_PULSE | Headless test; button hex:period:duty[:start vi[:count]] | Unset produces no pulses | Startup, then each VI | Generates repeatable menu button edges; src/main.cpp | AERO_INPUT_PULSE=1000:150:4:300:4 ./build/aerogauge_modern |
| AERO_AUDIO_STATS | Audio investigation; any present value | Unset means no device statistics | Audio initialization | Logs device and queue counters; src/aero_audio.cpp | AERO_AUDIO_STATS=1 ./build/aerogauge_modern |
| AERO_AUDIO_RMS | Audio investigation; any present value | Unset means no RMS output | First submitted PCM, then audio callbacks | Logs the RMS of submitted PCM; src/aero_audio.cpp | AERO_AUDIO_RMS=1 ./build/aerogauge_modern |
| AERO_HARNESS_LOG | Timing investigation; use 1 | Unset or another value means off | First call to the shared helper | Enables low-rate thread and renderer logs; src/aero_config.cpp, src/main.cpp, and renderer files | AERO_HARNESS_LOG=1 ./build/aerogauge_modern |
| AERO_FRAME_LOG | Timing investigation; file path | Unset means no frame log | Each request to open a frame log | Creates or truncates the named log and optional VI log; src/aero_config.cpp | AERO_FRAME_LOG=tmp/frame.log ./build/aerogauge_modern |
| AERO_CRASH_SYMBOLS | Crash investigation; file or directory path | Unset searches the working directory and executable locations | Crash-handler startup | Adds a symbol-table search location; src/aero_crash.cpp | AERO_CRASH_SYMBOLS=aerogauge.syms.toml ./build/aerogauge_modern |
| AERO_CRASH_TEST | Crash-test run; text or any present value | Unset means no injected crash | Startup | Schedules a deliberate crash and report; src/main.cpp and src/aero_crash.cpp | AERO_CRASH_TEST=smoke ./build/aerogauge_modern |
| AERO_LIGHTING_SELFTEST | Host test; any present value | Unset runs the normal program | Before ROM loading | Runs the synthetic lighting test and exits; src/main.cpp and src/stub_renderer.cpp | AERO_LIGHTING_SELFTEST=1 ./build/aerogauge_modern |
| LAMBO_THREAD_TRACE | Runtime-patch investigation; non-empty and not 0 | Unset or 0 means off | First patched scheduler check and thread starts | Logs native thread activity; local patch 0001 | LAMBO_THREAD_TRACE=1 ./build/aerogauge_modern |
| RT64_MATCH_DEBUG | Renderer-patch investigation; integer 1 or 2 | Unset or 0 means off | First interpolation match call | Logs transform-match decisions; local patch 0006 | RT64_MATCH_DEBUG=1 ./build/aerogauge_modern |

### Renderer and display-list probes

The renderer probes are not player settings. Most are read once so a run is
repeatable. They can write large files or mutate a display list used by the
renderer; use them only with a saved investigation command.

| Variable | Scope and accepted form | Default and precedence | Read timing | Side effect and owner | Example |
| --- | --- | --- | --- | --- | --- |
| AERO_WS_RETAG | Renderer investigation; 0 disables, another value enables | Enabled when unset | First HUD hook | Re-emits HUD rectangles with RT64 alignment commands; src/aero_hud_widescreen.c | AERO_WS_RETAG=0 ./build/aerogauge_modern |
| AERO_WS_TRACE | Renderer investigation; 1 or 2 | Unset means off | First HUD trace call | Logs HUD gates and rectangle classes; src/aero_hud_widescreen.c | AERO_WS_TRACE=2 ./build/aerogauge_modern |
| AERO_WS_NEEDLE_DX | Renderer investigation; number | Unset uses 53.333 pixels | Each shifted HUD frame | Changes the speedometer needle translation; src/aero_hud_widescreen.c | AERO_WS_NEEDLE_DX=53.333 ./build/aerogauge_modern |
| AERO_WS_INTRO | Renderer investigation; 0 disables, another value enables | Enabled when unset | Each intro draw hook | Adds the widescreen intro rectangle path; src/aero_race_intro.c | AERO_WS_INTRO=0 ./build/aerogauge_modern |
| AERO_FT_SECTIONS | Full-track investigation; 0 or another value | Enabled when unset; 0 selects the original section path | First section registration | Chooses section registration mode; src/aero_full_track.cpp | AERO_FT_SECTIONS=0 ./build/aerogauge_modern |
| AERO_FT_OBJECTS | Full-track investigation; 0 or another value | Enabled when unset; 0 selects the original object path | First object registration | Chooses object registration mode; src/aero_full_track.cpp | AERO_FT_OBJECTS=0 ./build/aerogauge_modern |
| AERO_FT_ZONE_MASK | Full-track investigation; hexadecimal mask | All zones when unset | First zone-mask query | Limits registered zones; src/aero_full_track.cpp | AERO_FT_ZONE_MASK=ff ./build/aerogauge_modern |
| AERO_FT_TRACE | Full-track investigation; any present value | Unset means off | Each course rebuild | Logs course rebuild progress; src/aero_full_track.cpp | AERO_FT_TRACE=1 ./build/aerogauge_modern |
| AERO_DL_SKIP_DL | RT64 investigation; comma-separated hexadecimal addresses | Unset means no rewrite | First RT64 display-list submission | Replaces matching G_DL commands with no-ops after the guest walk; src/rt64_renderer.cpp | AERO_DL_SKIP_DL=80012340 ./build/aerogauge_modern |
| AERO_SWRENDER_NO_FOG | Software-renderer investigation; use 1 | Unset means fog is on | First software render | Omits fog from software captures; src/stub_renderer.cpp | AERO_SWRENDER_NO_FOG=1 AERO_HEADLESS=1 ./build/aerogauge_modern |
| AERO_PROJ_PROBE | Software-renderer investigation; any present value | Unset means off | First software render | Logs projection observations; src/stub_renderer.cpp | AERO_PROJ_PROBE=1 AERO_HEADLESS=1 ./build/aerogauge_modern |
| AERO_DL_INSPECT | Display-list investigation; any present value | Unset means off | First display-list submission | Writes one display-list summary when its state gate is reached; src/stub_renderer.cpp | AERO_DL_INSPECT=1 ./build/aerogauge_modern |
| AERO_DL_INSPECT_STATE | Display-list investigation; integer | Unset uses state 8 | First inspection check | Sets the inspection threshold; src/stub_renderer.cpp | AERO_DL_INSPECT=1 AERO_DL_INSPECT_STATE=8 ./build/aerogauge_modern |
| AERO_RACE_DL_DUMP | Display-list investigation; output base name | Unset means no dump | First display-list submission | Writes a text display-list walk and an 8 MiB RDRAM file when its gate fires; src/stub_renderer.cpp | AERO_RACE_DL_DUMP=tmp/race ./build/aerogauge_modern |
| AERO_DL_DUMP_AT | Display-list investigation; send_dl count | Unset leaves the state gate unchanged | First display-list submission when a race dump is configured | Provides a frame-count trigger for AERO_RACE_DL_DUMP; src/stub_renderer.cpp | AERO_RACE_DL_DUMP=tmp/race AERO_DL_DUMP_AT=2500 ./build/aerogauge_modern |
| AERO_DL_GEOMSET | Display-list investigation; positive integer stride | Unset means off | First display-list submission | Logs sampled display-list geometry sets; src/stub_renderer.cpp | AERO_DL_GEOMSET=30 ./build/aerogauge_modern |
| AERO_MENU_DL_TRACE | Menu investigation; any present value | Unset means off | First display-list submission | Logs menu sprite command counts; src/stub_renderer.cpp | AERO_MENU_DL_TRACE=1 ./build/aerogauge_modern |
| AERO_MENU_DL_DUMP | Menu investigation; screen number | Unset means no dump | First display-list submission | Writes one menu display-list walk and RDRAM file; src/stub_renderer.cpp | AERO_MENU_DL_DUMP=4 ./build/aerogauge_modern |
| AERO_DL_RENDER_STATE | Software-renderer investigation; integer | Unset uses state 8 | Each capture check | Selects the software capture threshold; src/stub_renderer.cpp | AERO_DL_RENDER_STATE=8 AERO_HEADLESS=1 ./build/aerogauge_modern |
| AERO_DL_RENDER_OUT | Software-renderer investigation; output base name | Unset uses dl_render_state8.bmp | When a capture is written | Selects the BMP output name; src/stub_renderer.cpp | AERO_DL_RENDER_OUT=tmp/frame.bmp AERO_HEADLESS=1 ./build/aerogauge_modern |
| AERO_DL_RENDER_EVERY | Software-renderer investigation; positive integer | Unset or 0 captures only once | First display-list submission, then every stride | Requests numbered follow-up BMP captures; src/stub_renderer.cpp | AERO_DL_RENDER_EVERY=30 AERO_HEADLESS=1 ./build/aerogauge_modern |

## Build-only and compile-time names

These names affect tool discovery or compilation. They are not settings that
players should put in JSON or expect to work after the program starts.

| Name | Scope and accepted form | Default and precedence | Read timing | Effect and owner | Example |
| --- | --- | --- | --- | --- | --- |
| AERO_PYTHON_SCRIPTS | Windows build script; directory path | Unset searches the local Python installation, then PATH | Before CMake checks | Prepends Python's Scripts directory; build.ps1 | $env:AERO_PYTHON_SCRIPTS='path/to/python/Scripts'; .\build.ps1 |
| ROM_FILENAME | Build and CI; ROM file name | Defaults to AeroGauge (USA).z64; CI supplies the same name to both scripts | Before the ROM check | Selects the input file; build scripts and build-release.yml | ROM_FILENAME='AeroGauge (USA).z64' ./build.sh |
| Python3_EXECUTABLE | CMake configure; interpreter path | Unset searches standard interpreter names | Configure time | Selects the Python used by ROM-backed helper generation; CMakeLists.txt | cmake -S . -B build -DPython3_EXECUTABLE=python3 |
| CMAKE_BUILD_TYPE | CMake configure; build type | Release in the host scripts; a direct configure may choose another type | Configure time | Selects compiler optimization and debug settings; CMake | cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug |
| RT64_STATIC | Internal CMake value; boolean | Set true by CMakeLists.txt | Configure time | Selects static RT64 linkage; CMakeLists.txt | cmake -S . -B build -DRT64_STATIC=TRUE |
| RT64_SDL_WINDOW_VULKAN | Internal CMake value; boolean | Set for non-Windows targets | Configure time | Selects the SDL/Vulkan window path; CMakeLists.txt | cmake -S . -B build -DRT64_SDL_WINDOW_VULKAN=TRUE |
| SDL_MAIN_HANDLED | Frontend test compile definition; no user value | Set only for the Windows frontend test | Compile time | Prevents SDL from supplying a second main entry point; cmake/Frontend.cmake | cmake -S . -B build |
| NOMINMAX | Frontend test compile definition; no user value | Set only for the Windows frontend test | Compile time | Prevents Windows headers from defining min and max macros; cmake/Frontend.cmake | cmake -S . -B build |
| _WIN32 and __linux__ | Compiler platform macros; compiler-defined | Set by the compiler for the target platform | Compile time | Select platform window, audio, and crash paths; source files | cmake -S . -B build |
| AERO_CRASH_WIN32 and AERO_CRASH_POSIX | Internal source macros; no user value | Defined by aero_crash.cpp from the platform | Compile time | Selects the crash-report implementation; src/aero_crash.cpp | cmake -S . -B build |

## File errors and live writes

If a JSON file is missing, the port writes a complete file using defaults. If
an individual key is missing or has the wrong type, that key keeps its
default. A malformed JSON file is left untouched and defaults are used in
memory. Fix or delete the malformed file yourself.

A settings change is not written by the thread that drains the SDL event loop. A
menu action or hotkey records the change in memory, and a background I/O worker
writes it once the edits stop. Every change restarts a 250 ms window, so a run of
clicks or a slider drag costs one file round trip instead of one per edit.

The trade-off is latency against write volume. A window that restarts on every
change means a continuously manipulated setting is not written until the user
stops; a fixed-rate window would write on a bound instead, at the cost of one
round trip per window during the same interaction. Continuous input is the case
here, so the debounce wins, and the exit flush covers the rest.

Both files are still written inline at startup, so they exist before the game
starts. Pending writes are flushed on the normal exit paths, including the
SDL_QUIT path and the headless VI cap.

A crash does not flush. The crash handler ends the process from a signal or
exception context where taking a lock is not safe, so a crash discards whatever
was still inside the debounce window.

The worker re-reads the file before merging, so a hand edit made while the game
is running is preserved exactly as before. A malformed file is still left
untouched.

AERO_CONFIG_WRITE_DEBOUNCE_MS overrides the debounce window. It exists for tests
that need to observe an unwritten change; a value of 0 writes as soon as the
worker wakes. A malformed value, such as trailing characters or an overflow,
falls back to the default rather than silently meaning something else.

The runtime's live graphics configuration is safe to read from any thread. Local
runtime patch 0018 makes `ultramodern::renderer::get_graphics_config()` return a
snapshot copy taken while the runtime's configuration mutex is held, so a
menu-thread `set_graphics_config()` cannot change a configuration a game, VI, or
graphics thread is reading. See the
[patch inventory](../patches/README.md).
