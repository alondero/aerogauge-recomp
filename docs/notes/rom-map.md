# AeroGauge derived ROM/RAM map

Consolidated reference of every game-internal address, table, and protocol this
project has derived so far. Each block names how it was derived; per CLAUDE.md,
**verify from the ROM bytes before building on any entry** (the tools in
`tools/rom/` make that a one-liner — `disasm.py`, `callers.py`, `find_refs.py`).
Authoritative per-function evidence for libultra routing lives as comments in
`scripts/gen_syms_toml.py` (LIBULTRA_NAMES); this file covers *game* code.

## ROM layout

| Fact | Value |
|---|---|
| Entrypoint / vram base | `0x80000400` (rom = vram − 0x80000400 + 0x1000) |
| CPU .text | ROM `0x1000..0x7F4C0`, contiguous; tail ~0x100 = CP0 exception handler |
| Boot | entry trampoline clears DMA table, `jr $t2` → `0x800653F0` (boot body) |
| aspMain audio ucode | ROM `0x7F330` (0xE1C), byte-identical to Lamborghini's SDK mixer; ucode_data ROM `0xC8610`; rspboot `0x7CA00` |
| ROM identity | code NAGE, 8 MiB, XXH3-64 `0x89ea0690f3e22201`, internal name "AEROGAUGE" |
| Track names | ROM `0x960A0`: CANYON RUSH, BIKINI ISLAND, CHINATOWN, NEO ARENA, CHINATOWN JAM, NEO SPEED WAY (2/4 share geometry) |
| Ghost/record blobs | ROM `0x48e7f0..0x49fad0` (replay input streams, NOT music) |
| Sequenced-music bank/samples | bank "B1" ROM `0x49fad0`, samples `0x4a30f0+`; songs at `0x596000..0x5A0400` |
| Stream/stinger clips | 43 short ADPCM clips ROM `0x5a2840..0x64a8c0`; config header `0x5a0490`; ROM is 0xFF-padding after `0x64a8c0` |

## Scene manager (top-level game state) — live-derived 2026-07-12 (warp PR #4; header comment in `src/aero_warp.c` is authoritative)

| Address | Meaning |
|---|---|
| `0x8013FF80` | current scene word |
| `0x8013FF84` | scene REQUEST (driver copies req → cur) |
| `0x8013FF88` | scene-local phase: 6=fresh entry; race 1=load, 2=intro/countdown, 3=playing, 7=exit |
| `0x8013FF8C` | runner transition word = REQUESTED phase (`func_800162C0` copies it into 0x8013FF88; transition tables `0x800969F0`/`0x80096A10`); storing **7** during a race triggers the runner's own ~60-frame teardown — the ONLY safe way out of a live race |
| `0x8013FF90` | race-param block (below) |
| `0x8013FF44` | current-course table ptr (BSS; race init repoints to `0x8008B290 + track*0x14` word[2]); never cleared by ROM |
| `0x8008F290` | game-mode word 0–5 (==3 uses alt track table `0x80081F48`) |
| `0x8013FF70/72` | pad snapshot halfwords (written by race runner via 0x80009460) |

- Driver `func_80015C8C` runs every frame (thread entry `func_800657B0` → main loop
  `func_800658FC` → driver): copies request→current, calls `func_80015F2C`, jump-tables
  (`0x800969C0`, 10 scenes) to the scene runner.
- Scenes: 0/1 boot logo, 9 logo interstitial, 2 attract, 3 title, 4 menu (ALL
  sub-screens), 5 race, 7? results. **`func_80015FD0` is only the RACE runner (case 5)**
  — hooks there are silent in menus.
- Countdown (phase 2): step word = race block+0x2B0 = `0x8013FF38`, stepped 1/2/3 at
  elapsed 105/150/195 by `func_80016890`; step 2 = full HUD at steady coords (~45
  frames before GO); step 3 requests phase 3.
- Menu RACE confirm block `0x80042304..18` in `func_80041D2C`: commits track, sets
  `0x8008EE9C=1`, `0x8008F294=0`, `sh 0 → 0x80109BDC`, requests scene 5.

### Race-param block `0x8013FF90` (init `func_80015EC0`, boot only)

| Offset | Field |
|---|---|
| +0x00 u8 | mode: 4 = menu race, 7 = attract demo, 0/6 other confirms |
| +0x02 u16 | demo timeout seed (0x78 default / 0x3C demo) |
| +0x04 u8 | course GROUP → music-table row (menu confirm copies `0x8008F248`); group 0 = no music |
| +0x05/+0x06 u8 | P1/P2 craft (0–9) |
| +0x07/+0x08 u8 | duplicate-craft colour flags |
| +0x0B u8 | track (0–5) |
| +0x0C u8 | =6 menu-entry byte |
| +0x1A u8 | block-inited flag |

