#include "input.hpp"

SDL_Keycode Input::toSDL(Key k) {
    switch (k) {
        case Key::W:         return SDLK_w;
        case Key::A:         return SDLK_a;
        case Key::S:         return SDLK_s;
        case Key::D:         return SDLK_d;
        case Key::E:         return SDLK_e;
        case Key::F:         return SDLK_f;
        case Key::C:         return SDLK_c;
        case Key::R:         return SDLK_r;
        case Key::Q:         return SDLK_q;
        case Key::H:         return SDLK_h;
        case Key::I:         return SDLK_i;
        case Key::Escape:    return SDLK_ESCAPE;
        case Key::Enter:     return SDLK_RETURN;
        case Key::Shift:     return SDLK_LSHIFT;
        case Key::Tab:       return SDLK_TAB;
        case Key::ArrowUp:   return SDLK_UP;
        case Key::ArrowDown: return SDLK_DOWN;
        case Key::ArrowLeft: return SDLK_LEFT;
        case Key::ArrowRight:return SDLK_RIGHT;
        case Key::Ctrl:      return SDLK_LCTRL;
        case Key::Backspace: return SDLK_BACKSPACE;
    }
    return SDLK_UNKNOWN;
}

void Input::beginFrame() {
    previous    = current;
    scrollDelta_ = 0;
}

void Input::handleEvent(const SDL_Event& e) {
    if (e.type == SDL_MOUSEWHEEL) {
        scrollDelta_ += e.wheel.y;
        return;
    }

    // Normalise modifier pairs so a single Key:: matches either physical key.
    SDL_Keycode key = e.key.keysym.sym;
    if (key == SDLK_RSHIFT) key = SDLK_LSHIFT;
    if (key == SDLK_RCTRL)  key = SDLK_LCTRL;

    if (e.type == SDL_KEYDOWN && e.key.repeat == 0)
        current.insert(key);
    else if (e.type == SDL_KEYUP)
        current.erase(key);
}

bool Input::held(Key k) const {
    return current.count(toSDL(k)) > 0;
}

bool Input::pressed(Key k) const {
    SDL_Keycode code = toSDL(k);
    return current.count(code) && !previous.count(code);
}

bool Input::released(Key k) const {
    SDL_Keycode code = toSDL(k);
    return !current.count(code) && previous.count(code);
}
