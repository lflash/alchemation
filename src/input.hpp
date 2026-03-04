#pragma once

#include <SDL2/SDL.h>
#include <unordered_map>
#include <unordered_set>

// Logical actions — what the player intends, not which physical key is pressed.
enum class Action {
    // Movement
    MoveUp, MoveDown, MoveLeft, MoveRight,
    Strafe,          // hold to move without turning
    // Terrain
    Dig, Plant, PlacePortal,
    // Recording / agents
    Record, CycleRecording, Deploy,
    // Grid
    SwitchGrid,
    // Camera
    PanUp, PanDown, PanLeft, PanRight,
    ResetCamera, ZoomModifier,
    // UI
    Quit, Confirm, ToggleControls, ToggleRecordings,
};

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
};

// ─── Input ───────────────────────────────────────────────────────────────────

class Input {
public:
    // Call once per frame before polling SDL events.
    void beginFrame();

    // Call for each SDL event (keydown, keyup, mousewheel).
    void handleEvent(const SDL_Event& e);

    bool held(Action a)     const;   // action key is currently down
    bool pressed(Action a)  const;   // action key went down this frame
    bool released(Action a) const;   // action key went up this frame

    // Signed scroll accumulator for the current frame (positive = scroll up).
    int scroll() const { return scrollDelta_; }

    // Replace the active key map. Takes effect from the next query.
    void setMap(const InputMap& map) { map_ = map; }
    const InputMap& getMap() const   { return map_; }

private:
    std::unordered_set<SDL_Keycode> current_;
    std::unordered_set<SDL_Keycode> previous_;
    int      scrollDelta_ = 0;
    InputMap map_         = InputMap::defaults();
};
