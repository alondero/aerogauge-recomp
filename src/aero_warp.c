// Developer warp menu (issue #3; survey pattern A13 — Banjo warps_table, Dr Mario
// scene_table; ported from the Lamborghini repo's #12/PR#48 after re-deriving every
// address from THIS ROM).
//
// AeroGauge funnels every race start through its scene manager: a current/target
// scene-id pair at 0x8013FF80/0x8013FF84 (race scene = 5, menu = 4, title = 3,
// attract = 2) driven by the top-level per-frame scene driver func_80015C8C
// (request->current copy, then a jump table over the 10 scene runners), and a
// race-parameter block at 0x8013FF90 (byte layout below, defaults from the
// initializer func_80015EC0). The menu flow for game mode 0 (the word at 0x8008F290) is:
//   - menu-scene entry (dispatcher 0x80057308, case mode 0): 0x8013FF90 = 4 and
//     0x8013FF9C = 6 (writers 0x80057338 / 0x8003EFA8);
//   - craft select writes P1 craft to 0x8013FF95 (+ duplicate-craft colour flag
//     0x8013FF97), track select keeps its cursor live in 0x8013FF9B;
//   - the RACE confirm (func_80041D2C, launch block 0x8004236C..0x80042418):
//     request scene 5, sw 1 -> 0x8008EE9C, clear go-latches 0x8008EF50/54,
//     sb 1 -> 0x8008EF58, sw 0 -> 0x8008F294, sh 0 -> 0x80109BDC, and commit
//     sw table[track] -> 0x8008B318 + track*20 (table 0x80081F30; the 0x80081F48
//     variant is only used when game mode == 3).
// The warp performs exactly those stores — the scene manager's own transition
// (fade, load, phase walk 1->2->3) does everything else, so nothing about the
// race is invented. The attract demo (launcher 0x8000DFC8) is the same protocol
// with mode byte 7, which is why warping out of the attract race also works.
//
// Trigger paths (both consumed on the game thread by aero_warp_tick, hooked at
// the entry of func_80015FD0 via the [[patches.hook]] block in
// scripts/gen_syms_toml.py PATCH_BLOCKS):
//   - F1..F6 (SDL keyboard, published from input_sample in main.cpp): warp to
//     that track as a 1-player game-mode-0 race with the current craft.
//   - AERO_WARP=track[:craft] (track 1-6, craft 1-10; 1-based to match the UI):
//     one-shot boot warp, fired on the first frame the game is warpable.

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>

#include "recomp.h"

// Recompiled per-course record/ghost loader (see the preload comment at the launch
// stores): reads RP_GROUP/RP_TRACK, resolves the blob id (0x800974F0 table), copies
// the id's default-record row (0x80096B50) to 0x8008ED20 and DMAs the course's
// replay blob to 0x801B5A30. Self-guarded (blob id 0 = no-op).
void func_80036C54(uint8_t* rdram, recomp_context* ctx);

// Scene manager (all u32).
#define SCENE_CUR   0x8013FF80u
#define SCENE_REQ   0x8013FF84u
#define SCENE_PHASE 0x8013FF88u  // scene-local phase; 6 = fresh entry (boot seed, and the
                                 // value every ROM launch site fires from — race scene
                                 // dispatches on it at 0x800161C4 and walks 1->2->3)
#define SCENE_TRANS 0x8013FF8Cu  // runner transition word; the race runner's own finish/
                                 // exit detection stores 5/6/7 here (0x80016A4C..0x80016B44)
                                 // and 7 drives phase->7, the ~60-frame teardown sequence
                                 // that then requests the next scene itself (measured on the
                                 // attract demo's natural end, vi 4502->4566)
#define SCENE_RACE  5
#define SCENE_MENU  4
#define PHASE_ENTRY 6
#define TRANS_EXIT  7

