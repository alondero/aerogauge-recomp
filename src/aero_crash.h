// Native crash reporting for the translated game.
//
// The handler is a last-resort diagnostic boundary. It reports the native
// fault, maps a generated native PC back to a guest address when symbols are
// available, and prints the recent trace ring. It must not allocate complex
// runtime state or attempt to resume a crashed game thread.

#ifndef AERO_CRASH_H
#define AERO_CRASH_H

#include <cstdint>
#include <cstdio>

namespace aero::crash {

struct SymbolInfo {
    const char* name;
    uint32_t    vram;
    uint32_t    size;
};

// Idempotent. Safe to call before register_overlays(); the native_pc -> vram
// map just stays empty until register_overlays() feeds it sections.
void install();

// Thread-safe; push sites are checked by handlers for fault context.
void record_recent(uint32_t vram, uint32_t ra_vram);

// nullptr when the vram is below the lowest symbol, in a gap, or past end.
const SymbolInfo* lookup(uint32_t vram, uint32_t* out_offset_bytes = nullptr);

// Smoke-test path. Use only with AERO_CRASH_TEST.
[[noreturn]] void crash_dump_and_die(const char* reason);

} // namespace aero::crash

#endif // AERO_CRASH_H
