# Documentation map

This project has two audiences. Players need a short path to a working game.
Developers need clear build steps, evidence, and ownership.

## Start here

| Task | Read |
| --- | --- |
| Install a release | [README](../README.md) |
| Build from source | [BUILDING.md](../BUILDING.md) |
| Change code | [CONTRIBUTING.md](../CONTRIBUTING.md) |
| Run checks | [Testing](testing.md) |
| Investigate a failure | [Debugging](debugging.md) |
| Report a ROM finding | [Reverse-engineering issue template](../.github/ISSUE_TEMPLATE/reverse_engineering.md) |
| Understand dependency patches | [Patch inventory](../patches/README.md) |

## Player help

- [README](../README.md) covers supported systems, installation, controls,
  settings, saves, and known limits.
- [Configuration](configuration.md) is for players who need to inspect or
  change a file by hand. The normal settings screen is the preferred path.
- [Controllers and accessories](controllers.md) explains input, Controller
  Pak storage, and rumble.

## Developer help

- [Architecture](architecture.md) describes generated code, runtime ownership,
  threads, guest memory, renderer, audio, input, saves, and patches.
- [Testing](testing.md) lists host, ROM-backed, and end-to-end checks with
  prerequisites and exact commands.
- [Debugging](debugging.md) describes logs, captures, debugger use, and
  reproducible failures.
- [Settings frontend](frontend.md) describes the current menu boundary and
  its platform limits.
- [Configuration](configuration.md) lists persistent files and environment
  overrides.
- [Glossary](glossary.md) defines project terms in plain English.

## Stable reference

- [ROM reference](reference/rom.md) records the supported ROM and the
  ROM-specific addresses used by the port.
- [Runtime reference](reference/runtime.md) records runtime services, memory,
  threads, callbacks, and shutdown limits.
- [Renderer reference](reference/renderer.md) separates RT64 capabilities
  from AeroGauge behavior.
- [Audio reference](reference/audio.md) records audio and save-device
  ownership.
- [Controllers and accessories](controllers.md) records the input and
  device contracts.
- [Peer-project comparison](peer-projects.md) records durable lessons from
  established N64 projects with public player and developer workflows.

## How evidence becomes documentation

Source, tests, checked-in fixtures, and reproducible measurements are the
project record. An uncertain ROM observation belongs in a
[reverse-engineering issue](../.github/ISSUE_TEMPLATE/reverse_engineering.md)
until it becomes a named source rule, a test, or a stable reference entry.
The issue is a work record; the source, test, or reference page is the lasting
documentation.

A plausible explanation, an old issue number, or a screenshot is not proof of
a current behavior. State what was checked and what remains unknown.
