#include "effectSpread.hpp"
#include <queue>
#include <unordered_set>
#include <vector>
#include <unordered_map>

// ─── Fire simulation ─────────────────────────────────────────────────────────
//
// 50 Hz timestep: 1s = 50 ticks, 3s = 150 ticks, 5s = 250 ticks, 10s = 500 ticks.
// Grass adjacent to fire catches after 50 ticks and burns for 150 ticks → BareEarth.
// TreeStump/Log adjacent to fire ignites after 250 ticks and despawns 500 ticks later.

void tickFire(Field& field, EntityRegistry& registry, Tick currentTick) {
    static const TilePos kDirs4[] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0}};

    // 1. Expire fire tiles whose timer has elapsed → BareEarth.
    //    Also extinguish any Fire tile adjacent to a Water entity.
    {
        std::vector<TilePos> done;
        for (const auto& [pos, expiry] : field.fireTileExpiry) {
            if (currentTick >= expiry) { done.push_back(pos); continue; }
            for (const auto& d : kDirs4) {
                bool hasWater = false;
                for (EntityID adj : field.spatial.at(pos + d)) {
                    const Entity* ae = registry.get(adj);
                    if (ae && ae->type == EntityType::Water) { hasWater = true; break; }
                }
                if (hasWater) { done.push_back(pos); break; }
            }
        }
        for (const TilePos& pos : done) {
            // Despawn the Fire entity and replace with BareEarth.
            for (EntityID feid : std::vector<EntityID>(field.spatial.at(pos))) {
                Entity* fe = registry.get(feid);
                if (fe && fe->type == EntityType::Fire) {
                    field.remove(feid, *fe); registry.destroy(feid); break;
                }
            }
            EntityID beid = registry.spawn(EntityType::BareEarth, pos);
            field.add(beid, *registry.get(beid));
            field.fireTileExpiry.erase(pos);
            field.tileFireExp.erase(pos);
        }
    }

    // 2. Despawn burning entities whose timer has elapsed.
    {
        std::vector<EntityID> done;
        for (const auto& [eid, burnEnd] : field.entityBurnEnd)
            if (currentTick >= burnEnd) done.push_back(eid);
        for (EntityID eid : done) {
            Entity* e = registry.get(eid);
            if (e) field.remove(eid, *e);
            registry.destroy(eid);
            field.entityBurnEnd.erase(eid);
            field.entityFireExp.erase(eid);
        }
    }

    // 3. Build heated set: all tiles adjacent to a Fire tile or Campfire entity.
    std::unordered_set<TilePos, TilePosHash> heated;
    for (const auto& [pos, _] : field.fireTileExpiry)
        for (const auto& d : kDirs4)
            heated.insert(pos + d);
    for (EntityID eid : field.entities) {
        const Entity* e = registry.get(eid);
        if (!e || e->type != EntityType::Campfire) continue;
        for (const auto& d : kDirs4)
            heated.insert(e->pos + d);
    }

    // 4. Decay (remove) exposure for tiles/entities no longer in heat.
    {
        std::vector<TilePos> cold;
        for (const auto& [pos, _] : field.tileFireExp)
            if (!heated.count(pos)) cold.push_back(pos);
        for (const TilePos& pos : cold) field.tileFireExp.erase(pos);
    }
    {
        std::vector<EntityID> cold;
        for (const auto& [eid, _] : field.entityFireExp) {
            const Entity* e = registry.get(eid);
            if (!e) { cold.push_back(eid); continue; }
            bool anyHeated = heated.count(e->pos) > 0;
            for (int i = 1; i < e->tileCount && !anyHeated; ++i)
                anyHeated = heated.count(e->extraTiles[i - 1]) > 0;
            if (!anyHeated) cold.push_back(eid);
        }
        for (EntityID eid : cold) field.entityFireExp.erase(eid);
    }

    // 5. Heat LongGrass entities: accumulate exposure; ignite at 50 ticks.
    //    Fire only spreads through LongGrass — bare Grass tiles are fireproof.
    {
        std::vector<EntityID> toIgnite;
        for (EntityID eid : field.entities) {
            const Entity* e = registry.get(eid);
            if (!e || e->type != EntityType::LongGrass) continue;
            if (!heated.count(e->pos)) continue;
            if (++field.tileFireExp[e->pos] >= 50)
                toIgnite.push_back(eid);
        }
        for (EntityID eid : toIgnite) {
            Entity* e = registry.get(eid);
            if (!e) continue;
            TilePos pos = e->pos;
            field.remove(eid, *e);
            registry.destroy(eid);
            EntityID feid = registry.spawn(EntityType::Fire, pos);
            field.add(feid, *registry.get(feid));
            field.fireTileExpiry[pos] = currentTick + 150;
            field.tileFireExp.erase(pos);
        }
    }

    // 6. Heat TreeStump/Log entities: accumulate exposure; start burning at 250 ticks.
    for (EntityID eid : field.entities) {
        if (field.entityBurnEnd.count(eid)) continue;   // already burning
        Entity* e = registry.get(eid);
        if (!e) continue;
        if (e->type != EntityType::TreeStump && e->type != EntityType::Log) continue;
        // Check all occupied tiles for heat (multi-tile support).
        bool anyHeated = heated.count(e->pos) > 0;
        for (int i = 1; i < e->tileCount && !anyHeated; ++i)
            anyHeated = heated.count(e->extraTiles[i - 1]) > 0;
        if (!anyHeated) continue;
        if (++field.entityFireExp[eid] >= 250) {
            field.entityBurnEnd[eid] = currentTick + 500 * e->mass;
            field.entityFireExp.erase(eid);
            e->burning = true;
        }
    }
}

