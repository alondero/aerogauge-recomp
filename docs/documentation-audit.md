# AeroGauge documentation audit

**Audit date:** 2026-09-14

**Repository state:** `f010792` (`v0.2.1`), with the current GitHub issues and pull requests also reviewed
**Scope:** user documentation, contributor/AI guidance, source comments, tools and tests, GitHub issues and pull requests, and documentation practices in Banjo-Kazooie decompilation, Ship of Harkinian/Shipwright, and Spaghetti Kart.

> This is the audit snapshot that started the documentation overhaul. Some
> findings and recommendations below are now addressed. Use [the documentation
> map](index.md) and its linked reference pages for the current contract; use
> this file to understand the evidence and the reasons for the structure.

## Executive summary

AeroGauge does not have a knowledge shortage. It has an information-architecture problem.

The repository contains unusually good reverse-engineering evidence: ROM addresses, call paths, display-list observations, failure modes, validation commands, and comments explaining why the port does not simply copy a neighbouring project. The strongest examples are `docs/notes/rom-map.md`, `docs/notes/hud-widescreen.md`, `docs/notes/controller-accessories.md`, the module headers in `src/aero_full_track.cpp` and `src/aero_audio.cpp`, and the structured bodies of recent pull requests.

That material is difficult for a new developer to consume because it is spread across four overlapping systems:

1. The README is a useful player/building guide, but it is also the only public navigation page.
2. `CLAUDE.md` is excellent AI priming and maintainer context, but it is not a complete human contributor guide and contains local-machine assumptions.
3. The notes are mostly chronological investigation ledgers rather than stable references.
4. Source comments and PR bodies often contain the real design rationale, but their references are not canonical or durable.

The central recommendation is to establish a small documentation system with explicit audiences and canonical sources:

```text
README.md                 user-facing overview and links
BUILDING.md               reproducible build entry point
CONTRIBUTING.md           human contributor workflow
docs/index.md             documentation map
docs/architecture.md     runtime/module/thread/data-flow model
docs/testing.md           host, ROM-backed, E2E, and CI test matrix
docs/debugging.md        debugger, probes, capture, and failure triage
docs/configuration.md    JSON files and public environment variables
docs/reference/           stable ROM/runtime/renderer facts
docs/investigations/      dated evidence, hypotheses, and falsifications
docs/decisions/           durable design decisions and trade-offs
CLAUDE.md                 short AI/maintainer-specific constraints and index
```

Do not flatten the existing research into generic prose. Preserve the evidence, but separate stable facts from the history of discovering them.

## Current documentation surface

