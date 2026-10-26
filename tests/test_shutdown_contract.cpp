// Host test for the runtime's coordinated game-thread shutdown contract (issue #114).
//
// The port owns the process and the runtime owns the guest threads, so quitting
// is a handshake: request() stops new guest work and releases the parked guest
// threads, the owner flushes its stores, and join_threads() waits until no
// guest thread can still be executing translated code against RDRAM.
//
// This test links the dependency's real shutdown coordinator and pins the
// parts of the contract that the port depends on:
//
//   1. request() publishes the flag and releases every registered guest thread
//      exactly once, and is idempotent: a second request must not wake a
//      thread twice, because a second wake would leave a stray scheduler
//      signal behind.
//   2. A guest thread that is registered after the request is released right
//      away: a shutdown admits no new guest work.
//   3. join_threads() reports the number of threads that are still running, so
//      the owner can tell a clean join from one that ran out of time.
//   4. unregister_thread() for an unknown token must not corrupt the count.
//
// No ROM, no renderer, no SDL, no game threads.

#undef NDEBUG
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <thread>
#include <vector>

#include "ultramodern/shutdown.hpp"

namespace {

// One wake counter per simulated guest thread. The token is the thread's
// index, which is what the runtime's wake callback would use to find the
// thread's scheduler semaphore.
constexpr int kThreadCount = 4;
constexpr int kLateThread = kThreadCount;   // registered after the request
constexpr int kSlotCount = kThreadCount + 1;
std::atomic<int> g_wake_counts[kSlotCount];

void wake_thread(void* token) {
    const int index = static_cast<int>(reinterpret_cast<intptr_t>(token));
    assert(index >= 0 && index < kSlotCount);
    g_wake_counts[index].fetch_add(1, std::memory_order_relaxed);
}

int fail(const char* what) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    return 1;
}

} // anonymous namespace

int main() {
    using namespace std::chrono_literals;

    if (ultramodern::shutdown::requested()) {
        return fail("the shutdown flag must start unset");
    }
    if (ultramodern::shutdown::pending_threads() != 0) {
        return fail("no guest thread may be registered at startup");
    }

    // --- phase 1: register the guest threads -------------------------------
    for (int i = 0; i < kThreadCount; ++i) {
        ultramodern::shutdown::register_thread(reinterpret_cast<void*>(static_cast<intptr_t>(i)),
                                               &wake_thread);
    }
    if (ultramodern::shutdown::pending_threads() != kThreadCount) {
        return fail("every registered guest thread must be counted as pending");
    }

    // --- phase 1: request() releases every registered thread exactly once ---
    ultramodern::shutdown::request();
    if (!ultramodern::shutdown::requested()) {
        return fail("request() must publish the shutdown flag");
    }
    for (int i = 0; i < kThreadCount; ++i) {
        if (g_wake_counts[i].load(std::memory_order_relaxed) != 1) {
            return fail("request() must release every registered guest thread exactly once");
        }
    }

    // A second request must not release a thread again: a stray scheduler
    // signal would let a guest thread resume without being scheduled.
    ultramodern::shutdown::request();
    for (int i = 0; i < kThreadCount; ++i) {
        if (g_wake_counts[i].load(std::memory_order_relaxed) != 1) {
            return fail("request() must be idempotent");
        }
    }

    // --- phase 1: a thread created after the request is released at once ---
    ultramodern::shutdown::register_thread(
        reinterpret_cast<void*>(static_cast<intptr_t>(kLateThread)), &wake_thread);
    if (g_wake_counts[kLateThread].load(std::memory_order_relaxed) != 1) {
        return fail("a guest thread registered after the request must be released at once");
    }
    if (ultramodern::shutdown::pending_threads() != kThreadCount + 1) {
        return fail("a late guest thread must be counted as pending");
    }

    // --- phase 3: the join reports the threads that are still running -------
    if (ultramodern::shutdown::join_threads(20ms) != kThreadCount + 1) {
        return fail("join_threads() must report the guest threads that are still running");
    }

    // Unregistering an unknown token must not corrupt the count.
    ultramodern::shutdown::unregister_thread(reinterpret_cast<void*>(static_cast<intptr_t>(9999)));
    if (ultramodern::shutdown::pending_threads() != kThreadCount + 1) {
        return fail("unregistering an unknown token must not change the pending count");
    }

    // --- phase 3: the join completes once every thread has unwound ----------
    for (int i = 0; i <= kLateThread; ++i) {
        ultramodern::shutdown::unregister_thread(
            reinterpret_cast<void*>(static_cast<intptr_t>(i)));
    }
    if (ultramodern::shutdown::pending_threads() != 0) {
        return fail("every unregistered guest thread must leave the pending count");
    }
    if (ultramodern::shutdown::join_threads(20ms) != 0) {
        return fail("join_threads() must report a clean join once every thread unwound");
    }

    std::printf("coordinated shutdown contract holds for %d guest threads\n", kThreadCount);
    return 0;
}
