# Prior release format reference

The shape of Lambo releases (v0.1.0 / v0.3.0 / v0.4.0 / v0.4.1 / v0.4.2 /
…), side-by-side. Use this to match voice and structure when drafting the
next AeroGauge release — readers compare releases by scanning these sections
in parallel.

## The inaugural-release caveat

**AeroGauge has not yet cut a release.** This skill was forked from the
Lambo one at a point where Lambo has nine releases of convention baked in
(v0.1.0 through v0.6.2). The inaugural AeroGauge release (planned v0.1.0)
sets the precedent for everything that follows.

What that means in practice:

- **Follow Lambo's section order verbatim** (Pre-release quality → ROM →
  What's working → What's not done yet → How to run → Build provenance →
  closing links). It's much easier to keep the convention stable across
  forks than to retrain it later.
- **The "Closed since" section can be omitted for v0.1.0** — there is no
  prior release to diff against. Replace it with a one-paragraph "What
  shipped since the repo went public" summary drawn from the merged PRs on
  `main` since the first commit.
- **"What's not done yet" should name specific gaps** — the headline
  limitations at v0.1.0 are: any remaining `force_stub.txt` entries
  (cross-check the file before drafting), audio ucode coverage if it isn't
  fully live yet, and any tracked issue with a known-gap mitigation.
- **Category names from Lambo can be reused**; add new ones (e.g.
  "HUD widescreen", "Save-state", "Audio ucode") as they accrue. The
  inaugural release often only needs one category — don't pad.

For v0.1.0 specifically, the closest stylistic analogue is Lambo v0.1.0
itself: "the whole game recompiles, runs at native resolution, and the
headline feature works". Save the multi-category, multi-bullet release for
v0.2.0+ when there are PRs to enumerate.

## Common header (every release)

Every prior Lambo release opens with this exact blockquote:

> **Pre-release quality.** Built from `main` for early testing and feedback.
> Expect rough edges — crashes, audio glitches, and missing native
> translations are still the rule, not the exception. Please open issues
> rather than running off the latest commit silently.

Don't paraphrase. The word "Pre-release" is the project's voice; rewriting
it dilutes the convention.

The "## ⚠️ You must supply your own ROM" section is also identical across
releases — same three-step "extract → drop ROM → launch" instructions,
same save-path note. The wording is stable; copy it verbatim, swapping
the ROM filename + save path. AeroGauge adds a third line about
`graphics.json` because the in-game config menu writes there — see
`references/release-notes-template.md`.

## What's working — headline pattern

The first paragraph of "What's working" is the single most important
sentence in the release. The pattern is:

1. State the high-level capability (game is playable, widescreen works,
   rumble works).
2. Name THE one change that's the headline for this release.
3. Optionally quantify (X fixes, Y new knobs).
4. End with "There's still glitches so please report issues you find." —
   this line is in every prior release.

Examples from real Lambo releases:

- **v0.4.0**: "The headline of this release is **3- and 4-player
  split-screen**: the 2×2 quadrant views used to be pillarboxed with dark
  side bars, and now each quarter fills its slice of a wide monitor —
  with the per-quadrant HUD text, minimap, fog, and sky all tracking the
  wide frame."
- **v0.4.1**: "The headline of this release is **N64 Rumble Pak support**:
  the ROM's per-frame PWM rumble engine now drives SDL pad rumble at the
  moments ares rumbles (with the Controller Pak save flow intact in the
  same session). Two follow-up fixes round out this release — ..."
