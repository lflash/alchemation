#include "doctest.h"
#include "input.hpp"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

// ─── Helpers ─────────────────────────────────────────────────────────────────

// Write text to a temp file; returns path. Caller should remove() it.
static std::string writeTmp(const char* name, const std::string& content) {
    std::string path = (std::filesystem::temp_directory_path() / name).string();
    std::ofstream f(path);
    f << content;
    return path;
}

// ─── InputMap::defaults ───────────────────────────────────────────────────────

TEST_CASE("InputMap::defaults binds every action") {
    InputMap m = InputMap::defaults();
    for (int i = 0; i < INPUT_ACTION_COUNT; ++i)
        CHECK(m.get(static_cast<Action>(i)) != SDLK_UNKNOWN);
}

TEST_CASE("InputMap::defaults WASD movement bindings") {
    InputMap m = InputMap::defaults();
    CHECK(m.get(Action::MoveUp)    == SDLK_w);
    CHECK(m.get(Action::MoveDown)  == SDLK_s);
    CHECK(m.get(Action::MoveLeft)  == SDLK_a);
    CHECK(m.get(Action::MoveRight) == SDLK_d);
}

// ─── InputMap::get / set ──────────────────────────────────────────────────────

TEST_CASE("InputMap::get returns SDLK_UNKNOWN for unbound action") {
    InputMap m;   // empty bindings
    CHECK(m.get(Action::Dig) == SDLK_UNKNOWN);
}

TEST_CASE("InputMap::set overrides existing binding") {
    InputMap m = InputMap::defaults();
    m.set(Action::MoveUp, SDLK_UP);
    CHECK(m.get(Action::MoveUp) == SDLK_UP);
}

// ─── save / load round-trip ───────────────────────────────────────────────────

TEST_CASE("InputMap save/load round-trip preserves all bindings") {
    InputMap orig = InputMap::defaults();
    orig.set(Action::Dig, SDLK_x);
    orig.set(Action::Plant, SDLK_z);

    std::string path = writeTmp("inputmap_roundtrip.dat", "");
    orig.save(path);

    InputMap loaded = InputMap::load(path);
    std::remove(path.c_str());

    for (int i = 0; i < INPUT_ACTION_COUNT; ++i) {
        Action a = static_cast<Action>(i);
        CHECK(loaded.get(a) == orig.get(a));
    }
}

TEST_CASE("InputMap load returns defaults when file is absent") {
    InputMap loaded  = InputMap::load("/tmp/no_such_file_xyz.dat");
    InputMap def     = InputMap::defaults();
    for (int i = 0; i < INPUT_ACTION_COUNT; ++i) {
        Action a = static_cast<Action>(i);
        CHECK(loaded.get(a) == def.get(a));
    }
}

// ─── Partial / malformed files ───────────────────────────────────────────────

TEST_CASE("InputMap load falls back to default for missing actions") {
    // File only rebinds MoveUp; all others should stay at defaults.
    std::string content = "MoveUp=" + std::to_string(SDLK_UP) + "\n";
    std::string path = writeTmp("inputmap_partial.dat", content);
    InputMap loaded  = InputMap::load(path);
    InputMap def     = InputMap::defaults();
    std::remove(path.c_str());

    CHECK(loaded.get(Action::MoveUp)    == SDLK_UP);
    CHECK(loaded.get(Action::MoveDown)  == def.get(Action::MoveDown));
    CHECK(loaded.get(Action::MoveLeft)  == def.get(Action::MoveLeft));
    CHECK(loaded.get(Action::MoveRight) == def.get(Action::MoveRight));
}

TEST_CASE("InputMap load ignores unrecognised action names") {
    std::string content = "NonExistentAction=65\nMoveDown=" + std::to_string(SDLK_DOWN) + "\n";
    std::string path = writeTmp("inputmap_badaction.dat", content);
    InputMap loaded = InputMap::load(path);
    std::remove(path.c_str());

    CHECK(loaded.get(Action::MoveDown) == SDLK_DOWN);
}

