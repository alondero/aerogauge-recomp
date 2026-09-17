# ADR 0001: Keep generated output separate from port hooks

- Status: accepted transition policy
- Date: 2026-09-14

## Context

The port currently translates the supported ROM into generated CPU and audio
files. It also needs hand-written code for host services and a few ROM
boundaries that are not yet represented as readable game source. Direct
guest-memory writes are tempting because they can cross a missing source
boundary quickly, but they depend on addresses, byte order, allocator use,
thread timing, and display-list shape.

## Decision

Keep these boundaries explicit:

- N64Recomp and RSPRecomp own generated output.
- ROM evidence, symbol input, TOML hooks, and stub policy are reviewed inputs.
- Hand-written port code owns host integration and narrow named hooks.
- Direct guest-memory manipulation is transitional and must document an exit
  path toward decompilation, source patches, stable symbols, or a versioned
  code-mod seam.

Generated files must never be repaired by editing their output. A source
change must identify the input that caused the generated result.

## Consequences

This makes regeneration safer and lets a reviewer see which part of a change
is ROM-derived. It also means that a small feature may need an investigation,
a symbol update, a hook, and a test before it is safe to keep.

The decision does not choose the future decompilation method or define the
future mod API. Those require separate evidence and maintainer approval.
