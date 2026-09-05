// Behavioural spec for the P1 semantic-control assist in src/aero_turbo_boost.c.
// The hook runs immediately after AeroGauge maps the configured controller
// buttons into car+0x40, so these tests use the game's accelerator/brake/drift
// bits rather than assuming the default A/B/Z bindings.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "recomp.h"

#undef NDEBUG
#include <assert.h>

extern "C" void aero_turbo_boost_tick(uint8_t* rdram, recomp_context* ctx);
static int g_enabled = 1;
extern "C" int aero_easy_turbo_enabled(void) { return g_enabled; }

#define RDRAM_SIZE (8u * 1024u * 1024u)
#define CAR         0x8013FFB0u
#define RACE_PHASE  0x8013FF88u
#define RACE_STEP   0x8013FF38u

#define ACCEL 0x80u
#define BRAKE 0x40u
#define DRIFT 0x20u

static uint8_t* rdram;

static uint32_t off(uint32_t a) { return a - 0x80000000u; }
static void w8(uint32_t a, uint8_t v) { rdram[off(a ^ 3u)] = v; }
static uint8_t r8(uint32_t a) { return rdram[off(a ^ 3u)]; }
static void w16(uint32_t a, uint16_t v) { *(uint16_t*)(rdram + off(a ^ 2u)) = v; }
static uint16_t r16(uint32_t a) { return *(uint16_t*)(rdram + off(a ^ 2u)); }
static void w32(uint32_t a, uint32_t v) { *(uint32_t*)(rdram + off(a)) = v; }
static uint8_t actions(void) { return r8(CAR + 0x40u) & (ACCEL | BRAKE | DRIFT); }

static void set_turn(int turn) {
    assert(turn >= -20 && turn <= 20);
    uint16_t controls = r16(CAR + 0x40u);
    controls = (uint16_t)((controls & 0xF03Fu) | ((uint16_t)(turn + 20) << 6));
    w16(CAR + 0x40u, controls);
}

static void reset_guest(uint32_t phase, uint32_t step, uint8_t controls, int turn) {
    memset(rdram, 0, RDRAM_SIZE);
    w32(RACE_PHASE, phase);
    w32(RACE_STEP, step);
    w8(CAR + 0x40u, controls);
    set_turn(turn);
}

static void tick(void) {
    recomp_context ctx = {};
    ctx.r16 = (gpr)(int32_t)CAR; // func_8005C750's saved P1 car pointer at 0x8005C7A8
    aero_turbo_boost_tick(rdram, &ctx);
}

int main(void) {
    rdram = (uint8_t*)malloc(RDRAM_SIZE);
    assert(rdram != nullptr);

    // Holding only the configured accelerator is enough for a boost start:
    // the assist holds semantic brake through SET, then releases immediately
    // afterward (countdown step 3).
    reset_guest(1, 0, ACCEL, 0);
    tick();
    assert(actions() == (ACCEL | BRAKE));

    reset_guest(2, 1, ACCEL, 0);
    tick();
    assert(actions() == (ACCEL | BRAKE));

    w32(RACE_STEP, 2);
    w8(CAR + 0x40u, ACCEL | BRAKE);
    tick();
    assert(actions() == (ACCEL | BRAKE));

    w32(RACE_STEP, 3);
    w8(CAR + 0x40u, ACCEL | BRAKE);
    tick();
    assert(actions() == ACCEL);

    // No accelerator means no automatic launch and no stolen control.
    reset_guest(1, 0, 0, 0);
    tick();
    assert(actions() == 0);

    // In the race, ordinary steering remains untouched. A hard turn alone does
    // not steal drift control; the player must explicitly hold drift.
    reset_guest(3, 3, ACCEL, 0);
    tick();
    assert(actions() == ACCEL);

    reset_guest(3, 3, ACCEL, 20);
    tick();
    assert(actions() == ACCEL);

    reset_guest(3, 3, ACCEL | DRIFT, 20);
    tick();
    assert(actions() == (ACCEL | DRIFT));

    // Bit 0x2000 in car+0x34 is the ROM-observed turbo-ready drift state. The
    // assist performs the real two-tick release followed by an accelerator
    // re-press; AeroGauge itself remains responsible for awarding the turbo.
    w32(CAR + 0x34u, 0x00002000u);
    w8(CAR + 0x40u, ACCEL | DRIFT);
    tick();
    assert(actions() == 0);

    w8(CAR + 0x40u, ACCEL | DRIFT);
    tick();
    assert(actions() == 0);

    w8(CAR + 0x40u, ACCEL | DRIFT);
    tick();
    assert(actions() == ACCEL);

    // One assist cycle is enough for this corner. Keeping the stick and drift
    // held does not immediately chain another boost or force another release.
    w8(CAR + 0x40u, ACCEL | DRIFT);
    tick();
    assert(actions() == (ACCEL | DRIFT));

    // Releasing drift re-arms the next deliberate corner attempt.
    w8(CAR + 0x40u, ACCEL);
    tick();
    w32(CAR + 0x34u, 0);
    w8(CAR + 0x40u, ACCEL | DRIFT);
    tick();
    assert(actions() == (ACCEL | DRIFT));

    // The original behaviour is a strict no-op when the enhancement is off.
    reset_guest(1, 0, ACCEL, 0);
    g_enabled = 0;
    tick();
    assert(actions() == ACCEL);

    free(rdram);
    puts("turbo_boost: all assertions passed");
    return 0;
}
