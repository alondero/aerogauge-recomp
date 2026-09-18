# Release-note structure

Use the same high-level sections in each release so players can find the
information quickly. Fill every section from the current commit and workflow
run. Do not copy an old status list or claim that a platform was tested when
it was not.

## Opening

Start with one short paragraph that names the release quality and the main
player-visible change. State the supported platform and the most important
limitation.

## Closed since

Group changes by player-facing topic. Link the local issue or pull request
that provides the evidence. Lead with the benefit, then name the relevant
source, patch, or documentation area.

## Not done yet

List only limitations that still exist in the release. The issue tracker is
the detailed follow-up list. Do not recreate a project-history document in
the release notes.

## How to run

Repeat the short archive, ROM, and launch instructions from README.md.
Mention platform-specific files and the F11 or Alt+Enter fullscreen shortcut
when relevant.

## Build provenance

Record the release commit, workflow run, toolchains, and ROM identity when a
ROM-backed build was used. Link [BUILDING.md](../../../../BUILDING.md) for
source builds.
