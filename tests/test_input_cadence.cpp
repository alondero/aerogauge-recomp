#include "aero_input_cadence.h"

#include <cstdio>
#include <cstdlib>

static void require(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

int main() {
    using namespace std::chrono;
    AeroInputCadence cadence;
    const auto start = AeroInputCadence::Clock::time_point{};
    unsigned polls = 0;
    for (unsigned ms = 0; ms < 1000; ++ms) {
        const bool due = cadence.due(start + milliseconds(ms));
        require(due == (ms % 4 == 0), "1 ms callbacks must pump every 4 ms, including startup");
        polls += due;
    }
    require(polls == 250, "one second must perform 250 input pumps instead of 1000");
    require(cadence.due(start + seconds(10)), "resume must pump immediately after a stall");
    require(!cadence.due(start + seconds(10)), "resume must not replay missed input pumps");
    require(!cadence.due(start + seconds(10) + milliseconds(3)), "resume retains the spacing bound");
    require(cadence.due(start + seconds(10) + milliseconds(4)), "resume uses a fresh deadline");
    std::puts("PASS input cadence: 1000 callbacks -> 250 pumps, no catch-up burst");
}
