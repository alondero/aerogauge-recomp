#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace aero::gyro {
// Pure host input policy. Angles/rates are radians and radians/second; sensor
// timestamps are seconds. SDL sampling and all mutable state stay on its thread.
struct Settings {
    bool enabled = false;
    float sensitivity = 1.0f; // Full steering at 30 degrees / sensitivity.
    float dead_zone = 2.0f;   // Degrees, removed continuously from the range.
};

inline Settings sanitize(Settings value) {
    value.sensitivity = std::isfinite(value.sensitivity) ? std::clamp(value.sensitivity, 0.25f, 4.0f) : 1.0f;
    value.dead_zone = std::isfinite(value.dead_zone) ? std::clamp(value.dead_zone, 0.0f, 5.0f) : 2.0f;
    return value;
}

struct Sample {
    double time;
    float ax, ay, az; // Acceleration including gravity, m/s^2.
    float gx, gy, gz;
};

struct Reading {
    float x = 0, y = 0, z = 0;
    uint32_t received_ms = 0; // SDL event delivery clock, wrapping at 2^32 ms.
    uint64_t sensor_us = 0;   // Optional driver timestamp; Android SDL reports 0.
    bool received = false;
};

inline bool sensor_sample(const Reading& accel, const Reading& gyro, uint64_t now_ms, Sample& sample) {
    const auto accel_age = uint32_t(now_ms) - accel.received_ms;
    const auto gyro_age = uint32_t(now_ms) - gyro.received_ms;
    if (!accel.received || !gyro.received || accel_age > 250 || gyro_age > 250 ||
        (accel_age > gyro_age ? accel_age - gyro_age : gyro_age - accel_age) > 100) return false;
    for (float value : {accel.x, accel.y, accel.z, gyro.x, gyro.y, gyro.z})
        if (!std::isfinite(value)) return false;
    // Zero hardware timestamps mean unavailable timing, not an unavailable
    // sensor. Fresh event delivery supplies the fallback clock and stale events
    // are rejected above even when a driver keeps returning cached sensor data.
    const double time = gyro.sensor_us ? double(gyro.sensor_us) / 1000000.0 : double(now_ms - gyro_age) / 1000.0;
    sample = {time, accel.x, accel.y, accel.z, gyro.x, gyro.y, gyro.z};
    return true;
}

// Android Display.getRotation(), relative to the natural orientation (including
// landscape-native tablets). Gyro Z is unchanged by these rotations about Z.
inline Sample screen_sample(Sample sample, int rotation) {
    const float x = sample.ax, y = sample.ay;
    switch (rotation) {
    case 1: sample.ax = -y; sample.ay = x; break;
    case 2: sample.ax = -x; sample.ay = -y; break;
    case 3: sample.ax = y; sample.ay = -x; break;
    }
    return sample;
}

inline float wrap(float angle) {
    return std::remainder(angle, 6.28318530718f);
}

enum class Status { Uncalibrated, Calibrating, Ready };

class Steering {
    static constexpr float radians = 0.01745329252f;
    Status state = Status::Uncalibrated;
    double previous = -1.0;
    float angle = 0, neutral = 0, bias = 0;
    double calibration_time = 0;
    float sum_x = 0, sum_y = 0, sum_bias = 0;

    void clear_average() { calibration_time = 0; sum_x = sum_y = sum_bias = 0; }
public:
    Status status() const { return state; }
    void reset() { *this = Steering{}; }
    void calibrate() {
        reset();
        state = Status::Calibrating;
    }

    void update(const Sample& sample) {
        if (!std::isfinite(sample.time) || !std::isfinite(sample.ax) || !std::isfinite(sample.ay) ||
            !std::isfinite(sample.az) || !std::isfinite(sample.gx) || !std::isfinite(sample.gy) ||
            !std::isfinite(sample.gz)) { reset(); return; }
        if (sample.time == previous) return; // Never integrate a cached SDL reading twice.
        const double dt = previous < 0 ? 0 : sample.time - previous;
        if (dt < 0 || dt > 0.25) { reset(); return; }
        previous = sample.time;
        const float gravity = std::sqrt(sample.ax * sample.ax + sample.ay * sample.ay + sample.az * sample.az);
        const bool stable_gravity = gravity > 7.8f && gravity < 11.8f &&
            std::hypot(sample.ax, sample.ay) > 4.9f;
        const float measured = -std::atan2(sample.ax, sample.ay);
        if (state == Status::Calibrating) {
            // Half a second at rest, held like a steering wheel. Movement restarts
            // the average; calibration never learns an intentional turn as bias.
            if (!stable_gravity || std::hypot(sample.gx, sample.gy, sample.gz) > 0.15f) {
                clear_average();
                return;
            }
            calibration_time += dt;
            sum_x += sample.ax * float(dt);
            sum_y += sample.ay * float(dt);
            sum_bias += sample.gz * float(dt);
            if (calibration_time >= 0.5) {
                angle = neutral = -std::atan2(sum_x, sum_y);
                bias = sum_bias / float(calibration_time);
                state = Status::Ready;
            }
        } else if (state == Status::Ready) {
            angle = wrap(angle - (sample.gz - bias) * float(dt));
            // A complementary filter anchors gyro integration to gravity, while
            // rejecting strong linear acceleration. It crosses +/-pi continuously.
            if (stable_gravity) angle = wrap(angle + wrap(measured - angle) * (1.0f - std::exp(-float(dt) * 2.0f)));
        }
    }

    float steering(Settings settings) const {
        settings = sanitize(settings);
        if (!settings.enabled || state != Status::Ready) return 0;
        const float degrees = wrap(angle - neutral) / radians;
        const float magnitude = std::max(0.0f, std::abs(degrees) - settings.dead_zone);
        return std::copysign(std::min(1.0f, magnitude / (30.0f / settings.sensitivity - settings.dead_zone)), degrees);
    }
};
}
