// Spec for the developer warp menu's gating + forged-store logic (src/aero_warp.c,
// issue #3) against a synthetic RDRAM. Pins:
//   1. A request published from any thread holds (no stores) while the race-param
//      block is uninitialized, while a scene transition is in flight, and while the
//      current scene is younger than the stability window.
//   2. From a stable menu scene (4) the fire performs exactly the derived menu-launch
//      stores: game mode 0, race-mode byte 4, menu byte 6, track byte, committed-track
//      pointer copy (table 0x80081F30 -> 0x8008B318 + track*20), course-table clear,
//      phase 6, scene request 5 — and consumes the request.
//   3. From the race scene (5) the same pending request does NOT switch scenes itself:
//      it triggers the race runner's own exit sequence (0x8013FF8C = 7) and stays
//      pending, so the launch happens later from a ROM launch scene.
//   4. An explicit craft forges the craft byte; the hotkey default (-1) leaves it.
//
// Standalone host test, no ROM build needed. Compile from the repo root (both files
// as C11 -- aero_warp.c uses C11 atomics and this test is plain C):
//   gcc -std=c11 -I lib/N64ModernRuntime/N64Recomp/include -x c tests/test_warp_gating.cpp src/aero_warp.c -o build/test_warp_gating
//   ./build/test_warp_gating
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "recomp.h"

extern void aero_warp_request(int track0, int craft);
extern void aero_warp_tick(uint8_t* rdram, recomp_context* ctx);

static int failures = 0;
#define CHECK(cond, msg)                                                    \
    do {                                                                    \
        if (!(cond)) {                                                      \
            fprintf(stderr, "FAIL: %s (%s)\n", msg, #cond);                 \
            ++failures;                                                     \
        }                                                                   \
    } while (0)

#define RDRAM_SIZE (8u * 1024u * 1024u)
static uint8_t* rdram;

// Guest byte order helpers (mirror recomp.h's XOR swizzles on the host buffer).
static void  w32(uint32_t a, uint32_t v) { *(uint32_t*)(rdram + (a - 0x80000000u)) = v; }
static uint32_t r32(uint32_t a) { return *(uint32_t*)(rdram + (a - 0x80000000u)); }
static void  w8(uint32_t a, uint8_t v) { rdram[((a ^ 3u) - 0x80000000u)] = v; }
static uint8_t r8(uint32_t a) { return rdram[((a ^ 3u) - 0x80000000u)]; }
static uint16_t r16(uint32_t a) { return *(uint16_t*)(rdram + ((a ^ 2u) - 0x80000000u)); }

static void reset_guest(uint32_t scene) {
    memset(rdram, 0, RDRAM_SIZE);
    w8(0x8013FFAAu, 1);          // race-param block initialized
    w32(0x8013FF80u, scene);     // current == requested (stable)
    w32(0x8013FF84u, scene);
    w32(0x8013FF88u, 6);         // fresh-entry phase
    w32(0x80081F30u + 8, 0x80080E68u);  // track table entry for track 2
}

static void tick_n(int n) {
    recomp_context ctx;
    memset(&ctx, 0, sizeof(ctx));
    for (int i = 0; i < n; i++) aero_warp_tick(rdram, &ctx);
}

int main(void) {
    rdram = (uint8_t*)calloc(RDRAM_SIZE, 1);

    // 1a. Uninitialized block: request must hold, no scene stores.
    reset_guest(4);
    w8(0x8013FFAAu, 0);
    aero_warp_request(2, -1);
    tick_n(60);
    CHECK(r32(0x8013FF84u) == 4, "holds while block uninitialized");

    // 1b. Transition in flight: request must hold.
    reset_guest(4);
    w32(0x8013FF84u, 5);  // cur=4, req=5
    tick_n(60);
    CHECK(r8(0x8013FF90u) == 0, "holds while transition in flight");

    // 1c. Scene younger than the stability window: request must hold, then fire.
    reset_guest(4);
    tick_n(10);
    CHECK(r32(0x8013FF84u) == 4, "holds inside stability window");
    tick_n(40);
    CHECK(r32(0x8013FF84u) == 5, "fires after stability window");

    // 2. Fire from a stable menu scene: exactly the derived menu-launch stores.
    CHECK(r32(0x8008F290u) == 0, "game mode forged to 0");
    CHECK(r8(0x8013FF90u) == 4, "race-mode byte = 4 (menu race)");
    CHECK(r8(0x8013FF9Cu) == 6, "menu-entry byte = 6");
    CHECK(r8(0x8013FF9Bu) == 2, "track byte = requested track");
    CHECK(r32(0x8008B318u + 20 * 2) == 0x80080E68u, "committed-track pointer copied");
    CHECK(r32(0x8008EE9Cu) == 1, "launch word 0x8008EE9C = 1");
    CHECK(r8(0x8008EF58u) == 1, "launch latch 0x8008EF58 = 1");
    CHECK(r16(0x80109BDCu) == 0, "halfword 0x80109BDC cleared");
    CHECK(r32(0x8013FF44u) == 0, "course table cleared");
    CHECK(r32(0x8013FF88u) == 6, "phase = fresh entry");
    CHECK(r8(0x8013FF95u) == 0, "hotkey default keeps craft byte");
    tick_n(60);
    CHECK(r32(0x8013FF84u) == 5, "request consumed (no double fire)");

    // 3. From the race scene: trigger the runner's own exit, stay pending.
    reset_guest(5);
    aero_warp_request(0, -1);
    tick_n(40);
    CHECK(r32(0x8013FF8Cu) == 7, "race scene: runner exit sequence triggered");
    CHECK(r32(0x8013FF84u) == 5, "race scene: no direct scene switch");
    // ... the exit lands in a launch scene; the held request fires there.
    reset_guest(3);
    tick_n(40);
    CHECK(r32(0x8013FF84u) == 5, "held request fires from title after exit");
    CHECK(r8(0x8013FF9Bu) == 0, "track byte = re-warp track");

    // 4. Explicit craft forges the craft byte + colour flag.
    reset_guest(4);
    w8(0x8013FF95u, 1);
    w8(0x8013FF97u, 1);
    aero_warp_request(1, 3);
    tick_n(40);
    CHECK(r8(0x8013FF95u) == 3, "explicit craft forged");
    CHECK(r8(0x8013FF97u) == 0, "duplicate-craft colour flag cleared");

    if (failures == 0) printf("test_warp_gating: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
