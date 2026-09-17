# Save-state investigation — 2026-09-14

This record keeps the evidence behind the developer save-state experiment. It
was assembled from the existing implementation, tests, and source comments.
This audit did not rerun the ROM-backed save-state scripts.

## Current format and boundary

- A save contains the low 8 MiB guest RDRAM plus a small host-endian header.
- The header stores a magic value, version, RDRAM size, scene number, and the
  save-process value of the runtime's monotonic clock.
- F7/F8 only publish an atomic request on the SDL/main thread. The game thread
  performs the copy at a frame-boundary hook.
- A temporary file and rename protect the slot from a torn write. Invalid
  magic, version, size, or short input is rejected before guest memory changes.

This is not a native save-state. Native thread registers, C stacks, RT64
state, audio state, and work in progress remain in the process. Loading from a
different process can relink the guest thread records only for threads that
already exist in that process.

## Time and settle evidence

The race clock stores a runtime osGetTime sample in guest memory at
0x8016C4F0. The clock uses the difference between consecutive samples. A
snapshot restored into a process with a different clock epoch once produced a
large first delta and reached the time-over path within two frames. The load
path rebases the recorded anchor by the difference between save and load
epochs. The anchor list must be extended if another RAM-resident clock sample
is found.

The current load gate requires the requested and current scenes to match, a
stable scene window, and race phase 3 when the scene is a race. Existing
evidence showed that loading during a short scene transition or while course
data was still being prepared can leave native loader state inconsistent with
restored RAM. This is why a settled load is a debugging aid, not a general
quick-save feature.

## Falsifying checks and open risk

The repeatable checks are test_savestate_roundtrip.ps1 for a headless save and
load, and test_savestate_hotkey.ps1 for the interactive F7/F8 path. Record the
ROM identity, port commit, host, and complete logs for either check.

The open [windowed restore issue](https://github.com/alondero/aerogauge-recomp/issues/22)
means that a live renderer can still read guest memory while a load replaces
it. The long-term choice between a safer runtime snapshot boundary and a
different save-state design requires a maintainer decision; this documentation
pass does not change the implementation.
