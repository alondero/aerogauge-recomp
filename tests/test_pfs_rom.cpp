// Exercises the actual ROM SDK filesystem against our block device, including
// a ghost-sized note surviving a fresh guest RAM and device initialization.
#undef NDEBUG
#include <cassert>
#include <chrono>
#include <fstream>
#include <iostream>
#include <vector>
#include "aero_pak.h"
#include "recomp.h"

extern "C" {
void func_8006B440(uint8_t*, recomp_context*); // osPfsInitPak
void func_8006F040(uint8_t*, recomp_context*); // osPfsAllocateFile
void func_8006CDE0(uint8_t*, recomp_context*); // osPfsFindFile
void func_8006EC1C(uint8_t*, recomp_context*); // osPfsReadWriteFile
void func_8006D0F0(uint8_t*, recomp_context*); // osPfsFreeBlocks
// Only the SDK's SI lock uses queues in this single-threaded harness. Hardware
// transfers are replaced at the block boundary, exactly as in the shipping port.
void osCreateMesgQueue_recomp(uint8_t*, recomp_context*) {}
void osRecvMesg_recomp(uint8_t*, recomp_context* ctx) { ctx->r2 = 0; }
void osSendMesg_recomp(uint8_t*, recomp_context* ctx) { ctx->r2 = 0; }
void osGetCount_recomp(uint8_t*, recomp_context* ctx) { ctx->r2 = 123456; }
}

int main(int argc, char** argv) {
    assert(argc == 2);
    const auto path = std::filesystem::temp_directory_path() /
        ("aero-pfs-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".mpk");
    constexpr gpr stack = (gpr)(int32_t)0x807FF000;
    constexpr gpr pfs = (gpr)(int32_t)0x80400000;
    constexpr gpr name = pfs + 0x100, ext = name + 32, number = ext + 32;
    constexpr gpr buffer = pfs + 0x1000;
    constexpr int size = 0x2DE0; // AeroGauge ghost slot size
    std::vector<uint8_t> memory(8 * 1024 * 1024);
    auto* rdram = memory.data();
    recomp_context ctx{};
    for (int run = 0; run < 2; ++run) {
        // Reset SDK caches and globals to their original ROM values.
        std::fill(memory.begin(), memory.end(), 0);
        std::ifstream rom(argv[1], std::ios::binary);
        assert(rom);
        rom.seekg(0x1000);
        std::vector<char> data(0x100000);
        rom.read(data.data(), data.size());
        assert(rom);
        for (size_t i = 0; i < data.size(); ++i) MEM_B(i, (gpr)(int32_t)0x80000400) = data[i];
        aero::pak::configure(path);
        ctx = {};
        ctx.r29 = stack;
        ctx.r4 = pfs + 0x200;
        ctx.r5 = pfs;
        ctx.r6 = 0;
        func_8006B440(rdram, &ctx);
        std::cout << "InitPak run " << run << ": " << (int)ctx.r2 << '\n';
        assert(ctx.r2 == 0);
        MEM_B(0, name) = 0x1A;
        MEM_B(0, ext) = 0x1B;
        ctx.r4 = pfs; ctx.r5 = 0x1234; ctx.r6 = 0x4E414745; ctx.r7 = name;
        MEM_W(0x10, stack) = static_cast<int32_t>(ext);
        if (run == 0) {
            MEM_W(0x14, stack) = size;
            MEM_W(0x18, stack) = static_cast<int32_t>(number);
            func_8006F040(rdram, &ctx);
        } else {
            MEM_W(0x14, stack) = static_cast<int32_t>(number);
            func_8006CDE0(rdram, &ctx);
        }
        assert(ctx.r2 == 0);
        for (int i = 0; i < size; ++i) MEM_B(i, buffer) = run == 0 ? (i * 37 + 11) & 255 : 0;
        ctx.r4 = pfs; ctx.r5 = MEM_W(0, number); ctx.r6 = run == 0 ? 1 : 0; ctx.r7 = 0;
        MEM_W(0x10, stack) = size;
        MEM_W(0x14, stack) = static_cast<int32_t>(buffer);
        func_8006EC1C(rdram, &ctx);
        assert(ctx.r2 == 0);
        for (int i = 0; i < size; ++i) assert(MEM_BU(i, buffer) == ((i * 37 + 11) & 255));
        ctx.r4 = pfs; ctx.r5 = number;
        func_8006D0F0(rdram, &ctx);
        assert(ctx.r2 == 0);
        assert(MEM_W(0, number) == (123 - (size + 255) / 256) * 256);
    }
    std::filesystem::remove(path);
    std::cout << "PASS: ROM filesystem allocated, wrote, reopened and read a ghost-sized note\n";
}