// ─── Voltage simulation ───────────────────────────────────────────────────────
//
// Battery entities emit 5V. BFS propagates through adjacent Puddle tiles,
// decrementing by 1 per hop. Lightbulb entities lit if their tile has ≥1V.

void tickVoltage(Field& field, EntityRegistry& registry) {
    field.voltage.clear();

    static const TilePos kDirs4[] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0}};

    std::queue<std::pair<TilePos, int>> q;

    // Helper: is there a Puddle entity at pos?
    auto hasPuddle = [&](TilePos pos) -> bool {
        for (EntityID peid : field.spatial.at(pos)) {
            const Entity* pe = registry.get(peid);
            if (pe && pe->type == EntityType::Puddle) return true;
        }
        return false;
    };

    // Seed from Battery entities: adjacent puddle tiles get 4V (5-1).
    for (EntityID eid : field.entities) {
        const Entity* e = registry.get(eid);
        if (!e || e->type != EntityType::Battery) continue;
        for (const auto& d : kDirs4) {
            TilePos adj = e->pos + d;
            if (!hasPuddle(adj)) continue;
            if (!field.voltage.count(adj) || field.voltage[adj] < 4) {
                field.voltage[adj] = 4;
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
            if (!hasPuddle(adj)) continue;
            if (!field.voltage.count(adj) || field.voltage[adj] < v - 1) {
                field.voltage[adj] = v - 1;
                q.push({adj, v - 1});
            }
        }
    }

    // Update Lightbulb lit state and electrified flag for all other entities.
    for (EntityID eid : field.entities) {
        Entity* e = registry.get(eid);
        if (!e) continue;
        auto it = field.voltage.find(e->pos);
        bool charged = (it != field.voltage.end() && it->second >= 1);
        if (e->type == EntityType::Lightbulb)
            e->lit = charged;
        else
            e->electrified = charged;
    }
}

// ─── LongGrass propagation ────────────────────────────────────────────────────
//
// Each tick, every LongGrass entity has a 1-in-1200 chance (~24 s on average)
// to spread to one adjacent Grass or BareEarth tile in the Grassland biome.
// Rabbit grazing is handled in tickRabbitAI (game.cpp / movement.cpp).

void tickLongGrass(Field& field, EntityRegistry& registry, std::mt19937& rng) {
    static const TilePos kDirs4[] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0}};
    std::uniform_int_distribution<int> rollSpread(0, 1199);

    // Snapshot entity list so we don't iterate while spawning.
    std::vector<EntityID> snapshot(field.entities.begin(), field.entities.end());

    // ── Spread ────────────────────────────────────────────────────────────────
    std::unordered_set<TilePos, TilePosHash> occupied;
    for (EntityID eid : snapshot) {
        const Entity* e = registry.get(eid);
        if (e && e->type == EntityType::LongGrass) occupied.insert(e->pos);
    }

    std::vector<TilePos> toSpawn;
    for (EntityID eid : snapshot) {
        const Entity* e = registry.get(eid);
        if (!e || e->type != EntityType::LongGrass) continue;
        if (rollSpread(rng) != 0) continue;

        int dirs[4] = {0, 1, 2, 3};
        std::shuffle(std::begin(dirs), std::end(dirs), rng);
        for (int di : dirs) {
            TilePos adj = e->pos + kDirs4[di];
            adj.z = field.terrain.levelAt(adj);
            if (field.terrain.biomeAt(adj) != Biome::Grassland) continue;
            if (occupied.count(adj)) continue;
            // Tile must be empty or have only a BareEarth entity (no Fire/Puddle/Straw/Portal etc.)
            bool blocked = false;
            for (EntityID bid : field.spatial.at(adj)) {
                const Entity* be = registry.get(bid);
                if (be && be->type != EntityType::BareEarth) { blocked = true; break; }
            }
            if (blocked) continue;
            occupied.insert(adj);
            toSpawn.push_back(adj);
            break;
        }
    }

    for (const TilePos& pos : toSpawn) {
        EntityID newEid = registry.spawn(EntityType::LongGrass, pos);
        field.add(newEid, *registry.get(newEid));
    }
}
