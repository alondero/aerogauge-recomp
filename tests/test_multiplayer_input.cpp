#undef NDEBUG
#include <cassert>
#include <filesystem>
#include <SDL.h>
#include "recompinput/input_state.h"
#include "recompinput/players.h"
#include "recompinput/profiles.h"
#include "recompui/recompui.h"

int cont_button_to_key(SDL_ControllerButtonEvent&);

// SDL virtual pads exercise the same assignment/profile path as real pads,
// including two devices with identical identity and no serial number.
void test_multiplayer_input(const std::filesystem::path& controls_path) {
    using namespace recompinput;
    SDL_SetMainReady();
    assert(SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) == 0);
    int indices[2];
    SDL_GameController* pads[2];
    SDL_Joystick* sticks[2];
    SDL_VirtualJoystickDesc desc{};
    desc.version = SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
    desc.type = SDL_JOYSTICK_TYPE_GAMECONTROLLER;
    desc.naxes = SDL_CONTROLLER_AXIS_MAX;
    desc.nbuttons = SDL_CONTROLLER_BUTTON_MAX;
    desc.button_mask = (1u << SDL_CONTROLLER_BUTTON_MAX) - 1;
    desc.axis_mask = (1u << SDL_CONTROLLER_AXIS_MAX) - 1;
    desc.name = "AeroGauge test pad";
    for (int i = 0; i < 2; ++i) {
        indices[i] = SDL_JoystickAttachVirtualEx(&desc);
        assert(indices[i] >= 0);
        pads[i] = SDL_GameControllerOpen(indices[i]);
        assert(pads[i]);
        sticks[i] = SDL_GameControllerGetJoystick(pads[i]);
        add_controller_state(SDL_JoystickInstanceID(sticks[i]), pads[i]);
    }
    auto press = [&](int player, SDL_GameControllerButton button, bool down) {
        assert(SDL_JoystickSetVirtualButton(sticks[player], button, down) == 0);
        SDL_JoystickUpdate();
        poll_inputs();
    };
    auto input = [&](int player, float* stick_x = nullptr) {
        uint16_t buttons = 0;
        float x = 0, y = 0;
        profiles::get_n64_input(player, &buttons, &x, &y);
        if (stick_x) *stick_x = x;
        return buttons;
    };
    assert(players::uses_single_player_input());
    SDL_ControllerButtonEvent menu_event{};
    menu_event.which = SDL_JoystickInstanceID(sticks[1]);
    menu_event.button = SDL_CONTROLLER_BUTTON_A;
    assert(cont_button_to_key(menu_event) == recompui::menu_action_mapping::accept.sdl);
    press(1, SDL_CONTROLLER_BUTTON_A, true);
    assert(input(0) == 0x8000 && input(1) == 0); // legacy P1 only
    press(1, SDL_CONTROLLER_BUTTON_A, false);
    auto assign = [&](int pad) {
        SDL_Event event{};
        event.type = SDL_CONTROLLERBUTTONDOWN;
        event.cbutton.which = SDL_JoystickInstanceID(sticks[pad]);
        event.cbutton.button = SDL_CONTROLLER_BUTTON_A;
        playerassignment::process_sdl_event(&event);
    };
    auto commit = [&] {
        playerassignment::commit_player_assignment();
        SDL_Event event{};
        playerassignment::process_sdl_event(&event); // drain deferred modal close
    };
    playerassignment::start();
    assign(0); assign(0); // one device cannot occupy both ports
    assign(1);
    playerassignment::add_keyboard_player(); // maximum remains two
    commit();
    assert(players::get_number_of_assigned_players() == 2);
    const int p1 = profiles::get_input_profile_for_player(0, InputDevice::Controller);
    const int p2 = profiles::get_input_profile_for_player(1, InputDevice::Controller);
    assert(p1 >= 0 && p2 >= 0 && p1 != p2);
    profiles::clear_input_binding(p2, GameInput::ACCEPT_MENU);
    profiles::set_input_binding(p2, GameInput::ACCEPT_MENU, 0, InputField::controller_digital(SDL_CONTROLLER_BUTTON_X));
    menu_event.button = SDL_CONTROLLER_BUTTON_X;
    assert(cont_button_to_key(menu_event) == recompui::menu_action_mapping::accept.sdl);
    assert(recompui::get_last_controller_id() == menu_event.which);
    menu_event.which = SDL_JoystickInstanceID(sticks[0]);
    assert(cont_button_to_key(menu_event) != recompui::menu_action_mapping::accept.sdl);
    profiles::clear_input_binding(p2, GameInput::A);
    profiles::set_input_binding(p2, GameInput::A, 0, InputField::controller_digital(SDL_CONTROLLER_BUTTON_X));
    press(0, SDL_CONTROLLER_BUTTON_A, true);
    press(1, SDL_CONTROLLER_BUTTON_X, true);
    assert((input(0) & 0x8000) && (input(1) & 0x8000));
    press(0, SDL_CONTROLLER_BUTTON_A, false);
    assert(!(input(0) & 0x8000) && (input(1) & 0x8000));
    // Stale keyboard profiles must not supply controls to an assigned pad.
    int keyboard = profiles::get_or_create_mp_keyboard_profile_index(0);
    profiles::set_input_binding(keyboard, GameInput::A, 0, InputField::controller_digital(SDL_CONTROLLER_BUTTON_X));
    profiles::set_input_profile_for_player(1, keyboard, InputDevice::Keyboard);
    press(1, SDL_CONTROLLER_BUTTON_X, false);
    press(1, SDL_CONTROLLER_BUTTON_A, true);
    assert(!(input(1) & 0x8000));
    press(1, SDL_CONTROLLER_BUTTON_A, false);
    assert(SDL_JoystickSetVirtualAxis(sticks[1], SDL_CONTROLLER_AXIS_LEFTX, 32767) == 0);
    SDL_JoystickUpdate(); poll_inputs();
    float x1, x2;
    input(0, &x1); input(1, &x2);
    assert(x1 == 0 && x2 > 0.9f);
    // The selected profile survives a controls.json round trip and reassign.
    int custom = profiles::add_input_profile("test_p2_custom", "P2 custom", InputDevice::Controller, true);
    profiles::reset_profile_bindings(custom, InputDevice::Controller);
    profiles::set_input_profile_for_player(1, custom, InputDevice::Controller);
    assert(profiles::save_controls_config(controls_path));
    profiles::set_input_profile_for_player(1, p2, InputDevice::Controller);
    assert(profiles::load_controls_config(controls_path));
    assert(profiles::get_input_profile_for_player(1, InputDevice::Controller) == custom);
    playerassignment::start(); assign(0); assign(1);
    commit();
    assert(profiles::get_input_profile_for_player(1, InputDevice::Controller) == custom);
    // Disconnect P1: P2 retains both its port and its controls.
    remove_controller_state(SDL_JoystickInstanceID(sticks[0]));
    assert(!players::get_player_is_assigned(0) && players::get_player_is_assigned(1));
    press(1, SDL_CONTROLLER_BUTTON_A, true);
    assert(input(0) == 0 && (input(1) & 0x8000));
    // A keyboard can occupy port 2 without affecting the controller on port 1.
    playerassignment::start(); assign(1); playerassignment::add_keyboard_player();
    commit();
    assert(players::get_player_input_device(0) == InputDevice::Controller);
    assert(players::get_player_input_device(1) == InputDevice::Keyboard);
    int p2_keyboard = profiles::get_input_profile_for_player(1, InputDevice::Keyboard);
    assert(p2_keyboard >= 0);
    for (int i = 0; i < 2; ++i) {
        remove_controller_state(SDL_JoystickInstanceID(sticks[i]));
        SDL_GameControllerClose(pads[i]);
    }
    for (int i = 1; i >= 0; --i) SDL_JoystickDetachVirtual(indices[i]);
    SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);
}
