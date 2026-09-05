# Prior release format reference

The shape of every AeroGauge release. Use this to match voice and structure
when drafting the next release — readers compare releases by scanning these
sections in parallel, so keep the section order stable across versions.

For an existing release's exact body (to anchor a new draft), use:

```bash
gh release view <tag> --repo alondero/aerogauge-recomp --json body --jq '.body'
```

## Common header (every release)

Every release opens with this exact blockquote:

> **Pre-release quality.** Built from `main` for early testing and feedback.
> Expect rough edges — crashes, audio glitches, and missing native
> translations are still the rule, not the exception. Please open issues
> rather than running off the latest commit silently.

Don't paraphrase. The "Pre-release" voice is part of the project's
self-description (mirror of semver's "anything <1.0 is pre-release"); the
GitHub release's `prerelease` *flag* is `false` (see gotchas.md). Both
hold for every release.

The "## ⚠️ You must supply your own ROM" section is also stable across
releases — same three-step "extract → drop ROM → launch" instructions,
same save-path note. Copy verbatim, swapping the ROM filename and save
path for the current build.

## What's working — headline pattern

The first paragraph of "What's working" is the single most important
sentence in the release. The pattern is:

1. State the high-level capability (game is playable, widescreen works,
   save-state works).
2. Name THE one change that's the headline for this release.
3. Optionally quantify (X fixes, Y new knobs).
4. End with "There's still glitches so please report issues you find." —
   this line is in every release.

For follow-up releases that fix a small number of issues, frame the
release as a follow-up explicitly ("This is a small follow-up to the
v0.X.Y release that …").

## Closed since — category names

The "Closed since" list groups by theme, not by PR. Categories that have
actually shipped in AeroGauge releases so far:

- HUD widescreen
- Audio ucode
- Draw distance / full-track visibility
- Developer tooling
- Configuration / desktop integration

Categories that fit common project focus areas and can be used when
relevant:

- Stability / driver compatibility
- Save-state
- Frame pacing
- CI / build
- Project / developer docs
- Release packaging / CI

If a release has only one category, that's fine — don't pad. If it has
many, 3-5 is the typical range.

## Bullet format

Each bullet follows the same shape:

```
- **#<ISSUE> / PR #<PR>** — <prose>. <mechanism details>. <numbers>. <links>.
```

Lead with the user benefit, then the mechanism. Concrete numbers are much
better than adjectives ("improved performance"). Inline code for env vars
(`AERO_WARP`, `AERO_HEADLESS`, `AERO_DL_SKIP_DL`, `AERO_STATE_*`,
`AERO_HARNESS_LOG`); experienced users know.

Cite issues/PRs as `**#N / PR #N**` — GitHub auto-links both numbers.

## What's not done yet

The opening paragraph is fixed:

> This is still pre-1.0 — `force_stub.txt` still includes some game
> primitives as no-ops …

Below that, optional follow-up paragraph(s) name specific known gaps with
issue numbers. These tell users where to report if they hit the edge
case. Update these as the tag nears — they should reflect the *actual*
current state, not historical context.

For the first release (v0.1.0), the "What's not done yet" section also
named the disabled Lamborghini carry-overs (`TODO(aerogauge)` markers in
`src/`) so reviewers could see what's still pending AeroGauge address
derivation.

## How to run

Linux: unzip, copy ROM, run. Windows: extract, drop ROM, double-click.
Don't rewrite — copy verbatim (with `aerogauge_modern(.exe)` and the
`aerogauge-recomp-{linux,windows}-x64.zip` filenames).

AeroGauge-specific lines that belong in "How to run" (Windows section):

- **F11 / Alt+Enter** fullscreen toggle.
- **F1–F6** developer warp menu (`src/aero_warp.c`).
- **F7 / F8** save-state hotkeys (`AERO_STATE_*`).

Users discover these immediately after extracting the binary, so they
belong in "How to run" rather than buried in "What's working".

## Build provenance

```
- **Commit:** `<SHORT_SHA>` (main, <YYYY-MM-DD>)
- **Workflow run:** [#<RUN_ID>][run] (Build & Release, dispatch from this SHA)
- **Toolchain:** Linux build on `ubuntu-latest` with gcc / cmake / ninja /
  `libsdl2-dev` / `libvulkan-dev`. Windows build on `windows-latest` with
  MinGW-w64 GCC + Ninja (PowerShell, not MSYS bash) per the project's toolchain
  notes.
```

The toolchain note is a permanent reminder about the MSYS bash / PowerShell
quirk documented in `CLAUDE.local.md` — keep it verbatim.

## Closing

Always end with:

---

If you'd rather build it yourself from source instead of running a pre-built
binary, see [BUILDING.md]. For graphics settings and the in-game menu, see
[README.md].

[BUILDING.md]: https://github.com/alondero/aerogauge-recomp/blob/<VERSION>/BUILDING.md
[README.md]: https://github.com/alondero/aerogauge-recomp/blob/<VERSION>/README.md
[run]: https://github.com/alondero/aerogauge-recomp/actions/runs/<RUN_ID>

With any additional `[docs/foo.md]:` link references for documents cited in
the body.
