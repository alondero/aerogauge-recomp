#undef NDEBUG
#include <cassert>
#include <filesystem>
#include <fstream>
#include <unordered_map>
#include <SDL.h>
#include "recompinput/input_state.h"
#include "recompinput/players.h"
#include "recompinput/profiles.h"
#include "recompui/recompui.h"
#include "composites/ui_player_card.h"
#include "elements/ui_document.h"

namespace {
std::unordered_map<SDL_JoystickID, std::string> test_controller_serials;
class NullRenderer : public Rml::RenderInterface {
    Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex>, Rml::Span<const int>) override { return 1; }
    void RenderGeometry(Rml::CompiledGeometryHandle, Rml::Vector2f, Rml::TextureHandle) override {}
    void ReleaseGeometry(Rml::CompiledGeometryHandle) override {}
    Rml::TextureHandle LoadTexture(Rml::Vector2i&, const Rml::String&) override { return 0; }
    Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte>, Rml::Vector2i) override { return 0; }
    void ReleaseTexture(Rml::TextureHandle) override {}
    void EnableScissorRegion(bool) override {}
    void SetScissorRegion(Rml::Rectanglei) override {}
};
}

extern "C" const char* SDLCALL __wrap_SDL_GameControllerGetSerial(SDL_GameController* controller) {
    auto* joystick = SDL_GameControllerGetJoystick(controller);
    auto it = test_controller_serials.find(SDL_JoystickInstanceID(joystick));
    return it == test_controller_serials.end() ? SDL_JoystickGetSerial(joystick) : it->second.c_str();
}

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
    auto connect = [&](int i, const std::string& serial = "") {
        indices[i] = SDL_JoystickAttachVirtualEx(&desc);
        assert(indices[i] >= 0);
        pads[i] = SDL_GameControllerOpen(indices[i]);
        assert(pads[i]);
        sticks[i] = SDL_GameControllerGetJoystick(pads[i]);
        if (!serial.empty()) test_controller_serials[SDL_JoystickInstanceID(sticks[i])] = serial;
        add_controller_state(SDL_JoystickInstanceID(sticks[i]), pads[i]);
    };
    auto detach = [&](int i) {
        const auto id = SDL_JoystickInstanceID(sticks[i]);
        int index = 0;
        while (index < SDL_NumJoysticks() && SDL_JoystickGetDeviceInstanceID(index) != id) ++index;
        assert(SDL_JoystickDetachVirtual(index) == 0);
        return id;
    };
    auto disconnect = [&](int i, bool missed_event = false) {
        const auto id = detach(i);
        if (missed_event) {
            std::vector<SDL_GameController*> detached;
            collect_detached_controller_states(detached);
            assert(detached.size() == 1 && detached[0] == pads[i]);
        } else {
            remove_controller_state(id);
        }
        test_controller_serials.erase(id);
        SDL_GameControllerClose(pads[i]);
        pads[i] = nullptr;
        sticks[i] = nullptr;
    };
    for (int i = 0; i < 2; ++i) connect(i);
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
    // Opening Controls before assignment must not precreate overlapping keys.
    NullRenderer renderer;
    Rml::SetRenderInterface(&renderer);
    assert(Rml::Initialise());
    auto* rml = Rml::CreateContext("player-card-test", {800, 600});
    assert(rml);
    auto context = recompui::create_context(rml->CreateDocument());
    context.open();
    for (int i = 0; i < 2; ++i)
        context.create_element<recompui::PlayerCard>(context.get_root_element(), i);
    context.close();
    for (int i = 0; i < 2; ++i)
        assert(profiles::get_input_profile_by_key(profiles::get_mp_keyboard_profile_key(i)) < 0);
    recompui::destroy_context(context);
    Rml::Shutdown();
    auto commit = [&] {
        playerassignment::commit_player_assignment();
        SDL_Event event{};
        playerassignment::process_sdl_event(&event); // drain deferred modal close
    };
    SDL_ControllerButtonEvent menu_event{};
    menu_event.which = SDL_JoystickInstanceID(sticks[1]);
    menu_event.button = SDL_CONTROLLER_BUTTON_A;
    assert(cont_button_to_key(menu_event) == recompui::menu_action_mapping::accept.sdl);
    press(1, SDL_CONTROLLER_BUTTON_A, true);
    assert(input(0) == 0x8000 && input(1) == 0); // legacy P1 only
    press(1, SDL_CONTROLLER_BUTTON_A, false);
    playerassignment::start();
    playerassignment::add_keyboard_player(); playerassignment::add_keyboard_player(); commit();
    const int initial_p2_keyboard = profiles::get_input_profile_for_player(1, InputDevice::Keyboard);
    assert(profiles::get_input_binding(initial_p2_keyboard, GameInput::A, 0).is_empty());
    auto assign = [&](int pad) {
        SDL_Event event{};
        event.type = SDL_CONTROLLERBUTTONDOWN;
        event.cbutton.which = SDL_JoystickInstanceID(sticks[pad]);
        event.cbutton.button = SDL_CONTROLLER_BUTTON_A;
        playerassignment::process_sdl_event(&event);
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
    profiles::set_input_binding(p2, GameInput::ACCEPT_MENU, 0, InputField::controller_digital(SDL_CONTROLLER_BUTTON_RIGHTSTICK));
    menu_event.button = SDL_CONTROLLER_BUTTON_RIGHTSTICK;
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
    add_controller_state(SDL_JoystickInstanceID(sticks[0]), pads[0]);
    players::restore_controllers(); // identical model without serial stays manual
    assert(!players::get_player_is_assigned(0) && players::get_player_is_assigned(1));
    press(1, SDL_CONTROLLER_BUTTON_A, true);
    assert(input(0) == 0 && (input(1) & 0x8000));
    // A keyboard can occupy port 2 without affecting the controller on port 1.
    profiles::set_input_profile_for_player(1, -1, InputDevice::Keyboard);
    playerassignment::start(); assign(1); playerassignment::add_keyboard_player();
    commit();
    assert(players::get_player_input_device(0) == InputDevice::Controller);
    assert(players::get_player_input_device(1) == InputDevice::Keyboard);
    int p2_keyboard = profiles::get_input_profile_for_player(1, InputDevice::Keyboard);
    assert(p2_keyboard >= 0);
    profiles::clear_input_binding(p2_keyboard, GameInput::A);
    profiles::reset_profile_bindings(p2_keyboard, InputDevice::Keyboard);
    assert(profiles::get_input_binding(p2_keyboard, GameInput::A, 0) == InputField::keyboard(SDL_SCANCODE_X));
    // Share one keyboard with separate configured action keys on both ports.
    playerassignment::start();
    playerassignment::add_keyboard_player(); playerassignment::add_keyboard_player(); commit();
    const int p1_keyboard = profiles::get_input_profile_for_player(0, InputDevice::Keyboard);
    profiles::reset_profile_bindings(p1_keyboard, InputDevice::Keyboard);
    profiles::reset_profile_bindings(p2_keyboard, InputDevice::Keyboard);
    assert(profiles::get_input_binding(p2_keyboard, GameInput::A, 0).is_empty());
    profiles::set_input_binding(p1_keyboard, GameInput::A, 0, InputField::keyboard(SDL_SCANCODE_X));
    profiles::set_input_binding(p2_keyboard, GameInput::A, 0, InputField::keyboard(SDL_SCANCODE_V));
    // SDL owns a mutable key-state buffer. Set synthetic held keys here so
    // keyboard routing can be checked without depending on desktop focus.
    auto* keys = const_cast<Uint8*>(SDL_GetKeyboardState(nullptr));
    const Uint8 saved_x = keys[SDL_SCANCODE_X], saved_v = keys[SDL_SCANCODE_V];
    keys[SDL_SCANCODE_X] = 1; keys[SDL_SCANCODE_V] = 0; poll_inputs();
    assert((input(0) & 0x8000) && !(input(1) & 0x8000));
    keys[SDL_SCANCODE_X] = 0; keys[SDL_SCANCODE_V] = 1; poll_inputs();
    assert(!(input(0) & 0x8000) && (input(1) & 0x8000));
    keys[SDL_SCANCODE_X] = saved_x; keys[SDL_SCANCODE_V] = saved_v;
    disconnect(0); disconnect(1);

    // Same model, distinct serials: restore the physical unit, preserving P2's
    // held input, slot, and custom profile while P1 is absent.
    connect(0, "unit-a"); connect(1, "unit-b");
    playerassignment::start(); assign(0); assign(1); commit();
    const auto p2_id = players::get_player(1).controller_id;
    const auto old_p1_id = players::get_player(0).controller_id;
    press(1, SDL_CONTROLLER_BUTTON_A, true);
    disconnect(0, true); // missed removal event uses the periodic cleanup path
    poll_inputs();
    assert(input(0) == 0 && (input(1) & 0x8000));
    connect(0, "unit-a");
    press(0, SDL_CONTROLLER_BUTTON_A, true);
    assert(input(0) == 0);
    players::restore_controllers(); players::restore_controllers();
    poll_inputs();
    assert(players::get_player(0).controller == pads[0]);
    assert(players::get_player(0).controller_id != old_p1_id);
    assert(players::get_player(1).controller_id == p2_id);
    assert(profiles::get_input_profile_for_player(1, InputDevice::Controller) == custom);
    assert((input(0) & 0x8000) && (input(1) & 0x8000));

    // Physical reservations are never serialized with binding profiles.
    assert(profiles::save_controls_config(controls_path));
    std::ifstream saved_controls(controls_path);
    const std::string saved_json((std::istreambuf_iterator<char>(saved_controls)), {});
    assert(saved_json.find("unit-a") == std::string::npos);
    assert(saved_json.find("unit-b") == std::string::npos);

    // Reversed enumeration order cannot exchange two identical pads' slots.
    disconnect(0); disconnect(1);
    connect(1, "unit-b"); connect(0, "unit-a");
    players::restore_controllers();
    assert(players::get_player(0).controller == pads[0]);
    assert(players::get_player(1).controller == pads[1]);

    // SDL may deliver ADDED before cleanup sees the old detached handle.
    // That is one returning unit, not a simultaneous duplicate serial.
    auto* stale_pad = pads[0];
    const auto stale_id = detach(0);
    connect(0, "unit-a");
    std::vector<SDL_GameController*> detached;
    collect_detached_controller_states(detached);
    assert(detached.size() == 1 && detached[0] == stale_pad);
    SDL_GameControllerClose(stale_pad);
    test_controller_serials.erase(stale_id);
    players::restore_controllers();
    assert(players::get_player(0).controller == pads[0]);

    // Restoration waits for manual assignment to finish. Cancelling retains
    // reservations; confirming a keyboard assignment replaces them.
    disconnect(0);
    playerassignment::start(); connect(0, "unit-a");
    players::restore_controllers();
    assert(!players::get_player_is_assigned(0));
    playerassignment::stop(); players::restore_controllers();
    assert(players::get_player(0).controller == pads[0]);
    disconnect(0);
    playerassignment::start(); playerassignment::add_keyboard_player(); assign(1); commit();
    connect(0, "unit-a"); players::restore_controllers();
    assert(players::get_player_input_device(0) == InputDevice::Keyboard);
    assert(players::get_player(1).controller == pads[1]);

    // A serial shared by two simultaneous physical units stays ambiguous even
    // after the duplicate leaves. Never bind that unit into both player slots.
    playerassignment::start(); assign(0); assign(1); commit();
    disconnect(1); connect(1, "unit-a"); // duplicate of P1
    disconnect(0); players::restore_controllers();
    assert(!players::get_player_is_assigned(0) && !players::get_player_is_assigned(1));
    disconnect(1); connect(0, "unit-a"); players::restore_controllers();
    assert(!players::get_player_is_assigned(0));
    connect(1, "unit-b"); players::restore_controllers();
    assert(players::get_player(1).controller == pads[1]);
    // Explicit assignment remains available for ambiguous controllers.
    playerassignment::start(); assign(0); assign(1); commit();
    assert(players::get_player(0).controller == pads[0]);
    disconnect(0); connect(0, "unit-a"); players::restore_controllers();
    assert(!players::get_player_is_assigned(0));
    disconnect(0); disconnect(1);
#ifndef _WIN32
    SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);
#else
    // Pinned SDL 2.26.3 can deliver a Windows Gaming Input callback after
    // freeing its joystick mutex. All test-owned devices are closed above;
    // retain global driver state until process exit, outside this input test.
#endif
}
