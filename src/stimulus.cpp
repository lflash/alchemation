#include "stimulus.hpp"
#include <queue>
#include <unordered_set>
#include <vector>
#include <unordered_map>

// ─── Fire simulation ─────────────────────────────────────────────────────────
//
// 50 Hz timestep: 1s = 50 ticks, 3s = 150 ticks, 5s = 250 ticks, 10s = 500 ticks.
// Grass adjacent to fire catches after 50 ticks and burns for 150 ticks → BareEarth.
// TreeStump/Log adjacent to fire ignites after 250 ticks and despawns 500 ticks later.

void tickFire(Grid& grid, EntityRegistry& registry, Tick currentTick) {
    static const TilePos kDirs4[] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0}};

    // 1. Expire fire tiles whose timer has elapsed → BareEarth.
    //    Also extinguish any Fire tile adjacent to a Water entity.
    {
        std::vector<TilePos> done;
        for (const auto& [pos, expiry] : grid.fireTileExpiry) {
            if (currentTick >= expiry) { done.push_back(pos); continue; }
            for (const auto& d : kDirs4) {
                bool hasWater = false;
                for (EntityID adj : grid.spatial.at(pos + d)) {
                    const Entity* ae = registry.get(adj);
                    if (ae && ae->type == EntityType::Water) { hasWater = true; break; }
                }
                if (hasWater) { done.push_back(pos); break; }
            }
        }
        for (const TilePos& pos : done) {
            grid.terrain.setType(pos, TileType::BareEarth);
            grid.fireTileExpiry.erase(pos);
            grid.tileFireExp.erase(pos);
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

