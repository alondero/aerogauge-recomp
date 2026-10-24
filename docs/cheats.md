# Cheats

Open **Settings > Cheats** during play or from the launcher. Changes apply
immediately, without Apply. Both cheats affect Player 1 and start off each time
the application launches. They are not stored in a configuration file.

| Toggle | Effect | When disabled |
| --- | --- | --- |
| Infinite Damage (Player 1) | Clears accumulated damage and its percentage at the next vehicle update, and prevents new collision damage. Does not revive an already destroyed car. | Damage accumulates normally from zero. Previous damage is not restored. |
| Always First Position (Player 1) | Moves Player 1 to the front of each standings update, retaining all opponents and their relative order. You must still drive the laps and finish. | The next standings update uses normal race progress. Completed results are not undone. |

These cheats are experimental. ROM-backed tests cover USA and Japan Rev A;
interactive race completion and Android device behavior need separate testing.
Normal game saves can retain results earned with cheats. Enabling either cheat
after a race has ended does not restart or retroactively change that race.
Player 2, AI, title demos and replay vehicles do not receive damage protection.
The standings cheat can put Player 2 behind Player 1 in a two-player race.

## References and implementation choice

The UI follows the session-only catalog and atomic flags in Automobili
Lamborghini Recompiled's src/lambo_cheats.h and src/ui/lambo_frontend_settings.cpp.
Its [cheat documentation](https://github.com/alondero/automobililamborghini-recomp/blob/main/docs/cheats.md)
describes the same immediate-toggle workflow. Shipwright's
[FreezeTime hook](https://github.com/HarbourMasters/Shipwright/blob/develop/soh/soh/Enhancements/Cheats/FreezeTime.cpp)
also scopes cheat work to a game update and checks the live game context.
Shipwright is a decompilation port; it is a hook-lifecycle reference, not an
address or persistence model for this port. A Zelda64Recomp fork's
[patches/cheats.c](https://github.com/sonicdcer/Zelda64Recomp/blob/dev/patches/cheats.c)
contains only a commented-out magic override and supplies no active cheat UI
to copy.

The requested [Libretro v1.9.13 USA list](https://git.libretro.com/libretro-assets/libretro-database/-/blob/v1.9.13/cht/Nintendo%20-%20Nintendo%2064/AeroGauge%20(USA).cht?ref_type=tags)
was read through its [tagged GitHub mirror](https://github.com/libretro/libretro-database/blob/v1.9.13/cht/Nintendo%20-%20Nintendo%2064/AeroGauge%20(USA).cht)
because the GitLab page was unavailable. The list is an address lead, not an
import format:

- Infinite Damage P1 zeros four halfwords at `0x8013FFD8..0x8013FFDE`.
  These cover the P1 vehicle's accumulated damage and percentage, two floats
  at vehicle offsets `+0x28` and `+0x2C`.
- Always First Position P1 writes one byte to `0x8013FC91`. The USA ROM reads
  this as the **racer count**, including at `0x8001CF94`, and loops over it
  when updating vehicles and standings. Reducing it to one removes opponents
  from those loops. The native cheat instead reorders the full standings.
- No timer, unlock, record-table, input-activator, value-editor, P2 damage, or
  executable-memory patch cheats are imported.

## Game-thread boundary

ROM identity is checked before translation: USA SHA-256
`2cc529109b11b00289d87f693a40591ef260d1dc7c1129113966ba6ddb1be4a5`,
Japan Rev A SHA-256
`1abff752862450bbfd3cfbb75b1c217daa57f524bee65f14ae436519a615368a`.
All addresses below are KSEG0 guest addresses. MEM helpers handle the N64 byte
order in the runtime's word-swapped 8 MiB RDRAM. Only game-thread hooks access
guest memory; UI callbacks publish independent relaxed atomic booleans.
These booleans publish no other data, need no SDL action, and perform no I/O.

| Contract | USA | Japan Rev A |
| --- | --- | --- |
| Vehicle damage updater | `func_80058114` | `jp_func_80058308` |
| Damage hook, after f12 loads collision damage and before the zero test | `0x80058ADC` | `0x80058D30` |
| Standings updater | `func_8001CF48` | `jp_func_8001D720` |
| Standings hook, after sorting and before rank publication | `0x8001D120` | `0x8001D900` |
| Race structure; racer count is unsigned byte at +9 | `0x8013FC88` | `0x8013CD08` |
| First vehicle; subsequent records use the listed stride | `0x8013FFB0`, stride `0x20A0` | `0x8013D030`, stride `0x2094` |
| Sorted standings table | `0x8016C478` | `0x80169478` |

Damage protection checks s0's aligned RDRAM bounds through `+0x2C` and the
32-bit input callback at `+4`, using aero_player.h to accept only P1. It clears
the collision value in f12 and two guest binary32 words: accumulated damage in
craft units at `+0x28` and percentage at `+0x2C`. The ROM's zero branch then
skips accumulation and its destruction threshold. Existing destruction flags
are retained. The collision word at `+0x24`, physics, and haptic observation
remain intact; haptics run before the protection hook.

The race owns up to ten standings entries of 12 bytes each: a 32-bit vehicle
pointer, lap/sector sorting keys, and a binary32 projected-distance key. The
hook validates the complete vehicle permutation and the racer count before
any write, then rotates P1's entire entry to the front. The ROM assigns unique
one-based byte ranks at vehicle `+8`, then copies them into each racer's
`0x54`-byte race record at `race + slot*0x54 + 0x58`. Laps, sorting keys, timers,
racer count and finish gates are retained. Missing P1 input ownership, malformed
counts or invalid/duplicate pointers cause no writes. No pointers or guest
snapshots are retained across calls, race changes, or developer state loads.

In the USA ROM, the result handler func_800178F0 reads `race+0x58` at
`0x800179A8` for the result summary and at `0x80017B08` for progression
decisions. The finish transition through func_8001DF40 retains the input
callback at vehicle `+4`. This supports result propagation beyond the HUD,
although the tests below stop at standings publication rather than executing
the result handler or a complete race.

The source replacements for these transitional bridges would be named vehicle
damage and standings-publication functions once those routines are decompiled.
Hook inputs live in scripts/gen_syms_toml.py and scripts/japan_rev_a.py;
generated TOMLs and translated code must be regenerated, never hand-edited.

## Verification

Build `test_cheats_us`, `test_cheats_jp`, and `test_frontend_settings`, then run:

~~~text
ctest --test-dir build -R "^(cheats_us|cheats_jp|frontend_settings|frontend_settings_jp)$" --output-on-failure
~~~

The cheat tests extract the original damage block and complete standings
updater from freshly generated code, retaining the injected hooks. Only the
track-space distance projection is stubbed. Tests cover normal damage and
destruction thresholds, repair, zero/lethal collisions, enable/disable, P2/AI
isolation, pointer validation, every racer count from one to ten, the real ROM
sort and rank publication, full entry preservation, opponent ordering, normal
ranking after disable, and demo/replay exclusion. Extraction fails if hooks
are missing or the standings call boundary changes. Frontend tests cover
immediate toggles, external refresh, initial defaults despite a stale
cheats.json, and absence of configuration writes.

Interactive acceptance: toggle each option mid-race, pause/resume, complete a
lap and race, return to menus, start another race, and restart the application.
Check collision response/rumble, all opponents, split-screen standings and
normal behavior after disabling. Repeat for both ROMs and on Android. A passing
synthetic-RAM test does not establish those device or visual outcomes.
