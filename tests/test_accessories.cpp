#undef NDEBUG
#include <cassert>
#include <bit>
#include <chrono>
#include <fstream>
#include <thread>
#include <vector>
#include "aero_pak.h"
#include "aero_haptics.h"
#include "recomp.h"

extern "C" {
void aero_pak_read(uint8_t*, recomp_context*);
void aero_pak_write(uint8_t*, recomp_context*);
void aero_pak_status(uint8_t*, recomp_context*);
void aero_haptics_race_tick(uint8_t*, recomp_context*);
void aero_haptics_frame(uint8_t*, recomp_context*);
}

int main() {
    std::vector<uint8_t> memory(8 * 1024 * 1024);
    auto* rdram = memory.data();
    recomp_context ctx{};
    const auto directory = std::filesystem::temp_directory_path() /
        ("aero-accessories-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(directory);
    const auto path = directory / "controller.mpk";
    aero::pak::configure(path);
    ctx.r29 = (gpr)(int32_t)0x807FF000;
    ctx.r7 = (gpr)(int32_t)0x80001000;
    ctx.r6 = 0x3FF; // last legal block: prove boundaries and guest byte order
    for (int i = 0; i < 32; ++i) MEM_B(i, ctx.r7) = i * 7;
    aero_pak_write(rdram, &ctx);
    assert(ctx.r2 == 0 && std::filesystem::file_size(path) == 32768);
    aero::pak::configure(path);
    for (int i = 0; i < 32; ++i) MEM_B(i, ctx.r7) = 0;
    aero_pak_read(rdram, &ctx);
    for (int i = 0; i < 32; ++i) assert(MEM_BU(i, ctx.r7) == static_cast<uint8_t>(i * 7));

    // Native block glue must reject malformed guest pointers without touching
    // host memory or acknowledging the transfer.
    ctx.r7 = 0;
    ctx.r6 = 0;
    aero_pak_read(rdram, &ctx);
    assert(ctx.r2 == 4);
    aero_pak_write(rdram, &ctx);
    assert(ctx.r2 == 4);
    ctx.r7 = (gpr)(int32_t)0x807FFFE1;
    aero_pak_read(rdram, &ctx);
    assert(ctx.r2 == 4);
    aero_pak_write(rdram, &ctx);
    assert(ctx.r2 == 4);
    ctx.r7 = (gpr)(int32_t)0x80001000;

    // Failed publication must preserve both the last good disk and memory image.
    std::filesystem::create_directory(path.string() + ".tmp");
    MEM_B(0, ctx.r7) = 99;
    aero_pak_write(rdram, &ctx);
    assert(ctx.r2 == 4);
    aero_pak_read(rdram, &ctx);
    assert(MEM_BU(0, ctx.r7) == 0);
    aero::pak::configure(path);
    aero_pak_read(rdram, &ctx);
    assert(MEM_BU(0, ctx.r7) == 0);
    ctx.r6 = 1;
    aero_pak_read(rdram, &ctx);
    const int id = MEM_BU(1, ctx.r7);
    MEM_B(1, ctx.r7) = 99;
    aero_pak_write(rdram, &ctx); // protected block, no force
    assert(ctx.r2 == 0);
    aero_pak_read(rdram, &ctx);
    assert(MEM_BU(1, ctx.r7) == id);
    ctx.r6 = 0x400;
    aero_pak_read(rdram, &ctx);
    assert(ctx.r2 == 4);
    ctx.r5 = 1;
    aero_pak_status(rdram, &ctx);
    assert(ctx.r2 == 1);
    ctx.r5 = 0;
    aero::pak::configure(path, false);
    aero_pak_status(rdram, &ctx);
    assert(ctx.r2 == 1);
    const auto bad = directory / "truncated.mpk";
    { std::ofstream stream(bad); stream << "keep me"; }
    aero::pak::configure(bad);
    aero_pak_write(rdram, &ctx);
    assert(ctx.r2 == 1 && std::filesystem::file_size(bad) == 7);

    using namespace aero::haptics;
    configure(true);
    const gpr car = (gpr)(int32_t)0x80002000;
    ctx.r16 = car;
    MEM_W(0, (gpr)(int32_t)0x8013FF80) = 5;
    MEM_W(0, (gpr)(int32_t)0x8013FF88) = 3;
    MEM_W(4, car) = (int32_t)0x8005C750;
    MEM_B(0x55, car) = 8;
    aero_haptics_race_tick(rdram, &ctx);
    assert(sample().low == 0x5000);
    MEM_W(0x24, car) = std::bit_cast<int32_t>(3.0f);
    aero_haptics_race_tick(rdram, &ctx);
    assert(sample().low == 0xC000); // impact overrides overlapping turbo
    std::this_thread::sleep_for(std::chrono::milliseconds(170));
    assert(sample().low == 0); // no stale effect after a stalled guest
    MEM_W(4, car) = (int32_t)0x8005C878; // another player's callback
    aero_haptics_race_tick(rdram, &ctx);
    assert(sample().low == 0);
    MEM_W(4, car) = (int32_t)0x8005C750;
    aero_haptics_race_tick(rdram, &ctx);
    MEM_W(0, (gpr)(int32_t)0x8013FF88) = 4; // pause
    aero_haptics_frame(rdram, &ctx);
    assert(sample().low == 0);
    motor(true);
    assert(sample().low == 0xFFFF);
    motor(false);
    assert(sample().low == 0);
    configure(false);
    motor(true);
    assert(sample().low == 0);
    configure(true, false);
    MEM_W(0, (gpr)(int32_t)0x8013FF88) = 3;
    MEM_W(0x24, car) = 0;
    aero_haptics_race_tick(rdram, &ctx);
    assert(sample().low == 0); // impacts-only preference
    std::filesystem::remove_all(directory);
}
