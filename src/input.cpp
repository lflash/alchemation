#include "input.hpp"

SDL_Keycode Input::toSDL(Key k) {
    switch (k) {
        case Key::W:      return SDLK_w;
        case Key::A:      return SDLK_a;
        case Key::S:      return SDLK_s;
        case Key::D:      return SDLK_d;
        case Key::E:      return SDLK_e;
        case Key::F:      return SDLK_f;
        case Key::C:      return SDLK_c;
        case Key::R:      return SDLK_r;
        case Key::Q:      return SDLK_q;
        case Key::Escape: return SDLK_ESCAPE;
    }
    return SDLK_UNKNOWN;
}

void Input::beginFrame() {
    previous = current;
}

void Input::handleEvent(const SDL_Event& e) {
    if (e.type == SDL_KEYDOWN && e.key.repeat == 0)
        current.insert(e.key.keysym.sym);
    else if (e.type == SDL_KEYUP)
        current.erase(e.key.keysym.sym);
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
