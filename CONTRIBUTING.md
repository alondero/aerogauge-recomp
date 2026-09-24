# Contributing

The pull request is the review and approval point for a change. The author
should describe the evidence and the trade-offs. The maintainer decides
whether the change is safe and whether a dependency or architecture boundary
should move.

AI tools may help with drafting or analysis. They do not replace source
reading, tests, measurements, or pull-request review.

Start with the [documentation map](docs/README.md), then read
[BUILDING.md](BUILDING.md), [Architecture](docs/architecture.md), and
[Testing](docs/testing.md). Read the relevant source and reference page before
changing a subsystem.

## Before changing code

1. Check the branch, dependency pins, and supported ROM identity.
2. Read the owning source, tests, and build path.
3. Reproduce the behavior when possible. If it cannot be reproduced, say so.
4. Decide whether the change belongs in generated input, hand-written port
   code, a dependency patch, a test, or stable documentation.
5. Open a regular issue when the question is still uncertain. Domain
   knowledge belongs in the owning source comment, focused test, or domain
   page (controllers, audio, renderer, full-course geometry, etc.); an
   issue only preserves the question while it is being worked out and is
   not a substitute for those once the answer is known.

## File ownership

| Path | Owner | Normal change |
| --- | --- | --- |
| src/ | Port maintainers | Hand-written host code and narrow game hooks |
| tests/ | Port maintainers | Host, ROM-backed, and end-to-end regression tests |
| docs/ | Port maintainers | Stable facts and user or developer guidance |
| patches/ | Port maintainers | Diffs against pinned dependency commits |
| scripts/gen_syms_toml.py | Port maintainers and ROM evidence | Generator for the symbol and hook TOML |
| scripts/japan_rev_a.py | Port maintainers and ROM evidence | Verified Japanese SDK mappings and hook instructions |
| aerogauge.syms.toml | Generated output | Regenerate and review; do not edit |
| aerogauge.us.toml | Generated output | Regenerate and review; do not edit |
| aerogauge.jp.syms.toml and aerogauge.jp.toml | Generated output | Regenerate with --region jp and review; do not edit |
| aspMain.us.toml | Port maintainers and ROM evidence | Reviewed RSP input |
| force_stub.txt and force_stub.jp.txt | Port maintainers | Deliberate translation fallbacks for USA and Japan Rev A respectively |

## Generated files

| Path | Generator | Rule |
| --- | --- | --- |
| RecompiledFuncs/ | N64Recomp | Never edit; regenerate from the ROM |
| RecompiledFuncsJP/ | N64Recomp | Never edit; regenerate from Japan Rev A |
| src/aspMain.cpp | RSPRecomp | Never edit; regenerate from the ROM |
| build/ | CMake and Ninja | Never commit |

If generated code is wrong, check the ROM identity, symbol input, stub list,
hook address, and recompiler input. Change the input, regenerate, and review
the generated diff. Do not repair generated C or C++ by hand.

## From ROM evidence to a change

Use this path:

~~~text
ROM or runtime observation
    -> named function, table, or invariant
    -> generator input or narrow hook
    -> focused test or capture
    -> stable reference or subsystem documentation
~~~

An address alone is not an interface. Include the ROM identity, byte order,
units, owner, thread, frame or scene boundary, and failure behavior. State
what would disprove the interpretation.

If the observation is not confirmed, keep it in the issue and label it as a
hypothesis. Do not turn it into a confident permanent comment.

## Guest-memory bridges

Some current features write directly to guest RDRAM. Full-course geometry
builds synthetic display lists. The widescreen HUD rewrites display-list
commands and a matrix. Save-state loading restores guest memory and relinks
some native thread references.

These are fragile transitional bridges. They depend on this ROM's address
layout, N64 byte order, bounds, thread timing, and renderer behavior. They are
not the desired architecture for new features.

Every new guest-memory access must document:

- the address, width, units, and byte order;
- the reading or writing thread;
- the owner and lifetime of the data;
- bounds and failure behavior;
- a test or capture that could expose a bad assumption; and
- the source-level or decompilation step that could remove the bridge.

The intended direction is decompiled or readable game code, stable symbols,
explicit source patches, and a versioned code-mod or asset interface. Do not
add a general guest-memory API to make one hook convenient.

## Dependency patches

Read the [patch inventory](patches/README.md) before editing a dependency.
The supported build scripts reset tracked submodule files and apply patches
from the pinned commits.

1. Start at the pinned dependency commit.
2. Apply the existing patches in the documented order.
3. Make the smallest dependency change and add a focused test when possible.
4. Export the change as a numbered patch.
5. Compare it with the dependency's current source.
6. Update both build paths, the patch inventory, and the relevant test or
   reference page in one pull request.

Decide whether the change is game-specific, a temporary compatibility fix, or
a candidate for the dependency's upstream project. A general upstream
proposal belongs in that project's issue or pull request. The local patch
inventory records only the build contract.

If a patch no longer applies, stop and investigate dependency drift. Do not
reset unrelated submodule work or silently refresh the patch.

## Renderer changes

RT64 owns normal display-list interpretation, presentation, aspect handling,
interpolation, extended GBI commands, and texture replacement. The port owns
the AeroGauge ROM integration, game-specific display-list behavior, and SDL
or runtime wiring.

Before changing RT64:

1. read the [renderer reference](docs/reference/renderer.md) and
   [patch inventory](patches/README.md);
2. reduce the failure to the smallest display-list, matrix, viewport, or
   toolchain case;
3. decide whether it is an AeroGauge rule or a general renderer behavior;
4. keep a game-specific fix in the port; and
5. prepare an upstream issue or patch when the behavior is general.

The headless software renderer is a test instrument. It is not the normal
player renderer.

## Tests, comments, and docs

Add the smallest regression test that can fail for the bug:

- host test for pure math or classification;
- synthetic RDRAM test for a guest-memory contract;
- ROM-backed test for a translated function or display-list boundary;
- bounded end-to-end test for renderer, audio, or device behavior; or
- a scripted or manual acceptance check for a player workflow.

Run the relevant checks and record skipped checks with their reason in the
pull request. A screenshot is evidence, not a repeatable test.

Comments should explain purpose, ownership, invariants, address and endian
assumptions, failure behavior, and why a workaround exists. Keep research
chronology in the issue or pull request. Move only the lasting rule into
source or documentation.

When documentation changes, run:

~~~text
python -B scripts/check_docs.py
git diff --check
~~~

## Pull requests

Use the repository pull-request template. State:

- what changed and why;
- which seam and owner are affected;
- the commit, ROM identity, OS, and renderer/backend used;
- expected and actual behavior;
- commands run and checks skipped;
- generated files and dependency patches;
- documentation impact and known limits; and
- the next useful step.

Do not include private session links, machine-specific paths, ROM data,
unexplained issue numbers, or claims about a run that did not happen.
