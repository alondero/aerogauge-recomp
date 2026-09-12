# AeroGauge derived ROM/RAM map

Consolidated reference of every game-internal address, table, and protocol this
project has derived so far. Each block names how it was derived; per CLAUDE.md,
**verify from the ROM bytes before building on any entry** (the tools in
`tools/rom/` make that a one-liner — `disasm.py`, `callers.py`, `find_refs.py`).
Authoritative per-function evidence for libultra routing lives as comments in
`scripts/gen_syms_toml.py` (LIBULTRA_NAMES); this file covers *game* code.

## Championship replay results (2026-09-10)

Derived by finding the ROM string `ROUND  RANK  POINT` at ROM `0x97A4C`
(`0x80096E4C`), tracing its caller, and disassembling the draw chain:

- `func_800191FC` calls `func_8001C030` at `0x800192C0` when race flags
  `0x8013FC8C & 0x1000` and mode byte `0x8013FF90` is 9 or 10.
- `func_8001C030` selects pages using race frame counter / 300. It draws
  per-round statistics via `func_8001C29C`, then the final points table via
  `func_8001C758`. These are overlays in the race replay, so the HUD's race
  scene/phase gate does not exclude them.
- Entry receives a DL cursor holder in `$a0`. The function copies its cursor
  into `sp+0x58` and commits it back at `0x8001C24C`. All branches converge
  before `0x8001C250`; the stack is restored later at `0x8001C258`.
- Widescreen ownership hooks bracket `0x8001C030..0x8001C250`, reading
  `MEM_W(0, ctx->r4)` on entry and `MEM_W(0x58, ctx->r29)` at the end.
  This protects the complete page, including its original scissor, without
  moving the ROM's local cursor or changing page timing/input handling.

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

### Turbo / Boost Start — corrected 2026-09-03 (src/aero_turbo_boost.c)

The earlier investigation misidentified two unrelated paths as the player mechanics:
`func_80016890`'s START check skips the race introduction/countdown, and the uncalled
`func_8000877C` reads controller 2 for a camera/debug helper. A live breakpoint also
showed zero calls to `func_80012EFC` during a normal warped race. Do not use those paths
for player turbo or Boost Start.

The real P1 input chain is:

| Function/address | Role |
|---|---|
| `func_8005C750` | P1 car-input callback; reads controller index 0 and calls `func_8005C9E4` |
| `func_8005C9E4` | maps the user's configured physical buttons to semantic controls |
| `func_8005C750 + 0x58` (`0x8005C7A8`) | safe post-map hook seam; `$s0`/`ctx->r16` is the P1 car |
| car `+0x40`, high byte | semantic accelerator `0x80`, brake `0x40`, drift `0x20` |
| car `+0x40`, bits 6..11 | horizontal control encoded as `turn + 20` (`-20..20`) |
| car `+0x57/+0x58/+0x59/+0x5A` | current accelerator, accelerator counter, drift counter, current brake |
| car `+0x34 & 0x2000` | turbo-ready drift state observed immediately before the successful release/re-press |
| car `+0x34 & 0x20000000` just after GO | ROM-owned state bit that signals Boost Start in the launch window; it is reused by later driving states |
| car `+0x55` | ROM-owned turbo timer; copies craft setting `+0x28` (`10` for the default craft) when the maneuver succeeds |
| `0x8010CAB0 + port*8` | raw per-port controller block (func_800092C4 repacks osContGetReadData here via func_80009494); buttons u16 at +0x2, i.e. P1 `0x8010CAB2`, returned by func_80009438 |

