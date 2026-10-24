// Exercise the real SDL bridge with deterministic sensor events and fake physical
// devices. No SDL driver, window, ROM or guest memory is involved. SDL's sensor
// contract supplies radians/s and acceleration including gravity in m/s^2.
#include "aero_gyro_sdl.h"
#include "aero_config.h"
#include "recompinput/input_state.h"
#include "recompinput/players.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <string_view>

namespace {
struct Pad {
    int id;
    bool attached = true, has_gyro = true, has_accel = true;
    // Real SDL controllers start with reporting disabled. Capability and active
    // reporting must be separate; failed activation must suppress sensor events.
    bool gyro_enabled = false, accel_enabled = false;
    SDL_SensorType failing_sensor = SDL_SENSOR_INVALID;
    unsigned enable_requests = 0;
};
std::array<Pad, 2> pads{{{10}, {20}}};
std::array<recompinput::Player, 2> slots;
std::array<aero::gyro::Settings, 2> settings{};
bool legacy = true;
bool menu_open = false;
unsigned menu_refreshes = 0;
Uint64 ticks = 1000;
Pad& pad(SDL_GameController* value) { return *reinterpret_cast<Pad*>(value); }
SDL_GameController* controller(int i) { return reinterpret_cast<SDL_GameController*>(&pads[i]); }
void require(bool value, const char* message) {
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
void readings(int index, float degrees, float rate = 0.02f) {
    if (!pads[index].attached || !pads[index].gyro_enabled || !pads[index].accel_enabled) return;
    SDL_Event event{};
    event.type = SDL_CONTROLLERSENSORUPDATE;
    event.csensor.which = pads[index].id;
    event.csensor.timestamp = Uint32(ticks);
    // Deliberately zero: the pinned Android SDL sensor backend supplies this.
    event.csensor.timestamp_us = 0;
    event.csensor.sensor = SDL_SENSOR_ACCEL;
    event.csensor.data[0] = -9.80665f * std::sin(degrees * 0.01745329252f);
    event.csensor.data[1] = 9.80665f * std::cos(degrees * 0.01745329252f);
    aero::gyro::notify_event(event);
    event.csensor.sensor = SDL_SENSOR_GYRO;
    event.csensor.data[0] = event.csensor.data[1] = 0;
    event.csensor.data[2] = rate;
    aero::gyro::notify_event(event);
}
void calibrate_both() {
    aero::gyro::calibrate(0);
    aero::gyro::calibrate(1);
    for (int i = 0; i < 61; ++i) {
        ticks += 10;
        readings(0, 0); readings(1, 0);
        aero::gyro::update();
    }
}
}

Uint64 SDL_GetTicks64() { return ticks; }
int SDL_NumJoysticks() { return 2; }
SDL_JoystickID SDL_JoystickGetDeviceInstanceID(int index) { return pads[index].id; }
SDL_bool SDL_GameControllerGetAttached(SDL_GameController* value) { return pad(value).attached ? SDL_TRUE : SDL_FALSE; }
SDL_bool SDL_GameControllerHasSensor(SDL_GameController* value, SDL_SensorType type) {
    return (type == SDL_SENSOR_GYRO ? pad(value).has_gyro : pad(value).has_accel) ? SDL_TRUE : SDL_FALSE;
}
SDL_bool SDL_GameControllerIsSensorEnabled(SDL_GameController* value, SDL_SensorType type) {
    return (type == SDL_SENSOR_GYRO ? pad(value).gyro_enabled : pad(value).accel_enabled) ? SDL_TRUE : SDL_FALSE;
}
int SDL_GameControllerSetSensorEnabled(SDL_GameController* value, SDL_SensorType type, SDL_bool enabled) {
    auto& device = pad(value);
    ++device.enable_requests;
    if (!device.attached || !SDL_GameControllerHasSensor(value, type) || device.failing_sensor == type) return -1;
    (type == SDL_SENSOR_GYRO ? device.gyro_enabled : device.accel_enabled) = enabled == SDL_TRUE;
    return 0;
}
SDL_Joystick* SDL_GameControllerGetJoystick(SDL_GameController* value) { return reinterpret_cast<SDL_Joystick*>(value); }
SDL_JoystickID SDL_JoystickInstanceID(SDL_Joystick* value) { return reinterpret_cast<Pad*>(value)->id; }

namespace recompinput {
SDL_GameController* get_controller_from_joystick_id(SDL_JoystickID id) {
    for (int i = 0; i < 2; ++i) if (pads[i].id == id) return controller(i);
    return nullptr;
}
namespace players {
bool uses_single_player_input() { return legacy; }
const Player& get_player(int index, bool) { return slots[index]; }
}
}
namespace aero::config {
gyro::Settings gyro_settings(int player) { return settings[player]; }
}
namespace aero::menu {
bool captures_input() { return menu_open; }
void refresh_motion_settings() { ++menu_refreshes; }
}

int main() {
    using namespace aero::gyro;
    update();
    require(!available(0) && !available(1), "multiple legacy gyro pads require explicit player assignment");
    legacy = false;
    slots[0].controller = controller(0);
    slots[1].controller = controller(1);
    update();
    require(available(0) && available(1), "capable controllers are discoverable before sensor reporting is enabled");
    require(pads[0].enable_requests == 0 && pads[1].enable_requests == 0,
            "default-off motion must not activate controller sensors");
    settings[0].enabled = true;
    update();
    require(pads[0].gyro_enabled && pads[0].accel_enabled && pads[0].enable_requests == 2,
            "enabling motion explicitly activates both SDL sensors when reporting starts disabled");
    require(!pads[1].gyro_enabled && !pads[1].accel_enabled, "one player's activation does not enable the other controller");
    require(!available(0) && steering(0) == 0, "activation alone does not make missing sensor data usable");
    readings(0, 0); update();
    require(available(0) && pads[0].enable_requests == 2, "already enabled frontend sensors are reused without repeated activation");
    settings[1].enabled = true;
    pads[1].failing_sensor = SDL_SENSOR_ACCEL;
    update();
    require(!available(1) && steering(1) == 0 && available(0), "sensor activation failure fails neutral and preserves the other player");
    pads[1].failing_sensor = SDL_SENSOR_INVALID;
    update();
    readings(1, 0); update();
    require(available(0) && available(1), "fresh independent sensor streams recover after successful activation");
    require(menu_refreshes == 0, "closed settings must not synchronize the motion schema during gameplay");
    menu_open = true; update();
    require(menu_refreshes == 1, "open settings receive current motion status");
    menu_open = false; update();
    require(menu_refreshes == 1, "closing settings stops motion schema synchronization");
    calibrate_both();
    require(steering(0) == 0 && steering(1) == 0, "zero hardware timestamps calibrate both sources to neutral");
    require(std::string_view(description(0)).starts_with("Calibrated."), "calibration reaches Ready on real bridge");
    for (int i = 1; i <= 100; ++i) {
        ticks += 10;
        readings(0, 30.0f * i / 100, 0.02f - 30 * 0.01745329252f);
        readings(1, -15.0f * i / 100, 0.02f + 15 * 0.01745329252f);
        update();
    }
    require(steering(0) > 0.99f && steering(1) < -0.45f, "simultaneous turns stay in the correct player slots");
    settings[0].enabled = false;
    update();
    require(steering(0) == 0 && steering(1) < -0.45f, "disabling one source leaves the other independent");
    settings[0].enabled = true;
    update();
    calibrate_both();
    SDL_Event event{};
    event.type = SDL_APP_WILLENTERBACKGROUND;
    notify_event(event);
    update();
    require(!available(0) && !available(1), "background suppresses all motion");
    event.type = SDL_APP_DIDENTERFOREGROUND;
    notify_event(event);
    readings(0, 30); readings(1, -15);
    update();
    require(available(0) && steering(0) == 0 && steering(1) == 0, "resume requires explicit recalibration");
    calibrate_both();
    std::swap(slots[0].controller, slots[1].controller);
    update();
    require(std::string_view(description(0)).starts_with("Calibration required"), "reassignment invalidates the old source calibration");
    slots[1].controller = slots[0].controller;
    update();
    require(!available(1), "one source cannot drive two players even with a duplicate assignment");
    slots[0].controller = controller(0);
    slots[1].controller = controller(1);
    readings(0, 0); readings(1, 0); update();
    calibrate_both();
    ticks += 251;
    update();
    require(!available(0) && steering(0) == 0, "no fresh sensor events fails neutral after 250 ms");
    readings(0, 0); readings(1, 0); update();
    require(std::string_view(description(0)).starts_with("Calibration required"), "sensor recovery cannot reuse stale calibration");
    pads[0].attached = false;
    event.type = SDL_CONTROLLERDEVICEREMOVED;
    event.cdevice.which = pads[0].id;
    notify_event(event); update();
    require(!available(0) && available(1), "disconnect preserves the other player's sensor");
    pads[1].has_accel = false;
    const auto requests = pads[1].enable_requests;
    update();
    require(!available(1) && pads[1].enable_requests == requests, "a controller missing either sensor is unavailable without activation attempts");
    shutdown();
    std::puts("PASS SDL gyro bridge: timestamp-less events, sources, isolation, lifecycle, stale delivery and disconnect");
}
