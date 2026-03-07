#include "game.hpp"
#include "terrain.hpp"
#include <algorithm>
#include <cstdlib>

// ─── Height helper (local to movement subsystem) ──────────────────────────────

static bool resolveDestHeight(TilePos& dest, const TilePos& from, const Grid& grid) {
    if (grid.isBounded()) return true;   // flat floor; no height check
    dest.z = grid.terrain.levelAt(dest);
    return std::abs(dest.z - from.z) <= 1;
}

// ─── Goblin wander ────────────────────────────────────────────────────────────

void Game::tickGoblinWander(Grid& grid) {
    static const TilePos wanderDirs[] = {{1,0},{-1,0},{0,1},{0,-1}};

    for (EntityID eid : grid.entities) {
        Entity* ent = registry_.get(eid);
        if (!ent || ent->type != EntityType::Goblin || !ent->isIdle()) continue;
        if (std::rand() % 80 != 0) continue;

        TilePos delta   = wanderDirs[std::rand() % 4];
        TilePos newDest = ent->pos + delta;
        if (grid.isBounded()) {
            newDest.x = std::clamp(newDest.x, 0, grid.width  - 1);
            newDest.y = std::clamp(newDest.y, 0, grid.height - 1);
        }

        if (!resolveDestHeight(newDest, ent->pos, grid)) continue;

        std::vector<MoveIntention> intentions = {{
            eid, ent->pos, newDest, ent->type, ent->size
        }};
        auto allowed = resolveMoves(intentions, grid.spatial, registry_);
        if (allowed.count(eid)) {
            ent->destination = newDest;
            ent->facing      = toDirection(delta);
        }
    }
}

// ─── Movement ────────────────────────────────────────────────────────────────

void Game::tickMovement(Grid& grid) {
    std::vector<EntityID> snapshot = grid.entities;

    for (EntityID eid : snapshot) {
        Entity* ent = registry_.get(eid);
        if (!ent) continue;
        TilePos oldPos  = ent->pos;
        bool    arrived = stepMovement(*ent);
        if (arrived) {
            grid.spatial.move(eid, oldPos, ent->pos, ent->size);

            // Water slows movement: half speed while standing on a Water tile.
            if (ent->speed > 0.0f) {
                TileType tileHere = grid.terrain.typeAt(ent->pos);
                float    baseSpeed = defaultConfig(ent->type).speed;
                ent->speed = (tileHere == TileType::Water) ? baseSpeed * 0.5f : baseSpeed;
            }
            grid.events.emit({ EventType::Arrived, eid });

            if (eid == playerID_) {
                audioEvents_.push_back(AudioEvent::PlayerStep);
                if (ent->pos.z != playerPrevZ_) {
                    visualEvents_.push_back({ VisualEventType::PlayerLand,
                                             toVec(ent->pos),
                                             static_cast<float>(ent->pos.z) });
                }
                playerPrevZ_ = ent->pos.z;
            }

            // Portal check: queue a transfer for any entity that lands on a portal.
            auto pit = grid.portals.find(ent->pos);
            if (pit != grid.portals.end())
                pendingTransfers_.push_back({ eid, grid.id,
                                              pit->second.targetGrid, pit->second.targetPos });
        }
    }
}
