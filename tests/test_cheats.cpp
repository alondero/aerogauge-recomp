#include "aero_cheats.h"
#include "aero_region.h"
#include "recomp.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cstdio>
#include <vector>

#undef NDEBUG
#include <cassert>

int aero_japan = 0;
extern "C" void test_rom_damage(uint8_t*, recomp_context*);
extern "C" void aero_cheat_damage(uint8_t*, recomp_context*);
extern "C" void aero_cheat_standings(uint8_t*, recomp_context*);
extern "C" void func_8001CF48(uint8_t*, recomp_context*);
extern "C" void jp_func_8001D720(uint8_t*, recomp_context*);

static std::vector<uint8_t> ram(8 * 1024 * 1024);
static uint8_t* rdram = ram.data(); // MEM_* contract
static gpr addr(uint32_t address) { return static_cast<int32_t>(address); }
static gpr cars() { return addr(AERO_ADDR(0x8013FFB0u, 0x8013D030u)); }
static unsigned stride() { return AERO_BRANCH(0x20A0u, 0x2094u); }
static gpr car(unsigned i) { return cars() + i * stride(); }
static gpr race() { return addr(AERO_ADDR(0x8013FC88u, 0x8013CD08u)); }
static gpr table() { return addr(AERO_ADDR(0x8016C478u, 0x80169478u)); }
static gpr gates() { return addr(AERO_ADDR(0x8016C470u, 0x80169470u)); }
static unsigned projections;

// Only the track-space distance projection is stubbed. The ROM still reads
// laps/sector keys, sorts all entries, publishes car+8 and race-record+0x58,
// and maintains its position-change animation. Give each car a fixed distance.
static void project(recomp_context* ctx) {
    ++projections;
    ctx->f0.fl = -float((static_cast<uint32_t>(ctx->r4) - static_cast<uint32_t>(cars())) / stride());
}
extern "C" void func_8001D598(uint8_t*, recomp_context* ctx) { project(ctx); }
extern "C" void jp_func_8001DDB0(uint8_t*, recomp_context* ctx) { project(ctx); }

static void reset(unsigned count = 10) {
    std::fill(ram.begin(), ram.end(), 0);
    for (const auto& entry : aero::cheats::catalog) aero::cheats::set_enabled(entry.cheat, false);
    MEM_B(9, race()) = count;
    MEM_W(0, gates()) = 1;
    MEM_W(4, gates()) = addr(0x80700000u); // sector lookup table, all zeros
    MEM_W(4, cars()) = AERO_ADDR(0x8005C750u, 0x8005CCD0u);
    MEM_W(4, car(1)) = AERO_ADDR(0x8005C878u, 0x8005CDF4u);
    projections = 0;
}
static void standings() {
    recomp_context ctx{};
    ctx.r29 = addr(0x807FF000u);
#ifdef TEST_JP_ROM
    jp_func_8001D720(rdram, &ctx);
#else
    func_8001CF48(rdram, &ctx);
#endif
}
static void damage(unsigned slot, float collision, float accumulated) {
    const gpr settings = addr(0x80600000u);
    MEM_W(0x20, car(slot)) = settings;
    MEM_W(0x0C, settings) = std::bit_cast<uint32_t>(50.0f); // ROM craft damage limit
    MEM_W(0x24, car(slot)) = std::bit_cast<uint32_t>(collision);
    MEM_W(0x28, car(slot)) = std::bit_cast<uint32_t>(accumulated);
    recomp_context ctx{};
    ctx.r16 = car(slot);
    test_rom_damage(rdram, &ctx);
}