### Race status / clock / exit detection — derived 2026-07-17 (save-state #17)

Race-global block base `0x8013FC88`:

| Address | Field |
|---|---|
| `0x801402B4` | race STATUS word: 0 running, 1 finished (`func_8001E06C` finish-line path), 2 pause-menu quit (`func_8001745C`, pause state `0x8016C2A8` case 2), 3 finish variant, 4 time-over (`func_8001D660`) |
| `0x801402B8` | P2 status twin |
| `0x801402C4` | time-over DEADLINE for the race clock (finish sets clock+10,000,000) |
| `0x8014000C` | race CLOCK, accumulated per frame by `func_8001D660` from the timekeeper delta |
| `0x8016C4E0/E4` | u64 last frame's elapsed osGetTime delta (`func_8001D7F0`) |
| `0x8016C4F0/F4` | u64 last frame's **osGetTime() sample** — a native-clock anchor stored in guest RAM; a restored snapshot must REBASE it to the loading process's clock epoch (see `src/aero_savestate.c` k_ostime_anchors) or the first post-restore delta is garbage → instant time-over |

Exit detector `func_800169E0` (called per frame from the race chain): maps STATUS to the
requested-phase word `0x8013FF8C` — 1 → phase 5, any other nonzero → phase 6 (the ~exit
walk observed as phase 6 → scene 6 results).

### Turbo / Boost Start — derived 2026-08-30 (one-button QoL, src/aero_turbo_boost.c)

Two pad SOURCES feed the two mechanics (do not confuse them):

| Address | Source |
|---|---|
| `0x8013FF70` (u16) | P1 pad latch written every race frame by `func_80015FD0` at 0x80015FEC from `func_80009460(0)`; read by the countdown GO check (`func_80016890`, 0x80016968) |
| `0x8013FF72` (u16) | P2 twin (written at 0x80016000) |
| `0x8011CAB2` (u16) | controller-1 osCont read-back buttons (`func_80009438(1)` reads `0x8011CAB0+2`); read by the temperature/turbo handler `func_8000877C` (NOT the P1 latch!) |
| `0x8011CAB0/0x8011CAB8` | per-controller prev/edge/cur pad state (`func_80009494` writes +2 prev, +4 pressed-xor, +6 cur) |

Temperature/gauge block (global floats, `lui $at,0x8008`):
`0x80081FB0` speed gauge, `0x80081FB4`, `0x80081FB8`, `0x80081FBC`, `0x80081FC0`,
`0x80081FC4` drift heat (Z+A held raises, Z+B held lowers; written by `func_8000877C`),
`0x80081FC8` one-shot drift flag (written by `func_80008AF0` init).

Boost dispatch (the actual turbo firing):
- `func_80012EFC` (0x80012EFC) — per-frame per-car boost dispatcher; loops 5 boost
  slots stride 0x14 from the car base; per slot calls `func_80012FDC` (arm) →
  `func_80013020` (fire) while slot+0x2B8 > 0, else `func_80013428`.
- `func_80012FDC` (0x80012FDC) — ARMS the turbo: `slot+0x2B8 = 7` (turbo timer),
  `slot+0x2BA = 10` (turbo sub-timer), copies `car+0x278..0x280` (track launch
  vector) into `slot+0x2C0..0x2C8`.
- `func_80013020` (0x80013020) — FIRES the boost: applies `slot+0x2C0..0x2C8` to the
  car+0x1018 sub-object's velocity `+0x38/0x3C/0x40`, writes flame-color bytes
  `+0x34..0x37`, decrements `slot+0x2BA`, then `func_80012EFC` decrements `slot+0x2B8`.

Countdown / boost start:
- `func_80016890` (0x80016890) — countdown stepper: step word `RACE+0x2B0` (0/1/2/3=GO),
  elapsed `RACE+0x2C0` (u16, 0..0xC3=195=GO). At the 0xC3 boundary the GO check reads
  `0x8013FF70` and tests B (0x4000) + START (0x1000); on success ORs `0x4000` into the
  race-block flag `RACE+0x4` and the same `func_80012FDC` path arms the start boost.
