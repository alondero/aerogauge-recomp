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
| Understand dependency patches | [Patch inventory](../patches/README.md) |

## Player help

- [Android](android.md) covers APK installation, ROM and GPU-driver import,
  touch controls, save backups, building, and release signing.

- [README](../README.md) covers supported systems, installation, controls,
  settings, saves, and known limits.
- [Configuration](configuration.md) is for players who need to inspect or
  change a file by hand. The normal settings screen is the preferred path.
- [Modding](modding.md) explains installing packages and the experimental
  code-mod and texture-pack support.
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
- [Modding](modding.md) describes package types, install and load behavior, and
  the experimental code-mod boundary.
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
project record. Domain knowledge lives in the relevant domain page:
controllers, audio, renderer, full-course geometry, ROM reference, etc. An
uncertain ROM observation goes into a regular bug report while it is being
worked out; once confirmed, the lasting rule moves into the source comment,
focused test, or domain page and the bug report is closed.

A plausible explanation, an old issue number, or a screenshot is not proof of
a current behavior. State what was checked and what remains unknown.
