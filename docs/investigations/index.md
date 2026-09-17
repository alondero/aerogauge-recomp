# Investigations

This directory holds dated evidence. An investigation can end in a fix, a
decision, a rejected idea, or an unresolved question.

Each record should name the ROM, commit, host, command, observation,
hypothesis, and falsifying check. Separate what was measured from what was
inferred. A screenshot or a plausible explanation is not enough to change a
runtime boundary.

## Current records

- [Current-state ledger](2026-09-14-current-state.md) records what the
  checked-in branch and public issue tracker establish.
- [Reference-project comparison](2026-09-14-reference-projects.md) compares
  public user and developer documentation for three human-driven N64 port
  projects.
- [Renderer audit](2026-09-14-renderer-audit.md) separates RT64 capability
  from AeroGauge-specific code and local patches.
- [Build patch parity](2026-09-14-build-patch-parity.md) records the mismatch
  between the host build scripts and the CI comments.
- [Audio buffering](2026-09-14-audio.md) records the generated-audio,
  host-device, and headless-FIFO evidence.
- [Save-state](2026-09-14-savestate.md) records the guest-memory format,
  clock rebasing, settle gate, and live-reader risk.
- [Settings frontend review](2026-09-17-settings-frontend-review.md) records
  the post-merge frontend boundary, earlier targeted test evidence, and the
  checks that remain open.
- [Generated symbol inputs](2026-09-17-generated-symbols.md) records the
  historical ROM evidence behind the hand-maintained symbol generator.

The earlier [documentation audit](../documentation-audit.md) and
[working notes](../notes/) remain available. They contain useful historical
material, but the reference pages are the current contract.
