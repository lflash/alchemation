#include "input.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <string>

// ─── InputMap ────────────────────────────────────────────────────────────────

SDL_Keycode InputMap::get(Action a) const {
    auto it = bindings.find(a);
    return it != bindings.end() ? it->second : SDLK_UNKNOWN;
}

InputMap InputMap::defaults() {
    InputMap m;
    m.bindings = {
        { Action::MoveUp,           SDLK_w         },
        { Action::MoveDown,         SDLK_s         },
        { Action::MoveLeft,         SDLK_a         },
        { Action::MoveRight,        SDLK_d         },
        { Action::Strafe,           SDLK_LSHIFT    },
        { Action::Dig,              SDLK_f         },
        { Action::Plant,            SDLK_c         },
        { Action::PlacePortal,      SDLK_o         },
        { Action::Record,           SDLK_r         },
        { Action::CycleRecording,   SDLK_q         },
        { Action::Deploy,           SDLK_e         },
        { Action::SwitchGrid,       SDLK_TAB       },
        { Action::PanUp,            SDLK_UP        },
        { Action::PanDown,          SDLK_DOWN      },
        { Action::PanLeft,          SDLK_LEFT      },
        { Action::PanRight,         SDLK_RIGHT     },
        { Action::ResetCamera,      SDLK_BACKSPACE },
        { Action::ZoomModifier,     SDLK_LCTRL     },
        { Action::Quit,             SDLK_ESCAPE    },
        { Action::Confirm,          SDLK_RETURN    },
        { Action::ToggleControls,   SDLK_h         },
        { Action::ToggleRecordings, SDLK_i         },
        { Action::ToggleRebind,     SDLK_k         },
    };
    return m;
}

// ─── Action ↔ string table (order must match Action enum) ────────────────────

static constexpr const char* ACTION_NAMES[] = {
    "MoveUp", "MoveDown", "MoveLeft", "MoveRight", "Strafe",
    "Dig", "Plant", "PlacePortal",
    "Record", "CycleRecording", "Deploy",
    "SwitchGrid",
    "PanUp", "PanDown", "PanLeft", "PanRight", "ResetCamera", "ZoomModifier",
    "Quit", "Confirm", "ToggleControls", "ToggleRecordings", "ToggleRebind",
};
static_assert(std::size(ACTION_NAMES) == INPUT_ACTION_COUNT,
              "ACTION_NAMES must have one entry per Action enum value");
static constexpr int ACTION_COUNT = static_cast<int>(std::size(ACTION_NAMES));

static const char* actionToString(Action a) {
    int i = static_cast<int>(a);
    return (i >= 0 && i < ACTION_COUNT) ? ACTION_NAMES[i] : nullptr;
}

static bool actionFromString(const std::string& s, Action& out) {
    for (int i = 0; i < ACTION_COUNT; ++i) {
        if (s == ACTION_NAMES[i]) { out = static_cast<Action>(i); return true; }
    }
    return false;
}

// ─── InputMap save / load ─────────────────────────────────────────────────────

void InputMap::save(const std::string& path) const {
    std::ofstream f(path);
    if (!f) return;
    InputMap def = defaults();
    for (int i = 0; i < ACTION_COUNT; ++i) {
        Action a = static_cast<Action>(i);
        SDL_Keycode code = get(a);
        if (code == SDLK_UNKNOWN) code = def.get(a);
        const char* keyName = SDL_GetKeyName(code);
        f << ACTION_NAMES[i] << '=' << keyName << '\n';
    }
}

InputMap InputMap::load(const std::string& path) {
    InputMap result = defaults();
    std::ifstream f(path);
    if (!f) return result;

    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string name   = line.substr(0, eq);
        std::string keyStr = line.substr(eq + 1);
        Action a;
        if (!actionFromString(name, a)) continue;
        SDL_Keycode code = SDL_GetKeyFromName(keyStr.c_str());
        if (code == SDLK_UNKNOWN) continue;
        result.set(a, code);
    }
    return result;
}

// ─── GamepadMap ───────────────────────────────────────────────────────────────

GamepadBinding GamepadMap::get(Action a) const {
    auto it = bindings.find(a);
    return it != bindings.end() ? it->second : GamepadBinding{};
}

