#include <SDL2/SDL.h>
#include <algorithm>
#include <cstdint>

#include "types.hpp"
#include "terrain.hpp"
#include "entity.hpp"
#include "input.hpp"
#include "spatial.hpp"
#include "scheduler.hpp"
#include "events.hpp"
#include "renderer.hpp"

int main() {
    Renderer       renderer;
    Terrain        terrain;
    EntityRegistry registry;
    SpatialGrid    spatial;
    Scheduler      scheduler;
    EventBus       events;
    Input          input;

    EntityID playerID = registry.spawn(EntityType::Player, {0, 0});
    {
        Entity* e = registry.get(playerID);
        spatial.add(playerID, e->pos, e->size);
    }

    EntityID goblinID = registry.spawn(EntityType::Goblin, {5, 5});
    {
        Entity* e = registry.get(goblinID);
        spatial.add(goblinID, e->pos, e->size);
    }

    const double TICK_DT    = 1.0 / 50.0;
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

            // ── Execute scheduled actions ────────────────────────────────────
            for (auto& action : scheduler.popDue(currentTick)) {
                if (action.type == ActionType::Despawn) {
                    Entity* target = registry.get(action.entity);
                    if (target) spatial.remove(target->id, target->pos, target->size);
                    registry.destroy(action.entity);
                } else if (action.type == ActionType::ChangeMana) {
                    Entity* target = registry.get(action.entity);
                    if (target)
                        target->mana += std::get<ChangeManaPayload>(action.payload).delta;
                }
            }

            // ── Player input ─────────────────────────────────────────────────
            Entity* player = registry.get(playerID);
            if (player && player->isIdle()) {
                TilePos delta = {0, 0};
                if (input.held(Key::W)) delta.y -= 1;
                if (input.held(Key::S)) delta.y += 1;
                if (input.held(Key::A)) delta.x -= 1;
                if (input.held(Key::D)) delta.x += 1;

                if (delta != TilePos{0, 0}) {
                    TilePos newDest = player->pos + delta;
                    player->facing  = toDirection(delta);

                    std::vector<MoveIntention> intentions = {{
                        playerID, player->pos, newDest, player->type, player->size
                    }};
                    auto allowed = resolveMoves(intentions, spatial, registry);
                    if (allowed.count(playerID))
                        player->destination = newDest;
                }
            }

            // ── Step movement ────────────────────────────────────────────────
            for (Entity* ent : registry.all()) {
                TilePos oldPos  = ent->pos;
                bool    arrived = stepMovement(*ent);
                if (arrived) {
                    spatial.move(ent->id, oldPos, ent->pos, ent->size);
                    events.emit({ EventType::Arrived, ent->id });
                }
            }

            events.flush();
            accumulator -= TICK_DT;
            ++currentTick;
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