int main() {
#ifdef TEST_JP_ROM
    aero_japan = 1;
#endif
    using aero::cheats::Cheat;
    for (const auto& entry : aero::cheats::catalog) assert(!aero::cheats::enabled(entry.cheat));
    aero::cheats::set_enabled(static_cast<Cheat>(999), true);
    assert(!aero::cheats::enabled(static_cast<Cheat>(999)));
    reset();
    damage(0, 10, 45);
    assert(MEM_W(0x28, cars()) == std::bit_cast<uint32_t>(50.0f));
    assert(MEM_W(0x2C, cars()) == std::bit_cast<uint32_t>(100.0f));
    assert((MEM_BU(0x35, cars()) & 2) != 0); // normal destruction threshold
    reset();
    aero::cheats::set_enabled(Cheat::InfiniteDamage, true);
    for (float collision : {0.0f, 10.0f, 500.0f}) {
        damage(0, collision, 45);
        assert(MEM_W(0x28, cars()) == 0 && MEM_W(0x2C, cars()) == 0);
        assert((MEM_BU(0x35, cars()) & 2) == 0);
        assert(MEM_W(0x24, cars()) == std::bit_cast<uint32_t>(collision));
    }
    // An already set destruction bit is not undone, and P2/AI take damage.
    MEM_B(0x35, cars()) = 2;
    damage(0, 10, 45);
    assert(MEM_BU(0x35, cars()) == 2);
    for (unsigned slot : {1u, 2u}) {
        damage(slot, 10, 45);
        assert(MEM_W(0x28, car(slot)) == std::bit_cast<uint32_t>(50.0f));
        assert(MEM_BU(0x35, car(slot)) & 2);
    }
    aero::cheats::set_enabled(Cheat::InfiniteDamage, false);
    MEM_B(0x35, cars()) = 0;
    damage(0, 10, 0);
    assert(MEM_W(0x28, cars()) == std::bit_cast<uint32_t>(10.0f));
    // Pointer normalization, bounds, null inputs and independent flags.
    aero::cheats::set_enabled(Cheat::InfiniteDamage, true);
    recomp_context ctx{};
    ctx.r16 = static_cast<uint32_t>(cars());
    ctx.f12.fl = 5;
    aero_cheat_damage(rdram, &ctx);
    assert(ctx.f12.u32l == 0);
    const auto unchanged = ram;
    for (uint32_t invalid : {0u, 0x80000001u, 0x807FFFD4u, 0xA013FFB0u}) {
        ctx.r16 = invalid;
        ctx.f12.fl = 7;
        aero_cheat_damage(rdram, &ctx);
        assert(ctx.f12.fl == 7 && ram == unchanged);
    }
    aero_cheat_damage(nullptr, &ctx);
    aero_cheat_damage(rdram, nullptr);

    for (unsigned count = 1; count <= 10; ++count) {
        reset(count);
        standings();
        assert(projections == count);
        // With equal laps/sectors, these decreasing distance keys put
        // car 0 last. Its real sort, not the cheat, establishes this baseline.
        assert(MEM_BU(8, cars()) == count);
        std::array<std::array<uint32_t, 3>, 10> normal{};
        for (unsigned i = 0; i < count; ++i)
            for (unsigned w = 0; w < 3; ++w) normal[i][w] = MEM_W(i * 12 + w * 4, table());
        aero::cheats::set_enabled(Cheat::AlwaysFirstPosition, true);
        standings();
        assert(projections == count * 2 && MEM_BU(9, race()) == count);
        assert(MEM_BU(8, cars()) == 1 && MEM_BU(0x58, race()) == 1);
        for (unsigned i = 0; i < count; ++i) {
            const unsigned previous = i == 0 ? count - 1 : i - 1;
            for (unsigned w = 0; w < 3; ++w)
                assert(static_cast<uint32_t>(MEM_W(i * 12 + w * 4, table())) == normal[previous][w]);
            const gpr ranked_car = addr(MEM_W(i * 12, table()));
            assert(MEM_BU(8, ranked_car) == i + 1);
        }
        // Toggling off re-sorts from current progress; nothing is restored
        // from an earlier race. Unrelated vehicle bytes are untouched.
        aero::cheats::set_enabled(Cheat::AlwaysFirstPosition, false);
        standings();
        assert(MEM_BU(8, cars()) == count);
        for (unsigned i = 0; i < count; ++i) assert(MEM_BU(9, car(i)) == 0);
    }
    reset();
    standings();
    aero::cheats::set_enabled(Cheat::AlwaysFirstPosition, true);
    // Validate every table pointer before any write, even when P1 is valid.
    for (uint32_t invalid : {0u, 0x807FFFFCu, static_cast<uint32_t>(cars()),
                             static_cast<uint32_t>(car(2)) + 1u}) {
        MEM_W(0, table()) = invalid;
        const auto before = ram;
        aero_cheat_standings(rdram, nullptr);
        assert(ram == before);
    }
    for (unsigned invalid_count : {0u, 11u, 255u}) {
        MEM_B(9, race()) = invalid_count;
        const auto before = ram;
        aero_cheat_standings(rdram, nullptr);
        assert(ram == before);
    }
    reset();
    MEM_W(4, cars()) = 0; // demo/replay
    standings();
    const auto demo = ram;
    aero::cheats::set_enabled(Cheat::AlwaysFirstPosition, true);
    aero_cheat_standings(rdram, nullptr);
    assert(ram == demo);
    aero_cheat_standings(nullptr, nullptr);
    std::puts("PASS: ROM damage and standings, toggles, P1 isolation and bounds");
}
