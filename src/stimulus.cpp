#include "stimulus.hpp"
#include <queue>
#include <unordered_set>
#include <vector>

// ─── Fire simulation ─────────────────────────────────────────────────────────
//
// 50 Hz timestep: 1s = 50 ticks, 3s = 150 ticks, 5s = 250 ticks, 10s = 500 ticks.
// Grass adjacent to fire catches after 50 ticks and burns for 150 ticks → BareEarth.
// TreeStump/Log adjacent to fire ignites after 250 ticks and despawns 500 ticks later.

void tickFire(Grid& grid, EntityRegistry& registry, Tick currentTick) {
    static const TilePos kDirs4[] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0}};

    // 1. Expire fire tiles whose timer has elapsed → BareEarth.
    //    Also extinguish any Fire tile adjacent to a Water tile.
    {
        std::vector<TilePos> done;
        for (const auto& [pos, expiry] : grid.fireTileExpiry) {
            if (currentTick >= expiry) { done.push_back(pos); continue; }
            for (const auto& d : kDirs4) {
                if (grid.terrain.typeAt(pos + d) == TileType::Water)
                    { done.push_back(pos); break; }
            }
        }
        for (const TilePos& pos : done) {
            grid.terrain.setType(pos, TileType::BareEarth);
            grid.fireTileExpiry.erase(pos);
            grid.tileFireExp.erase(pos);
            grid.waterLevel.erase(pos);   // in case fire tile was converted from water
        }
    }

    // 2. Despawn burning entities whose timer has elapsed.
    {
        std::vector<EntityID> done;
        for (const auto& [eid, burnEnd] : grid.entityBurnEnd)
            if (currentTick >= burnEnd) done.push_back(eid);
        for (EntityID eid : done) {
            Entity* e = registry.get(eid);
            if (e) grid.remove(eid, *e);
            registry.destroy(eid);
            grid.entityBurnEnd.erase(eid);
            grid.entityFireExp.erase(eid);
        }
    }

    // 3. Build heated set: all tiles adjacent to a Fire tile or Campfire entity.
    std::unordered_set<TilePos, TilePosHash> heated;
    for (const auto& [pos, _] : grid.fireTileExpiry)
        for (const auto& d : kDirs4)
            heated.insert(pos + d);
    for (EntityID eid : grid.entities) {
        const Entity* e = registry.get(eid);
        if (!e || e->type != EntityType::Campfire) continue;
        for (const auto& d : kDirs4)
            heated.insert(e->pos + d);
    }

    // 4. Decay (remove) exposure for tiles/entities no longer in heat.
    {
        std::vector<TilePos> cold;
        for (const auto& [pos, _] : grid.tileFireExp)
            if (!heated.count(pos)) cold.push_back(pos);
        for (const TilePos& pos : cold) grid.tileFireExp.erase(pos);
    }
    {
        std::vector<EntityID> cold;
        for (const auto& [eid, _] : grid.entityFireExp) {
            const Entity* e = registry.get(eid);
            if (!e || !heated.count(e->pos)) cold.push_back(eid);
        }
        for (EntityID eid : cold) grid.entityFireExp.erase(eid);
    }

    // 5. Heat Grass tiles: accumulate exposure; ignite at 50 ticks.
    for (const TilePos& pos : heated) {
        if (grid.terrain.typeAt(pos) != TileType::Grass) continue;
        if (++grid.tileFireExp[pos] >= 50) {
            grid.terrain.setType(pos, TileType::Fire);
            grid.fireTileExpiry[pos] = currentTick + 150;
            grid.tileFireExp.erase(pos);
        }
    }

    // 6. Heat TreeStump/Log entities: accumulate exposure; start burning at 250 ticks.
    for (EntityID eid : grid.entities) {
        if (grid.entityBurnEnd.count(eid)) continue;   // already burning
        Entity* e = registry.get(eid);
        if (!e) continue;
        if (e->type != EntityType::TreeStump && e->type != EntityType::Log) continue;
        if (!heated.count(e->pos)) continue;
        if (++grid.entityFireExp[eid] >= 250) {
            grid.entityBurnEnd[eid] = currentTick + 500;
            grid.entityFireExp.erase(eid);
            e->burning = true;
        }
    }
}