- Car array: `0x8013FFB0` stride 0x20A0 (`func_80018C84`); car+0x2B8/0x2BA/0x2C0..0x2C8
  are the per-car boost timers/vector; car+0x278..0x280 = track launch-speed floats
  (loaded from course table `0x8009_4D68 + track*0x14`).
- The one-button QoL hook (`aero_turbo_boost_tick`, func_80015FD0 entry) ORs B|A into
  `0x8013FF70` + `0x8011CAB2` while `RACE_ELAPSED <= 0xC3` (countdown incl. GO frame),
  and Z|A once racing — synthesizing the exact combos the ROM's own checks expect.

## Music / audio — SOLVED 2026-07-16 (PR #11); all verified live in the port

Two engines; confusing them wasted sessions:

1. **Sequenced music engine** (the real music, 26 songs = the Zophar USF rip):
   song table `*(0x80109C18) = 0x800FED80`, 8-byte records (+4 ROM addr, +8 size|1).
   Client voice slots `0x80109BC8`, stride 0x20 (slot 0 = music; fields: +0 song id,
   +4 state 1=stop/2=play/3=stop-start/4=DMA-pending/0=idle, +8 player obj 0x800F7A10,
   +0x10 seq buffer 0x800F7B90, +0x14 volume 0x6E14=full, +0x1C OSIoMesg 0x800FC9B0).
   Request: `func_800005C0(song, 0)`; streams: `func_800013C0(song, slot)`.
   Per-frame service: `func_800658FC` → `func_80004A34` → `func_80000978` (osPiStartDma
   + osRecvMesg on queue `0x80109BE8`, **compares the received OSMesg to its stored
   OSIoMesg*** — the bug patch 0012 fixed: librecomp posted 0).
   Race BGM director `func_80002180` (from `func_80015FD0`, gated on scene phase):
   phase 1 arms `0x80081F28`, 30 frames later posts fade event 0x15; phase 3 posts the
   track's song via jump table `0x80094DC0` on RP_TRACK (track 0 → song 0xE). Engine
   drones: `func_80002E3C` → stream songs 3..0xC. Title music = song 0x14; boot jingle
   = stream song 36.
2. **Stream/stinger engine**: 43 short clips; descriptor
   `desc = *(*(*(0x80109B9C)+4)+0xC) + 0x10 + songId*4`; desc+0x10 ROM src, +0x14 len.
   Slot table `0x80109C28` stride 0x18. Songs 0x10..0x23 = countdown/race SFX.

**Falsified (do not resurrect):** `func_80036C54` = per-course default-record/GHOST
loader (not music); `func_80032BB0` = Controller Pak / ghost service (`func_800643E4`
= pak note read). Ghost slots `0x801AFE70 + n*0x2DE0`, resident flag `0x8019E32E`.

## 2D/HUD dispatch — live-derived (docs/notes/hud-widescreen.md has full evidence)

- Sound/scene 2D director `func_8001E8D8`: switch on scene word, jump table `0x80096F44`.
- Master 2D dispatcher `func_80022408` (once/frame, from func_8001E8D8) walks object
  lists, calls handlers **indirectly** (`jalr obj+0x104`, `jalr obj+0x34` at 0x80022644 —
  one call per GROUP of elements, 4/frame).
- **No global DL cursor** (key difference from Lamborghini): dispatchers keep the cursor
  in their stack frame and pass a POINTER TO THE CURSOR-HOLDER in `$a0`; in steady race
  the holder is the fixed global `0x8016C508` (`cursor = MEM_W(0, holder)`). The DL is
  double-buffered (`0x8018xxxx` / `0x80173xxx`) — never hardcode DL addresses.
- Handlers (steady 1P race): dial-ring `func_80018CF0`; recursive scene-graph walker
  (needle etc.) `func_800226AC`; mixed digits/TEMP/GLPS `func_80018EA0` chain; low-level
  texrect emitter `func_80019D0C`; DAMAGE `func_8003A190`. Literal-texrect emitters:
  `func_8002F994`, `func_8003B398`, `func_80049E34`.

## Course zone/visibility model — derived 2026-07-16 (full-track PR #10; `src/aero_full_track.cpp`)

- Course row `0x8008B290 + 0x14*track` (track byte `0x8013FF9B`): word[0]
  section→zone byte map (u16 craft+4 section index), word[1] zone-visibility 3
  bytes/zone (hand-authored PVS; its lbu-vs-(−1) compare is a ROM bug, never matches),
  word[2]=`*(0x8013FF44)` zone→object lists (0x28 stride, callback +0x20 = one-shot
  node initialiser, hw +0x26), word[+0x10] zone→section-DL groups (8-byte {dl,hw4,hw6}).
  Zone count = (row[2]−row[+0x10])/4.
