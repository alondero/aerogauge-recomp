// Port enhancement: feedback observes the ROM's collision damage and turbo timer.
// No guest state is changed. See docs/notes/controller-accessories.md for ROM evidence.
#include "aero_haptics.h"
#include "recomp.h"

#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>

namespace {
bool enabled = true;
bool turbo_enabled = true;
std::atomic<uint64_t> impact_until{0}, turbo_until{0};
std::atomic<bool> native_motor{false};
uint64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
bool racing(uint8_t* rdram) {
    return MEM_W(0, (gpr)(int32_t)0x8013FF80) == 5 &&
           MEM_W(0, (gpr)(int32_t)0x8013FF88) == 3 &&
           MEM_BU(0, (gpr)(int32_t)0x8013FF90) != 7; // attract demo
}
}

void aero::haptics::configure(bool on, bool turbo) {
    enabled = on;
    turbo_enabled = turbo;
    stop();
}
void aero::haptics::stop() {
    impact_until.store(0, std::memory_order_relaxed);
    turbo_until.store(0, std::memory_order_relaxed);
    native_motor.store(false, std::memory_order_relaxed);
}
void aero::haptics::motor(bool on) {
    native_motor.store(on, std::memory_order_relaxed);
}
aero::haptics::Motors aero::haptics::sample() {
    if (!enabled) return {};
    if (native_motor.load(std::memory_order_relaxed)) return {0xFFFF, 0xFFFF};
    const auto now = now_ms();
    if (now < impact_until.load(std::memory_order_relaxed)) return {0xC000, 0x9000};
    if (now < turbo_until.load(std::memory_order_relaxed)) return {0x5000, 0x7000};
    return {};
}

extern "C" void aero_haptics_frame(uint8_t* rdram, recomp_context*) {
    if (!enabled || !racing(rdram)) aero::haptics::stop();
}

// Hook at 0x80058AD8: s0 is the craft; +0x24 is this tick's collision damage.
// +4 is its input callback: 0x8005C750 identifies the local P1, excluding AI/P2.
extern "C" void aero_haptics_race_tick(uint8_t* rdram, recomp_context* ctx) {
    if (!enabled || !racing(rdram)) return;
    const auto address = static_cast<uint32_t>(ctx->r16);
    if (address < 0x80000000u || address > 0x807FFF80u) return;
    const gpr car = static_cast<int32_t>(address);
    if (static_cast<uint32_t>(MEM_W(4, car)) != 0x8005C750u) return;
    const auto now = now_ms();
    const float damage = std::bit_cast<float>(static_cast<uint32_t>(MEM_W(0x24, car)));
    // A short impact pulse remains perceptible even for a single collision tick.
    if (std::isfinite(damage) && damage > 0.0f)
        impact_until.store(now + 120, std::memory_order_relaxed);
    // Refresh only while the ROM timer is active; pause/stall cannot latch rumble.
    turbo_until.store(turbo_enabled && MEM_BU(0x55, car) != 0 ? now + 150 : 0,
                      std::memory_order_relaxed);
}
