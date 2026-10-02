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
