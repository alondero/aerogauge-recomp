// Host block device for the ROM's original Controller Pak filesystem.
// The game retains ownership of note allocation, checksums, repair and ghost data.
#include "aero_pak.h"
#include "recomp.h"

#include <array>
#include <cstdio>
#include <fstream>
#include <mutex>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {
constexpr int NoPak = 1;
constexpr int IoError = 4; // PFS_ERR_CONTRFAIL
constexpr size_t BlockSize = 32;
using Image = std::array<uint8_t, 32768>;
std::mutex mutex;
Image image{};
std::filesystem::path image_path;
bool available = false;

// Same SDK-compatible one-bank layout as Automobili Lamborghini's pak formatter:
// four redundant ID blocks, two inode tables, 123 free pages and 16 empty notes.
void format() {
    image.fill(0);
    for (unsigned block : {1u, 3u, 4u, 6u}) {
        auto* id = image.data() + block * BlockSize;
        id[1] = 0x2A;
        id[5] = 5; id[6] = 0xA1; id[7] = 0xB0;
        id[8] = 5; id[9] = 7; id[10] = 0xC3; id[11] = 0x39;
        id[25] = 1; // memory device
        id[26] = 1; // one bank
        uint16_t sum = 0, inverse = 0;
        for (unsigned i = 0; i < 28; i += 2) {
            const uint16_t word = (id[i] << 8) | id[i + 1];
            sum += word;
            inverse += static_cast<uint16_t>(~word);
        }
        id[28] = sum >> 8; id[29] = sum;
        id[30] = inverse >> 8; id[31] = inverse;
    }
    for (unsigned page : {1u, 2u}) {
        for (unsigned slot = 0; slot < 128; ++slot) image[page * 256 + slot * 2 + 1] = 3;
        image[page * 256 + 1] = (123 * 3) & 255;
    }
}

bool publish(const Image& next) {
    auto temporary = image_path;
    temporary += ".tmp";
    std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
    stream.write(reinterpret_cast<const char*>(next.data()), next.size());
    stream.close();
    bool success = !stream.fail();
    if (success) {
#ifdef _WIN32
        success = MoveFileExW(temporary.c_str(), image_path.c_str(),
                             MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
        std::error_code error;
        std::filesystem::rename(temporary, image_path, error);
        success = !error;
#endif
    }
    if (!success) {
        std::fprintf(stderr, "[pak] Save failed; previous Controller Pak image retained.\n");
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
    }
    return success;
}
}

void aero::pak::configure(const std::filesystem::path& path, bool enabled) {
    std::lock_guard lock(mutex);
    image_path = path;
    available = false;
    if (!enabled) return;
    std::error_code error;
    const bool exists = std::filesystem::exists(path, error);
    if (error) {
        std::fprintf(stderr, "[pak] Cannot inspect Controller Pak image.\n");
        return;
    }
    if (!exists) {
        format();
        available = true;
        return;
    }
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream || stream.tellg() != static_cast<std::streamoff>(image.size())) {
        std::fprintf(stderr, "[pak] Cannot load Controller Pak: expected a readable 32 KiB MPK; file left untouched.\n");
        return;
    }
    stream.seekg(0);
    stream.read(reinterpret_cast<char*>(image.data()), image.size());
    available = stream.good();
}

// __osPfsGetStatus(queue, channel), ROM 0x800742F0.
extern "C" void aero_pak_status(uint8_t*, recomp_context* ctx) {
    std::lock_guard lock(mutex);
    ctx->r2 = available && ctx->r5 == 0 ? 0 : NoPak;
}

// __osContRamRead(queue, channel, block, buffer), ROM 0x80075290.
extern "C" void aero_pak_read(uint8_t* rdram, recomp_context* ctx) {
    std::lock_guard lock(mutex);
    const unsigned block = static_cast<uint16_t>(ctx->r6);
    ctx->r2 = NoPak;
    if (!available || ctx->r5 != 0) return;
    ctx->r2 = IoError;
    if (block >= image.size() / BlockSize) return;
    const gpr buffer = static_cast<int32_t>(ctx->r7);
    for (unsigned i = 0; i < BlockSize; ++i) MEM_B(i, buffer) = image[block * BlockSize + i];
    ctx->r2 = 0;
}

// __osContRamWrite(queue, channel, block, buffer, force), ROM 0x80077260.
extern "C" void aero_pak_write(uint8_t* rdram, recomp_context* ctx) {
    std::lock_guard lock(mutex);
    const unsigned block = static_cast<uint16_t>(ctx->r6);
    ctx->r2 = NoPak;
    if (!available || ctx->r5 != 0) return;
    ctx->r2 = 0;
    // The SDK silently ignores unforced writes to protected ID blocks 1..6.
    if (block > 0 && block < 7 && MEM_W(0x10, ctx->r29) != 1) return;
    const gpr buffer = static_cast<int32_t>(ctx->r7);
    // __osPfsSelectBank writes its active bank to address 0x8000 (block 0x400).
    if (block == 0x400) {
        ctx->r2 = MEM_BU(0, buffer) == 0 ? 0 : IoError;
        return;
    }
    ctx->r2 = IoError;
    if (block >= image.size() / BlockSize) return;
    Image next = image;
    for (unsigned i = 0; i < BlockSize; ++i) next[block * BlockSize + i] = MEM_BU(i, buffer);
    // A successful guest write is already on disk, including at the port's _Exit.
    // Never acknowledge a failed publish or mutate the in-memory image on failure.
    if (publish(next)) {
        image = next;
        ctx->r2 = 0;
    }
}
