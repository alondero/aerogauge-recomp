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
struct Pad { int id; bool attached = true; bool sensors = true; };
std::array<Pad, 2> pads{{{10}, {20}}};
std::array<recompinput::Player, 2> slots;
std::array<aero::gyro::Settings, 2> settings{{{true, 1, 2}, {true, 1, 2}}};
bool legacy = true;
Uint64 ticks = 1000;
Pad& pad(SDL_GameController* value) { return *reinterpret_cast<Pad*>(value); }
SDL_GameController* controller(int i) { return reinterpret_cast<SDL_GameController*>(&pads[i]); }
void require(bool value, const char* message) {
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
void readings(int index, float degrees, float rate = 0.02f) {
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
SDL_bool SDL_GameControllerIsSensorEnabled(SDL_GameController* value, SDL_SensorType) { return pad(value).sensors ? SDL_TRUE : SDL_FALSE; }
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
void refresh_motion_settings() { }
}

int main() {
    using namespace aero::gyro;
    update();
    require(!available(0) && !available(1), "multiple legacy gyro pads require explicit player assignment");
    legacy = false;
    slots[0].controller = controller(0);
    slots[1].controller = controller(1);
    readings(0, 0); readings(1, 0);
    update();
    require(available(0) && available(1), "assigned players get independent motion sources");
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
    shutdown();
    std::puts("PASS SDL gyro bridge: timestamp-less events, sources, isolation, lifecycle, stale delivery and disconnect");
}
