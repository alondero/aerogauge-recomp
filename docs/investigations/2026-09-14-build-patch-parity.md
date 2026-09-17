# Build patch parity — 2026-09-14

## Observation

The intended dependency patch set is described in [BUILDING](../../BUILDING.md)
and in the two host scripts. The actual host arrays are different.

The current files show:

| Path | Observed list |
| --- | --- |
| build.ps1 | Runtime 0001, 0007, 0012, 0014, 0013; RT64 0006, 0008, 0005, 0009, 0010, 0011; Plume 0004 |
| build.sh array | Runtime 0001, 0007, 0012, 0014; RT64 0006, 0008, 0009, 0010, 0011 |

The Linux path currently omits 0013, while the Windows path applies it. The
source does not establish whether the omission is intentional. Patch 0013
changes non-graphics RSP task handling, so it may affect more than Windows.

The comments in the shell script and release workflow now defer to their host
arrays. This pass changed those comments only; it did not change patch order
or patch behavior.

## What was not done

This documentation pass did not initialize submodules, run both build
scripts, or apply patches. The behavior of each patched build therefore
remains unexecuted in this worktree.

## Decision needed

The maintainer should choose one of these paths:

1. make a small patch-parity change and test both host scripts;
2. state that 0013 is intentionally Windows-only, explain why, and add a
   Linux test proving the unpatched behavior is safe; or
3. move the patch manifest to one checked-in source of truth used by both
   scripts and CI.

Until then, a Linux source build should be reported with its actual patch
list. Do not call the two host builds equivalent.