TEST_CASE("InputMap load ignores lines with unrecognised key names") {
    InputMap def  = InputMap::defaults();
    std::string path = writeTmp("inputmap_badkey.dat",
        "MoveUp=NotARealKey\n");
    InputMap loaded = InputMap::load(path);
    std::remove(path.c_str());

    // Bad value (not an integer, not a valid SDL key name) → fall back to default
    CHECK(loaded.get(Action::MoveUp) == def.get(Action::MoveUp));
}

TEST_CASE("InputMap load ignores comment lines") {
    std::string content =
        "# this is a comment\n"
        "MoveUp="   + std::to_string(SDLK_UP)   + "\n"
        "# another comment\n"
        "MoveDown=" + std::to_string(SDLK_DOWN)  + "\n";
    std::string path = writeTmp("inputmap_comments.dat", content);
    InputMap loaded = InputMap::load(path);
    std::remove(path.c_str());

    CHECK(loaded.get(Action::MoveUp)   == SDLK_UP);
    CHECK(loaded.get(Action::MoveDown) == SDLK_DOWN);
}

TEST_CASE("InputMap load ignores malformed lines (no '=')") {
    InputMap def  = InputMap::defaults();
    std::string content =
        "this line has no equals sign\n"
        "MoveUp=" + std::to_string(SDLK_UP) + "\n";
    std::string path = writeTmp("inputmap_malformed.dat", content);
    InputMap loaded = InputMap::load(path);
    std::remove(path.c_str());

    CHECK(loaded.get(Action::MoveUp) == SDLK_UP);
    CHECK(loaded.get(Action::Dig)    == def.get(Action::Dig));
}

// ─── Input class integration ──────────────────────────────────────────────────

static SDL_Event makeKeyDown(SDL_Keycode key) {
    SDL_Event e{};
    e.type           = SDL_KEYDOWN;
    e.key.keysym.sym = key;
    e.key.repeat     = 0;
    return e;
}

TEST_CASE("Input::setMap takes effect immediately on next query") {
    Input input;
    InputMap m = InputMap::defaults();
    m.set(Action::Dig, SDLK_x);
    input.setMap(m);

    input.beginFrame();
    input.handleEvent(makeKeyDown(SDLK_x));
    CHECK(input.pressed(Action::Dig));
    CHECK(!input.pressed(Action::Dig == Action::MoveUp ? Action::MoveDown : Action::MoveUp));
}

TEST_CASE("Input rebound key triggers action; old key does not") {
    Input input;
    InputMap m = InputMap::defaults();
    m.set(Action::MoveUp, SDLK_UP);   // rebind from W to Up-arrow
    input.setMap(m);

    input.beginFrame();
    input.handleEvent(makeKeyDown(SDLK_UP));
    CHECK(input.pressed(Action::MoveUp));

    input.beginFrame();
    input.handleEvent(makeKeyDown(SDLK_w));   // old key no longer bound
    CHECK(!input.pressed(Action::MoveUp));
}

// ─── GamepadMap ───────────────────────────────────────────────────────────────

