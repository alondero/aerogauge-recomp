#include "aero_gyro_sdl.h"
#include "aero_config.h"
#include "ui/aero_frontend_settings.h"
#include "recompinput/input_state.h"
#include "recompinput/players.h"

#include <array>
#include <atomic>
#include <mutex>
#include <unordered_map>

#if defined(__ANDROID__)
#include <jni.h>
#endif

namespace aero::gyro {
namespace {
struct Player {
    Steering filter;
    int64_t source = -1;
    bool usable = false, enabled = false;
};
std::array<Player, 2> players;
// SDL may invoke its event filter from another thread. Only copy readings here;
// source selection, calibration and steering remain on the SDL/main thread.
struct Readings { Reading accel, gyro; };
std::mutex readings_mutex;
std::unordered_map<SDL_JoystickID, Readings> controller_readings;
std::unordered_map<SDL_SensorID, Reading> device_readings;
std::atomic<bool> background{false};
std::atomic<unsigned> generation{0};
unsigned previous_generation = 0;
std::atomic<int> rotation{0};
int previous_rotation = 0;
#if defined(__ANDROID__)
SDL_Sensor* device_gyro = nullptr;
SDL_Sensor* device_accel = nullptr;
bool sensors_initialized = false;
void close_sensors() {
    const auto gyro_id = device_gyro ? SDL_SensorGetInstanceID(device_gyro) : -1;
    const auto accel_id = device_accel ? SDL_SensorGetInstanceID(device_accel) : -1;
    {
        std::lock_guard lock(readings_mutex);
        device_readings.erase(gyro_id);
        device_readings.erase(accel_id);
    }
    if (device_gyro) SDL_SensorClose(device_gyro);
    if (device_accel) SDL_SensorClose(device_accel);
    device_gyro = device_accel = nullptr;
}
int sensor_index(SDL_SensorType type) {
    for (int i = 0; i < SDL_NumSensors(); ++i)
        if (SDL_SensorGetDeviceType(i) == type) return i;
    return -1;
}
#endif

bool supports(SDL_GameController* controller) {
    return controller && SDL_GameControllerGetAttached(controller) &&
        SDL_GameControllerIsSensorEnabled(controller, SDL_SENSOR_GYRO) &&
        SDL_GameControllerIsSensorEnabled(controller, SDL_SENSOR_ACCEL);
}

SDL_GameController* controller_for(int player) {
    if (!recompinput::players::uses_single_player_input())
        return recompinput::players::get_player(player).controller;
    if (player != 0) return nullptr;
    // The legacy profile combines all pads. Motion needs one unambiguous source;
    // assignment is required when more than one sensor-capable pad is attached.
    SDL_GameController* result = nullptr;
    for (int i = 0; i < SDL_NumJoysticks(); ++i) {
        auto* candidate = recompinput::get_controller_from_joystick_id(SDL_JoystickGetDeviceInstanceID(i));
        if (!supports(candidate)) continue;
        if (result) return nullptr;
        result = candidate;
    }
    return result;
}

bool read(SDL_GameController* controller, Sample& sample) {
    Readings readings;
    const auto controller_id = controller ? SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(controller)) : -1;
#if defined(__ANDROID__)
    const auto accel_id = device_accel ? SDL_SensorGetInstanceID(device_accel) : -1;
    const auto gyro_id = device_gyro ? SDL_SensorGetInstanceID(device_gyro) : -1;
#endif
    {
        std::lock_guard lock(readings_mutex);
        if (controller) {
            const auto found = controller_readings.find(controller_id);
            if (found == controller_readings.end()) return false;
            readings = found->second;
        }
#if defined(__ANDROID__)
        else if (device_accel && device_gyro) {
            const auto accel = device_readings.find(accel_id);
            const auto gyro = device_readings.find(gyro_id);
            if (accel == device_readings.end() || gyro == device_readings.end()) return false;
            readings = {accel->second, gyro->second};
        }
#endif
    }
    // Read the clock after copying: another SDL callback thread may have just
    // published a newer event. No SDL calls occur while readings_mutex is held.
    if (!sensor_sample(readings.accel, readings.gyro, SDL_GetTicks64(), sample)) return false;
    if (!controller) sample = screen_sample(sample, rotation.load());
    return true;
}
}

void suspend(bool value) {
    background.store(value);
    generation.fetch_add(1);
}

