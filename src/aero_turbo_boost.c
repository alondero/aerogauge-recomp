// One-button Turbo + Boost Start (QoL; see docs/notes/rom-map.md "Turbo / boost").
//
// AeroGauge's two speed tricks are notoriously finicky to pull off manually:
//   - TURBO: hold Z (drift) + steer, release Z AND A, re-press A while still
//     drifting. The ROM's real condition is drift-temperature driven, not an
//     edge detect: func_8000877C (0x8000877C) reads controller-1 buttons from
//     the osCont read-back array 0x8011CAB2 (via func_80009438(1)); Z+A held
//     heats the temperature floats at 0x80081FB0..0x80081FC4, which keeps the
//     per-frame boost dispatcher func_80012EFC (0x80012EFC) re-arming the
//     turbo timers (car slot +0x2B8 = 7, +0x2BA = 10; launch vector +0x2C0..
//     +0x2C8) through func_80012FDC, and func_80013020 fires velocity + flames
//     while slot+0x2B8 > 0.
//   - BOOST START: hold B (brake) + A (accel) through the countdown; the GO
//     check in func_80016890 (0x80016890) reads the P1 pad latch 0x8013FF70 at
//     countdown elapsed == 0xC3 (195) and arms the same boost path when the
//     combo is held (tests B 0x4000 and START 0x1000; the flag lands in the
//     race block 0x8013FC88 + 0x4).
//
// This hook makes ONE held button do both: while the countdown is still
// running (race block elapsed <= 0xC3, including the GO frame itself where the
// 0xC3 boost-start check runs) it ORs B|A into both P1 pad sources; once the
// race is running (elapsed > 0xC3) it ORs Z|A so the drift/turbo stays alive
// and re-fires. The game's own state machines see exactly the combo they expect
// for each phase -- nothing is invented. Toggle via graphics.json
// "easy_turbo_boost" (default true) or AERO_EASY_TURBO=1/0 (see
// aero_config.cpp); when off this hook is a no-op.
//
// Hooked at the entry of func_80015FD0 (the RACE scene runner, case 5 of the
// scene table at 0x800969C0 -- runs every frame during a race, silent in every
// other scene) via the [[patches.hook]] block in scripts/gen_syms_toml.py
// PATCH_BLOCKS. The hook runs before the runner reads the pads, so both pad
// sources are rewritten before any consumer.
#include <stdint.h>

#include "recomp.h"

// The race runner (func_80015FD0) latches P1/P2 pads here every frame
// (written at 0x80015FEC / 0x80016000 from func_80009460).
#define PAD_P1  0x8013FF70u  // u16 P1 buttons
#define PAD_P2  0x8013FF72u  // u16 P2 buttons

// Controller-1 osCont read-back buttons (func_80009438(1) reads 0x8011CAB2).
// The temperature/turbo handler func_8000877C reads THIS, not PAD_P1, so both
// sources must be rewritten for the synthesized input to reach every consumer.
#define CTRL1_BUTTONS 0x8011CAB2u

// Race block + countdown elapsed (func_80016890): u16 0..0xC3 (195 = GO).
#define RACE_BLK      0x8013FC88u
#define RACE_ELAPSED  (RACE_BLK + 0x2C0u)
#define COUNTDOWN_GO  0xC3u

// N64 OSContPad button bits (main.cpp enum; libultra controller.h).
#define N64_A  0x8000u
#define N64_B  0x4000u
#define N64_Z  0x2000u

// Set by aero_config (atomic bool + AERO_EASY_TURBO env override); the
// extern "C" bridge lives in aero_config.cpp. 1 = enhancement active.
extern int aero_easy_turbo_enabled(void);

// Per-frame tick, injected at the entry of the race runner func_80015FD0.
void aero_turbo_boost_tick(uint8_t* rdram, recomp_context* ctx) {
    (void)ctx;
    if (!aero_easy_turbo_enabled()) return;

    // Countdown still running (elapsed 0..0xC3, INCLUDING the GO frame where
    // the 0xC3 boost-start check runs after this hook) => hold B+A for the
    // boost start. Race running (elapsed > 0xC3 after GO) => hold Z+A so the
    // drift temperature stays up and the turbo keeps firing. Only P1 is
    // touched; 2P/4P keep the original mechanic.
    uint16_t add = ((uint16_t)MEM_HU(0, (gpr)(int32_t)RACE_ELAPSED) <= COUNTDOWN_GO)
                       ? (N64_B | N64_A)
                       : (N64_Z | N64_A);

    uint16_t p1 = (uint16_t)MEM_HU(0, (gpr)(int32_t)PAD_P1);
    MEM_H(0, (gpr)(int32_t)PAD_P1) = (int16_t)(p1 | add);

    uint16_t c1 = (uint16_t)MEM_HU(0, (gpr)(int32_t)CTRL1_BUTTONS);
    MEM_H(0, (gpr)(int32_t)CTRL1_BUTTONS) = (int16_t)(c1 | add);
}