// Race-parameter block (base 0x8013FF90; initializer func_80015EC0).
#define RP_MODE    0x8013FF90u  // u8: 4 = mode-0 menu race, 7 = attract demo
#define RP_GROUP   0x8013FF94u  // u8: course group; menu confirm (0x80052078, func_80051F2C)
                                //     copies the menu group global 0x8008F248 here. The record/
                                //     ghost loader func_80036C54 indexes its blob-id table
                                //     0x800974F0 with group*6+track; group 0 = every entry 0
                                //     = no blob. Real launches use 1 (tracks 0-3), 2 (4-5).
#define MENU_GROUP 0x8008F248u  // s8: the menu-side source of RP_GROUP (kept in sync)
#define RP_CRAFT1  0x8013FF95u  // u8: P1 craft 0-9 (craft-select writer 0x80045EF0)
#define RP_DUPCOL  0x8013FF97u  // u8: duplicate-craft colour flag (0 in 1P)
#define RP_TRACK   0x8013FF9Bu  // u8: track 0-5 (track-select cursor)
#define RP_MENU9C  0x8013FF9Cu  // u8: 6 after menu-scene entry (writer 0x8003EFA8)
#define RP_INITED  0x8013FFAAu  // u8: 1 once func_80015EC0 ran (block +0x1A)

// Current-course context: table pointer consumed per-frame by the race chain
// (func_80007310 reads it as its ready guard — 0 means early-out; the course
// loader re-points it at race init, e.g. 0x80356D68 for track 0). BSS, so 0 at
// every clean boot launch; a warp out of a live race must restore that zero or
// the next race's first frames walk the OLD course's tables with the NEW track
// id and crash in unloaded memory.
#define COURSE_TAB 0x8013FF44u

// Menu-launch side effects (RACE confirm, func_80041D2C).
#define GO_EE9C    0x8008EE9Cu  // u32 = 1
#define LATCH_EF50 0x8008EF50u  // u8 = 0
#define LATCH_EF54 0x8008EF54u  // u8 = 0
#define LATCH_EF58 0x8008EF58u  // u8 = 1
#define STATE_F294 0x8008F294u  // u32 = 0
#define HALF_109BDC 0x80109BDCu // u16 = 0
#define GAME_MODE  0x8008F290u  // u32: main-menu game mode; warp forges mode 0
#define TRACK_TAB  0x80081F30u  // u32[6]: per-track data ptr (game mode != 3)
#define TRACK_REC  0x8008B318u  // committed per-track slot, stride 20

// Track names as printed by the ROM (name cluster at ROM 0x960A0; index order
// corroborated by the shared course-geometry pointers of CHINATOWN/CHINATOWN JAM
// at table indexes 2/4).
static const char* const aero_scene_table[6] = {
    "CANYON RUSH", "BIKINI ISLAND", "CHINATOWN",
    "NEO ARENA",   "CHINATOWN JAM", "NEO SPEED WAY",
};

// Request packing: bit 31 = pending, bits 0-7 track (0-based), 8-15 craft.
// Published by the SDL main thread / env parse, exchanged to 0 by the game tick.
static _Atomic uint32_t g_warp_request;

// Hotkey entry point (called from main.cpp on the SDL thread). track0 = 0-based;
// craft < 0 keeps whatever the block currently holds (menu default is craft 1).
void aero_warp_request(int track0, int craft) {
    if (track0 < 0 || track0 > 5) return;
    uint32_t req = 0x80000000u | (uint32_t)(track0 & 0xFF);
    req |= ((uint32_t)(craft < 0 ? 0xFF : (craft & 0xFF)) << 8);
    atomic_store_explicit(&g_warp_request, req, memory_order_relaxed);
}