void notify_event(const SDL_Event& event) {
    if (event.type == SDL_APP_WILLENTERBACKGROUND || event.type == SDL_APP_TERMINATING) suspend(true);
    if (event.type == SDL_APP_DIDENTERFOREGROUND) suspend(false);
    if (event.type == SDL_CONTROLLERSENSORUPDATE) {
        const auto& sensor = event.csensor;
        if (sensor.sensor != SDL_SENSOR_ACCEL && sensor.sensor != SDL_SENSOR_GYRO) return;
        std::lock_guard lock(readings_mutex);
        auto& readings = controller_readings[sensor.which];
        auto& reading = sensor.sensor == SDL_SENSOR_ACCEL ? readings.accel : readings.gyro;
        reading = {sensor.data[0], sensor.data[1], sensor.data[2], sensor.timestamp, sensor.timestamp_us, true};
    } else if (event.type == SDL_SENSORUPDATE) {
        const auto& sensor = event.sensor;
        std::lock_guard lock(readings_mutex);
        device_readings[sensor.which] = {sensor.data[0], sensor.data[1], sensor.data[2], sensor.timestamp, sensor.timestamp_us, true};
    } else if (event.type == SDL_CONTROLLERDEVICEREMOVED) {
        std::lock_guard lock(readings_mutex);
        controller_readings.erase(event.cdevice.which);
    }
}

void update() {
    const unsigned current_generation = generation.load();
    const int current_rotation = rotation.load();
    if (current_generation != previous_generation || current_rotation != previous_rotation) {
        for (auto& player : players) player = {};
        previous_generation = current_generation;
        previous_rotation = current_rotation;
#if defined(__ANDROID__)
        close_sensors();
#endif
    }
    const bool active = !background.load();
#if defined(__ANDROID__)
    if (!sensors_initialized && active)
        sensors_initialized = SDL_InitSubSystem(SDL_INIT_SENSOR) == 0;
    const int gyro_index = sensors_initialized ? sensor_index(SDL_SENSOR_GYRO) : -1;
    const int accel_index = sensors_initialized ? sensor_index(SDL_SENSOR_ACCEL) : -1;
    const bool device_present = gyro_index >= 0 && accel_index >= 0;
    if (!active || !config::gyro_settings(0).enabled) close_sensors();
#endif
    for (int i = 0; i < 2; ++i) {
        auto& player = players[i];
        const auto settings = config::gyro_settings(i);
        auto* controller = controller_for(i);
        if (!supports(controller)) controller = nullptr;
        int64_t source = controller ? SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(controller)) : -1;
#if defined(__ANDROID__)
        if (!controller && i == 0 && device_present) source = -2;
#endif
        if (i == 1 && source == players[0].source) source = -1;
        if (source != player.source || settings.enabled != player.enabled) {
            player = {};
            player.source = source;
            player.enabled = settings.enabled;
        }
        player.usable = active && source != -1;
        if (!player.usable || !settings.enabled) continue;
#if defined(__ANDROID__)
        if (source == -2 && !device_gyro) {
            device_gyro = SDL_SensorOpen(gyro_index);
            device_accel = SDL_SensorOpen(accel_index);
            if (!device_gyro || !device_accel) {
                close_sensors();
                player.usable = false;
                continue;
            }
        }
#endif
        Sample sample{};
        if (!read(controller, sample)) {
            player.filter.reset();
            player.usable = false;
            continue;
        }
        player.filter.update(sample);
    }
    menu::refresh_motion_settings();
}

float steering(int player) {
    return available(player) ? players[player].filter.steering(config::gyro_settings(player)) : 0;
}

bool available(int player) {
    return player >= 0 && player < 2 && players[player].usable;
}

void calibrate(int player) {
    if (available(player) && config::gyro_settings(player).enabled) {
        players[player].enabled = true;
        players[player].filter.calibrate();
    }
}

const char* description(int player) {
    if (!available(player)) return "No usable motion source. Android device motion is Player 1 only; assign each other player a controller with gyro and accelerometer sensors.";
    if (!config::gyro_settings(player).enabled) return "Motion source available. Enable gyro steering, then calibrate. Manual steering takes priority.";
    switch (players[player].filter.status()) {
    case Status::Calibrating: return "Hold still for half a second, with the device or controller raised like a steering wheel. Calibration is in progress.";
    case Status::Ready: return "Calibrated. Tilt like a steering wheel; return to this resting angle to steer straight. Calibrate again to recenter.";
    default: return "Calibration required. Hold the device or controller raised like a steering wheel at your comfortable resting angle, then select Calibrate / recenter.";
    }
}

void shutdown() {
    for (auto& player : players) player = {};
#if defined(__ANDROID__)
    close_sensors();
    if (sensors_initialized) SDL_QuitSubSystem(SDL_INIT_SENSOR);
    sensors_initialized = false;
#endif
    std::lock_guard lock(readings_mutex);
    controller_readings.clear();
    device_readings.clear();
}

#if defined(__ANDROID__)
void set_rotation(int value) { rotation.store(value); }
#endif
}

#if defined(__ANDROID__)
extern "C" JNIEXPORT void JNICALL
Java_io_github_alondero_aerogaugerecomp_GameActivity_nativeMotionRotation(JNIEnv*, jclass, jint value) {
    aero::gyro::set_rotation(value);
}
#endif
