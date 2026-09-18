---
name: session-management
description: Close a work session — validate, reflect progress in the GitHub issue, capture new derivations in the checked-in docs, and write a lean grounded handoff. Use at the end of a session, before a PR, or when the user says "wrap up".
---

# Session close-out

## Workflow

1. **Summarize changes**: `git diff --stat HEAD` (+ `git log --oneline main..`).
2. **Validate**: run the build-and-verify gate; record the actual result.
3. **Persist derivations** — the step that makes the next session cheap:
   - New game addresses/protocols → `docs/reference/rom.md` after they are
     confirmed and useful beyond the current change.
   - New libultra routing → its evidence comment in `scripts/gen_syms_toml.py`.
   - New probe/env var → the table in `.claude/skills/port-debugging/SKILL.md`.
   - A falsified theory → update the issue or pull request, and update any
     lasting source comment or reference entry that would otherwise mislead
     the next developer.
4. **Tracker**: `gh issue comment <n>` with a one-paragraph status (what landed,
   what's blocking), or close it. Reference exact files/functions.
5. **Handoff** (`nextsessionprompt.md`, gitignored — REWRITE, don't append):
   date, accomplishments (exact files/functions), blockers (with symptoms), and
   the grounded next *area* — a theory to test, NOT an implementation plan.

## Rules

- **Method line per load-bearing claim**: `[method: live-ares | port-gdb |
  static-asm (tools/rom/...) | recompiled-C]` plus a `[re-verify: <≤5-min
  command>]`. A claim without one is unverified input — the next session will
  (correctly) re-derive it.
- **Pre-flight probe for multi-session investigations**: name the cheap check
  (one watchpoint / one disasm) that confirms the game is actually in the
  assumed state before any instrumentation is trusted.
- **Ship code, not notes** — if the session produced only analysis, say so
  plainly in the issue rather than dressing it as progress.
- Scaffolding landed this session (hand-rolled shortcut instead of the ROM's
  real behaviour)? Confirm it has a tracker entry before closing.
