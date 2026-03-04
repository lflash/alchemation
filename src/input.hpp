#pragma once

#include <SDL2/SDL.h>
#include <unordered_set>

// Logical actions — what the player intends, not which physical key is pressed.
// The mapping from Action to SDL_Keycode lives in InputMap (input_map.hpp).
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

class Input {
public:
    // Call once per frame before polling SDL events.
    // Saves current key state as previous so pressed()/released() work correctly.
    void beginFrame();

    // Call for each SDL_KEYDOWN / SDL_KEYUP event.
    void handleEvent(const SDL_Event& e);

    bool held(Action a)     const;   // key is currently down
    bool pressed(Action a)  const;   // key went down this frame (not on repeat)
    bool released(Action a) const;   // key went up this frame

    // Signed scroll accumulator for the current frame (positive = scroll up).
    // Reset to zero in beginFrame(). Caller checks held(Key::Ctrl) separately.
    int scroll() const { return scrollDelta_; }

private:
    std::unordered_set<SDL_Keycode> current;
    std::unordered_set<SDL_Keycode> previous;
    int scrollDelta_ = 0;

    static SDL_Keycode toSDL(Action a);
};
