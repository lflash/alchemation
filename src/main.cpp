#include <SDL2/SDL.h>
#include <algorithm>
#include <cstdint>

#include "types.hpp"
#include "terrain.hpp"
#include "entity.hpp"
#include "input.hpp"
#include "renderer.hpp"

int main() {
    Renderer       renderer;
    Terrain        terrain;
    EntityRegistry registry;
    Input          input;

    EntityID playerID = registry.spawn(EntityType::Player, {0, 0});

    const double TICK_DT    = 1.0 / 50.0;
    double       accumulator = 0.0;
    uint64_t     lastTime    = SDL_GetTicks64();
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

            // ── Player input ─────────────────────────────────────────────────
            Entity* player = registry.get(playerID);
            if (player && player->isIdle()) {
                TilePos delta = {0, 0};
                if (input.held(Key::W)) delta.y -= 1;
                if (input.held(Key::S)) delta.y += 1;
                if (input.held(Key::A)) delta.x -= 1;
                if (input.held(Key::D)) delta.x += 1;

                if (delta != TilePos{0, 0}) {
                    player->destination = player->pos + delta;
                    player->facing      = toDirection(delta);
                }
            }

            // ── Step movement ────────────────────────────────────────────────
            for (Entity* ent : registry.all())
                stepMovement(*ent);

            accumulator -= TICK_DT;
        }

        // ── Render ───────────────────────────────────────────────────────────
        renderer.beginFrame();
        renderer.drawTerrain(terrain);

        for (const Entity* ent : registry.drawOrder()) {
            Vec2f renderPos = lerp(toVec(ent->pos), toVec(ent->destination), ent->moveT);
            renderer.drawSprite(renderPos, ent->type);
        }

        renderer.endFrame();
    }

    return 0;
}
