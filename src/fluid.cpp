#include "fluid.hpp"
#include "grid.hpp"
#include "entity.hpp"
#include <algorithm>
#include <unordered_map>
#include <vector>

// ─── Shallow water simulation ──────────────────────────────────────────────────
//
// Simplified shallow water equations on a discrete tile grid.
// Per water entity (one per wet tile):
//
//   1. Gravity step:  accelerate (vx, vy) toward lower surface height
//                     surface(p) = h(p) + terrain.levelAt(p)
//   2. Advect step:   transfer mass in velocity direction; clamp to avoid over-draining
//
// Volume is approximately conserved (small loss at despawn threshold).
// Tiles blocked by Portal or Fire do not receive flow.

void tickFluid(Grid& grid, ComponentStore<FluidComponent>& fluids,
               EntityRegistry& registry) {
    static const TilePos kDirs4[] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0}};
    constexpr float G      = 0.4f;    // gravity acceleration per tick
    constexpr float DAMP   = 0.97f;   // velocity damping per tick
    constexpr float H_MIN  = 0.02f;   // below this → despawn
    constexpr float MAX_FLUX = 0.25f; // max fraction of h transferred per tick per direction

    // ── Build tile → EntityID map for fast neighbour lookup ───────────────────
    std::unordered_map<TilePos, EntityID, TilePosHash> waterMap;
    for (EntityID eid : grid.entities) {
        const Entity* e = registry.get(eid);
        if (e && e->type == EntityType::Water && fluids.has(eid))
            waterMap[e->pos] = eid;
    }

    // ── Snapshot velocities so gravity step reads consistent state ─────────────
    struct VSnap { float vx, vy; };
    std::unordered_map<EntityID, VSnap> vsnap;
    for (auto& [pos, eid] : waterMap) {
        FluidComponent* fc = fluids.get(eid);
        vsnap[eid] = { fc->vx, fc->vy };
    }

    // ── Helper: surface height at a tile (h + terrain level) ──────────────────
    auto surfaceAt = [&](TilePos p) -> float {
        auto it = waterMap.find(p);
        float h = (it != waterMap.end()) ? fluids.get(it->second)->h : 0.f;
        return h + static_cast<float>(grid.terrain.levelAt(p));
    };

    auto canFlow = [&](TilePos from, TilePos to) -> bool {
        TileType toType = grid.terrain.typeAt(to);
        if (toType == TileType::Portal || toType == TileType::Fire) return false;
        // Allow flow only if terrain height difference ≤ 1 (consistent with movement).
        return std::abs(grid.terrain.levelAt(to) - grid.terrain.levelAt(from)) <= 1;
    };

    // ── Gravity step: update velocity from surface gradient ───────────────────
    for (auto& [pos, eid] : waterMap) {
        FluidComponent* fc = fluids.get(eid);
        float dvx = G * (surfaceAt({pos.x-1,pos.y,pos.z}) - surfaceAt({pos.x+1,pos.y,pos.z})) * 0.5f;
        float dvy = G * (surfaceAt({pos.x,pos.y-1,pos.z}) - surfaceAt({pos.x,pos.y+1,pos.z})) * 0.5f;
        fc->vx = (fc->vx + dvx) * DAMP;
        fc->vy = (fc->vy + dvy) * DAMP;
    }

    // ── Advect step: transfer mass based on velocity ───────────────────────────
    std::unordered_map<TilePos, float, TilePosHash> newH;  // newly wet tiles
    std::unordered_map<EntityID, float>             dh;    // deltas for existing cells

    for (auto& [pos, eid] : waterMap) {
        FluidComponent* fc = fluids.get(eid);
        float h = fc->h;

        // x-direction
        if (std::abs(fc->vx) > 0.001f) {
            TilePos to = {pos.x + (fc->vx > 0 ? 1 : -1), pos.y, pos.z};
            if (canFlow(pos, to)) {
                float flux = std::min(std::abs(fc->vx) * h, h * MAX_FLUX);
                if (flux > H_MIN) {
                    dh[eid] -= flux;
                    auto it = waterMap.find(to);
                    if (it != waterMap.end()) dh[it->second] += flux;
                    else                      newH[to] += flux;
                }
            }
        }

        // y-direction
        if (std::abs(fc->vy) > 0.001f) {
            TilePos to = {pos.x, pos.y + (fc->vy > 0 ? 1 : -1), pos.z};
            if (canFlow(pos, to)) {
                float flux = std::min(std::abs(fc->vy) * h, h * MAX_FLUX);
                if (flux > H_MIN) {
                    dh[eid] -= flux;
                    auto it = waterMap.find(to);
                    if (it != waterMap.end()) dh[it->second] += flux;
                    else                      newH[to] += flux;
                }
            }
        }
    }

    // ── Apply h deltas; collect despawns ──────────────────────────────────────
    std::vector<EntityID> toDespawn;
    for (auto& [eid, delta] : dh) {
        FluidComponent* fc = fluids.get(eid);
        if (!fc) continue;
        fc->h += delta;
        if (fc->h < H_MIN) toDespawn.push_back(eid);
    }
    for (EntityID eid : toDespawn) {
        Entity* e = registry.get(eid);
        if (e) grid.remove(eid, *e);
        fluids.remove(eid);
        registry.destroy(eid);
    }

    // ── Spawn Water entities for newly wet tiles ──────────────────────────────
    for (auto& [pos, h] : newH) {
        if (h < H_MIN) continue;
        // z from terrain so the water entity sits at the right height level.
        TilePos spawnPos = {pos.x, pos.y, grid.terrain.levelAt(pos)};
        // Skip if already occupied by a water entity (shouldn't happen but guard it).
        if (waterMap.count(spawnPos)) continue;
        EntityID newEid = registry.spawn(EntityType::Water, spawnPos);
        Entity*  e      = registry.get(newEid);
        if (e) {
            grid.add(newEid, *e);
            fluids.add(newEid, {h, 0.f, 0.f});
        }
    }
}
