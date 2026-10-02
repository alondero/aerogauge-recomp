#undef NDEBUG
#include <cassert>
#include <bit>
#include <vector>
#include "aero_region.h"
#include "aero_haptics.h"
#include "recomp.h"

int aero_japan = 0;
static bool enabled[2] = {true, true};
extern "C" int aero_easy_turbo_enabled_for_player(int player) { return enabled[player]; }
extern "C" void aero_turbo_boost_tick(uint8_t*, recomp_context*);
extern "C" void aero_haptics_race_tick(uint8_t*, recomp_context*);
extern "C" void aero_haptics_frame(uint8_t*, recomp_context*);

static void check_region(int japan) {
    aero_japan = japan;
    std::vector<uint8_t> memory(8 * 1024 * 1024);
    auto* rdram = memory.data();
    const gpr cars[2] = {(gpr)(int32_t)0x80002000, (gpr)(int32_t)0x80003000};
    const gpr pad = (gpr)(int32_t)AERO_ADDR(0x8010CAB2u, 0x80109BA2u);
    const gpr phase = (gpr)(int32_t)AERO_ADDR(0x8013FF88u, 0x8013D008u);
    const gpr step = (gpr)(int32_t)AERO_ADDR(0x8013FF38u, 0x8013CFB8u);
    const gpr settings = (gpr)(int32_t)0x80004000;
    MEM_W(4, cars[0]) = AERO_ADDR(0x8005C750u, 0x8005CCD0u);
    MEM_W(4, cars[1]) = AERO_ADDR(0x8005C878u, 0x8005CDF4u);
    MEM_W(0, phase) = 3;
    MEM_W(0, step) = 3;
    MEM_B(0x28, settings) = 13;
    for (auto car : cars) MEM_W(0x20, car) = settings;
    recomp_context ctx{};
    auto tick = [&](int player) {
        ctx.r16 = cars[player];
        aero_turbo_boost_tick(rdram, &ctx);
    };
    // A simultaneous press must award both cars, independently of tick order.
    tick(0); tick(1);
    MEM_H(0, pad) = MEM_H(8, pad) = 0x10;
    tick(1); tick(0);
    assert(MEM_BU(0x55, cars[0]) == 13 && MEM_BU(0x55, cars[1]) == 13);
    // P1 release cannot create a second edge on P2's held button.
    MEM_B(0x55, cars[1]) = 0;
    MEM_H(0, pad) = 0;
    tick(0); tick(1);
    assert(MEM_BU(0x55, cars[1]) == 0);
    // Disabled P2 still consumes presses; enabling while held is not a press.
    MEM_H(8, pad) = 0; tick(1);
    enabled[1] = false;
    MEM_H(8, pad) = 0x10; tick(1);
    enabled[1] = true; tick(1);
    assert(MEM_BU(0x55, cars[1]) == 0);
    MEM_H(8, pad) = 0; tick(1);
    MEM_W(0x22C, cars[1]) = std::bit_cast<int32_t>(80.0f);
    MEM_H(8, pad) = 0x10; tick(1);
    assert(MEM_BU(0x55, cars[1]) == 0);
    MEM_H(8, pad) = 0; tick(1);
    MEM_W(0x22C, cars[1]) = 0;
    MEM_H(8, pad) = 0x10; tick(1);
    assert(MEM_BU(0x55, cars[1]) == 13);
    // P2 launch uses its semantic accelerator, retaining drift and steering.
    MEM_W(0, phase) = 2;
    MEM_W(0, step) = 2;
    MEM_B(0x40, cars[1]) = 0xA0;
    MEM_B(0x41, cars[1]) = 0x19;
    tick(1);
    assert(MEM_BU(0x40, cars[1]) == 0xE0 && MEM_BU(0x41, cars[1]) == 0x19);
    MEM_W(0, step) = 3; tick(1);
    assert(MEM_BU(0x40, cars[1]) == 0xA0);
    enabled[0] = false;
    MEM_B(0x40, cars[0]) = 0x80;
    MEM_W(0, step) = 2; tick(0);
    assert(MEM_BU(0x40, cars[0]) == 0x80);
    enabled[0] = true;
    // Unknown/AI callbacks must never receive local-player assistance.
    MEM_W(4, cars[1]) = 0;
    MEM_B(0x40, cars[1]) = 0x80; tick(1);
    assert(MEM_BU(0x40, cars[1]) == 0x80);
    MEM_W(4, cars[1]) = AERO_ADDR(0x8005C878u, 0x8005CDF4u);

    using namespace aero::haptics;
    configure(true);
    MEM_W(0, (gpr)(int32_t)AERO_ADDR(0x8013FF80u, 0x8013D000u)) = 5;
    MEM_W(0, phase) = 3;
    ctx.r16 = cars[1];
    aero_haptics_race_tick(rdram, &ctx);
    assert(sample(1).low == 0x5000 && sample(0).low == 0);
    MEM_W(0x24, cars[0]) = std::bit_cast<int32_t>(3.0f);
    ctx.r16 = cars[0]; aero_haptics_race_tick(rdram, &ctx);
    assert(sample(0).low == 0xC000 && sample(1).low == 0x5000);
    stop_player(0);
    assert(sample(0).low == 0 && sample(1).low == 0x5000);
    MEM_W(0, phase) = 4; aero_haptics_frame(rdram, &ctx);
    assert(sample(0).low == 0 && sample(1).low == 0);
    motor(true, 1);
    assert(sample(1).low == 0xFFFF && sample(0).low == 0);
    stop();
}

int main() { check_region(0); check_region(1); }
