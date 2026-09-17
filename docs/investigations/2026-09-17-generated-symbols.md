# Generated symbol inputs - 2026-09-17

This note moves the dated evidence behind scripts/gen_syms_toml.py out of the
generator. It is historical evidence carried forward from ROM analysis, not a
new ROM-backed result from this documentation pass.

## Fixed inputs

- ROM: the accepted USA 8 MiB image described in docs/reference/rom.md.
- Commit at the time of the measurements: not recorded in the original
  generator comments.
- Host: not recorded in the original generator comments.
- Commands: density scan, byte decoding, boot-smoke and windowed runs, and
  indirect-target scans. The exact command lines were not recorded.

## Observations carried forward

- CPU text was treated as contiguous from ROM 0x1000 through about 0x7F4C0.
  In the recorded scan, in-range jal targets stayed inside that window.
- Function starts combine the ROM entry trampoline, static jr targets, in-range
  jal targets, and prologues after function terminators. This is a heuristic,
  not a disassembler-quality proof of every boundary.
- Several libultra mappings were byte-checked while boot first-faults moved
  through scheduler, PI, VI, timer, RSP, controller, EEPROM, and rumble code.
  The native replacements are recorded in the generator and in the runtime
  reference.
- Indirect function starts were added after boot-smoke, windowed-run, and
  load-address scans found targets that the jal and prologue scans could not
  see.
- The two course registrars were decoded from the ROM and mapped to the
  game's visibility tables. The full-track native uses those addresses but
  remains an AeroGauge-specific hook.

## Use and falsification

The script is a hand-maintained input to N64Recomp. Before changing an address,
name, size, or start list, use the accepted ROM, record the command and output
in a new dated investigation, and regenerate the ignored output. A build or
boot failure is not proof that a boundary is wrong; compare the ROM bytes,
generated output, and patch input together.

The current documentation pass did not rerun these ROM measurements because the
worktree has no ROM or generated build output.
