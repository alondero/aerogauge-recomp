#include "aero_cheats.h"
#include "aero_player.h"

// These are narrow game-thread bridges for the supported USA/Japan Rev A ROMs.
// All guest accesses use MEM_* (N64 byte order in word-swapped RDRAM).
// See docs/cheats.md for the ROM evidence and eventual source replacements.
extern "C" void aero_cheat_damage(uint8_t* rdram, recomp_context* ctx) {
    if (!rdram || !ctx || !aero::cheats::enabled(aero::cheats::Cheat::InfiniteDamage)) return;
    const uint32_t address = static_cast<uint32_t>(ctx->r16);
    // s0 is the current vehicle, owned by the ROM update for this call. Require
    // aligned KSEG0 RDRAM through +0x2C; malformed pointers do nothing.
    if (address < 0x80000000u || address > 0x807FFFD0u || (address & 3u)) return;
    const gpr car = static_cast<int32_t>(address);
    if (aero_car_player(rdram, car) != 0) return; // excludes P2, AI and replays
    // Before USA 80058ADC / JP 80058D30, f12 holds this update's collision
    // damage. Zero it before the ROM's zero test and destruction threshold.
    // +0x28 is accumulated damage in craft units; +0x2C is its HUD percentage
    // (both IEEE binary32). Repair existing damage without clearing crash flags.
    ctx->f12.u32l = 0;
    MEM_W(0x28, car) = 0;
    MEM_W(0x2C, car) = 0;
}

extern "C" void aero_cheat_standings(uint8_t* rdram, recomp_context*) {
    if (!rdram || !aero::cheats::enabled(aero::cheats::Cheat::AlwaysFirstPosition)) return;
    const gpr race = static_cast<int32_t>(AERO_ADDR(0x8013FC88u, 0x8013CD08u));
    const gpr cars = static_cast<int32_t>(AERO_ADDR(0x8013FFB0u, 0x8013D030u));
    const gpr standings = static_cast<int32_t>(AERO_ADDR(0x8016C478u, 0x80169478u));
    const uint32_t stride = AERO_BRANCH(0x20A0u, 0x2094u);
    const unsigned count = MEM_BU(9, race);
    // The ROM's race-owned table has at most ten 12-byte entries. It is fully
    // sorted at this seam, immediately before the ROM publishes car+8 ranks.
    // Validate the entire permutation before writing, including the P1 input
    // callback so title demos and replays retain their normal standings.
    if (count < 1 || count > 10 || aero_car_player(rdram, cars) != 0) return;
    unsigned seen = 0;
    unsigned player_index = 0;
    for (unsigned i = 0; i < count; ++i) {
        const uint32_t pointer = MEM_W(i * 12, standings);
        const uint32_t base = static_cast<uint32_t>(cars);
        if (pointer < base || (pointer - base) % stride != 0) return;
        const unsigned slot = (pointer - base) / stride;
        if (slot >= count || (seen & (1u << slot))) return;
        seen |= 1u << slot;
        if (slot == 0) player_index = i;
    }
    // Move the complete P1 entry to the front, preserving every opponent's
    // relative order and all lap/sector/distance keys. No racer count, lap,
    // timer, finish gate or saved data is changed here. The ROM publishes the
    // resulting unique ranks and copies them into its per-racer race records.
    uint32_t player[3];
    for (unsigned word = 0; word < 3; ++word)
        player[word] = MEM_W(player_index * 12 + word * 4, standings);
    for (unsigned i = player_index; i > 0; --i)
        for (unsigned word = 0; word < 3; ++word)
            MEM_W(i * 12 + word * 4, standings) = MEM_W((i - 1) * 12 + word * 4, standings);
    for (unsigned word = 0; word < 3; ++word)
        MEM_W(word * 4, standings) = player[word];
}
