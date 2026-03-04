#include "input.hpp"

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
    };
    return m;
}

// ─── Input ───────────────────────────────────────────────────────────────────

void Input::beginFrame() {
    previous_    = current_;
    scrollDelta_ = 0;
}

void Input::handleEvent(const SDL_Event& e) {
    if (e.type == SDL_MOUSEWHEEL) {
        scrollDelta_ += e.wheel.y;
        return;
    }

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
    return code != SDLK_UNKNOWN && current_.count(code) > 0;
}

bool Input::pressed(Action a) const {
    SDL_Keycode code = map_.get(a);
    return code != SDLK_UNKNOWN && current_.count(code) && !previous_.count(code);
}

bool Input::released(Action a) const {
    SDL_Keycode code = map_.get(a);
    return code != SDLK_UNKNOWN && !current_.count(code) && previous_.count(code);
}
