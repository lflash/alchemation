#pragma once

#include <SDL2/SDL.h>
#include <string>
#include <unordered_map>
#include <unordered_set>

// Logical actions — what the player intends, not which physical key is pressed.
enum class Action {
    // Movement
    MoveUp, MoveDown, MoveLeft, MoveRight,
    Strafe,          // hold to move without turning
    // Terrain
    Dig, Plant, PlacePortal, Scythe, Mine,
    // Recording / agents
    Record, CycleRecording, Summon, CycleAction,
    // Carry
    PickUp, Drop,
    // Combat
    Hit,
    // Grid
    SwitchGrid,
    // Camera
    PanUp, PanDown, PanLeft, PanRight,
    ResetCamera, ZoomModifier,
    // UI
    Quit, Confirm, ToggleControls, ToggleRecordings, ToggleRebind,
};

// Total number of bindable actions.
inline constexpr int INPUT_ACTION_COUNT = static_cast<int>(Action::ToggleRebind) + 1;

// std::hash specialisation so Action can be used as an unordered_map key.
template<>
struct std::hash<Action> {
    std::size_t operator()(Action a) const noexcept {
        return std::hash<int>{}(static_cast<int>(a));
    }
};

// ─── InputMap ────────────────────────────────────────────────────────────────
//
// Mutable table mapping each Action to a physical SDL_Keycode.
// Swap the map at runtime (via Input::setMap) to implement key remapping.

struct InputMap {
    std::unordered_map<Action, SDL_Keycode> bindings;

    // Returns the keycode bound to action, or SDLK_UNKNOWN if unbound.
    SDL_Keycode get(Action a) const;

    // Bind a single action to a keycode.
    void set(Action a, SDL_Keycode code) { bindings[a] = code; }

    // Returns a map pre-filled with the default WASD keyboard layout.
    static InputMap defaults();

    // Persist bindings to a plain-text file (one "ActionName=KeyName" per line).
    void save(const std::string& path) const;

    // Load bindings from a settings file.  Missing or unrecognised lines
    // fall back to the default binding for that action, so a partial file
    // is safe.  Returns defaults() if the file is absent.
    static InputMap load(const std::string& path);
};

// ─── GamepadBinding ──────────────────────────────────────────────────────────
//
// Describes how one Action maps to a controller input.

struct GamepadBinding {
    enum class Type { None, Button, AxisPos, AxisNeg } type = Type::None;
    int    index    = 0;       // SDL_GameControllerButton or SDL_GameControllerAxis
    Sint16 deadzone = 12000;   // axis-only: threshold (~37 % of max 32767)
};

// ─── GamepadMap ───────────────────────────────────────────────────────────────
//
// Mutable table mapping each Action to a controller button or axis.

struct GamepadMap {
    std::unordered_map<Action, GamepadBinding> bindings;

    // Returns the binding for the given action (Type::None if unbound).
    GamepadBinding get(Action a) const;

    // Bind an action to a digital controller button.
    void setButton(Action a, int button) {
        bindings[a] = { GamepadBinding::Type::Button, button };
    }

    // Bind an action to an axis threshold (positive or negative side).
    void setAxis(Action a, int axis, bool positive, Sint16 deadzone = 12000) {
        bindings[a] = {
            positive ? GamepadBinding::Type::AxisPos : GamepadBinding::Type::AxisNeg,
            axis, deadzone
        };
    }

    // Returns the default Xbox-style layout.
    static GamepadMap defaults();
};

// ─── Input ───────────────────────────────────────────────────────────────────

class Input {
public:
    ~Input();

    // Call once per frame before polling SDL events.
    void beginFrame();

    // Call for each SDL event (keydown, keyup, mousewheel, controller events).
    void handleEvent(const SDL_Event& e);

    bool held(Action a)     const;   // action is currently active (key or pad)
    bool pressed(Action a)  const;   // action went active this frame
    bool released(Action a) const;   // action went inactive this frame

    // Signed scroll accumulator for the current frame (positive = scroll up).
    int scroll() const { return scrollDelta_; }

    // Replace the active key map. Takes effect from the next query.
    void setMap(const InputMap& map) { map_ = map; }
    const InputMap& getMap() const   { return map_; }

    // Replace the active gamepad map. Takes effect at the next beginFrame().
    void setGamepadMap(const GamepadMap& m) { padMap_ = m; }
    const GamepadMap& getGamepadMap() const { return padMap_; }

    // True when a controller is currently open.
    bool hasGamepad() const { return controller_ != nullptr; }

private:
    // ── Keyboard ──────────────────────────────────────────────────────────────
    std::unordered_set<SDL_Keycode> current_;
    std::unordered_set<SDL_Keycode> previous_;
    int      scrollDelta_ = 0;
    InputMap map_         = InputMap::defaults();

    // ── Gamepad ───────────────────────────────────────────────────────────────
    // Actions are rebuilt each beginFrame() from padButtons_ + axisValues_,
    // so adding to padCurrent_ from two sources (D-pad + stick) is always safe.
    std::unordered_set<Action> padCurrent_;
    std::unordered_set<Action> padPrevious_;
    std::unordered_set<int>    padButtons_;                    // raw held buttons
    Sint16 axisValues_[SDL_CONTROLLER_AXIS_MAX] = {};          // raw axis values

    GamepadMap          padMap_        = GamepadMap::defaults();
    SDL_GameController* controller_    = nullptr;
    SDL_JoystickID      controllerID_  = -1;   // -1 = no controller / test mode

    // Left-stick deadzone for hardcoded movement mapping.
    static constexpr Sint16 STICK_DEAD = 12000;
};