GamepadMap GamepadMap::defaults() {
    GamepadMap m;
    // Movement — D-pad (left stick is hardcoded in Input::beginFrame)
    m.setButton(Action::MoveUp,          SDL_CONTROLLER_BUTTON_DPAD_UP);
    m.setButton(Action::MoveDown,        SDL_CONTROLLER_BUTTON_DPAD_DOWN);
    m.setButton(Action::MoveLeft,        SDL_CONTROLLER_BUTTON_DPAD_LEFT);
    m.setButton(Action::MoveRight,       SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
    // Terrain
    m.setButton(Action::Strafe,          SDL_CONTROLLER_BUTTON_LEFTSHOULDER);
    m.setButton(Action::Dig,             SDL_CONTROLLER_BUTTON_X);
    m.setButton(Action::Plant,           SDL_CONTROLLER_BUTTON_Y);
    m.setButton(Action::PlacePortal,     SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
    // Recording
    m.setButton(Action::Record,          SDL_CONTROLLER_BUTTON_BACK);
    m.setButton(Action::CycleRecording,  SDL_CONTROLLER_BUTTON_LEFTSTICK);
    m.setButton(Action::Deploy,          SDL_CONTROLLER_BUTTON_B);
    // Grid
    m.setButton(Action::SwitchGrid,      SDL_CONTROLLER_BUTTON_START);
    // Camera — right stick
    m.setAxis  (Action::PanLeft,         SDL_CONTROLLER_AXIS_RIGHTX, false);
    m.setAxis  (Action::PanRight,        SDL_CONTROLLER_AXIS_RIGHTX, true);
    m.setAxis  (Action::PanUp,           SDL_CONTROLLER_AXIS_RIGHTY, false);
    m.setAxis  (Action::PanDown,         SDL_CONTROLLER_AXIS_RIGHTY, true);
    m.setButton(Action::ResetCamera,     SDL_CONTROLLER_BUTTON_RIGHTSTICK);
    m.setAxis  (Action::ZoomModifier,    SDL_CONTROLLER_AXIS_TRIGGERLEFT, true, 8000);
    // UI
    m.setButton(Action::Confirm,         SDL_CONTROLLER_BUTTON_A);
    m.setButton(Action::ToggleControls,  SDL_CONTROLLER_BUTTON_GUIDE);
    // Quit, ToggleRecordings, ToggleRebind — keyboard-only by default
    return m;
}

// ─── Input ───────────────────────────────────────────────────────────────────

Input::~Input() {
    if (controller_) SDL_GameControllerClose(controller_);
}

void Input::beginFrame() {
    previous_    = current_;
    padPrevious_ = padCurrent_;
    scrollDelta_ = 0;

    // Rebuild gamepad action set from raw button + axis state.
    padCurrent_.clear();
    for (auto& [action, binding] : padMap_.bindings) {
        bool active = false;
        switch (binding.type) {
            case GamepadBinding::Type::Button:
                active = padButtons_.count(binding.index) > 0;
                break;
            case GamepadBinding::Type::AxisPos:
                active = axisValues_[binding.index] > binding.deadzone;
                break;
            case GamepadBinding::Type::AxisNeg:
                active = axisValues_[binding.index] < -binding.deadzone;
                break;
            default: break;
        }
        if (active) padCurrent_.insert(action);
    }

    // Left stick always drives movement (in addition to D-pad).
    if (axisValues_[SDL_CONTROLLER_AXIS_LEFTX] < -STICK_DEAD) padCurrent_.insert(Action::MoveLeft);
    if (axisValues_[SDL_CONTROLLER_AXIS_LEFTX] >  STICK_DEAD) padCurrent_.insert(Action::MoveRight);
    if (axisValues_[SDL_CONTROLLER_AXIS_LEFTY] < -STICK_DEAD) padCurrent_.insert(Action::MoveUp);
    if (axisValues_[SDL_CONTROLLER_AXIS_LEFTY] >  STICK_DEAD) padCurrent_.insert(Action::MoveDown);
}

void Input::handleEvent(const SDL_Event& e) {
    // ── Gamepad device management ─────────────────────────────────────────────
    if (e.type == SDL_CONTROLLERDEVICEADDED && controller_ == nullptr) {
        controller_   = SDL_GameControllerOpen(e.cdevice.which);
        controllerID_ = SDL_JoystickInstanceID(
                            SDL_GameControllerGetJoystick(controller_));
        return;
    }
    if (e.type == SDL_CONTROLLERDEVICEREMOVED) {
        if (controller_ && e.cdevice.which == controllerID_) {
            SDL_GameControllerClose(controller_);
            controller_   = nullptr;
            controllerID_ = -1;
            padButtons_.clear();
            std::fill(std::begin(axisValues_), std::end(axisValues_), Sint16(0));
        }
        return;
    }

    // ── Gamepad input ─────────────────────────────────────────────────────────
    // controllerID_ == -1 when no controller is open; events with which == -1
    // still match so that tests can inject synthetic gamepad events without
    // a physical device.
    if (e.type == SDL_CONTROLLERBUTTONDOWN && e.cbutton.which == controllerID_) {
        padButtons_.insert(e.cbutton.button);
        return;
    }
    if (e.type == SDL_CONTROLLERBUTTONUP && e.cbutton.which == controllerID_) {
        padButtons_.erase(e.cbutton.button);
        return;
    }
    if (e.type == SDL_CONTROLLERAXISMOTION && e.caxis.which == controllerID_) {
        if (e.caxis.axis < SDL_CONTROLLER_AXIS_MAX)
            axisValues_[e.caxis.axis] = e.caxis.value;
        return;
    }

    // ── Mouse wheel ───────────────────────────────────────────────────────────
    if (e.type == SDL_MOUSEWHEEL) {
        scrollDelta_ += e.wheel.y;
        return;
    }

    // ── Keyboard ──────────────────────────────────────────────────────────────
    // Normalise modifier pairs so a single Action matches either physical key.
    SDL_Keycode key = e.key.keysym.sym;
    if (key == SDLK_RSHIFT) key = SDLK_LSHIFT;
    if (key == SDLK_RCTRL)  key = SDLK_LCTRL;

    if (e.type == SDL_KEYDOWN && e.key.repeat == 0)
        current_.insert(key);
    else if (e.type == SDL_KEYUP)
        current_.erase(key);
}

bool Input::held(Action a) const {
    SDL_Keycode code = map_.get(a);
    if (code != SDLK_UNKNOWN && current_.count(code)) return true;
    return padCurrent_.count(a) > 0;
}

bool Input::pressed(Action a) const {
    SDL_Keycode code = map_.get(a);
    bool keyPressed  = code != SDLK_UNKNOWN && current_.count(code) && !previous_.count(code);
    bool padPressed  = padCurrent_.count(a) > 0 && padPrevious_.count(a) == 0;
    return keyPressed || padPressed;
}

bool Input::released(Action a) const {
    SDL_Keycode code = map_.get(a);
    bool keyReleased = code != SDLK_UNKNOWN && !current_.count(code) && previous_.count(code);
    bool padReleased = padCurrent_.count(a) == 0 && padPrevious_.count(a) > 0;
    return keyReleased || padReleased;
}
