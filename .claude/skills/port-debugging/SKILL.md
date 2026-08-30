---
name: port-debugging
description: Live-debug the native port (build/aerogauge_modern) — gdb recipes, the AERO_* probe/env-var catalogue, headless vs windowed runs, menu navigation via input pulses, screenshot capture, and token-efficient log analysis. Use when diagnosing why the port misbehaves, verifying a change in the running game, or reading any large log.
---

# Debugging the running port

## Running the port

- **Headless smoke**: `AERO_HEADLESS=1` — boots, counts VIs, self-exits. No
  swapchain, so anything needing a window (HUD aspect scaling!) is a no-op.
- **Windowed 60s race**: `AERO_WARP=1 AERO_MODERN_MAX_VIS=3600` (warp straight
  into a race, self-exit after 3600 VIs). Launch via `Start-Process` for a real
  console. Warp: `AERO_WARP=track[:craft]` (1-based), `AERO_WARP_AT=vi:track[:craft]`,
  or F1–F6 live.
- **Menu navigation without a human** — input pulses `AERO_INPUT_PULSE=start:period:count:gap`:
  `8000:150:4:300` (A taps) walks title→GP→machine→track→RACE (HUD by VI ~5000);
  `1000:150:4:300` (START) parks on MACHINE SELECT.

## AERO_* probe catalogue (all permanent, env-gated, zero-cost unset)

| Var | What |
|---|---|
| `AERO_HARNESS_LOG=1` | re-enable periodic diagnostics (rt64 send_dl heartbeat, VI fb-swap) |
| `AERO_FRAME_LOG=<path>` | frame-pacing anomalies: present gaps >25ms, slow send_dl bodies; VI-tick gaps to `<path>.vi` |
| `AERO_AUDIO_RMS=1` | per-second output RMS — the headless "is music sounding" gate (jingle t=1–3s, title from t≈6s, race RMS 5–7k) |
| `AERO_DL_GEOMSET=<stride>` | per-frame G_DL target sets (diff runs to prove geometry-identical) |
| `AERO_DL_DUMP_AT=<count>` | race DL dump at a send_dl count |
| `AERO_DL_SKIP_DL=<hex,hex,...>` | at send_dl time, rewrite `G_DL` commands whose target is in the list to `G_SPNOOP`; the decisive "is this DL the cause?" tool for track-artefact diagnosis (skill: track-artefact-diagnosis, Phase 3). KSEG0 target form, one or more comma-separated hex addresses |
| `AERO_WS_TRACE=1|2` | HUD widescreen gate/per-rect tracing; `AERO_WS_RETAG=0` kill-switch |
| `AERO_DRAW_DISTANCE_SCALE`, `AERO_FULL_TRACK` | enhancement overrides (see graphics.json) |
| `AERO_FT_SECTIONS=0` / `AERO_FT_OBJECTS=0` / `AERO_FT_ZONE_MASK=<hex>` / `AERO_FT_TRACE=1` | full-track bisection: force the windowed path per registrar, restrict full-track to a zone bitmask, trace course builds — the knobs that attributed the Bikini zone-21 occluder |

Config file: `%LOCALAPPDATA%\AeroGaugeRecomp\graphics.json`. **Never add a
periodic fprintf on the gfx or VI thread** — a Windows console write is 10–77ms
and caused a 1Hz gameplay hitch; event-driven one-time logs only.

## Native gdb on the port

`gdb -batch -x <script>.gdb build/aerogauge_modern` — local harnesses live at
`.claude/*.gdb` (gitignored; `hud-watch.gdb` / `hud-attribution.gdb` patterns).

- **Arm rdram watchpoints only after rdram is allocated**: set them inside a
  first-breakpoint `commands` block (e.g. at `create_render_context`,
  stub_renderer.cpp) — `g_aero_rdram` is null before boot. Use
  `watch -l *(int*)(g_aero_rdram + phys)` (fires on value change).
- **Never put a gdb `condition` on a hardware watchpoint** (wedges the
  inferior) — filter inside the command block.
- **Count hits with `ignore N <big>` + `info breakpoints`**, not printf loops.
- Read game state INSIDE the hook/breakpoint — offline RDRAM dumps race the
  next frame's rebuild (matrices read as zeros).

## Screenshots (windowed verification)

PowerShell: get the process `MainWindowHandle` + `PrintWindow` flag 2 (grabs
the D3D12 "AeroGauge" window; `FindWindow($null,...)` from PowerShell marshals
$null as "" and fails). Capture recipe in docs/notes/hud-widescreen.md.

## Log analysis — the 20-line rule

NEVER read more than ~20 raw lines of a port log into context. Summarize first:

```bash
python tools/trace_analyst.py <log> [extra_regex]
```

Read the `FIRST CRASH/ERROR` block + frequency table, then Read only the
pinpointed 5–10-line window. Repetitive RSP/stub output is the main context
killer — always summarize regardless of size.

## Convergence discipline

Verify against ares (ares-debugger skill), not screenshots or vibes: same
global, same event order, same value. When a diagnosis is "solved", name the
one-line root cause and the probe that proves it, and check
`docs/notes/rom-map.md` still agrees.
