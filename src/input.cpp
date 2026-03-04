#include "input.hpp"

SDL_Keycode Input::toSDL(Action a) {
    switch (a) {
        case Action::MoveUp:           return SDLK_w;
        case Action::MoveDown:         return SDLK_s;
        case Action::MoveLeft:         return SDLK_a;
        case Action::MoveRight:        return SDLK_d;
        case Action::Strafe:           return SDLK_LSHIFT;
        case Action::Dig:              return SDLK_f;
        case Action::Plant:            return SDLK_c;
        case Action::PlacePortal:      return SDLK_o;
        case Action::Record:           return SDLK_r;
        case Action::CycleRecording:   return SDLK_q;
        case Action::Deploy:           return SDLK_e;
        case Action::SwitchGrid:       return SDLK_TAB;
        case Action::PanUp:            return SDLK_UP;
        case Action::PanDown:          return SDLK_DOWN;
        case Action::PanLeft:          return SDLK_LEFT;
        case Action::PanRight:         return SDLK_RIGHT;
        case Action::ResetCamera:      return SDLK_BACKSPACE;
        case Action::ZoomModifier:     return SDLK_LCTRL;
        case Action::Quit:             return SDLK_ESCAPE;
        case Action::Confirm:          return SDLK_RETURN;
        case Action::ToggleControls:   return SDLK_h;
        case Action::ToggleRecordings: return SDLK_i;
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

bool Input::held(Action a) const {
    return current.count(toSDL(a)) > 0;
}

bool Input::pressed(Action a) const {
    SDL_Keycode code = toSDL(a);
    return current.count(code) && !previous.count(code);
}

bool Input::released(Action a) const {
    SDL_Keycode code = toSDL(a);
    return !current.count(code) && previous.count(code);
}