// AERO_WARP=track[:craft], both 1-based to match the game UI.
static void parse_env_request(void) {
    const char* env = getenv("AERO_WARP");
    if (env == NULL || env[0] == '\0') return;
    int track = (int)strtol(env, NULL, 10);
    int craft = 0;
    const char* c = env;
    while (*c != '\0' && *c != ':') c++;
    if (*c == ':') craft = (int)strtol(c + 1, NULL, 10);
    if (track < 1 || track > 6 || craft < 0 || craft > 10) {
        fprintf(stderr, "[warp] AERO_WARP=%s invalid: track 1-6[, craft 1-10]\n", env);
        return;
    }
    aero_warp_request(track - 1, craft - 1);
}

// Per-frame tick, injected at the entry of the scene driver func_80015C8C
// (see the [[patches.hook]] block in scripts/gen_syms_toml.py). Runs on the game
// thread, before the driver consumes this frame's scene request.
void aero_warp_tick(uint8_t* rdram, recomp_context* ctx) {
    (void)ctx;
    static int env_parsed;
    if (!env_parsed) {
        env_parsed = 1;
        parse_env_request();
    }
    // SCAFFOLD (empirical pacing, not a decoded mechanic): track how long the
    // current scene has been current and idle (no transition in flight). The ROM's
    // own launches always fire from a quiescent scene behind a completed fade;
    // acting on a 1-2 frame old scene (whose entry loading is still in flight)
    // crashes the course loader (func_80007310 chain, guest NULL deref)
    // nondeterministically. 30 ticks ≈ one fade. The decoded ready signal (the
    // menu fade object 0x80052E60 polls) would replace this — tracked in issue #3.
    uint32_t cur = (uint32_t)MEM_W(0, (gpr)(int32_t)SCENE_CUR);
    uint32_t tgt = (uint32_t)MEM_W(0, (gpr)(int32_t)SCENE_REQ);
    static uint32_t stable_scene = ~0u;
    static int stable_ticks;
    if (cur != stable_scene || cur != tgt) {
        stable_scene = (cur == tgt) ? cur : ~0u;
        stable_ticks = 0;
    }
    stable_ticks++;

    uint32_t req = atomic_load_explicit(&g_warp_request, memory_order_relaxed);
    if (!(req & 0x80000000u)) return;

    // Warpable = the race-param block is initialized, the scene manager is not
    // mid-transition, and the current scene is one the ROM itself launches races
    // from with the one-time init of the generic scene runner (func_80015D70,
    // guard 0x8008B1F0) already behind it: 3 (title) or 4 (menu). Anything else
    // holds the request and, where the ROM has its own way out, forges that
    // transition first:
    //   scene 5 (race)    -> the race runner's own exit sequence (see below)
    //   scene 2 (attract) -> the attract-interrupt request (scene 3)
    //   scenes 0/1 (boot logo) -> wait; the logo auto-advances. Direct 0->5 warps
    //   DID mostly work but crashed nondeterministically in the course loader
    //   (func_80007310 chain) because the generic runner's one-time init never ran.
    if (MEM_BU(0, (gpr)(int32_t)RP_INITED) != 1) return;
    if (cur != tgt) return;         // transition in flight; hold the request
    if (stable_ticks < 30) return;  // scene too fresh; hold (see SCAFFOLD above)

    if (cur == SCENE_RACE) {
        // Trigger the race runner's OWN exit sequence (the teardown the demo's
        // natural end runs): it destroys race objects, quiesces the course
        // consumers and requests the follow-up scene itself. Forging the scene
        // request directly instead leaves live race state behind and the next
        // race's load crashes on it (func_80007310 / func_8002E5E0 chains).
        MEM_W(0, (gpr)(int32_t)SCENE_TRANS) = TRANS_EXIT;
        return;
    }
    if (cur == 2) {
        MEM_W(0, (gpr)(int32_t)SCENE_REQ)   = 3;
        MEM_W(0, (gpr)(int32_t)SCENE_PHASE) = PHASE_ENTRY;
        return;
    }
    if (cur != 3 && cur != SCENE_MENU) return;
    if (!atomic_compare_exchange_strong_explicit(&g_warp_request, &req, 0,
                                                 memory_order_relaxed,
                                                 memory_order_relaxed)) {
        return;
    }

    int track = (int)(req & 0xFF);
    int craft = (int)((req >> 8) & 0xFF);

    // Menu-side stores the warp forges (see the header comment for the per-store
    // provenance). Order matches the ROM: mode-select -> menu-scene entry ->
    // craft/track select -> RACE confirm.
    MEM_W(0, (gpr)(int32_t)GAME_MODE)  = 0;
    MEM_B(0, (gpr)(int32_t)RP_MODE)    = 4;
    MEM_B(0, (gpr)(int32_t)RP_MENU9C)  = 6;
    if (craft != 0xFF) {
        MEM_B(0, (gpr)(int32_t)RP_CRAFT1)  = (int8_t)craft;
        MEM_B(0, (gpr)(int32_t)RP_DUPCOL)  = 0;
    }
    MEM_B(0, (gpr)(int32_t)RP_TRACK)   = (int8_t)track;
    // Course group: the menu's confirm handler copies MENU_GROUP into RP_GROUP;
    // the ROM's own launches carry 1 for tracks 0-3 and 2 for tracks 4-5 (the
    // blob-id table 0x800974F0 has exactly those two rows populated). Group 0
    // indexes the all-zero row = no per-course record/ghost blob loads. Must be
    // set BEFORE the loader call below (the loader reads RP_GROUP/RP_TRACK).
    {
        int8_t group = (int8_t)((track < 4) ? 1 : 2);
        MEM_B(0, (gpr)(int32_t)MENU_GROUP) = group;
        MEM_B(0, (gpr)(int32_t)RP_GROUP)   = group;
    }
    MEM_W(0, (gpr)(int32_t)GO_EE9C)    = 1;
    MEM_B(0, (gpr)(int32_t)LATCH_EF50) = 0;
    MEM_B(0, (gpr)(int32_t)LATCH_EF54) = 0;
    MEM_B(0, (gpr)(int32_t)LATCH_EF58) = 1;
    MEM_W(0, (gpr)(int32_t)STATE_F294) = 0;
    MEM_H(0, (gpr)(int32_t)HALF_109BDC) = 0;
    // Record/ghost preload: on console the per-frame sound director (func_8001E8D8)
    // runs this load chain only in the MENU scene (jump-table case 4 ->
    // func_8001F0D8 -> ... -> func_80036C54). The warp skips the menu scene, so
    // run the ROM's own loader once here, AFTER all the menu-launch stores above
    // have settled (RP_MODE/MENU9C/TRACK/GROUP all set). Self-guarded: looks up
    // the blob id (0 -> no-op), copies the course's default-record row and DMAs
    // its replay blob to 0x801B5A30 exactly as a real menu launch would.
    //
    // (Race BGM needs none of this: the race scene's own per-frame music director
    // func_80002180 — called from func_80015FD0 — posts the track's song on the
    // scene-phase walk. It was silent under EVERY launch path until the runtime's
    // osPiStartDma completion message was fixed to carry the OSIoMesg pointer;
    // see patches/0012 and issue #7.)
    func_80036C54(rdram, ctx);
    uint32_t track_ptr = (uint32_t)MEM_W(0, (gpr)(int32_t)(TRACK_TAB + 4u * (uint32_t)track));
    MEM_W(0, (gpr)(int32_t)(TRACK_REC + 20u * (uint32_t)track)) = (int32_t)track_ptr;
    MEM_W(0, (gpr)(int32_t)COURSE_TAB)  = 0;
    MEM_W(0, (gpr)(int32_t)SCENE_PHASE) = PHASE_ENTRY;
    MEM_W(0, (gpr)(int32_t)SCENE_REQ)   = SCENE_RACE;

    fprintf(stderr, "[warp] %s: game-mode 0 race, craft %d (from scene %u)\n",
            aero_scene_table[track],
            (int)(int8_t)MEM_B(0, (gpr)(int32_t)RP_CRAFT1) + 1, cur);
}
