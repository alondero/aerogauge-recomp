# Glossary

These definitions use the meaning the project needs. They are not a general
N64 manual.

| Term | Plain-English meaning |
| --- | --- |
| Address | A number used to identify a place in the original game's code or data. |
| Code mod | A deliberate change to game code through a named and supported code interface. |
| Decompilation | Recreating readable source code from a program binary, then checking that it matches the original behavior. |
| Display list | A list of drawing commands made by the game for the graphics processor. |
| Guest | The original N64 program and the memory model it expects. |
| Guest memory | The byte array that stands in for the N64's RAM while the port runs. |
| Hook | A small call inserted at a known point in generated game code. It lets port code observe or change behavior. |
| HLE | High-level emulation. A host implementation replaces a piece of console hardware or an operating-system service. |
| Host | The PC process, its operating-system threads, files, window, input, audio, and graphics APIs. |
| KSEG0 | The N64 address range used by normal pointers in this ROM. The port maps these addresses into its guest-memory buffer. |
| Librecomp | The library that connects generated game functions to the modern runtime, ROM reads, overlays, and save storage. |
| N64Recomp | The tool that translates N64 machine code into C source files. |
| Native replacement | Hand-written host code used instead of a generated game function. It is a port hook, not decompiled game source. |
| OSThread | An N64 operating-system thread record. In this port the record is in guest memory but its running context is native. |
| Patch | A file that changes a pinned dependency submodule. It is kept in this repository so a build can reproduce the dependency state. |
| PFS | The N64 Controller Pak file system. AeroGauge uses it for notes and Time Attack ghosts. |
| PI | The N64 peripheral interface. In this port it handles ROM reads and save-file transfers through librecomp. |
| PVS | Potentially visible set. A table saying which course zones should be drawn from another zone. |
| RDRAM | The N64's main memory. Here it is an 8 MiB host buffer in the normal guest address range. |
| RecompiledFuncs | The ignored directory containing C/C++ generated from the game ROM. |
| RSP | The N64 co-processor used for graphics and audio work. |
| RSPRecomp | The tool that translates an RSP microcode program into C++. |
| RT64 | The graphics renderer used by this port. It reads N64 display lists and presents them through a PC graphics API. |
| Runtime | The host libraries that provide threads, timing, input, audio, graphics callbacks, ROM access, and saves. |
| Scene | A top-level game state such as the title screen, menu, or race. |
| Static recompilation | Translating a console program before running it, then compiling the translated source into a native program. |
| Stub | A placeholder implementation that intentionally does not run the original function. |
| Synthetic display list | A drawing-command list built by port code rather than by the original game. |
| VI | Video interface timing. The runtime uses VI ticks to drive frame timing and callbacks. |
| Worktree | A separate Git checkout of the repository. |

## Guest and host in one sentence

The recompiled game is the guest; the C++ port, SDL, runtime, RT64, and files
are the host. A safe change says which side owns each piece of state.

## Memory and byte order

The N64 stores multi-byte values in a layout that is not the same as a normal
PC byte array. Generated code uses the N64Recomp MEM_W, MEM_H, and MEM_B
helpers. Hand-written hooks must use those helpers when they access guest
memory. A direct C pointer or a guessed byte order can silently corrupt the
game.