The opt-in race assist uses a dedicated physical button for Turbo: the raw N64 R
bit of P1's controller block (`0x8010CAB2`), hooked only in the P1 callback
func_8005C750 (P2's func_8005C878 carries no hook, so 2P races are unchanged).
It is never keyed to a semantic action, so the configured drift button keeps its
meaning and drifting is never consumed — outside the countdown Boost Start the
hook only reads car+0x40 and never rewrites it. A rising edge awards the boost
without steering, drift readiness, or accelerator release/re-press. Held buttons
do not repeat; presses during active turbo are consumed, not queued.
Countdown/disabled input also updates the edge latch to avoid a press at GO or
when toggling the option. Because Turbo reads the physical bit, remapping an
action onto R makes that press do both (default bindings are assumed).

R is not part of the racing control scheme (its documented in-game use is the
car-colour selector, a menu screen). Audited pad readers: P1 in-race vehicle
input (func_8005C750 / func_8005C878) reaches the pad only through func_8005C9E4's
configured accel/brake/drift masks; the race runner's pad-snapshot reads
(func_80015FD0 at 0x8001604C/0x8001613C/0x800161E8/0x80016214, func_80016464,
func_80016890) test only START (0x1000); the one hardcoded R (0x10) test,
func_800277E0 at 0x80027818, is on controller index 1 (P2, a cheat-code
combination).

ROM-byte disassembly of `0x800584B8..0x800584D4` identifies the award:
clear pending flag `0x1000`, set car `+0x56 = 5`, and copy the byte at
`*(car+0x20)+0x28` (craft settings) to car `+0x55`. The assist mirrors these
writes, rather than hardcoding a duration of 10. At `0x8005AE00..0x8005AE68`,
the original update decrements that timer, multiplies thrust by four, adds
craft setting `+0x20` to heat at car `+0x22C`, and cancels turbo and sets heat
to 500 when heat exceeds 80. This update remains unmodified.

ROM-byte disassembly of the turbo state gate at `0x800583EC` and the update's
80.0 constant load at `0x8005AE28`/comparison at `0x8005AE4C` establishes the
engine overheat limit: car heat `+0x22C` is overheated above 80
(`0x42A00000`). The HUD separately clamps its display at 100 (`0x42C80000`)
in `0x80010628..0x80010664`; that cosmetic limit is not the boost eligibility
limit. Easy Turbo rejects new awards at heat >= 80, including the cooldown from
500, because the native update applies turbo thrust before cancelling an
overheated turbo. Rejected presses are consumed; after cooling below 80 a fresh
press can boost. Invalid float values such as NaN are rejected fail-safe.

The countdown step is the word at `0x8013FF38` (`0/1/2/3`) and the race phase is
`0x8013FF88` (`1` setup, `2` countdown, `3` racing). A live assisted-vs-original
comparison showed that releasing brake at step 2 was too early and produced byte-for-byte
identical car state to accelerator alone. The successful timing holds semantic brake
alongside the player's accelerator through step 2 (SET), then releases it at step 3.
Within six P1 input callbacks the ROM sets car flag `0x20000000` and the craft pulls
decisively ahead of the unassisted control run; accelerator alone did not set it during
the first 24 callbacks. The bit is reused during later hard steering, so the automated
harness only classifies it as Boost Start during the first 12 callbacks after GO. The
same post-map seam awards the dedicated-button Turbo. Boost Start still uses the
mapped accelerator/brake semantics; the ROM still awards Boost Start and updates
both boosts. The
setting is disabled by default and persisted in `enhancements.json` (or overridden at
process start with `AERO_EASY_TURBO=1`); it is deliberately not part of `graphics.json`.

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
- **Bikini rotating-tunnel cap** (2026-09-12, saved-RDRAM section-table attribution
  and single-DL removal): track 1, zone 23, entry 0, `DL 0x80396070`, `hw4=0`,
  `hw6=0`, bounds x=-1747..-1027, y=-228..456, z=8112..8280. Full-track merging
  puts its metal panel across the rotating tunnel from section 126 / zone 20;
  the original zone-20 PVS `[20,21,19]` excludes it. Keep this DL PVS-gated.
  Verified the rebuilt executable at infinite draw distance: merged section count
  drops 82→81, the captured frame omits `0x80396070`, and the corridor and rotating
  tunnel (`0x803952A8`, `0x803A0308`) remain visible.
  For saved-state comparisons, `func_8000631C` calls the section registrar only
  when `func_80006704` reports a section crossing (ROM 0x80006354..0x80006390),
  while the object registrar runs every frame. A parked restore retains its saved
  section nodes, so changing visibility options alone is not a valid A/B test.
  A diagnostic copy set craft `0x801511E0` to preceding section 125 and its +8
  pointer to `*(craft+0)+125*0x8C`; the ROM returned it to section 126 and rebuilt
  the lists without moving the craft. The original user save was left untouched.
- The far plane: ONE global far=500 (near=5, fovy=55) passed to guPerspectiveF
  (ROM func at `0x8006BA60`) by all 11 projection sites; race cameras
  `0x8001F59C`/`0x80020748` read far from camera struct +0x10. No CPU distance culling.
- RT64's F3DEX `G_CULLDL` (0xBE) is a TODO no-op, so RSP chunk culls never fire in the port.
- RDRAM `0x80700000+` is safe scratch (game never allocates above ~`0x803C87xx`).

## Race announcement draw ownership (2026-09-08)

Derived by disassembling the USA ROM with `tools/rom/disasm.py` and decoding its
font descriptors and strings. The widescreen pass records these original DL ranges
and keeps their texrects centred as a group:

- `func_8001AB94` draws the central sprite at HUD object +0xFC (countdown/announcements).
  Entry cursor is `*a0`; at `0x8001AC54` the final cursor remains in `sp+0x3C`.
- `func_8001AC64` draws `WRONG WAY` (string `0x80096C98`, font `0x8008C540`)
  plus the direction sprite. Entry `*a0`; exit `0x8001ADB0`, cursor `sp+0x3C`.
- `func_80019508` selects `TIME OVER`, `GAME OVER`, `RETIRE`, or `TIME UP`
  (`0x80096BA8..6BC8`). Entry `*a0`; exit `0x8001961C`, cursor `sp+0x24`.
- Mixed timer handler `func_8001A020`: only its announcement tails are centred.
  Normal lap announcements begin at the shared `0x8001A390` (the zero-lap path
  branches directly here from `0x8001A258`, skipping `A38C`); time-trial lap/time
  announcements begin at `0x8001A5A0`. Both end at `0x8001A724`, cursor `sp+0x94`.
  Steady timer digits/labels are outside these ranges and retain their right pin.
- `func_8001024C` passes notification origin `(116,131)` to `func_80010C88`,
  stored at HUD object +0x130/+0x132. `FINAL LAP` is string `0x80096C28`, drawn
  by `func_8001F790` with font `0x8008C31C` (14x10 glyphs). Individual letters
  cross the x=168 right-pin threshold, explaining the split. `N LAPS LEFT` uses
  three separate string calls, so grouping each string alone is insufficient.

## Race intro drawing (2026-09-09)

Derived with `tools/rom/disasm.py` against the USA ROM; hooks live in
`src/aero_race_intro.c` and `scripts/gen_syms_toml.py`.

- `func_800191FC` calls the solid-colour texrect emitter `func_80020EFC` at
  `0x800193C4` / `0x800194B8`, with bounds (0,8)-(319,231). Post-call hooks at
  `0x800193CC` / `0x800194C0` use the local cursor holder `sp+0x34`.
  The final 32 bytes are E4/B4/B3 plus pipe sync. Fade channels at object
  +0x244/+0x245 still decrement in the ROM after the draw hooks.
- `func_8000D708` owns the pre-race ticker. At `0x8000D860`, a2 holds
  `max(230 - 3*frame, -500)`, the shared text origin. Its cursor holder is
  `sp+0x2C`. All exits reach `0x8000D968`, which stores t6 through t8:
  a closing hook must refresh the register value after mutating the local holder.
- `func_8000D97C` emits the banner (1,180)-(303,203). At `0x8000DBB0`,
  `sp+0x64` holds its final cursor, after E4/B4/B3 and pipe sync.
- `func_8001F998` emits individual glyphs. At `0x80020174`, v0=0 means
  missing glyph/no draw. Otherwise `sp+0xE0` holds the caller's cursor-holder
  pointer and `sp+0xE4` the original signed x. The final 32 bytes have the same
  rectangle/sync layout. Using that signed x avoids the original 12-bit wrap.

Intro hooks bracket only these draws with a full-output signed scissor and
`G_EX_ASPECT_ADJUST`; rectangle triplets are replaced in place with extended
signed-coordinate triplets. The fade intentionally changes the original y range
(8..231) to 0..240 so the overlay covers every output row; the eight top and nine
bottom rows that were uncovered in the ROM are therefore covered on widescreen.
Added commands are bounded at 56 bytes per bracket
(ticker plus at most two fades = 168 bytes), including the extended-GBI enable
command required before the steady HUD has run. Text keeps its original glyph size
and spacing; its origin enters at the right output edge and travels proportionally
to output width while the ROM retains its sequence/fade timing. `AERO_WS_INTRO=0`
disables the change. 4:3/non-Expand output follows the original drawing path.

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


## Controller Pak and haptics (2026-09-12)

ROM-byte disassembly confirmed the SDK block-device seam: `0x800742F0` is
`__osPfsGetStatus`, `0x80075290` / `0x80077260` are `__osContRamRead/Write`
(32-byte blocks; write force is the fifth argument). `0x8006B440` InitPak and
its filesystem now remain recompiled. The host supplies a persistent 32 KiB MPK.
Allocate/Find/ReadWrite/FreeBlocks are `0x8006F040`, `0x8006CDE0`, `0x8006EC1C`,
`0x8006D0F0`; `0x8006CFA0` is NumFiles, with three arguments.

The motor test `0x80063930` checks D-pad input and has no discovered callers;
its presence does not establish native race rumble. User-requested race feedback
instead observes actual collision damage at craft+0x24 at `0x80058AD8`, where all
collision branches converge before damage accumulation. Turbo uses craft+0x55.
Craft+4 == `0x8005C750` selects P1. Both hooks are read-only; no physics is changed.
See [controller-accessories.md](controller-accessories.md) for the byte evidence,
storage/error behavior, EEPROM shutdown barrier and verification coverage.
