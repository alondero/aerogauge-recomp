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

static void tick_with_car(gpr car) {
    recomp_context ctx = {};
    ctx.r16 = car; // func_8005C750's saved P1 car pointer at 0x8005C7A8
    aero_turbo_boost_tick(rdram, &ctx);
}

static void tick(void) {
    tick_with_car((gpr)(int32_t)CAR);
}

int main(void) {
    rdram = (uint8_t*)malloc(RDRAM_SIZE);
    assert(rdram != nullptr);

    // Defensive guards must tolerate missing runtime state and malformed guest
    // pointers without touching RDRAM.
    recomp_context guard_ctx = {};
    guard_ctx.r16 = (gpr)(int32_t)CAR;
    aero_turbo_boost_tick(nullptr, &guard_ctx);
    aero_turbo_boost_tick(rdram, nullptr);
    const gpr invalid_cars[] = { 0, 0x7FFFFFFFu, 0x80800000u };
    for (gpr invalid_car : invalid_cars) {
        guard_ctx.r16 = invalid_car;
        aero_turbo_boost_tick(rdram, &guard_ctx);
    }

    // Recompiled callers may pass a zero-extended 32-bit car address. The hook
    // must canonicalize it before the first MEM_* access.
    reset_guest(3, 3, 0, 0);
    w32(CAR + 0x20u, 0x80100000u);
    w8(0x80100028u, 13);
    tick_with_car((gpr)CAR);
    w8(CAR + 0x40u, DRIFT);
    tick_with_car((gpr)CAR);
    assert(r8(CAR + 0x55u) == 13);
    assert(actions() == 0);

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

    // A press in a straight line, with no accelerator or ready flag, awards
    // the craft-specific turbo and consumes the drift action.
    reset_guest(3, 3, 0, 0);
    w32(CAR + 0x20u, 0x80100000u);
    w8(0x80100028u, 13);
    tick();
    w8(CAR + 0x40u, DRIFT);
    tick();
    assert(r8(CAR + 0x55u) == 13);
    assert(r8(CAR + 0x56u) == 5);
    assert(actions() == 0);

    // An uninitialized or dangling craft-settings pointer must leave the ROM
    // award fields untouched while still consuming the Turbo button.
    reset_guest(3, 3, 0, 0);
    tick();
    w32(CAR + 0x20u, 0);
    w32(CAR + 0x34u, 0xA0001000u);
    w8(CAR + 0x55u, 77);
    w8(CAR + 0x56u, 9);
    w8(CAR + 0x40u, DRIFT);
    tick();
    assert(actions() == 0);
    assert(*(uint32_t*)(rdram + off(CAR + 0x34u)) == 0xA0001000u);
    assert(r8(CAR + 0x55u) == 77);
    assert(r8(CAR + 0x56u) == 9);

    // Restore a valid craft-settings table for the remaining award tests.
    w32(CAR + 0x20u, 0x80100000u);
    w8(0x80100028u, 13);

    // Holding the button never extends or repeats a turbo, even after expiry.
    for (int timer = 12; timer >= 0; --timer) {
        w8(CAR + 0x55u, (uint8_t)timer);
        w8(CAR + 0x40u, ACCEL | DRIFT);
        tick();
        assert(r8(CAR + 0x55u) == timer);
        assert(actions() == ACCEL);
    }
    // Releasing and pressing again re-arms it, even when steering hard.
    w8(CAR + 0x40u, ACCEL);
    tick();
    set_turn(-20);
    w8(CAR + 0x40u, ACCEL | BRAKE | DRIFT);
    const uint16_t other_controls = r16(CAR + 0x40u) & ~0x2000u;
    tick();
    assert(r8(CAR + 0x55u) == 13);
    assert(r16(CAR + 0x40u) == other_controls);

    // A press during an active turbo is consumed, not queued until expiry.
    w8(CAR + 0x40u, ACCEL);
    tick();
    w8(CAR + 0x40u, ACCEL | DRIFT);
    tick();
    w8(CAR + 0x55u, 0);
    w8(CAR + 0x40u, ACCEL | DRIFT);
    tick();
    assert(r8(CAR + 0x55u) == 0);

    // Steering and the old ready flag alone cannot trigger an assisted boost.
    w32(CAR + 0x34u, 0x2000u);
    w8(CAR + 0x40u, ACCEL);
    set_turn(20);
    tick();
    assert(r8(CAR + 0x55u) == 0);
    assert(actions() == ACCEL);

    // A button held across GO must be released before it can award race turbo.
    reset_guest(2, 3, ACCEL | DRIFT, 0);
    w32(CAR + 0x20u, 0x80100000u);
    w8(0x80100028u, 10);
    tick();
    w32(RACE_PHASE, 3);
    w8(CAR + 0x40u, ACCEL | DRIFT);
    tick();
    assert(r8(CAR + 0x55u) == 0);
    w8(CAR + 0x40u, ACCEL);
    tick();
    w8(CAR + 0x40u, ACCEL | DRIFT);
    tick();
    assert(r8(CAR + 0x55u) == 10);

    // The original behaviour is a strict no-op when the enhancement is off.
    reset_guest(1, 0, ACCEL, 0);
    g_enabled = 0;
    tick();
    assert(actions() == ACCEL);

    // Disabling restores all race controls and does not award Turbo. Enabling
    // while that button remains held must not synthesize a new press.
    reset_guest(3, 3, ACCEL | DRIFT, 20);
    w32(CAR + 0x20u, 0x80100000u);
    w8(0x80100028u, 10);
    tick();
    assert(actions() == (ACCEL | DRIFT));
    assert(r8(CAR + 0x55u) == 0);
    g_enabled = 1;
    tick();
    assert(r8(CAR + 0x55u) == 0);
    assert(actions() == ACCEL);

    // Only the native award fields change: heat is left to the ROM and
    // unrelated flags survive clearing its pending-award bit.
    w8(CAR + 0x40u, ACCEL);
    tick();
    w32(CAR + 0x34u, 0xA0001000u);
    w32(CAR + 0x22Cu, 0x42480000u); // heat = 50
    w8(CAR + 0x40u, DRIFT);
    tick();
    assert(r8(CAR + 0x55u) == 10);
    assert(*(uint32_t*)(rdram + off(CAR + 0x34u)) == 0xA0000000u);
    assert(*(uint32_t*)(rdram + off(CAR + 0x22Cu)) == 0x42480000u);

    // Outside racing, the button retains its original meaning.
    reset_guest(4, 3, DRIFT, 0);
    tick();
    assert(actions() == DRIFT);
    assert(r8(CAR + 0x55u) == 0);

    free(rdram);
    puts("turbo_boost: all assertions passed");
    return 0;
}
