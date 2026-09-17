# AeroGauge documentation

This is the documentation map. Choose the page for the job you have.

## I want to play

- [README](../README.md) explains the current release, installation, controls,
  settings, saves, and known limitations.
- [Configuration](configuration.md) explains the JSON files when you need to
  edit settings by hand.
- [GitHub releases](https://github.com/alondero/aerogauge-recomp/releases)
  contains the downloadable Windows and Linux packages.

## I want to build or contribute

- [Building](../BUILDING.md) gives the source-build commands.
- [Contributing](../CONTRIBUTING.md) explains generated files, dependency
  patches, evidence, tests, and pull requests.
- [Testing](testing.md) lists the CTest suite, ROM-backed tests, end-to-end
  checks, prerequisites, and skip conditions.
- [Debugging](debugging.md) gives repeatable log, debugger, and capture
  workflows.

## I want to understand the code

- [Architecture](architecture.md) describes the guest/host boundary, threads,
  generated code, runtime, renderer, audio, input, saves, and patches.
- [Glossary](glossary.md) defines project terms in plain English.
- [Reference: ROM](reference/rom.md) records the supported ROM and stable
  address facts.
- [Reference: runtime](reference/runtime.md) records runtime and thread facts.
- [Reference: renderer](reference/renderer.md) records upstream and local
  renderer behavior.
- [Reference: audio and accessories](reference/audio.md) records the audio
  and Controller Pak boundaries.

## I want to check an old claim

- [Investigations](investigations/index.md) contains dated experiments,
  measurements, hypotheses, and failed approaches.
- [Decisions](decisions/index.md) contains short architecture decisions.
- [Original documentation audit](documentation-audit.md) is the initial audit
  of the repository and public project references. It is retained as research,
  not as the canonical user guide.
- [Original notes](notes/) contain useful ROM and feature research. They are
  historical working notes; stable facts are copied into the reference pages.

The repository does not claim that an idea is settled merely because an AI
assistant wrote it. A source comment, test, ROM measurement, public upstream
documentation, or maintainer decision should support each important claim.
When evidence conflicts, record the conflict and ask the human maintainer to
choose the next step.

## Current status boundary

The documentation describes the current merged main branch. The shared
RecompFrontend settings screen is part of the Windows and Linux build paths.
This review directly checked Windows host tests only; see [the frontend guide](frontend.md)
for the remaining manual and Linux checks.
