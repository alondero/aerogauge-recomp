// Test-only stub for func_80036C54 (race-BGM preload), so the standalone
// warp-gating test (tests/test_warp_gating.cpp) can link without the
// recompiled ROM image. The warp at src/aero_warp.c:220 invokes the loader
// to populate the music blob and the loaded-flag; the stub below captures
// the contract the warp is supposed to forge:
//
//   - RP_GROUP (0x8013FF94, u8) = (track < 4) ? 1 : 2
//   - RP_TRACK (0x8013FF9B, u8) = requested track
//
// A real launch carries these via menu confirm (func_80051F2C); the warp
// forges them instead and calls the loader directly so the warp-skip-the-menu
// path doesn't leave the race silent.
//
// Linked only by tests/test_warp_gating.cpp — not part of aerogauge_modern.

#include <stdint.h>

#include "recomp.h"

// Mirror src/aero_warp.c's byte-write helpers (XOR-swizzled on the host buffer,
// per recomp.h's MEM_B convention).
static void w8(uint8_t* rdram, uint32_t a, uint8_t v) {
    rdram[((a ^ 3u) - 0x80000000u)] = v;
}
static uint8_t r8(const uint8_t* rdram, uint32_t a) {
    return rdram[((a ^ 3u) - 0x80000000u)];
}

// Last observed warp→loader input. The test reads this back to pin the
// contract. (Static so multiple .c files don't fight.)
static uint32_t g_last_track;
static uint8_t  g_last_group;
static uint8_t  g_last_track_byte;
static uint8_t  g_loader_called;
static uint8_t  g_loader_skip_called; // 1 if warp saw music_id == 0 and bailed

// Production func_80036C54 is void(uint8_t*, recomp_context*). We don't touch
// ctx; the warp passes the rdram through unchanged, which is what production
// does too (the loader reads + writes RAM directly).
void func_80036C54(uint8_t* rdram, recomp_context* ctx) {
    (void)ctx;
    g_loader_called++;
    g_last_group      = r8(rdram, 0x8013FF94u);
    g_last_track_byte = r8(rdram, 0x8013FF9Bu);
    // Simulate the loader's no-op when the music-id lookup is 0
    // (group == 0 indexes the all-zero row in the music-id table 0x800974F0).
    if (g_last_group == 0) {
        g_loader_skip_called++;
    }
}

// Test introspection — defined here so the test doesn't reach into statics.
uint32_t warp_loader_stub_last_track(void)        { return g_last_track; }
uint8_t  warp_loader_stub_last_group(void)        { return g_last_group; }
uint8_t  warp_loader_stub_last_track_byte(void)   { return g_last_track_byte; }
uint8_t  warp_loader_stub_called(void)            { return g_loader_called; }
uint8_t  warp_loader_stub_skipped(void)           { return g_loader_skip_called; }
void     warp_loader_stub_reset(void)             {
    g_last_track = 0; g_last_group = 0; g_last_track_byte = 0;
    g_loader_called = 0; g_loader_skip_called = 0;
}
