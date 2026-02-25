#pragma once

#include <SDL2/SDL.h>
#include <unordered_set>

enum class Key { W, A, S, D, E, F, C, R, Q, Escape };

class Input {
public:
    // Call once per frame before polling SDL events.
    // Saves current key state as previous so pressed()/released() work correctly.
    void beginFrame();

    // Call for each SDL_KEYDOWN / SDL_KEYUP event.
    void handleEvent(const SDL_Event& e);

    bool held(Key k)     const;   // key is currently down
    bool pressed(Key k)  const;   // key went down this frame (not on repeat)
    bool released(Key k) const;   // key went up this frame

private:
    std::unordered_set<SDL_Keycode> current;
    std::unordered_set<SDL_Keycode> previous;

    static SDL_Keycode toSDL(Key k);
};
