# Graphics release acceptance

[Issue #116](https://github.com/alondero/aerogauge-recomp/issues/116) starts
from source audit `023b13ef0d77adc1753b3e92d9da5d621f075405`. The release
inputs are USA (`89ea0690f3e22201`) and Japan Rev A (`d10125abf640077f`).
Use the [visual and timing route](testing.md#graphics-acceptance) to add a
bounded record for each tested build and device. Scope each result to the
actual configuration and scene.

## Backend coverage

| Platform / backend | Visual coverage | Frame pacing |
| --- | --- | --- |
| Windows / D3D12 | Single-player menu and race captures on RTX 3080. See the individual cells below. | Both ROMs measured on one nominal 120 Hz display. |
| Linux / Vulkan | Untested on a native Linux host with a physical display. | Untested. |
| Android / Vulkan | Untested for this build and route. Earlier Pixel 5 and Galaxy Z Fold 6 observations are recorded in [Android acceptance](android.md#acceptance-checks). | Untested. |

## Windows test system

The November 27, 2026 runs used dual-region Release commit
`b48a5ab884a0e0a3252ba50679d22029d124e7ff`, GCC 15.2.0 and the Windows
patch series at that commit. The dependency pins were RT64 `f0728a2`,
Plume `51b1ad4`, N64ModernRuntime `cdf5abb` and RecompFrontend `b1a1477`.

The desktop used Windows 11 Pro Insider Preview build 28120, Ryzen 9 3900X
and GeForce RTX 3080 on NVIDIA driver 630.20. The single attached LG display
reported 3840×2160 at nominal 120 Hz. All runs used windowed D3D12 and
fresh portable profiles. Shader and driver caches were left as the system
had them.

## Windows visual coverage

The captures use Windows Graphics Capture through FFmpeg 8.1.1 in separate
runs at the Display presentation rate. The 4:3, 16:9 and 21:9 game windows
were 1200×900, 1600×900 and 2100×900 respectively. The warp uses craft 1
with neutral driving input unless stated.

| Cell | Observed result |
| --- | --- |
| USA, 16:9, boot/title and Grand Prix menu | The publisher logo, title and machine-selection screen rendered. Scripted A/Start taps (`AERO_INPUT_PULSE=9000:180:12:20:8`) advanced from machine selection into the Canyon Rush flyover. Course selection and results were untested on this route. |
| USA, 4:3 Original HUD, Canyon Rush | The flyover ticker, countdown and steady HUD were visible. This control used Full-course geometry and Force Full LOD off, with Draw distance 1. |
| USA and Japan Rev A, 16:9 Clamp16x9, Canyon Rush | The ticker stayed in its bar, countdown banners stayed centred and the race HUD remained readable. The countdown footage showed READY → SET → GO in USA and SET → READY → GO in Japan Rev A. Regional console parity for that sequence is unverified. |
| USA, 21:9, Canyon Rush, all three HUD modes | The minimap and speedometer stayed inside the 4:3 band in Original, moved to the 16:9 band in Clamp16x9, and reached the wide edges in Full. The damage bar and countdown stayed centred. |
| USA, 16:9 Clamp16x9, Canyon Rush, geometry and LOD on/off | Compared Full-course geometry on/off at Draw distance 100 and Force Full LOD on/off with geometry enabled. The near course and player craft rendered in each setting. The qualifier stayed at the grid, leaving zone-boundary visibility and distant opponent LOD untested. |
| USA, 16:9 Clamp16x9, Bikini Island | The course loaded from the title and the flyover, countdown and opening stretch rendered. Straight acceleration (`AERO_MODERN_INPUT_AFTER=920:8000:0:0`) reached the first bend. Tunnel entry, occlusion and exit were untested. |
| Split-screen, results and transitions between completed races | Untested. |

The Canyon Rush comparison shows USA 4:3, USA 16:9 and Japan Rev A 16:9 from
left to right. The ultrawide comparison shows Original, Clamp16x9 and Full
from top to bottom. [Canyon Rush frames](graphics-acceptance/windows-canyon-rush.webp)
and [ultrawide HUD frames](graphics-acceptance/windows-ultrawide-hud.webp).

## Windows frame pacing

These are single-player Canyon Rush runs with craft 1 and neutral driving
input after the warp. They use a 1600×900 window, Expand aspect, Clamp16x9
HUD, Auto resolution and framebuffer precision, MSAA2X, full-course geometry
on, Force Full LOD off and Draw distance 100. PresentMon 2.6.0 records the
first 10 seconds after the first tracked Present and a separate settled race
interval. NVIDIA's `NvAPI_Disp_GetVRRInfo` was sampled approximately once a
second during timing. The table reports VRR activity as observed by the
driver during the settled race. [NVIDIA NVAPI](https://github.com/NVIDIA/nvapi/blob/main/nvapi.h).

| Region, rate and run | First 10 seconds, displayed intervals (n · p50 / p95 / p99 / max in ms) | Settled race, displayed intervals (n · p50 / p95 / p99 / max in ms) | Warm duration / dropped presents | Warm VRR |
| --- | --- | --- | --- | --- |
| USA, Original 1 | 293 · 33.25 / 37.86 / 56.79 / 82.63 | 767 · 33.04 / 40.55 / 46.65 / 48.33 | 25.62 s / 0 | Active, 24/24 |
| USA, Original 2 | 294 · 33.39 / 37.27 / 47.43 / 90.73 | 93 · 33.37 / 275.36 / 5763.94 / 5763.94 | 25.60 s / 655 | Inactive, 0/25 |
| USA, Manual 60 1 | 579 · 16.68 / 18.15 / 33.41 / 60.95 | 1161 · 16.66 / 18.46 / 33.35 / 83.46 | 25.63 s / 359 | Mixed, active 19/25 |
| USA, Manual 60 2 | 259 · 16.69 / 25.02 / 606.23 / 1317.82 | 0 · unavailable | 25.60 s / 809 | Inactive, 0/25 |
| USA, Display 1 | 375 · 8.35 / 91.72 / 533.85 / 534.01 | 769 · 16.84 / 91.76 / 108.42 / 158.48 | 25.60 s / 2299 | Inactive, 0/25 |
| USA, Display 2 | 1137 · 8.34 / 8.46 / 24.78 / 66.98 | 3050 · 8.34 / 8.41 / 8.63 / 41.72 | 25.62 s / 0 | Inactive, 0/25 |
| USA, Display, fixed refresh | 1159 · 8.34 / 8.43 / 16.74 / 58.42 | 2022 · 8.34 / 8.41 / 8.63 / 33.41 | 17.12 s / 0 | Fixed, 0/16 active |
| Japan Rev A, Display 1 | 359 · 9.07 / 91.77 / 108.34 / 116.85 | 1138 · 8.36 / 83.42 / 100.13 / 1893.50 | 25.63 s / 1916 | Inactive, 0/25 |

The fixed-refresh run used a temporary NVIDIA per-application profile for
a separately named copy of the same executable. NVAPI reported both active
VRR and display VRR mode off throughout the warm sample. The initial trial
reached the settled race too late for two guest swap samples. The recorded
retry used `--max-vis 3600` and hardware-composed independent flip. Focused
CTest checks overlapped part of that warm sample. The temporary profile was
removed after each trial.

The USA Display repeats held the same settings. The first ran almost
entirely in composed flip during the warm sample and had a displayed p99 of
108.42 ms. The second ran in hardware-composed independent flip with a
displayed p99 of 8.63 ms. Present-call p99 was 8.43 ms and 8.55 ms
respectively. The Japan Rev A Display run was also mostly composed flip.
A report of high submitted FPS on this system can coexist with a long wait
between visible frame changes.

Original 2 and Manual 60 2 also ran in composed flip throughout the warm
sample. Manual 60 2 yielded zero complete display intervals in the measured
window, so displayed percentiles are unavailable. Across the eight measured
runs the guest advanced at 60.00 VI/s and 29.91–30.00 framebuffer swaps/s during
the warm samples. The swaps are a measure of guest rendering progress.
Mixed-refresh monitor moves, a cleared driver-cache run, and Japan Rev A
Original and Manual timing remain untested.

## Desktop GPU/driver results

| GPU / driver | Observed symptom and comparison | Workaround evidence |
| --- | --- | --- |
| GeForce RTX 3080 / 630.20, Windows D3D12 | Slow displayed intervals and discarded presents accompanied composed flip in two Display runs. The USA repeat with independent flip had regular displayed intervals with the same game settings. | The runs identify a presentation-path correlation. A setting or driver change that reliably restores that path remains unverified. |
| Other desktop GPU and driver combinations | Untested in this record. | None measured. |

The [desktop triage table](debugging.md#desktop-graphics-and-driver-triage)
lists evidence to collect for a specific symptom.

## Follow-up ownership

- [#94](https://github.com/alondero/aerogauge-recomp/issues/94) tracks an
  optional render-FPS and guest-update diagnostic.
- [#80](https://github.com/alondero/aerogauge-recomp/issues/80) tracks
  conservative course culling.
- [#96](https://github.com/alondero/aerogauge-recomp/issues/96) tracks
  per-view HUD options.

For a repeatable graphics defect, link a separate report with the failing
cell, a same-scene control, and the expected and actual visual or timing
result.