// ─── Voltage simulation ───────────────────────────────────────────────────────
//
// Battery entities emit 5V. BFS propagates through adjacent Puddle tiles,
// decrementing by 1 per hop. Lightbulb entities lit if their tile has ≥1V.

void tickVoltage(Grid& grid, EntityRegistry& registry) {
    grid.voltage.clear();

    static const TilePos kDirs4[] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0}};

    std::queue<std::pair<TilePos, int>> q;

    // Seed from Battery entities: adjacent puddle tiles get 4V (5-1).
    for (EntityID eid : grid.entities) {
        const Entity* e = registry.get(eid);
        if (!e || e->type != EntityType::Battery) continue;
        for (const auto& d : kDirs4) {
            TilePos adj = e->pos + d;
            if (grid.terrain.typeAt(adj) != TileType::Puddle) continue;
            if (!grid.voltage.count(adj) || grid.voltage[adj] < 4) {
                grid.voltage[adj] = 4;
                q.push({adj, 4});
            }
        }
    }

    // BFS through puddle network, voltage decrements by 1 per hop.
    while (!q.empty()) {
        auto [pos, v] = q.front();
        q.pop();
        if (v <= 1) continue;
        for (const auto& d : kDirs4) {
            TilePos adj = pos + d;
            if (grid.terrain.typeAt(adj) != TileType::Puddle) continue;
            if (!grid.voltage.count(adj) || grid.voltage[adj] < v - 1) {
                grid.voltage[adj] = v - 1;
                q.push({adj, v - 1});
            }
        }
    }

    // Update Lightbulb lit state and electrified flag for all other entities.
    for (EntityID eid : grid.entities) {
        Entity* e = registry.get(eid);
        if (!e) continue;
        auto it = grid.voltage.find(e->pos);
        bool charged = (it != grid.voltage.end() && it->second >= 1);
        if (e->type == EntityType::Lightbulb)
            e->lit = charged;
        else
            e->electrified = charged;
    }
}

// ─── Water simulation ─────────────────────────────────────────────────────────
//
// Each water tile carries a floating-point level (depth). Each tick it distributes
// its level equally among itself and eligible adjacent non-water tiles. Total
// volume is conserved. Tiles that drop to ≤ 0.1 convert to Puddle and stop spreading.

void tickWater(Grid& grid) {
    static const TilePos kDirs4[] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0}};

    // Accumulate level deltas for this tick (applied after all sources are processed).
    std::unordered_map<TilePos, float, TilePosHash> delta;

    for (const auto& [pos, level] : grid.waterLevel) {
        if (level <= 0.1f) continue;   // will be converted to Puddle below

        int srcTerrain = grid.terrain.levelAt(pos);

        std::vector<TilePos> eligible;
        for (const auto& d : kDirs4) {
            TilePos adj = pos + d;
            TileType adjType = grid.terrain.typeAt(adj);
            if (adjType == TileType::Water || adjType == TileType::Portal ||
                adjType == TileType::Fire) continue;
            int dstTerrain = grid.terrain.levelAt(adj);
            if (dstTerrain > srcTerrain || srcTerrain - dstTerrain > 1) continue;
            eligible.push_back(adj);
        }

        if (eligible.empty()) continue;

        // Distribute level evenly: origin keeps 1 share, each neighbor gets 1 share.
        float share = level / static_cast<float>(eligible.size() + 1);
        delta[pos] -= level - share;   // origin loses the distributed portion
        for (const TilePos& n : eligible)
            delta[n] += share;
    }

    // Apply deltas.
    for (const auto& [pos, dv] : delta) {
        float newLevel = grid.waterLevel[pos] + dv;
        if (newLevel <= 0.1f) {
            grid.waterLevel.erase(pos);
            grid.terrain.setType(pos, TileType::Puddle);
        } else {
            grid.waterLevel[pos] = newLevel;
            grid.terrain.setType(pos, TileType::Water);
        }
    }

    // Convert any remaining sub-threshold water tiles to Puddle.
    std::vector<TilePos> toPuddle;
    for (const auto& [pos, level] : grid.waterLevel)
        if (level <= 0.1f) toPuddle.push_back(pos);
    for (const TilePos& pos : toPuddle) {
        grid.terrain.setType(pos, TileType::Puddle);
        grid.waterLevel.erase(pos);
    }
}
