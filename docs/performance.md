# Lower-end hardware performance audit

The October 2026 audit starts from abbaf4d and the pinned runtime, RT64 and
frontend dependencies. The ordering weighs expected benefit, direct source
evidence, implementation risk and how readily a change can be validated.
It is a prioritised source audit, not a claim that a profiler measured every
entry. GPU-bound and CPU-bound systems can need different priorities.

| Rank | Opportunity | Important work and expected benefit | Status |
| --- | --- | --- | --- |
| 1 | Reduce rendering cost with a Low power preset | Stage native resolution, no MSAA, original presentation rate and standard colour precision; preserve Apply/Discard and unrelated settings. Reduces pixel work, interpolation and framebuffer storage. | Implemented; Graphics > Rendering preset |
| 2 | Avoid redundant host input work | Limit the SDL bridge to 250 Hz instead of repeating profile lookup, event handling, controller maintenance and rumble every millisecond. Bound polling delay and avoid catch-up bursts. | Implemented; input_cadence regression |
| 3 | Bypass unchanged HUD processing | At effective 4:3, skip matrix scanning and the retag pass's scratch copy, rewrite and inserted commands. Re-evaluate live aspect each frame. | Implemented; hud_messages regression |
| 4 | Cull full-course geometry conservatively | Use verified visibility or conservative bounds between the original three zones and all-course submission; preserve scenery and transparent order in both regions and split screen. | [Issue #80](https://github.com/alondero/aerogauge-recomp/issues/80) |
| 5 | Remove idle millisecond wakeups | Replace runtime main/graphics loop timeouts with event/deadline waits while preserving input, shutdown, VI and audio completion. Port sampling limits do not remove runtime wakeups. | [Issue #81](https://github.com/alondero/aerogauge-recomp/issues/81) |
| 6 | Budget renderer workers on low-core CPUs | Measure concurrent shader, ubershader and texture workers competing with game/audio threads; choose counts from evidence about overlapping work. | [Issue #82](https://github.com/alondero/aerogauge-recomp/issues/82) |
| 7 | Reuse compiled shaders and pipelines | Measure cold/warm driver caching first; persist remaining expensive compilation with correct backend/device/version/option invalidation. Target startup and first-lap stutter. | [Issue #83](https://github.com/alondero/aerogauge-recomp/issues/83) |
| 8 | Replace proven translated SDK hotspots | Profile first, then verify regional SDK math/memory identities and guest ABI. Native replacements need differential checks for endian, overlap and numerical behavior. | [Issue #84](https://github.com/alondero/aerogauge-recomp/issues/84) |
| 9 | Tune generated-code builds with PGO/selective LTO | Train on representative tracks and measure CPU/cache/code-size gains. Preserve CPU baselines, precise math and distinct patchable code-mod entry addresses. | [Issue #85](https://github.com/alondero/aerogauge-recomp/issues/85) |
| 10 | Bound replacement texture memory | RT64's minimum 512 MiB replacement pool and dedicated-VRAM estimate deserve testing on shared-memory GPUs. Balance peak memory against eviction/upload churn; affects optional packs. | [Issue #86](https://github.com/alondero/aerogauge-recomp/issues/86) |

## What the first changes establish

The cadence test feeds 1,000 callbacks spaced 1 ms apart and verifies exactly
250 event/input work opportunities, including immediate startup and a fresh
deadline after a long stall. This establishes a 75% reduction in those work
opportunities under that schedule, not a 75% reduction in total process CPU.
The runtime still wakes at its original rate. Host polling may add up to
4 ms before the next runtime callback; guest and VI rates do not change.

The preset uses existing RT64 settings. At the default 900-pixel window height,
Auto chooses a 4x scale over the 240-line reference; native resolution has
approximately one sixteenth of those internal pixels at the same aspect.
Disabling MSAA and display-rate interpolation further reduces rendering work.
These are workload reductions, not measured frame-rate or power improvements;
full-course submission, shader compilation and other fixed costs still apply.
Original presentation follows detected guest rate, normally 30 Hz in a race.

The HUD regression executes the actual frame-end hook against synthetic guest
memory. It verifies byte-for-byte unchanged 4:3 commands and needle matrix,
then changes the effective aspect and checks that pinning resumes. Existing
central-message and overflow checks continue to exercise the widescreen path.

## Windowed measurements

The checked-in Windows harness ran the baseline abbaf4d Release binary and
the updated build on an AMD Ryzen 9 3900X with an NVIDIA RTX 3080, GCC 15.2.0
and D3D12. Each scenario ran twice with a separate portable profile, the USA
ROM (89ea0690f3e22201), a Canyon Rush warp, 30 seconds of warm-up and a
5-second sample. The harness required the live race before and throughout
sampling. All six runs rendered and reached their 2,700-VI exit normally.

| Scenario | Total process CPU, runs 1 / 2 (ms) | Main-thread CPU, runs 1 / 2 (ms) | Mean endpoint working set (MiB) |
| --- | --- | --- | --- |
| Baseline, default rendering | 6671.9 / 6343.8 | 218.8 / 296.9 | 328.6 |
| Updated, default rendering | 6359.4 / 6828.1 | 187.5 / 125.0 | 327.0 |
| Updated, Low power settings | 1062.5 / 1281.2 | 46.9 / 109.4 | 325.2 |

CPU time sums time on all cores, so it can exceed elapsed time. The samples
were approximately 5.02 to 5.06 seconds. With unchanged rendering settings,
mean main-thread CPU fell about 39%, while mean total process CPU rose about
1.3%; these short runs establish no overall CPU improvement at default quality.
With Low power selected, mean total process CPU was about 82% below baseline.
Memory remained similar. There is visible run-to-run variation, and two runs
on one high-end desktop do not establish a gain on integrated/mobile graphics
or every track. GPU time, FPS, power and physical input latency were not measured.

The rendering preset trades visual quality and interpolated presentation for
lower workload. Full-course geometry stayed enabled and draw distance remained
100 in all scenarios, so those enhancements did not cause the difference.

## Reproducing and extending the measurements

Build Release and run the focused checks:

~~~text
cmake --build build --target aerogauge_modern test_frontend_settings test_hud_messages test_input_cadence test_input_scaling test_live_config
ctest --test-dir build -R "frontend_settings|hud_messages|input_cadence|input_stick_scaling|live_config_updates" --output-on-failure
~~~

On Windows, retain a baseline Release executable and its required DLLs and
assets, then run the same live-race measurement:

~~~powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tests/measure_performance.ps1 -BaselineExe '<baseline-release-exe>' -Repeats 2
~~~

`tests/measure_performance.ps1` writes JSON results, settings and stderr logs
under the ignored build/performance directory. Increase WarmupSeconds if the
race is not ready; the harness rejects such a sample. It reads the USA ROM
from RepoRoot and runs with isolated portable profiles to protect player saves.

Use isolated settings and saves, a fixed race/input scenario and warm-up
interval, and repeat before/after runs on the same machine. Record hardware,
OS, backend, compiler, ROM region/hash and dependency pins. Separate CPU time,
GPU time, peak memory, input latency and p95/p99 frame times; an average alone
can hide stutter. Headless runs exercise translated logic and audio but cannot
establish GPU gains or visual correctness on D3D12/Vulkan.

All seven follow-up issues include concrete source evidence, ownership,
acceptance cases and the measurements needed before claiming a gain. Runtime
and renderer changes require reproducible dependency patches and an upstream
comparison. Generated C/C++ and ROM data must remain outside commits.

## Shared performance and Android backports (November 2026)

Compared with Automobili Lamborghini: Recompiled through `0b13e40`, the ports
pin the same RT64 (`f0728a2`), Plume (`51b1ad4`), N64ModernRuntime (`cdf5abb`)
and RecompFrontend (`b1a1477`) revisions. The applicable changes are now build
inputs in [the patch manifest](../patches/series.json); the source-project
commit numbers below identify the reviewed implementation, not AeroGauge
measurements.

| Source change | AeroGauge implementation and boundary |
| --- | --- |
| `64481d4`, `b3aed7f`: frame matching | Patches 0060 and 0061 reuse sorted vectors and join call keys instead of repeatedly expanding equal-hash call pairs. Original matching order and the resulting pair sets are checked against the original container algorithm, including duplicate hashes and steady-state allocations. Applies to desktop and Android. |
| `725bf97`: stalled queue producers | Patch 0046 waits on condition variables instead of spinning while a GPU consumer holds the barrier; shutdown wakes both producer waits. Applies to desktop and Android. |
| `64481d4`: Android automatic resolution | Patches 0042 and 0059 bound Auto by an 80 million pixel/second budget and retain the narrow Adreno 615 system-driver 2x workaround. Manual and Original settings and desktop Auto retain their behavior. Tests cover aspect ratios and refresh rates; the budget is a starting policy inherited from the other port, not a measured AeroGauge optimum. |
| `96676ff`: frontend and audio lock scopes | SDL event collection precedes the frontend lock; batch dispatch, profile updates, sampling and rumble remain inside it. Patch 0032 adds the batch dispatcher while preserving the existing no-argument API. Audio opens/closes outside the device-state mutex and resamples under its own mutex; generation checks discard output prepared before a frequency, device or suspension change. |
| Android Vulkan fixes through `f3b751d` | Surface capabilities, native-window generations, retryable acquisition, resource retirement, descriptor-pool retry, bounded clears and mapped-memory cache maintenance are portable renderer fixes; see [Android](android.md) and [the patch inventory](../patches/README.md). |
| `51187bf`: controller-aware touch visibility | Retains AeroGauge's Auto/Always/Hidden policy and analog overlay. Device-change callbacks use SDL's joystick acceptance rule, replacing repeated device enumeration during drawing. Surface destruction releases held touch input. |

The settings refresh optimization in `8d208f2` is unnecessary here: AeroGauge
refreshes its settings on opening or explicit actions, rather than walking the
schema every visible frame. It already debounces settings persistence.
Lamborghini's rain, fog, replay, harness and vehicle changes depend on systems
or guest functions absent here. Its asynchronous save-state writer is not
ported: AeroGauge's developer save-state path has its own guest-memory and
renderer-safety limitations. Its Pak batching cannot be copied without
changing AeroGauge's guarantee that a successful guest write is already on
disk; retain that failure/acknowledgement contract until a separately tested
storage design replaces it. Background crash-symbol loading and renderer cost
telemetry remain separate opportunities; neither is required by these patches.

The fixed-descriptor-table and shader non-uniform fallback patches are also
excluded. The latter relies on Lamborghini's verified texture-LOD behavior;
removing SPIR-V capabilities without equivalent AeroGauge evidence would be
unsafe. The Vulkan readback backport extends a diagnostic interface absent
here and is not needed by normal presentation.

Verification commands and device acceptance limits are in
[Testing](testing.md#shared-renderer-and-platform-regressions). These changes
have host regression coverage; no AeroGauge before/after FPS, power or Android
frame-time improvement is claimed. Measure a fixed live race on the same
device before tuning the budget or changing defaults further.

Validation of this backport on Windows x64, GCC 15.2 Release: the game and
focused audio/frontend targets built; all 63 renderer/platform fixture tests,
the audio buffering/gain/locking tests, and both regional settings-schema tests
passed. The nine dependency-free host checks passed. A four-second USA headless
run reached VI retraces, generated non-silent PCM and joined all guest threads
on coordinated exit. The unchanged USA translation was reused for this host
build; no generated source was edited. Android ARM64 built with NDK
28.2.13676358, packaged as a debug APK, and passed `lintDebug`. No physical
Android run, desktop GPU
visual check or before/after frame-time measurement was performed.
