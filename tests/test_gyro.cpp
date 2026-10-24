#include "aero_gyro.h"
#include "aero_input.h"

#include <cstdio>
#include <cstdlib>
#include <limits>

using namespace aero::gyro;
static void require(bool condition, const char* message) {
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
static bool near(float a, float b, float tolerance = 0.002f) { return std::abs(a - b) < tolerance; }
constexpr float rad = 0.01745329252f;
static Sample sample(double time, float degrees, float rate = 0.02f) {
    return {time, -9.80665f * std::sin(degrees * rad), 9.80665f * std::cos(degrees * rad), 0, 0, 0, rate};
}
static void calibrate(Steering& filter, double start = 1, float neutral = 0) {
    filter.calibrate();
    for (int i = 0; i <= 60; ++i) filter.update(sample(start + i * 0.01, neutral));
    require(filter.status() == Status::Ready, "half a second of stationary readings calibrates");
}
static void turn(Steering& filter, double start, float from, float to) {
    // One second of physical rotation: gyro is clockwise-negative and includes
    // a known 0.02 rad/s resting bias, independent of the expected steering.
    for (int i = 1; i <= 100; ++i)
        filter.update(sample(start + i * 0.01, from + (to - from) * i / 100, 0.02f - (to - from) * rad));
}
int main() {
    Steering first, second;
    Settings enabled{true, 1, 2};
    require(first.steering(enabled) == 0, "uncalibrated motion is neutral");
    calibrate(first, 1, 10);
    require(first.steering(enabled) == 0, "calibrated resting angle is neutral");
    turn(first, 1.6, 10, 40);
    require(near(first.steering(enabled), 1), "30 degree relative right turn reaches full steering");
    require(first.steering({}) == 0, "motion is default-off even with calibrated readings");
    require(second.steering(enabled) == 0, "another player never inherits calibration");
    turn(first, 2.6, 40, 10);
    require(near(first.steering(enabled), 0), "returning to neutral recenters steering");
    turn(first, 3.6, 10, 9);
    require(first.steering(enabled) == 0, "small left tilt stays in the dead zone");
    turn(first, 4.6, 9, -5);
    require(near(first.steering({true, 2, 2}), -1), "sensitivity 2 reaches full left at 15 degrees");
    require(near(first.steering({true, 1, 0}), -0.5f), "zero dead zone maps 15 degrees to half steering");
    require(near(first.steering(enabled), -13.0f / 28.0f), "dead zone is removed continuously");
    for (int i = 1; i <= 1000; ++i) first.update(sample(5.6 + i * 0.01, -5));
    require(near(first.steering(enabled), -13.0f / 28.0f), "calibrated gyro bias does not accumulate drift at rest");
    calibrate(first, 16, -5);
    require(first.steering(enabled) == 0, "explicit recalibration establishes a new neutral");
    first.update(sample(18, -5));
    require(first.status() == Status::Uncalibrated && first.steering(enabled) == 0,
            "long gaps invalidate calibration instead of integrating background time");
    calibrate(first, 19);
    first.update(sample(19.5, 0));
    require(first.status() == Status::Uncalibrated, "backward sensor timestamps invalidate calibration");
    calibrate(first, 20);
    const auto same_time_turn = sample(20.6, 30, -100);
    for (int i = 0; i < 500; ++i) first.update(same_time_turn);
    require(first.steering(enabled) == 0, "cached SDL samples never integrate twice");
    first.update(sample(20.61, std::numeric_limits<float>::quiet_NaN()));
    require(first.status() == Status::Uncalibrated && first.steering(enabled) == 0, "invalid sensors fail neutral");
    first.calibrate();
    for (int i = 0; i < 200; ++i) first.update(sample(21 + i * 0.01, 0, 1));
    require(first.status() == Status::Calibrating, "a moving controller cannot calibrate");
    first.reset();
    require(first.status() == Status::Uncalibrated, "source/orientation/lifecycle reset needs explicit calibration");

    // Fixed natural-axis vectors from Android's screen rotation geometry, also
    // used by the pinned SDL SDLSurface.onSensorChanged accelerometer path:
    // at ROTATION_90 natural +X points up; at ROTATION_270 natural -X points up.
    // Each row is the same clockwise screen tilt. Do not generate these inputs
    // with screen_sample or confuse rotation-matrix axes with vector components.
    const Sample rotated[] = {
        {1, -3, 8, 0, 0, 0, -0.2f}, {1, 8, 3, 0, 0, 0, -0.2f},
        {1, 3, -8, 0, 0, 0, -0.2f}, {1, -8, -3, 0, 0, 0, -0.2f}};
    const Sample upright[] = {
        {1, 0, 9.80665f, 0, 0, 0, 0.02f}, {1, 9.80665f, 0, 0, 0, 0, 0.02f},
        {1, 0, -9.80665f, 0, 0, 0, 0.02f}, {1, -9.80665f, 0, 0, 0, 0, 0.02f}};
    for (int rotation = 0; rotation < 4; ++rotation) {
        const auto screen = screen_sample(rotated[rotation], rotation);
        require(near(screen.ax, -3) && near(screen.ay, 8), "Android natural axes map to screen axes at each display rotation");
        require(screen.gz == -0.2f, "screen rotation preserves the gyro roll axis");
        auto neutral = screen_sample(upright[rotation], rotation);
        require(near(neutral.ax, 0) && near(neutral.ay, 9.80665f), "an upright screen has positive Y gravity in every orientation");
        Steering physical;
        physical.calibrate();
        for (int i = 0; i <= 60; ++i) {
            neutral.time = 1 + i * 0.01;
            physical.update(neutral);
        }
        require(physical.status() == Status::Ready && physical.steering(enabled) == 0,
                "physical upright vectors calibrate to neutral in every orientation");
        // Hold the fixed clockwise tilt, then its counterclockwise counterpart.
        // The gyro is stationary in both holds; gravity must settle to the right
        // direction without sustained disagreement with gyro integration.
        auto tilted = screen_sample(rotated[rotation], rotation);
        tilted.gz = 0.02f;
        for (int i = 1; i <= 300; ++i) {
            tilted.time = 1.6 + i * 0.01;
            physical.update(tilted);
        }
        require(physical.steering(enabled) > 0.6f, "clockwise physical tilt steers right in every orientation");
        Sample left = rotated[rotation];
        if (rotation % 2 == 0) left.ax = -left.ax;
        else left.ay = -left.ay;
        left = screen_sample(left, rotation);
        left.gz = 0.02f;
        for (int i = 1; i <= 300; ++i) {
            left.time = 4.6 + i * 0.01;
            physical.update(left);
        }
        require(physical.steering(enabled) < -0.6f, "counterclockwise physical tilt steers left in every orientation");
    }
    Steering android;
    android.calibrate();
    for (uint32_t ms = 1000; ms <= 1600; ms += 10) {
        Reading accel{0, 9.80665f, 0, ms, 0, true};
        Reading gyro{0, 0, 0.02f, ms, 0, true};
        Sample current{};
        require(sensor_sample(accel, gyro, ms, current), "Android sensor events work without hardware timestamps");
        android.update(current);
    }
    require(android.status() == Status::Ready && android.steering(enabled) == 0,
            "Android's zero-timestamp SDL backend can calibrate to neutral");
    Sample current{};
    const Reading accel{0, 9.80665f, 0, 2000, 0, true};
    const Reading gyro{0, 0, 0.02f, 2000, 0, true};
    require(!sensor_sample(accel, gyro, 2251, current), "cached gyro and acceleration expire after 250 ms");
    require(!sensor_sample(accel, Reading{0, 0, 0, 2110, 0, true}, 2110, current), "unsynchronized sensor delivery is rejected");
    require(!sensor_sample({}, gyro, 2000, current), "missing accelerometer readings are unavailable");
    require(sensor_sample(accel, Reading{0, 0, 0, 2000, 1234567, true}, 2000, current) &&
            current.time == 1.234567, "gamepad hardware timestamps retain microsecond precision");
    const Reading wrapped{0, 9.80665f, 0, 0xfffffff0u, 0, true};
    require(sensor_sample(wrapped, wrapped, 0x100000010ull, current) && current.time == 4294967.280,
            "SDL event clock rollover retains freshness and an unwrapped time");
    calibrate(first, 24, 175);
    turn(first, 24.6, 175, 190);
    require(near(first.steering({true, 1, 0}), 0.5f), "relative angles cross the pi boundary continuously");
    const auto invalid = sanitize({true, std::numeric_limits<float>::infinity(), -2});
    require(invalid.sensitivity == 1 && invalid.dead_zone == 0, "invalid preferences are bounded");
    require(sanitize({true, 100, 100}).sensitivity == 4 && sanitize({true, 100, 100}).dead_zone == 5,
            "preference bounds preserve a positive usable steering range");

    const auto motion = aero_merge_gyro(aero_sample_input(0x1030, 0, -0.5f), 1);
    require(motion.stick_x == 80 && motion.stick_y == -40 && motion.buttons == 0x1030,
            "gyro changes only steering; accelerator/Turbo/drift and vertical input survive");
    require(aero_merge_gyro(aero_sample_input(0, -1, 0), 1).stick_x == -80, "keyboard/gamepad steering wins");
    require(aero_merge_gyro(aero_sample_input(0, 0, 0, 0, -40, 0), 1).stick_x == -40, "touch steering wins");
    require(aero_merge_gyro(aero_sample_input(0, 0, 0), 0).stick_x == 0, "unavailable motion leaves bindings neutral");
    std::puts("PASS calibrated gyro: neutral, bias, range, dead zone, recenter, gaps, rotation, player isolation and manual input");
}
