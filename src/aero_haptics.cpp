// Port enhancement: feedback observes the ROM's collision damage and turbo timer.
// No guest state is changed. See docs/controllers.md for the device boundary.
#include "aero_haptics.h"
#include "recomp.h"
#include "aero_region.h"
#include "aero_player.h"
#include <array>
#include <algorithm>

#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>

namespace {
std::atomic<bool> enabled{true};
std::atomic<bool> turbo_enabled{true};
struct Feedback {
    std::atomic<uint64_t> impact_until{0}, turbo_until{0};
    std::atomic<bool> native_motor{false};
};
std::array<Feedback, 2> feedback;
uint64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
bool racing(uint8_t* rdram) {
    return MEM_W(0, (gpr)(int32_t)AERO_ADDR(0x8013FF80, 0x8013D000u)) == 5 &&
           MEM_W(0, (gpr)(int32_t)AERO_ADDR(0x8013FF88, 0x8013D008u)) == 3 &&
           MEM_BU(0, (gpr)(int32_t)AERO_ADDR(0x8013FF90, 0x8013D010u)) != 7; // attract demo
}
}

void aero::haptics::configure(bool on, bool turbo) {
    enabled.store(on, std::memory_order_relaxed);
    turbo_enabled.store(turbo, std::memory_order_relaxed);
    stop();
}
void aero::haptics::stop() {
    stop_player(0);
    stop_player(1);
}
void aero::haptics::stop_player(int player) {
    if (player < 0 || player >= 2) return;
    feedback[player].impact_until.store(0, std::memory_order_relaxed);
    feedback[player].turbo_until.store(0, std::memory_order_relaxed);
    feedback[player].native_motor.store(false, std::memory_order_relaxed);
}
void aero::haptics::motor(bool on, int player) {
    if (player >= 0 && player < 2)
        feedback[player].native_motor.store(on, std::memory_order_relaxed);
}
aero::haptics::Motors aero::haptics::sample(int player, Settings settings) {
    if (player < 0 || player >= 2) return {};
    const auto& state = feedback[player];
    if (!enabled.load(std::memory_order_relaxed)) return {};
    const int master = std::clamp(settings.strength, 0, 100);
    auto scaled = [master](Motors motors, int strength) -> Motors {
        const int gain = master * std::clamp(strength, 0, 100);
        return {uint16_t(uint32_t(motors.low) * gain / 10000),
                uint16_t(uint32_t(motors.high) * gain / 10000)};
    };
    if (master == 0) return {};
    if (state.native_motor.load(std::memory_order_relaxed)) return scaled({0xFFFF, 0xFFFF}, 100);
    const auto now = now_ms();
    // Preserve collision priority, but a muted collision must not mask Turbo.
    if (settings.collision_enabled && settings.collision_strength > 0 &&
        now < state.impact_until.load(std::memory_order_relaxed))
        return scaled({0xC000, 0x9000}, settings.collision_strength);
    if (settings.turbo_enabled && now < state.turbo_until.load(std::memory_order_relaxed))
        return scaled({0x5000, 0x7000}, settings.turbo_strength);
    return {};
}

extern "C" void aero_haptics_frame(uint8_t* rdram, recomp_context*) {
    if (!enabled.load(std::memory_order_relaxed) || !racing(rdram)) aero::haptics::stop();
}

// Hook at 0x80058AD8: s0 is the craft; +0x24 is this tick's collision damage.
// The input callback at +4 identifies the local player, excluding AI/replays.
extern "C" void aero_haptics_race_tick(uint8_t* rdram, recomp_context* ctx) {
    if (!enabled.load(std::memory_order_relaxed) || !racing(rdram)) return;
    const auto address = static_cast<uint32_t>(ctx->r16);
    if (address < 0x80000000u || address > 0x807FFF80u || (address & 3u) != 0) return;
    const gpr car = static_cast<int32_t>(address);
    const int player = aero_car_player(rdram, car);
    if (player < 0) return;
    auto& state = feedback[player];
    const auto now = now_ms();
    const float damage = std::bit_cast<float>(static_cast<uint32_t>(MEM_W(0x24, car)));
    // A short impact pulse remains perceptible even for a single collision tick.
    if (std::isfinite(damage) && damage > 0.0f)
        state.impact_until.store(now + 120, std::memory_order_relaxed);
    // Refresh only while the ROM timer is active; pause/stall cannot latch rumble.
    state.turbo_until.store(turbo_enabled.load(std::memory_order_relaxed) && MEM_BU(0x55, car) != 0 ? now + 150 : 0,
                      std::memory_order_relaxed);
}
