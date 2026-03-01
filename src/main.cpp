#include <SDL2/SDL.h>
#include <algorithm>
#include <cstdlib>
#include <ctime>

#include "types.hpp"
#include "input.hpp"
#include "game.hpp"
#include "renderer.hpp"

int main() {
    std::srand(static_cast<unsigned>(std::time(nullptr)));

    Renderer renderer;
    Input    input;
    Game     game;

    const double TICK_DT     = 1.0 / 50.0;
    double       accumulator = 0.0;
    uint64_t     lastTime    = SDL_GetTicks64();
    Tick         currentTick = 0;
    bool         quit        = false;

    while (!quit) {
        input.beginFrame();

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) quit = true;
            input.handleEvent(e);
        }

        if (input.pressed(Key::Escape)) quit = true;

        uint64_t now = SDL_GetTicks64();
        double   dt  = (now - lastTime) / 1000.0;
        lastTime = now;
        accumulator += std::min(dt, 0.1);

        while (accumulator >= TICK_DT) {
            game.tick(input, currentTick);
            renderer.setTitle("Grid Game  |  mana: " + std::to_string(game.playerMana()));
            accumulator -= TICK_DT;
            ++currentTick;
        }

        renderer.beginFrame();
        renderer.drawTerrain(game.terrain());

        for (const Entity* ent : game.registry().drawOrder()) {
            Vec2f renderPos = lerp(toVec(ent->pos), toVec(ent->destination), ent->moveT);
            renderer.drawSprite(renderPos, ent->type);
        }

        renderer.endFrame();
    }

    return 0;
}
