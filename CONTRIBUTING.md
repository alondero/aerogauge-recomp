# Contributing

This project needs developers who can question a plausible explanation. A
machine-generated answer, code suggestion, or measurement summary is not a
decision. The human maintainer decides when evidence is enough, which trade-off
is acceptable, and whether a temporary workaround may ship.

Please use the [documentation map](docs/index.md), then read
[Architecture](docs/architecture.md) and [Testing](docs/testing.md) before
changing runtime or generated-code boundaries.

## Before changing code

1. Check the branch and the exact ROM you will use.
2. Read the relevant source and its reference page.
3. Search for existing investigations, decisions, tests, and open
   [GitHub issues](https://github.com/alondero/aerogauge-recomp/issues).
4. Write down the current behavior and the behavior you want.
5. Decide whether the change belongs in generated game code, hand-written port
   code, a dependency patch, a test, or documentation.

Do not start with a fix based only on a crash message or an AI explanation.
Reproduce the behavior where possible. If you cannot reproduce it, say so.

## What is hand-written

| Path | Owner | Normal change |
| --- | --- | --- |
| src/ | Port maintainers | Hand-written host code and narrow game hooks |
| tests/ | Port maintainers | Host, ROM-backed, and end-to-end regression tests |
| docs/ | Port maintainers | Stable facts, investigations, decisions, and user guidance |
| patches/ | Port maintainers | Diffs against pinned dependency commits |
| aerogauge.syms.toml | Port maintainers and ROM evidence | Regenerated and reviewed symbol input |
| aerogauge.us.toml | Port maintainers and ROM evidence | Regenerated input plus reviewed hooks |
| force_stub.txt | Port maintainers | Deliberate translation fallbacks |

## What is generated

| Path | Generator | Rule |
| --- | --- | --- |
| RecompiledFuncs/ | N64Recomp | Never edit; regenerate from the ROM |
| src/aspMain.cpp | RSPRecomp | Never edit; regenerate from the ROM |
| build/ | CMake and Ninja | Never commit |

If generated code is wrong, find the input that is wrong. That may be the ROM
identity, symbol boundary, stub list, hook address, or recompiler behavior.
Change the input, regenerate, and review the result.

## Turning ROM evidence into a change

Use this chain:

~~~text
ROM observation
    -> dated investigation with address, bytes, and method
    -> stable symbol or named table
    -> narrow hook or source patch
    -> focused test or capture
    -> reference entry and decision, if the behavior is lasting
~~~

An address by itself is not a stable interface. Include the ROM version and
explain why the address is safe. If a hook relies on a frame phase, cursor,
allocator range, or byte-swapped field, write that invariant down.

If the original game behavior is not yet understood, prefer a probe that
measures it. Do not add a native replacement that invents new game rules
without recording that choice and asking the maintainer to approve it.

## Port hooks and guest memory

Some current features write directly into guest RDRAM:

- full-course geometry creates synthetic display lists and links them into
  game-owned lists;
- widescreen HUD code rewrites a display-list range and one matrix; and
- save-state loading copies guest memory and repairs native thread references.

These hooks are transitional and fragile. Their success depends on this ROM's
addresses, byte order, memory layout, thread timing, and renderer behavior.
They are not a model for new general APIs.

Every new direct guest-memory access must state:

- the guest addresses and their meaning;
- the thread that reads or writes them;
- the MEM helper and endian assumption;
- the bounds and lifetime assumption;
- the failure behavior;
- how a test can detect a bad assumption; and
- the source-level or decompilation step that could remove the hook later.

The desired direction is decompilation or readable source-level game code,
proper source patches, stable symbols, and explicit code-mod and mod
interfaces. Do not hide uncertainty by calling a memory write robust or
permanent.

## Dependency patches

The submodules are pinned in the parent repository. A build script resets
tracked submodule files before applying the patches. If you modify a
submodule directly, your change must become a patch before it is shared.

Suggested workflow:

1. start from the pinned submodule commit;
2. apply the existing patches in the order in BUILDING.md;
3. make the smallest dependency change;
4. run the dependency and port tests that exercise it;
5. export the diff as a new numbered patch;
6. add the patch to both build scripts and BUILDING.md;
7. explain why the patch is local and whether it should go upstream; and
8. verify a clean checkout can apply all patches.

Do not commit a dirty submodule as a substitute for a patch. Do not silently
refresh a patch after an upstream change. Stop and record the context drift.

## Renderer changes

RT64 already owns normal display-list interpretation, aspect ratio,
interpolation, extended GBI support, and texture replacement. The port owns
the AeroGauge ROM integration and game-specific display-list behavior.

Before changing RT64:

1. check [the renderer reference](docs/reference/renderer.md);
2. reproduce the behavior with the smallest display-list or math test;
3. decide whether it is specific to AeroGauge or general to RT64;
4. keep a game-specific fix in the port when possible; and
5. prepare an upstream issue or patch when the fix is general.

A local patch must have an owner, a removal condition, and a test. If the
upstream project accepts the fix, update the submodule pin and remove the
local patch in a separate, reviewable change.

## Mod support

The current port has no stable general mod API. RT64 texture loading and
dumping are useful developer features, but they are not a complete mod
contract. Do not present a texture path or a guest-memory hook as a supported
plugin system.

The expected future path is a versioned mod boundary with explicit ownership
of assets, code changes, configuration, load order, and failure behavior.
Choosing that boundary is a project decision. A proposal should compare
source patches, generated code mods, and runtime asset loading before code is
added.

## Tests and documentation

Add or update a regression test with the smallest useful scope. Run the
relevant host tests, ROM-backed tests, or end-to-end checks. Record tests that
could not run and why.

If a change affects a user-visible setting, platform, command, save location,
or generated file, update the matching document. If it exposes uncertainty,
write an investigation or ADR instead of smoothing it over in prose.

Run the documentation check before opening a pull request:

~~~bash
python3 scripts/check_docs.py
~~~

## Pull requests

Use the repository pull-request template. A useful pull request states:

- what changed and why;
- the human decision or trade-off;
- the commit, ROM identity, OS, and renderer/backend used;
- expected and actual behavior;
- tests run and skipped;
- generated files and dependency patches;
- documentation impact; and
- known limitations and follow-up work.

Do not include private AI-session links, local machine paths, unexplained
issue numbers, or claims about a run that you did not perform.
