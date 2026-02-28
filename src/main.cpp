#include <SDL2/SDL.h>
#include <algorithm>
#include <cstdint>
#include <cstdlib>

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

    std::srand(static_cast<unsigned>(SDL_GetTicks64()));

    // ── Mushroom collection on arrival ───────────────────────────────────────
    events.subscribe(EventType::Arrived, [&](const Event& ev) {
        if (ev.subject != playerID) return;
        Entity* player = registry.get(playerID);
        if (!player) return;

        for (EntityID cid : spatial.at(player->pos)) {
            if (cid == playerID) continue;
            Entity* cand = registry.get(cid);
            if (!cand || cand->type != EntityType::Mushroom) continue;

            player->mana += 3;
            spatial.remove(cid, cand->pos, cand->size);
            registry.destroy(cid);
            break;
        }
    });

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
                    if (!input.held(Key::Shift))
                        player->facing = toDirection(delta);

                    std::vector<MoveIntention> intentions = {{
                        playerID, player->pos, newDest, player->type, player->size
                    }};
                    auto allowed = resolveMoves(intentions, spatial, registry);
                    if (allowed.count(playerID)) {
                        player->destination = newDest;
                    } else {
                        // Bump combat: move blocked — check for goblin and push it.
                        Bounds destBounds = boundsAt(newDest, player->size);
                        for (EntityID cid : spatial.query(destBounds)) {
                            Entity* cand = registry.get(cid);
                            if (!cand || cand->type != EntityType::Goblin) continue;
                            if (!overlaps(destBounds, boundsAt(cand->pos, cand->size))) continue;

                            cand->health -= player->mana;
                            if (cand->health <= 0) {
                                spatial.remove(cid, cand->pos, cand->size);
                                if (cand->isMoving())
                                    spatial.remove(cid, cand->destination, cand->size);
                                registry.destroy(cid);
                            } else {
                                TilePos pushBase = cand->isMoving() ? cand->destination : cand->pos;
                                TilePos pushDest = pushBase + delta;
                                if (cand->isMoving()) {
                                    // Mid-move: check pushDest and swap destination registration.
                                    Bounds pushBounds = boundsAt(pushDest, cand->size);
                                    bool pushBlocked = false;
                                    for (EntityID oid : spatial.query(pushBounds)) {
                                        if (oid == cid) continue;
                                        const Entity* occ = registry.get(oid);
                                        if (!occ || !overlaps(pushBounds, boundsAt(occ->pos, occ->size))) continue;
                                        if (resolveCollision(cand->type, occ->type) == CollisionResult::Block) {
                                            pushBlocked = true;
                                            break;
                                        }
                                    }
                                    if (!pushBlocked) {
                                        spatial.remove(cid, cand->destination, cand->size);
                                        spatial.add(cid, pushDest, cand->size);
                                        cand->destination = pushDest;
                                    }
                                } else {
                                    std::vector<MoveIntention> push = {{
                                        cid, cand->pos, pushDest, cand->type, cand->size
                                    }};
                                    auto pushAllowed = resolveMoves(push, spatial, registry);
                                    if (pushAllowed.count(cid))
                                        cand->destination = pushDest;
                                }
                            }
                            break;
                        }
                    }
                }
            }

            // ── Terrain interaction ──────────────────────────────────────────
            if (player && player->isIdle()) {
                TilePos ahead = player->pos + dirToDelta(player->facing);

                if (input.pressed(Key::F)) {
                    terrain.dig(ahead);
                }

                if (input.pressed(Key::C)) {
                    if (terrain.typeAt(ahead) == TileType::BareEarth && player->mana >= 1) {
                        EntityID mid = registry.spawn(EntityType::Mushroom, ahead);
                        Entity*  m   = registry.get(mid);
                        spatial.add(mid, m->pos, m->size);
                        terrain.restore(ahead);
                        player->mana--;
                    }
                }
            }

            // ── Goblin wander ────────────────────────────────────────────────
            static const TilePos wanderDirs[] = {{1,0},{-1,0},{0,1},{0,-1}};
            for (Entity* ent : registry.all()) {
                if (ent->type != EntityType::Goblin || !ent->isIdle()) continue;
                if (std::rand() % 80 != 0) continue;

                TilePos delta   = wanderDirs[std::rand() % 4];
                TilePos newDest = ent->pos + delta;
                std::vector<MoveIntention> intentions = {{
                    ent->id, ent->pos, newDest, ent->type, ent->size
                }};
                auto allowed = resolveMoves(intentions, spatial, registry);
                if (allowed.count(ent->id)) {
                    ent->destination = newDest;
                    ent->facing = toDirection(delta);
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

            if (Entity* p = registry.get(playerID))
                renderer.setTitle("Grid Game  |  mana: " + std::to_string(p->mana));

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
