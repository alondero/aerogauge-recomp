// Regression spec for the oversize-AI-length crash (graphics-enhancement session,
// 2026-07-11): early boot hands osAiSetNextBuffer one garbage length (measured
// 0xFFFF5000 bytes = a negative AI DMA length reinterpreted as unsigned), and
// ultramodern::queue_audio_buffer forwards it verbatim as sample_count. The
// queue_samples scaffold memset()s the payload BEFORE submit()'s AI-max bounds
// guard ran, so a windowed run died with EXCEPTION_ACCESS_VIOLATION zeroing ~4 GB
// of not-mapped memory. The guard must reject the length before ANY write.
//
// Standalone host test, no ROM build needed (same convention as the Lamborghini
// port's tests/). Compile from the repo root with the host compiler and run from
// build/ so SDL2.dll resolves:
//   g++ -I src -I lib/N64ModernRuntime/ultramodern/include \
//       -I lib/rt64/src/contrib/mupen64plus-win32-deps/SDL2-2.26.3/include \
//       tests/test_audio_oversize_guard.cpp src/aero_audio.cpp \
//       -L lib/rt64/src/contrib/mupen64plus-win32-deps/SDL2-2.26.3/lib/x64 \
//       -lSDL2 -o build/test_audio_oversize_guard && (cd build && ./test_audio_oversize_guard)
//
// Failure mode is a crash (access violation), not an assert: the test passing IS
// the process surviving both calls and printing PASS.

#include <cstdint>
#include <cstdio>

#include "../src/aero_audio.h"
#include <ultramodern/ultramodern.hpp>

int main() {
    ultramodern::audio_callbacks_t cb{};
    aero::audio::get_callbacks(&cb);

    // No aero::audio::init() on purpose: the crash does not depend on a device
    // (the memset ran unconditionally); skipping init keeps the test hermetic.

    // A deliberately SMALL real buffer with the measured garbage length. Any
    // write past the buffer is the bug; a ~4 GB memset is an instant AV.
    static int16_t buf[1024] = {};
    const size_t garbage_samples = 0xFFFF5000u / sizeof(int16_t); // as forwarded by
                                                                  // ultramodern::queue_audio_buffer
    cb.queue_samples(buf, garbage_samples);

    // Sanity: a legitimate small submit still works (drops silently, no device).
    buf[0] = 1234;
    cb.queue_samples(buf, 1024);

    std::puts("PASS test_audio_oversize_guard");
    return 0;
}
