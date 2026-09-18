# Release notes template

Replace every placeholder before publishing. Keep the notes useful to a
player who has only downloaded the release archive.

> **Pre-release quality.** Built from `main` for early testing and feedback.
> Expect rough edges. Please report problems with the release commit and
> reproduction details.

## You must supply your own ROM

This release contains no game ROM. Obtain a legal copy of
`AeroGauge (USA).z64` and put it beside the executable.

1. Extract the archive.
2. Put the ROM beside the executable.
3. Start `aerogauge_modern` on Linux or
   `aerogauge_modern.exe` on Windows.

Game saves and settings use the locations in README.md. See BUILDING.md only
when building from source.

## What's working

<Describe the most important player-visible change in this release. State
which platforms were checked and name any important limitation.>

## Closed since <PREV_VERSION>

Group related changes by user-facing topic. For each item, include the issue
or pull request link, the player benefit, and the implementation area when it
helps a developer follow up.

- <Player-visible change, evidence, and link>

## What's not done yet

<List current limitations that affect players. Do not copy an old status list.
Use current issues for detailed follow-up work.>

## How to run

### Linux

Extract the Linux archive, place `AeroGauge (USA).z64` beside the binary,
and run `aerogauge_modern`.

### Windows

Extract the Windows archive, place `AeroGauge (USA).z64` beside the
executable, and start `aerogauge_modern.exe`. F11 and Alt+Enter switch
fullscreen. The README lists the settings menu and default controls.

## Build provenance

- Commit: `<SHORT_SHA>` (<YYYY-MM-DD>)
- Workflow run: [<RUN_ID>][run]
- Linux toolchain: <TOOLCHAIN>
- Windows toolchain: <TOOLCHAIN>

---

To build from source, see [BUILDING.md]. For controls and settings, see
[README.md].

To build from source, see BUILDING.md. For controls and settings, see
README.md. Add final links when the release body is assembled.
