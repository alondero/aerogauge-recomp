// Host test for the runtime's live graphics-configuration handoff (issue #25).
//
// ultramodern::renderer::get_graphics_config() used to return a reference into the shared
// config after releasing the mutex that guarded it. A live set_graphics_config() from the menu
// thread therefore wrote while game, VI, and graphics threads read the caller's reference.
// Local patch 0018 returns a snapshot copy taken with the mutex held.
//
// This test links the dependency's real renderer_context.cpp and pins two things:
//
//   1. the accessor's return type is a value, so no caller can reach shared state after the
//      lock is released. This half is a compile-time guard: restoring the reference return
//      type fails the static_assert below rather than only failing under a sanitizer.
//   2. a caller's copy is internally consistent while writers hammer set_graphics_config().
//      Each written config encodes one generation counter across fields that sit in different
//      parts of the object, so a copy taken without the lock can be observed half-updated.
//      ThreadSanitizer reports the same defect directly; the coherence check asks the
//      equivalent question when there is no sanitizer run.
//
// No ROM, no renderer, no SDL.

#undef NDEBUG
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <type_traits>
#include <vector>

#include "ultramodern/config.hpp"

// Declared in ultramodern/ultramodern.hpp. renderer_context.cpp reports each accepted write
// through the runtime's action queue, which lives in events.cpp; this focused test links
// neither the queue nor the graphics thread that drains it.
namespace ultramodern {
void trigger_config_action();
void trigger_config_action() {}
}

// renderer_context.cpp's unrelated renderer-factory entry point references these. This test
// never creates a context, so they only need to exist for the linker.
namespace ultramodern::error_handling {
void message_box(const char*) {}
[[noreturn]] void quick_exit(const char*, int, const char*, int) { std::abort(); }
}

namespace {

using ultramodern::renderer::Antialiasing;
using ultramodern::renderer::GraphicsConfig;
using ultramodern::renderer::WindowMode;

// Restoring `const GraphicsConfig&` reintroduces the race; fail the build in that case.
static_assert(
    std::is_same_v<decltype(ultramodern::renderer::get_graphics_config()), GraphicsConfig>,
    "get_graphics_config() must return a GraphicsConfig snapshot by value");

// One generation counter spread over a bool, three enums, and two ints, in that order in the
// object. A copy taken while another thread writes a neighbouring generation mixes them.
GraphicsConfig encode_generation(uint32_t generation) {
    GraphicsConfig config{};
    config.developer_mode = (generation & 1u) != 0;
    config.res_option = ultramodern::renderer::Resolution::Auto;
    config.wm_option =
        ((generation >> 1) & 1u) != 0 ? WindowMode::Fullscreen : WindowMode::Windowed;
    config.hr_option = ultramodern::renderer::HUDRatioMode::Clamp16x9;
    config.api_option = ultramodern::renderer::GraphicsApi::Auto;
    config.ar_option = ultramodern::renderer::AspectRatio::Expand;
    config.msaa_option = static_cast<Antialiasing>(generation % 4u);
    config.rr_option = ultramodern::renderer::RefreshRate::Manual;
    config.hpfb_option = ultramodern::renderer::HighPrecisionFramebuffer::Auto;
    config.rr_manual_value = static_cast<int>(generation);
    config.ds_option = static_cast<int>(generation);
    return config;
}

// Every generation-carrying field must agree with the one recovered from rr_manual_value.
bool coherent(const GraphicsConfig& config) {
    const uint32_t generation = static_cast<uint32_t>(config.rr_manual_value);
    return config.ds_option == config.rr_manual_value &&
           config.developer_mode == ((generation & 1u) != 0) &&
           config.wm_option == (((generation >> 1) & 1u) != 0 ? WindowMode::Fullscreen
                                                             : WindowMode::Windowed) &&
           config.msaa_option == static_cast<Antialiasing>(generation % 4u);
}

} // anonymous namespace

int main() {
    constexpr int kWriterThreads = 4;
    constexpr int kReaderThreads = 4;
    constexpr int kIterations = 100000;

    std::atomic<bool> stop{false};
    std::atomic<int> torn{0};
    std::vector<std::thread> threads;
    threads.reserve(kWriterThreads + kReaderThreads);

    for (int writer = 0; writer < kWriterThreads; ++writer) {
        threads.emplace_back([writer]() {
            for (int n = 0; n < kIterations; ++n) {
                ultramodern::renderer::set_graphics_config(
                    encode_generation(static_cast<uint32_t>(writer) * kIterations + n));
            }
        });
    }

    for (int reader = 0; reader < kReaderThreads; ++reader) {
        threads.emplace_back([&stop, &torn]() {
            while (!stop.load(std::memory_order_relaxed)) {
                if (!coherent(ultramodern::renderer::get_graphics_config())) {
                    torn.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }

    for (int writer = 0; writer < kWriterThreads; ++writer) {
        threads[writer].join();
    }
    // Only now that writing has stopped can the readers observe a settled value.
    stop.store(true, std::memory_order_relaxed);
    for (int reader = kWriterThreads; reader < kWriterThreads + kReaderThreads; ++reader) {
        threads[reader].join();
    }

    const int torn_count = torn.load();
    if (torn_count != 0) {
        std::fprintf(stderr, "FAIL: %d torn graphics-config snapshots observed\n", torn_count);
        return 1;
    }

    const GraphicsConfig last =
        encode_generation(static_cast<uint32_t>((kWriterThreads - 1) * kIterations +
                                                (kIterations - 1)));
    ultramodern::renderer::set_graphics_config(last);
    assert(ultramodern::renderer::get_graphics_config() == last);
    assert(coherent(ultramodern::renderer::get_graphics_config()));

    std::printf("graphics config snapshots are coherent under %d writer and %d reader threads\n",
                kWriterThreads, kReaderThreads);
    return 0;
}