TEST_CASE("GamepadMap::defaults has movement on D-pad") {
    GamepadMap m = GamepadMap::defaults();
    CHECK(m.get(Action::MoveUp).type    == GamepadBinding::Type::Button);
    CHECK(m.get(Action::MoveUp).index   == SDL_CONTROLLER_BUTTON_DPAD_UP);
    CHECK(m.get(Action::MoveDown).index == SDL_CONTROLLER_BUTTON_DPAD_DOWN);
    CHECK(m.get(Action::MoveLeft).index == SDL_CONTROLLER_BUTTON_DPAD_LEFT);
    CHECK(m.get(Action::MoveRight).index== SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
}

TEST_CASE("GamepadMap::defaults binds right stick to camera pan") {
    GamepadMap m = GamepadMap::defaults();
    CHECK(m.get(Action::PanLeft).type  == GamepadBinding::Type::AxisNeg);
    CHECK(m.get(Action::PanRight).type == GamepadBinding::Type::AxisPos);
    CHECK(m.get(Action::PanLeft).index == SDL_CONTROLLER_AXIS_RIGHTX);
}

TEST_CASE("GamepadMap::get returns None for unbound action") {
    GamepadMap m;
    CHECK(m.get(Action::Quit).type == GamepadBinding::Type::None);
}

TEST_CASE("GamepadMap::setButton overrides binding") {
    GamepadMap m = GamepadMap::defaults();
    m.setButton(Action::Dig, SDL_CONTROLLER_BUTTON_A);
    CHECK(m.get(Action::Dig).type  == GamepadBinding::Type::Button);
    CHECK(m.get(Action::Dig).index == SDL_CONTROLLER_BUTTON_A);
}

// ─── Gamepad → Input integration ─────────────────────────────────────────────
//
// Tests use synthetic SDL events with cbutton.which = -1 (the default
// controllerID_ when no physical device is connected), so no hardware needed.

static SDL_Event makePadButtonDown(int button) {
    SDL_Event e{};
    e.type          = SDL_CONTROLLERBUTTONDOWN;
    e.cbutton.which = -1;   // matches Input's default controllerID_
    e.cbutton.button= static_cast<Uint8>(button);
    return e;
}

static SDL_Event makePadButtonUp(int button) {
    SDL_Event e{};
    e.type          = SDL_CONTROLLERBUTTONUP;
    e.cbutton.which = -1;
    e.cbutton.button= static_cast<Uint8>(button);
    return e;
}

static SDL_Event makePadAxis(int axis, Sint16 value) {
    SDL_Event e{};
    e.type       = SDL_CONTROLLERAXISMOTION;
    e.caxis.which= -1;
    e.caxis.axis = static_cast<Uint8>(axis);
    e.caxis.value= value;
    return e;
}

TEST_CASE("Gamepad D-pad press triggers MoveUp") {
    Input input;
    input.beginFrame();
    input.handleEvent(makePadButtonDown(SDL_CONTROLLER_BUTTON_DPAD_UP));
    input.beginFrame();   // beginFrame rebuilds padCurrent_ from padButtons_
    CHECK(input.held(Action::MoveUp));
    CHECK(input.pressed(Action::MoveUp));
}

TEST_CASE("Gamepad D-pad release triggers released()") {
    Input input;
    input.handleEvent(makePadButtonDown(SDL_CONTROLLER_BUTTON_DPAD_UP));
    input.beginFrame();   // up appears in padCurrent_ and padPrevious_
    input.handleEvent(makePadButtonUp(SDL_CONTROLLER_BUTTON_DPAD_UP));
    input.beginFrame();
    CHECK(!input.held(Action::MoveUp));
    CHECK(input.released(Action::MoveUp));
}

TEST_CASE("Gamepad axis triggers PanRight when past deadzone") {
    Input input;
    // Right stick X-axis positive → PanRight
    input.handleEvent(makePadAxis(SDL_CONTROLLER_AXIS_RIGHTX, 20000));
    input.beginFrame();
    CHECK(input.held(Action::PanRight));
    CHECK(!input.held(Action::PanLeft));
}

TEST_CASE("Gamepad axis below deadzone does not trigger action") {
    Input input;
    input.handleEvent(makePadAxis(SDL_CONTROLLER_AXIS_RIGHTX, 5000));  // below 12000
    input.beginFrame();
    CHECK(!input.held(Action::PanRight));
}

TEST_CASE("Left stick triggers movement independently of D-pad bindings") {
    Input input;
    // Left stick Y-axis negative → MoveUp (hardcoded, not from GamepadMap)
    input.handleEvent(makePadAxis(SDL_CONTROLLER_AXIS_LEFTY, -20000));
    input.beginFrame();
    CHECK(input.held(Action::MoveUp));
}

TEST_CASE("Keyboard and gamepad both held: held() returns true; release of one keeps it active") {
    Input input;
    // Press MoveUp via keyboard (W) and D-pad simultaneously
    input.beginFrame();
    input.handleEvent(makeKeyDown(SDLK_w));
    input.handleEvent(makePadButtonDown(SDL_CONTROLLER_BUTTON_DPAD_UP));
    input.beginFrame();
    CHECK(input.held(Action::MoveUp));

    // Release only the keyboard key — pad still holds it
    input.handleEvent(makeKeyDown(SDLK_w));  // key stays down (simulate hold)
    input.handleEvent(makePadButtonUp(SDL_CONTROLLER_BUTTON_DPAD_UP));
    input.beginFrame();
    // W is still held so MoveUp still active
    CHECK(input.held(Action::MoveUp));
}