| Surface | What it does well | Main problem |
| --- | --- | --- |
| [`README.md`](../README.md) | Gives the ROM requirement, build commands, current features, save locations, public launch overrides, and a short static-recompilation explanation. | No documentation index, contributor path, test path, troubleshooting, release/nightly path, architecture map, or complete configuration reference. The menu section is Windows-only even though the then-open [settings frontend change](https://github.com/alondero/aerogauge-recomp/pull/47) was changing it to a portable Windows/Linux frontend. |
| [`BUILDING.md`](../BUILDING.md) | Correctly explains the three build stages, ROM boundary, patch application, generated files, Linux/Windows commands, and headless mode. | It is procedural but not yet a reproducible developer guide: no verification command, build-output expectations, clean/rebuild guidance, failure triage, tool-version matrix, or explanation of which tests need a ROM, debugger, SDL device, or physical controller. |
| [`CLAUDE.md`](../CLAUDE.md) | Excellent compact description of static recompilation, address conventions, generated-file boundaries, engineering constraints, tools, and debugging habits. | It is aimed at an AI/maintainer who already knows the port. It referred to an absolute path in an earlier local worktree, names a `build-and-verify` skill that is not present, and says “Use Github Issues” without a repository link or workflow. |
| [`docs/notes/rom-map.md`](notes/rom-map.md) | Valuable consolidated ROM/RAM facts and strong evidence for scene, race, audio, HUD, PVS, and Controller Pak work. | It mixes stable facts with dated experiments, closed-issue history, false hypotheses, implementation notes, and “do not resurrect” warnings. It is a lab notebook being used as a reference manual. |
| [`docs/notes/hud-widescreen.md`](notes/hud-widescreen.md) | Excellent attribution methodology, measurements, screenshots, formulas, and validation history. | It reads as four increments of an investigation. Several commands refer to untracked or missing capture and debugger artefacts and therefore cannot be followed by a fresh checkout. |
| [`docs/notes/controller-accessories.md`](notes/controller-accessories.md) | A compact, high-signal technical explanation of PFS/EEPROM/haptics ownership and validation. | Needs links to the owning code/tests, an explicit file-format contract, failure/recovery behaviour, and a thread/ownership summary. |
| Source comments | Frequently explain why a hook exists, what was measured, what is generated, and which invariant is being protected. | Comment density is uneven. `stub_renderer.cpp` contains a large investigation log, while `aero_menu.cpp` has little module-level explanation despite being a major integration seam. Many comments use dates, `W###` labels, foreign issue numbers, or private-port names without a durable link. |
| Tests and tools | The project has a meaningful regression suite and several self-describing scripts; CMake currently registers 15 tests, with platform/ROM conditions made explicit in the build. | The public docs do not explain how to run the suite, what each test proves, which tests are skipped, or how to add a regression test. |
| Issues and pull requests | Recent PRs often have unusually strong “what changed / evidence / validation / follow-up” bodies. | Important knowledge remains in PR bodies or private AI-session memory. There is no visible issue/PR template, documentation checklist, labeling convention, or requirement that a resolved investigation becomes a repository document. |

## What is already strong

These practices should be retained:

- The README is honest about the copyrighted ROM boundary and supplies the exact expected ROM identity.
- `BUILDING.md` distinguishes ROM-derived generated C from port-owned source. That distinction is essential in a static-recompilation project.
- Module comments such as the headers in `src/aero_full_track.cpp`, `src/aero_audio.cpp`, `src/aero_config.h`, and `src/aero_savestate.c` record contracts and rationale rather than merely restating syntax.
- The ROM map records addresses, inferred meanings, caveats, and evidence. That is more useful than an unexplained symbol dump.
- CMake comments identify test prerequisites and fail-fast behaviour. For example, the PFS test deliberately fails configuration when the generated function closure cannot be produced instead of silently disappearing.
- [PR 42](https://github.com/alondero/aerogauge-recomp/pull/42), [PR 44](https://github.com/alondero/aerogauge-recomp/pull/44), [PR 46](https://github.com/alondero/aerogauge-recomp/pull/46), and [PR 47](https://github.com/alondero/aerogauge-recomp/pull/47) usually state validation steps and remaining uncertainty. This is a good template for future work, provided the durable parts are copied into the repository.
- The checked-in local skill material captures repeatable investigation procedures. It should remain available, but it should be linked from human-facing contributor documentation rather than acting as a substitute for it.

## Findings and recommendations

### P0 — Create a canonical documentation front door

The biggest usability problem is that a new developer has no answer to “where should I start?” after reading the README. Add `docs/index.md` and link it from the README and `CLAUDE.md`.

The index should group documents by task, not by the date they were written:

- Build/run: `BUILDING.md`, supported platforms, ROM verification, release artefacts.
- Understand: architecture, generated-code boundary, thread model, renderer/input/audio/save seams.
- Change: contributor workflow, patch workflow, adding a hook, adding a test, updating ROM facts.
- Debug: debugger setup, probes, headless harnesses, capture recipes, common failures.
- Reference: ROM map, environment variables, config schema, renderer command coverage, glossary.
- Research: investigations and decisions, clearly marked as historical/evidentiary material.

The index is a small change with a large payoff: it makes every other documentation improvement findable.

### P0 — Separate stable reference from investigation history

The existing notes are too valuable to delete, but stable reference material must stop being buried in chronology. Use this migration pattern:

| Current material | Stable destination | Historical destination |
| --- | --- | --- |
| Address/table/function facts from `rom-map.md` | `docs/reference/rom.md` | Dated topic investigations |
| Final HUD mechanism, invariants, and test commands | `docs/reference/renderer.md` | `docs/notes/hud-widescreen.md` |
| PFS image contract and haptics lifecycle | `docs/reference/audio.md` | `docs/notes/controller-accessories.md` |
| Renderer command support and limitations | `docs/reference/renderer.md` | `docs/investigations/2026-09-14-headless-renderer.md` |
| “Solved”, “falsified”, or “do not resurrect” notes | A short current-state warning or decision link | Dated investigation/decision with evidence |

Each stable fact should have a compact provenance record: address/symbol, meaning, owning code, evidence source, validation method, and confidence/status. Define address notation once, including ROM vs RDRAM vs virtual address and the `MEM_W`/KSEG rules.

Keep the current note paths temporarily as compatibility pages or add redirects when content moves. Avoid breaking links from old PRs and AI sessions all at once.

### P0 — Make references durable and repository-local

The audit found several references that will confuse a new checkout or cannot be followed:

- `CLAUDE.md` contained an absolute local path to an earlier worktree.
- `CLAUDE.md` and `.claude/skills/` refer to a `build-and-verify` skill that is absent from this repository.
- `docs/notes/hud-widescreen.md` gives commands using missing or gitignored capture/debugger files.
- `src/aero_full_track.cpp` contains an unfinished GitHub URL and tracker placeholder.
- Source comments contain unscoped issue numbers that do not belong to the current repository. Some are carry-over references from another port; they are not actionable without context.
- An old audio discussion points future readers at unavailable project memory and a session marker. The durable audio diagnosis should be committed under `docs/investigations/`.
- A closed [repository issue](https://github.com/alondero/aerogauge-recomp/issues/1) and several source comments still use issue numbers as if they were the current specification. Link to the actual GitHub issue/PR or replace the number with a descriptive decision/reference.

Adopt a simple rule: canonical repository docs may link to public source/issue/PR URLs, but may not depend on private chat memory, absolute local paths, untracked scratchpads, or a bare issue number. If an experiment requires a local-only debugger script, say so explicitly and provide the tracked equivalent or a reproducible recipe.

### P0 — Reconcile user documentation with the current product

The checked-out README describes a Windows native menu and F11/Alt+Enter workflow. The then-open [settings frontend pull request](https://github.com/alondero/aerogauge-recomp/pull/47) describes a portable Windows/Linux RecompFrontend settings screen with Apply/Discard behaviour. Once that change lands, the README, `BUILDING.md`, and any configuration reference should be updated together. If the pull request is intentionally not the source of truth, state which branch/version the README describes.

This should be part of the release checklist: every release must verify the README’s supported-platform, settings, launch-key, save-path, and feature-status claims against the tagged binary.

### P1 — Add a human contributor guide and architecture guide

Create `CONTRIBUTING.md` for a developer who did not author the port. It should answer:

1. What can be edited directly (`src/`, tools, tests) and what is generated (`RecompiledFuncs/`, generated symbol/function files)?
2. How are patches applied, updated, and diagnosed when a hunk fails?
3. How does a ROM observation become a hook, a test, and a stable reference entry?
4. How do contributors run host tests, ROM-backed tests, headless tests, and debugger-assisted tests?
5. What evidence belongs in an issue and pull request?
6. Which files must never contain the ROM or copyrighted game assets?

Create `docs/architecture.md` with one page of diagrams/tables for:

- boot and generated-code flow;
- game thread, VI/render thread, audio task, input, frontend, and save-state ownership;
- guest RDRAM/ROM access versus host-owned state;
- RT64 and software-renderer boundaries;
- patch and generated-code boundaries;
- the lifecycle of a new port feature from ROM evidence to regression test.

Use a small Mermaid or ASCII flow diagram if it is easier to keep in sync than prose. The goal is not to document every recompiled function; it is to show the seams where a contributor can safely make a change.

### P1 — Document the test and verification matrix

Add `docs/testing.md`. The current CMake test registration is a foundation, but it is invisible from the public workflow. Start with:

```text
cmake --build build
ctest --test-dir build --output-on-failure
```

Then classify each test as:

| Class | Prerequisites | Examples | What a failure means |
| --- | --- | --- | --- |
| Host/unit | compiler and libraries only | HUD math, race-intro classification, turbo gate, config snapshot | Port-owned logic regression |
| ROM-backed host | ROM plus generated functions | Controller Pak filesystem | ROM-derived call/data contract changed |
| Headless E2E | built executable, ROM, scripted harness | audio task, logging/pacing, warp/turbo | Runtime integration regression |
| Window/device E2E | Windows/SDL window and sometimes controller hardware | frontend, haptics, live rendering | Platform/device or thread-lifecycle issue |

Document expected skip codes and whether a skipped test is acceptable. Add a “new feature” recipe: reproduce, add the smallest host test possible, add an E2E test when the seam requires it, run CTest, and record the exact command and ROM hash in the PR.

The three current open issues should be reflected in this matrix and in the troubleshooting guide:

- [#22](https://github.com/alondero/aerogauge-recomp/issues/22): live windowed save-state loads can race renderer readers;
- [#25](https://github.com/alondero/aerogauge-recomp/issues/25): `get_graphics_config()` returns a reference after releasing its mutex;
- [#26](https://github.com/alondero/aerogauge-recomp/issues/26): graphics JSON writes are synchronous on the main thread.

These are not merely issue-tracker items: they describe current lifecycle and threading constraints that belong in `docs/architecture.md` until fixed.

### P1 — Publish a configuration and environment-variable reference

The public README documents only a small subset of the variables found in the source. That is correct for ordinary players, but developers need a single authoritative reference. Add `docs/configuration.md` with separate tables for:

- persisted JSON settings and their defaults;
- supported user launch overrides;
- developer/debug probes;
- headless/E2E harness controls;
- compile-time or test-only macros that are not public runtime settings.

For every runtime variable record: scope, accepted syntax, default, precedence over JSON/config, read timing, output/file side effects, owning source file, and a copy-paste example. Do not publish internal knobs as supported gameplay features by accident.

The config docs should also state the current write semantics: `save_graphics_updates` performs synchronous file I/O today, and [issue 26](https://github.com/alondero/aerogauge-recomp/issues/26) tracks deferring/debouncing it. That gives readers an accurate contract without pretending the issue is already solved.

### P1 — Adopt a source-comment policy

Add a short section to `CONTRIBUTING.md` rather than requiring a heavy documentation generator immediately. For module-level comments, use this compact contract where applicable:

```text
Purpose
Owned state and thread
Inputs/outputs and guest/host boundary
Invariants and failure behaviour
Generated/dependency boundary
Tests or reproduction command
Canonical reference/decision
```

For code comments:

- explain why, ownership, invariants, endian/address assumptions, and failure modes;
- keep implementation comments next to the implementation;
- move dated measurements, probe output, and abandoned hypotheses to investigations;
- replace bare `#N`, `W###`, dates, and foreign-project names with descriptive links or a local decision ID;
- update the canonical reference when an address or behavioural contract changes.

`stub_renderer.cpp` is the clearest candidate for this cleanup: retain the algorithm and invariant comments, but move the long W103/W105/W107/W109/W110/W111/W112 research narrative into a renderer investigation/reference pair. `aero_menu.cpp` needs the opposite treatment: add a module contract covering event routing, thread ownership, setting application, and persistence.

Formal Doxygen is not an immediate requirement. There are currently no consistent Doxygen tags in the port-owned source. First make the Markdown/reference structure useful; add Doxygen later if the public host APIs and renderer interfaces justify generated API pages. Spaghetti Kart demonstrates the value of a generated domain reference, but its benefit depends on having stable API-level comments to generate.

### P1 — Turn issue and PR quality into a repeatable process

Add tracked GitHub templates:

- `.github/ISSUE_TEMPLATE/bug.yml` or a Markdown equivalent with port version/commit, ROM identity, platform, reproduction steps, logs, and expected/actual behaviour;
- an investigation template requesting the hypothesis, evidence, falsification criteria, and canonical output file;
- a feature template requesting scope, user-facing contract, ROM evidence, and test plan;
- `.github/pull_request_template.md` with What/Why, affected seam, validation commands, ROM/asset boundary, generated files, documentation update, and follow-ups.

Use labels such as `docs`, `bug`, `feature`, `investigation`, `porting`, `runtime`, `renderer`, `audio`, `input`, `save-state`, `needs-repro`, and `blocked-external`. A PR should link the issue, but the issue/PR should not be the only place a durable design fact exists.

The recent PR style is worth formalizing: “What changed”, “Why”, “Evidence”, “Validation”, “Risks/known limitations”, and “Follow-up”. Remove private AI-session links from the canonical path; they may be useful to the original author but are not project documentation.

### P2 — Add documentation checks to CI

The only checked-in workflow is an on-demand build/release workflow. Add a lightweight pull-request workflow that does not require a copyrighted ROM and checks:

- Markdown formatting and local links;
- no references to known private/local paths, private project memory, or missing scratchpads;
- no unfinished placeholder URLs or tracker placeholders;
- documentation links from the README and docs index;
- the documented test command and available CTest names;
- optionally, generated environment-variable/config tables if the project introduces a registry.

Keep ROM-backed builds in the private release workflow. Document exactly what that workflow validates and what contributors must validate locally. A docs check is valuable even when the full application build cannot run in public CI.

## Comparison with the reference projects

These projects solve adjacent problems rather than identical ones. Banjo-Kazooie is a decompilation project; Shipwright and Spaghetti Kart are closer in runtime/modding ecosystem. AeroGauge should borrow their information architecture and reproducibility habits, not copy their codebase shape.

| Project | Practices worth borrowing | AeroGauge adaptation |
| --- | --- | --- |
| [Banjo-Kazooie decomp README](https://github.com/n64decomp/banjo-kazooie/blob/master/README.md) | Concise build front door, table of contents, explicit supported platforms, dependency commands, multiple baserom checksums, progress/status, Docker/cloud paths, and module targets. | Add a docs index, exact ROM/version identity, supported-platform matrix, generated-file/module map, and a reproducible container or dependency check. Add a project-progress page only if it can be kept current. |
| [Banjo-Kazooie merge-request template](https://github.com/n64decomp/banjo-kazooie/blob/master/.gitlab/merge_request_templates/default.md) and [style guide](https://gitlab.com/banjo.decomp/banjo-kazooie/-/wikis/Style-Guide) | Small contribution checklist covering change type, build success, and documentation/style expectations. | Add a GitHub PR template with CTest, docs, ROM evidence, generated files, and platform validation. |
| [Shipwright README](https://github.com/HarbourMasters/Shipwright/blob/develop/README.md) | Strong user quick-start, ROM compatibility path, configuration/shortcuts, project overview, further-reading links, development links, and release/nightly paths. | Keep README player-focused and make it point to contributor/reference docs instead of absorbing all details. Add release artefact and compatibility guidance. |
| [Shipwright build guide](https://github.com/HarbourMasters/Shipwright/blob/develop/docs/BUILDING.md) and [formatting guide](https://github.com/HarbourMasters/Shipwright/blob/develop/docs/FORMATTING.md) | Platform-specific prerequisites, exact tool versions, development IDE paths, packaging, troubleshooting, and style tooling aligned with CI. | Split AeroGauge’s build guide from debugging/testing, record pinned tool versions, add clean/rebuild/packaging sections, and make style/test commands copy-pasteable. |
| [Spaghetti Kart docs main page](https://github.com/HarbourMasters/SpaghettiKart/blob/main/docs/mainpage.md) and [documentation tree](https://github.com/HarbourMasters/SpaghettiKart/tree/main/docs) | A public docs home, generated/reference documentation, FAQ, build guide, and domain-specific tutorials for tracks, actors, characters, and modding. | Add a domain glossary and topic pages for ROM mapping, full-track geometry, HUD, audio, Controller Pak, renderer limitations, and debugging. Consider Doxygen only after API comments are stable. |
| [Spaghetti Kart Doxygen configuration](https://github.com/HarbourMasters/SpaghettiKart/blob/main/Doxyfile) | Makes the technical reference a generated, discoverable product rather than an accidental set of source comments. | Treat generated docs as a later phase; first establish stable source comments and Markdown references so generation will not fossilize investigation logs. |

The consistent lesson is progressive disclosure:

```text
README quick start
  -> focused build/config/testing/debugging guides
    -> architecture and stable reference
      -> investigations, generated API docs, and modding detail
```

## Proposed implementation sequence at audit time

### First pass: make the current repository navigable

1. Add `docs/index.md` and link it from README and `CLAUDE.md`.
2. Add a “Documentation status” section to the index explaining stable reference versus investigation notes.
3. Correct the README platform/menu claims to match the released commit; update them again when the [settings frontend change](https://github.com/alondero/aerogauge-recomp/pull/47) lands.
4. Replace or qualify the broken/local references listed above.
5. Add `docs/testing.md` with `cmake --build build` and `ctest --test-dir build --output-on-failure`, the 15 current test names, and prerequisite/skip notes.
6. Add `CONTRIBUTING.md` with generated-file boundaries, test expectations, and PR evidence requirements.

### Second pass: extract the knowledge already present

1. Split `rom-map.md` into a stable reference and investigation history.
2. Split `hud-widescreen.md` into final mechanism/reference and measurement history; track the capture/debugger harnesses.
3. Add `docs/architecture.md` and `docs/configuration.md`.
4. Create a `docs/investigations/` index and migrate durable findings from [PR 6](https://github.com/alondero/aerogauge-recomp/pull/6) and other research-heavy PRs.
5. Add a glossary for guest/RDRAM/ROM, recompiled/generated code, RT64, ultramodern, librecomp, VI, DL, PFS, EEPROM, and the project’s scene/race terms.

### Third pass: make quality self-maintaining

1. Add issue and PR templates plus labels.
2. Add a docs/link/placeholder check to pull-request CI.
3. Add a config/environment-variable registry or a documented source-of-truth rule.
4. Add a release documentation checklist and a versioned compatibility/status page.
5. Reassess whether Doxygen or a static docs site adds value after the Markdown reference has settled.

## Definition of done

Documentation is in a substantially better state when a developer with only a fresh clone and a legally dumped matching ROM can:

- identify the correct build path and expected outputs without reading source history;
- understand which files are generated, which are port-owned, and which edits survive regeneration;
- find the module/thread/data-flow boundary relevant to a change;
- run the appropriate test class and understand a skip or failure;
- reproduce a documented debugger/capture investigation without private files;
- find the canonical current answer for a ROM address or behavioural contract;
- distinguish current behaviour, known limitations, open issues, and historical hypotheses;
- submit an issue or PR with enough evidence for the next developer to continue;
- reach all of the above from the README in two or three clicks.