- Registrars run per frame per viewport: `func_80007150` (sections → craft+0xC4/+0x2BE4),
  `func_80007310` (objects → +0x5704/+0x8224/+0xAD44 by type nibble). Arena placers
  `func_800077B4`/`func_8000791C`: node = list + counter*0xB8, fixed 48-slot arenas
  (slot 0 sentinel, active chain via node+0xA4); init loop `0x80006590`, handler tables
  `0x80095118/20/28`, node init `func_800204E0`.
- **Section-entry hw4/hw6 semantics** (derived 2026-07-18, background-clipping fix):
  registrar helper `func_800077B4` maps hw4's low nibble through jump table `0x800951B8`
  to a per-type render-flag halfword (0x18D0/0x18E1..E4) stored at node+6; **hw4 bit
  0x10 ORs 0x100 into that flag** and in practice marks enclosed-shell geometry the
  artists rely on the PVS to hide (Bikini Island zone 21's sealed finish corridor +
  its end-cap plane at z=8112 — drawn full-track it walls off the track right after
  the start line). hw6 indexes the constant table `0x8008B494` (3 entries, identity
  0/1/2 = environment channel); the byte lands at node+0xA and selects the per-channel
  lighting/fog set that `func_80006888` recomputes each frame into objects at
  `0x8019DDF0` (per-track 9-byte config rows at `0x8008B3C8`, consumed by
  `func_80017EE0` at race init). Bikini zones 13/14 carry hw6 1/2 = tunnel env variants.
- **Bikini PVS-gated authoring exception** (saved-state frame-DL runtime bisection,
  2026-08-29): internal track 1, zone 13, section entry 11 (`DL 0x803903B8` in the
  captured load) is an unflagged three-triangle rock wedge spanning roughly
  x=-6486..-5250, z=7151..7954. The zone-15 PVS excludes zone 13; merging this entry
  into the always-visible `(hw4=0, hw6=2)` bucket makes it cross the tunnel road.
  Treat it like `hw4 & 0x10` and register it only when its zone is in the faithful PVS.
- **Draw-list layer order** (built by `func_800187A8` into renderctx+0x1B8, from
  `func_80016B7C`): bg (craft+0xC) → map opa (+0xC4) → obj opa (+0x5704) → obj dec
  (+0x8224) → map xlu (+0x2BE4) → obj xlu (+0xAD44). The list sentinels' node+0 tag
  pointers are the ASCII labels at `0x80095100+` ("bg", "map opa", "map xlu",
  "obj opa", "obj dec", "obj xlu") — handy for locating craft arenas in RDRAM dumps.
- The far plane: ONE global far=500 (near=5, fovy=55) passed to guPerspectiveF
  (ROM func at `0x8006BA60`) by all 11 projection sites; race cameras
  `0x8001F59C`/`0x80020748` read far from camera struct +0x10. No CPU distance culling.
- RT64's F3DEX `G_CULLDL` (0xBE) is a TODO no-op, so RSP chunk culls never fire in the port.
- RDRAM `0x80700000+` is safe scratch (game never allocates above ~`0x803C87xx`).

## Key libultra globals (routing map lives in gen_syms_toml.py comments)

| Global | Address |
|---|---|
| __osThreadTail / __osRunQueue / __osActiveQueue / __osRunningThread | `0x80094870/78/7C/80` |
| __osEventStateTab | `0x801AAB10` |
| __osViCurr/Next ptrs; VI contexts; VI modes PAL/MPAL/NTSC | `0x80094C50/54`; `0x80094BF0`; `0x80094CA0/4CF0/4D40` |
| PIF controller buffer (__osContLastCmd / __osMaxControllers) | `0x801BAB90` (`0x801BABD0/D1`) |
| pfs/status buffer; __osPiDevMgr flag | `0x801BD350`; `0x80094840` |
| __osTimerList; __osCurrentTime; __osBaseCounter; osClockRate | `0x80094BE0`; `0x801BD330`; `0x801BD338`; `0x80094828` |

Note: AeroGauge game code never touches the private VI globals — the native VI manager
owns the swap path outright (Lambo's promote_vi_context bridge was retired).

Do NOT add to INDIRECT_STARTS: `0x800708A0/0x800708B0` ($k0 exception-handler entries),
`0x8007BED0` (exception vector blob, memcpy source only).
