# AeroGauge: Recompiled

AeroGauge: Recompiled is a PC version of the Nintendo 64 racing game AeroGauge.
It runs your own copy of the game in a native window.

This branch includes an in-game settings screen. It is still being checked.
The whole game, audio, and visual enhancements need more testing.
The details below describe the current main branch. A release archive can
predate this branch, so check its release notes before expecting the settings
screen or other newly merged features.

## Supported computers

Release targets are:

- Windows 10 or later, 64-bit, with Direct3D 12 support.
- Linux, 64-bit, with Vulkan support.

macOS is not a supported target in this branch. A build that compiles on
another system is not proof that the game runs there.

## Run a release

1. Download the archive for your system from
   [GitHub Releases](https://github.com/alondero/aerogauge-recomp/releases).
2. Extract it to a folder.
3. Put your legally dumped USA ROM in that folder. Name it
   `AeroGauge (USA).z64`.
4. Start `aerogauge_modern.exe` on Windows or `aerogauge_modern` on Linux.

The archive contains the program and its support files. It does not contain the
game ROM. Other ROM releases are not supported.

## Build from source

Building from source is a developer task and requires a matching ROM. See
[BUILDING.md](BUILDING.md) for the tools and complete commands.

## Controls

These are the default port bindings:

| Action | Keyboard | Gamepad |
| --- | --- | --- |
| Confirm / accelerate | X | A |
| Cancel / brake | C | B |
| Steer | Arrow keys or W/A/S/D | Left stick |
| Drift | Z | Left trigger |
| Pause / advance | Enter | Start |
| L button | Q | Left shoulder |
| R button | E or R | Right shoulder or right trigger |
| C buttons | I/J/K/L | X/Y/right stick |

The port exposes one physical controller as Controller 1. The gamepad uses the
standard SDL game-controller mapping.

## Settings

Open the settings screen with **Escape**, **F10**, or controller **Back**.
This also works in fullscreen. Use the mouse, keyboard, or controller D-pad.
The screen shows the active button prompts.

The **Graphics** page controls resolution, widescreen display, HUD placement,
presentation rate, anti-aliasing, window mode and size, and texture paths.
Press **Apply** to keep graphics changes. Press **Discard** to cancel them.

The **Enhancements** page controls draw distance, full-course geometry, and
Easy Turbo + Boost Start. These changes are saved as soon as they are made.
Full-course geometry is experimental and can cost performance or show visual
errors. Easy Turbo changes the driving controls; it is off by default.

The settings screen does not change the game's normal control bindings. F11 and
Alt+Enter switch fullscreen. Graphics API and texture-path changes take effect
after a restart.

## Saves and files

The program stores its files in these locations:

- Windows: `%LOCALAPPDATA%\AeroGaugeRecomp\`
- Linux: `$XDG_CONFIG_HOME/AeroGaugeRecomp`, or
  `~/.config/AeroGaugeRecomp`
- Portable mode: the folder from which you run the program, when that folder
  contains a file named `portable.txt`

The contents of `portable.txt` do not matter. Its presence selects portable
mode.

`graphics.json` stores display settings. `enhancements.json` stores the
gameplay assist setting. Game saves are in the `saves` subfolder. Back up
that folder before testing a new build.

## Known limitations

- The USA release is the only supported ROM.
- The settings screen has only Graphics and Enhancements pages. It does not
  provide a new control-binding page.
- Full-course geometry is experimental.
- Texture packs and texture dumps are developer features. They are not a
  general mod system.
- Platform, graphics-driver, and physical-controller coverage is incomplete.
- The settings screen is not shown by headless test runs.

See [docs/index.md](docs/index.md) for developer documentation, test commands,
configuration details, and the current evidence record.

## License

Project code is licensed under [LICENSE](LICENSE). The game and dependency
submodules have their own licenses. You must provide your own ROM.
