// Spec for the one-button Turbo + Boost Start pad-synthesis hook (src/aero_turbo_boost.c).
//
// Pins the gate that picks which combo the hook ORs into the P1 pad sources:
//   1. Countdown running (race-block elapsed <= 0xC3, INCLUDING the GO frame) =>
//      B|A is OR'd into BOTH the race-runner P1 latch (0x8013FF70) and the
//      controller-1 osCont read-back buttons (0x8011CAB2) -- the two sources the
//      countdown GO check (func_80016890) and the temperature/turbo handler
//      (func_8000877C) read respectively.
//   2. Race running (elapsed > 0xC3) => Z|A is OR'd instead (drift + accel).
//   3. P2 latch (0x8013FF72) is never touched (single-player QoL).
//   4. When aero_easy_turbo_enabled() returns 0 the hook is a strict no-op.
//
// The hook is pure guest-memory manipulation (no ROM state), so it is
// unit-testable in isolation. The only external symbol is
// aero_easy_turbo_enabled() (the aero_config bridge) -- stubbed here. Compile
// from the repo root, both files as C11 (aero_turbo_boost.c is plain C):
//   gcc -std=c11 -I lib/N64ModernRuntime/N64Recomp/include -x c tests/test_turbo_boost.cpp src/aero_turbo_boost.c -o build/test_turbo_boost
//   ./build/test_turbo_boost
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "recomp.h"

// This is a test binary: asserts ARE the test, so they must survive a Release
// (NDEBUG) build -- without this the whole file compiles to a vacuous pass.
#undef NDEBUG
#include <assert.h>

extern "C" void aero_turbo_boost_tick(uint8_t* rdram, recomp_context* ctx);
static int g_enabled = 1;
extern "C" int aero_easy_turbo_enabled(void) { return g_enabled; }

#define RDRAM_SIZE (8u * 1024u * 1024u)
static uint8_t* rdram;

// Guest addresses the hook touches (src/aero_turbo_boost.c).
#define PAD_P1         0x8013FF70u
#define PAD_P2         0x8013FF72u
#define CTRL1_BUTTONS  0x8011CAB2u
#define RACE_ELAPSED   0x8013FF48u  // 0x8013FC88 + 0x2C0

// N64 bits (main.cpp enum).
#define N64_A  0x8000u
#define N64_B  0x4000u
#define N64_Z  0x2000u
#define N64_START 0x1000u

// recomp.h's MEM_* macros XOR-swizzle for the big-endian guest. Mirror them
// for the host buffer so the assertions read what the hook wrote.
static void w16(uint32_t a, uint16_t v) { *(uint16_t*)(rdram + ((a ^ 2u) - 0x80000000u)) = v; }
static uint16_t r16(uint32_t a) { return *(uint16_t*)(rdram + ((a ^ 2u) - 0x80000000u)); }

static void reset_guest(uint16_t elapsed, uint16_t p1, uint16_t c1) {
    memset(rdram, 0, RDRAM_SIZE);
    w16(RACE_ELAPSED, elapsed);
    w16(PAD_P1, p1);
    w16(PAD_P2, 0x1234u);
    w16(CTRL1_BUTTONS, c1);
}

static void tick(void) {
    recomp_context ctx;
    memset(&ctx, 0, sizeof(ctx));
    aero_turbo_boost_tick(rdram, &ctx);
}

int main(void) {
    rdram = (uint8_t*)malloc(RDRAM_SIZE);
    assert(rdram != NULL);

    // --- countdown running (elapsed < 0xC3) => B|A on both P1 sources ----------
    reset_guest(0x0000, 0x0000, 0x0000);
    tick();
    assert((r16(PAD_P1) & (N64_B | N64_A)) == (N64_B | N64_A));
    assert((r16(CTRL1_BUTTONS) & (N64_B | N64_A)) == (N64_B | N64_A));
    assert((r16(PAD_P1) & (N64_Z | 0x1000u)) == 0);   // no drift bit leaked in
    assert(r16(PAD_P2) == 0x1234u);                    // P2 untouched

    // Pre-existing bits are preserved (OR, not overwrite).
    reset_guest(0x0040, N64_START, N64_START);
    tick();
    assert((r16(PAD_P1) & N64_START) == N64_START);
    assert((r16(CTRL1_BUTTONS) & N64_START) == N64_START);
    assert((r16(PAD_P1) & (N64_B | N64_A)) == (N64_B | N64_A));

    // --- the GO frame itself (elapsed == 0xC3) still gets B|A ------------------
    reset_guest(0x00C3, 0x0000, 0x0000);
    tick();
    assert((r16(PAD_P1) & (N64_B | N64_A)) == (N64_B | N64_A));
    assert((r16(CTRL1_BUTTONS) & (N64_B | N64_A)) == (N64_B | N64_A));

    // --- race running (elapsed > 0xC3) => Z|A (drift + accel) ------------------
    reset_guest(0x00C4, 0x0000, 0x0000);
    tick();
    assert((r16(PAD_P1) & (N64_Z | N64_A)) == (N64_Z | N64_A));
    assert((r16(CTRL1_BUTTONS) & (N64_Z | N64_A)) == (N64_Z | N64_A));
    assert((r16(PAD_P1) & N64_B) == 0);                // no brake bit leaked in
    assert(r16(PAD_P2) == 0x1234u);                    // P2 untouched

    // --- disabled => strict no-op ----------------------------------------------
    reset_guest(0x0000, 0x0000, 0x0000);
    g_enabled = 0;
    tick();
    assert(r16(PAD_P1) == 0x0000);
    assert(r16(CTRL1_BUTTONS) == 0x0000);
    g_enabled = 1;

    free(rdram);
    printf("turbo_boost: all assertions passed\n");
    return 0;
}
