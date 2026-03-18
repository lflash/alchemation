// play.cpp — turn-by-turn CLI driver for AI play sessions.
//
// Usage:
//   ./play [action] [ticks]
//   ./play action:ticks action:ticks ...   (multi-step)
//
// action: w a s d  (move)
//         f        (dig)
//         c        (plant)
//         h        (hit)
//         p        (pick up / drop)
//         b        (drop)
//         e        (summon / execute action)
//         z        (cycle active action)
//         g        (scythe)
//         r        (record toggle)
//         wait     (advance time without input)
//
// ticks: how many ticks to simulate after the action (default 20 — enough for
//        movement to settle at player speed 0.17, which needs ~6 ticks).
//
// Multi-step: each arg is "action:ticks" — run all steps in one invocation,
//   then print state and save once at the end. Useful for recording sessions
//   that must happen in a single run (recording state is not persisted).
//   Example: ./play r:1 f:1 c:1 s:15 r:1 w:15 w:15 s:15 e:60
//
// Save file: play.sav in the working directory.
// On first run (no save file) a fresh game is created and state is printed
// without applying any action.

#include "game.hpp"
#include "input.hpp"
#include "terminal_renderer.hpp"

#include <iostream>
#include <sstream>
#include <string>

static const char* SAVE_PATH = "play.sav";

// ── Key lookup ────────────────────────────────────────────────────────────────

static SDL_Keycode keyForAction(const std::string& a) {
    if (a == "w") return SDLK_w;
    if (a == "a") return SDLK_a;
    if (a == "s") return SDLK_s;
    if (a == "d") return SDLK_d;
    if (a == "f") return SDLK_f;   // Dig
    if (a == "c") return SDLK_c;   // Plant
    if (a == "h") return SDLK_h;   // Hit
    if (a == "p") return SDLK_p;   // PickUp
    if (a == "b") return SDLK_b;   // Drop
    if (a == "e") return SDLK_e;   // Summon/Execute
    if (a == "z") return SDLK_z;   // CycleAction
    if (a == "g") return SDLK_g;   // Scythe
    if (a == "o") return SDLK_o;   // PlacePortal
    if (a == "tab") return SDLK_TAB;  // SwitchGrid (studio)
    if (a == "r") return SDLK_r;   // Record
    return SDLK_UNKNOWN;
}

// ── State printer ─────────────────────────────────────────────────────────────

static void printState(Game& g) {
    std::ostringstream grid;
    TerminalRenderer r(grid);
    r.setCamera(g.playerPos());
    r.beginFrame();
    r.drawTerrain(g.terrain());
    for (const Entity* e : g.drawOrder()) {
        r.drawSprite(toVec(e->pos), 0.f, e->type, e->id, e->moveProgress);
        for (int i = 1; i < e->tileCount; ++i)
            r.drawSprite(toVec(e->extraTiles[i-1]), 0.f, e->type, e->id, e->moveProgress);
    }
    r.endFrame();
    std::cout << grid.str() << '\n' << gameStateText(g) << '\n';
}

// ── main ─────────────────────────────────────────────────────────────────────

// ── Step runner ───────────────────────────────────────────────────────────────

static void runStep(Game& g, Tick& t, const std::string& action, int ticks) {
    SDL_Keycode key = keyForAction(action);
    if (key != SDLK_UNKNOWN) {
        Input inp;
        SDL_Event dn{}; dn.type = SDL_KEYDOWN; dn.key.keysym.sym = key;
        inp.beginFrame(); inp.handleEvent(dn); g.tick(inp, t++);

        SDL_Event up{}; up.type = SDL_KEYUP; up.key.keysym.sym = key;
        inp.beginFrame(); inp.handleEvent(up); g.tick(inp, t++);
    }
    Input idle;
    for (int i = 0; i < ticks; ++i) { idle.beginFrame(); g.tick(idle, t++); }
}

// ── main ─────────────────────────────────────────────────────────────────────

int main(int argc, char** argv) {
    Game  g;
    Tick  t = 0;

    // Load existing save or start fresh.
    bool loaded = g.load(SAVE_PATH);
    if (!loaded) {
        std::cerr << "(no save file — starting fresh game)\n\n";
        Input idle;
        for (int i = 0; i < 3; ++i) { idle.beginFrame(); g.tick(idle, i); }
        t = 3;
        printState(g);
        g.save(SAVE_PATH);
        return 0;
    }

    // Multi-step mode: each argv is "action:ticks".
    if (argc >= 2 && std::string(argv[1]).find(':') != std::string::npos) {
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            auto colon = arg.find(':');
            std::string act  = arg.substr(0, colon);
            int         tick = (colon != std::string::npos) ? std::stoi(arg.substr(colon + 1)) : 20;
            runStep(g, t, act, tick);
        }
    } else {
        // Single-step mode: ./play [action] [ticks]
        std::string action = (argc >= 2) ? argv[1] : "wait";
        int         ticks  = (argc >= 3) ? std::stoi(argv[2]) : 20;
        runStep(g, t, action, ticks);
    }

    printState(g);
    g.save(SAVE_PATH);
    return 0;
}