- **v0.4.2**: "This is a small follow-up to the v0.4.1 Intel driver
  advisory (#110): where v0.4.1 shipped a manual escape hatch that
  required editing `graphics.json`, **v0.4.2 makes the fix automatic for
  modern Intel** — affected users no longer need to take any action."

Notice the v0.4.2 example explicitly frames itself as a follow-up — that's
the right move when a release has a single small change. For AeroGauge
v0.1.0, frame it as the headline capability instead ("the whole-ROM
recompile runs races at native resolution with widescreen + display-rate
interpolation").

## Closed since — category names

The "Closed since" list groups by theme, not by PR. Categories that Lambo
has actually used:

| Category | Used in |
|----------|---------|
| Widescreen graphics | v0.3.0 |
| Widescreen — 3P/4P split screen | v0.4.0 |
| Widescreen — skybox | v0.4.0 |
| Rumble | v0.4.1 |
| Stability | v0.4.0 |
| Stability / driver compatibility | v0.4.2 |
| Widescreen / multiplayer | v0.4.1, v0.4.2 |
| Developer tooling | v0.4.0, v0.4.1 |
| Developer tooling (now ships in the binary) | v0.3.0 |
| CI / build | v0.3.0 |
| Project / developer docs | v0.4.0 |
| Release packaging / CI | v0.4.0 |
| Texture tooling (developer-facing) | v0.3.0 |

A single-category release (like Lambo v0.4.2) is fine — don't pad. A
multi-PR release (like Lambo v0.4.0) usually gets 3-5 categories.

For AeroGauge, candidates that fit the current work in `main` include:

- **HUD widescreen** — dial-ring + needle matrix-shift + countdown-window
  gate (issue #1, PRs #8 / #12 / phase-2 #16)
- **Draw distance** — far plane + full-track (PR #10)
- **Save-state** — F7/F8 + AERO_STATE_* (issue #17, PRs #18 / #19)
- **Audio ucode** — aspMain crash fix (PR #11 + patch 0013)
- **Developer tooling** — warp menu (F1–F6 / `AERO_WARP`),
  `AERO_HEADLESS`, `AERO_HARNESS_LOG`, `AERO_FRAME_LOG`
- **CI / build** — skill refactor (PR #16), DPI manifest, etc.

For v0.1.0, only use whichever of these are actually merged and
user-visible at the tag point. The list will grow over time.

## Bullet format

Each bullet follows the same shape:

```
- **#<ISSUE> / PR #<PR>** — <prose>. <mechanism details>. <numbers>. <links>.
```

Real examples (Lambo):

- **v0.4.1**: "- **#101 / PR #108** — N64 Rumble Pak now rumbles the SDL
  controller in a real race. The ROM issues raw SI motor frames through a
  custom start/stop pair (audit in #102 / PR #107 — the ROM carries a
  complete per-frame PWM engine with per-channel intensity, every link in
  the chain is emitted and reachable). The port now forces the
  rumble-present flag and runs native `osMotorStart` / `osMotorStop`
  stubs..."

- **v0.4.2**: "- **#109 follow-up / PR #113** — Modern Intel GPUs (Iris Xe
  / Arc) now stay on D3D12 *automatically* — no `graphics.json` edit
  needed. v0.4.1's patch 0010 only worked when the user manually set
  `"api_option": "D3D12"`; two more users hit the silent black-screen
  wall before finding the workaround, so this makes it the default. The
  Intel force-Vulkan fallback in RT64 was originally written for 6th-gen
  HD Graphics (which device-*remove* on D3D12), but it was wrongly
  catching Gen12 Xe / Arc..."

Lead with the user benefit, then the mechanism. Numbers like "79 motor-start
/ 2395 motor-stop events" or "5 vs 3-4 segment groups" are great — they
make the change concrete. Inline code for env vars (`LAMBO_PAK_TRACE`,
`LAMBO_NO_LOD`); for AeroGauge, the equivalents are `AERO_WARP`,
`AERO_HEADLESS`, `AERO_DL_SKIP_DL`, `AERO_STATE_*`, `AERO_HARNESS_LOG`.

## What's not done yet

The opening paragraph is fixed:

> This is still pre-1.0 — `force_stub.txt` still includes some game
> primitives as no-ops …

Below that, optional follow-up paragraph(s) name specific known gaps with
issue numbers. v0.4.2 has one — about explicit-Vulkan-on-old-Iris-Xe
runtime device-loss (tracked in #114). These are good to include because
they tell users where to report if they hit the edge case.

For AeroGauge v0.1.0, the paragraphs after the opening should reflect the
*actual* current state of `force_stub.txt` and the open issues at the
v0.1.0 tag point. Update these as the tag nears.

## How to run

The "How to run" section is near-identical across every Lambo release.
Linux: unzip, copy ROM, run. Windows: extract, drop ROM, double-click.
Don't rewrite — copy verbatim (with AeroGauge's `aerogauge_modern(.exe)`
+ zip names).

AeroGauge adds three lines to the Windows section:

- **F11 / Alt+Enter** fullscreen toggle (same as Lambo).
- **F1–F6** developer warp menu (`src/aero_warp.c`).
- **F7 / F8** save-state hotkeys (`AERO_STATE_*`).

These belong in the "How to run" section because users discover them
immediately after extracting the binary.

## Build provenance

```
- **Commit:** `<SHORT_SHA>` (main, <YYYY-MM-DD>)
- **Workflow run:** [#<RUN_ID>][run] (Build & Release, dispatch from this SHA)
- **Toolchain:** Linux build on `ubuntu-latest` with gcc / cmake / ninja /
  `libsdl2-dev` / `libvulkan-dev`. Windows build on `windows-latest` with
  MinGW-w64 GCC + Ninja (PowerShell, not MSYS bash) per the project's toolchain
  notes.
```

The toolchain note is verbatim across releases — it's a permanent reminder
about the MSYS bash / PowerShell quirk documented in `CLAUDE.md` /
`CLAUDE.local.md`.

## Closing

Always end with:

---

If you'd rather build it yourself from source instead of running a pre-built
binary, see [BUILDING.md]. For graphics settings, see [README.md].

[BUILDING.md]: https://github.com/alondero/aerogauge-recomp/blob/<VERSION>/BUILDING.md
[README.md]: https://github.com/alondero/aerogauge-recomp/blob/<VERSION>/README.md
[run]: https://github.com/alondero/aerogauge-recomp/actions/runs/<RUN_ID>

With any additional `[docs/foo.md]:` link references for documents cited in
the body (e.g. `[docs/rom-map.md]:`).

## Reference: prior Lambo release SHAs (for diffing/quoting)

If you ever need to pull a release body from the Lambo repo to anchor your
draft:

- **v0.6.2** — `8fa0b23…` (Aug 27)
- **v0.6.1** — `1fc671c…` (Aug 25)
- **v0.6.0** — `004a6e2…` (Aug 24)
- **v0.5.0** — `2446cb3…` (Jul 24)
- **v0.4.2** — `03e533a1425bf8aa895c67affbb78b22986f411c` — workflow #29203966918
- **v0.4.1** — `1f489fbcc3e6db1c82cd3fcbea27ebaf6ce3c866` — workflow #29198294316
- **v0.4.0** — `5e07da1759fb8aa484a87b0ee210303d3921950b` — workflow #29082039804
- **v0.3.0** — `9bef7dcd7df8aef0c2e5d5356f25ec62255611e2` — workflow #28996890290
- **v0.1.0** — `6f3e7d914ce7b2b2424faec8d4acd56cbd785059` — workflow #28778465982

The v0.1.0 body is the closest stylistic match for AeroGauge's inaugural
release — read it for the "first release" tone.

`gh release view <tag> --repo alondero/automobililamborghini-recomp --json body`
returns the raw markdown.
