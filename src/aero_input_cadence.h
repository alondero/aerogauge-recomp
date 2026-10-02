#ifndef AERO_INPUT_CADENCE_H
#define AERO_INPUT_CADENCE_H

#include <chrono>

// The runtime invokes the SDL bridge every millisecond. Sample at most 250 Hz
// on the main thread: still faster than guest updates or normal presentation,
// adding up to 4 ms of polling delay before the next runtime callback. Never
// replay missed polls after a stall; operating-system scheduling can add delay.
class AeroInputCadence {
public:
    using Clock = std::chrono::steady_clock;

    bool due(Clock::time_point now) {
        if (started && now < next) return false;
        started = true;
        next = now + std::chrono::milliseconds(4);
        return true;
    }

private:
    bool started = false;
    Clock::time_point next{};
};

#endif
