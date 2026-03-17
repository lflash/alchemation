#include "fluid.hpp"
#include "field.hpp"
#include "entity.hpp"
#include <algorithm>
#include <unordered_map>
#include <vector>

// ─── Shallow water simulation ──────────────────────────────────────────────────
//
// Flux-based spreading: each tick, water flows to adjacent tiles with lower
// surface height (h + terrain level).
//
// Flux per direction = min(surface_diff * RATE, h * MAX_FLUX_DIR).
// A tile can only overflow onto a new (dry) neighbour when its depth h is
// above POOL_DEPTH.  Below that threshold the water equalises with existing
// wet neighbours and stays put — forming a stable puddle or shore.
//
// Tiles blocked by Portal or Fire do not receive flow.

void tickFluid(Field& grid, ComponentStore<FluidComponent>& fluids,
               EntityRegistry& registry) {
    static const TilePos kDirs4[] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0}};
    constexpr float H_MIN        = 0.02f;  // below this → despawn
    constexpr float RATE         = 0.25f;  // fraction of surface diff transferred
    constexpr float MAX_FLUX_DIR = 0.10f;  // max fraction of h per direction per tick
    constexpr float POOL_DEPTH   = 0.30f;  // source must be deeper than this to wet new tiles

    // ── Build tile → EntityID map for fast neighbour lookup ───────────────────
    std::unordered_map<TilePos, EntityID, TilePosHash> waterMap;
    for (EntityID eid : grid.entities) {
        const Entity* e = registry.get(eid);
        if (e && e->type == EntityType::Water && fluids.has(eid))
            waterMap[e->pos] = eid;
    }

    // ── Helper: surface height at a tile (h + terrain level) ──────────────────
    auto surfaceAt = [&](TilePos p) -> float {
        auto it = waterMap.find(p);
        float h = (it != waterMap.end()) ? fluids.get(it->second)->h : 0.f;
        return h + static_cast<float>(grid.terrain.levelAt(p));
    };

    auto canFlow = [&](TilePos from, TilePos to) -> bool {
        for (EntityID peid : grid.spatial.at(to)) {
            const Entity* pe = registry.get(peid);
            if (pe && (pe->type == EntityType::Portal || pe->type == EntityType::Fire))
                return false;
        }
        return std::abs(grid.terrain.levelAt(to) - grid.terrain.levelAt(from)) <= 1;
    };

    // ── Flux step: transfer mass to lower neighbours ───────────────────────────
    std::unordered_map<TilePos, float, TilePosHash> newH;
    std::unordered_map<EntityID, float>             dh;

    for (auto& [pos, eid] : waterMap) {
        FluidComponent* fc = fluids.get(eid);
        float h  = fc->h;
        float S  = surfaceAt(pos);

        float fluxes[4] = {};
        float totalFlux = 0.f;
        for (int d = 0; d < 4; ++d) {
            TilePos npos = pos + kDirs4[d];
            if (!canFlow(pos, npos)) continue;
            float diff = S - surfaceAt(npos);
            if (diff <= 0.f) continue;
            float flux = std::min(diff * RATE, h * MAX_FLUX_DIR);
            // Only overflow onto a new dry tile when source is deep enough.
            bool isWet = waterMap.count(npos) > 0;
            if (!isWet && h <= POOL_DEPTH) continue;
            fluxes[d]  = flux;
            totalFlux += flux;
        }

        if (totalFlux < 1e-5f) continue;

        // Scale down if total outflow would exceed available water.
        float scale = (totalFlux > h * 0.99f) ? (h * 0.99f / totalFlux) : 1.f;

        for (int d = 0; d < 4; ++d) {
            if (fluxes[d] <= 0.f) continue;
            float flux   = fluxes[d] * scale;
            TilePos npos = pos + kDirs4[d];
            dh[eid] -= flux;
            auto it = waterMap.find(npos);
            if (it != waterMap.end()) dh[it->second] += flux;
            else                      newH[npos]      += flux;
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
    // Also catch entities whose h was already below H_MIN with no outflow.
    for (auto& [pos, eid] : waterMap) {
        FluidComponent* fc = fluids.get(eid);
        if (fc && fc->h < H_MIN) toDespawn.push_back(eid);
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
        TilePos spawnPos = {pos.x, pos.y, grid.terrain.levelAt(pos)};
        if (waterMap.count(spawnPos)) continue;
        EntityID newEid = registry.spawn(EntityType::Water, spawnPos);
        Entity*  e      = registry.get(newEid);
        if (e) {
            grid.add(newEid, *e);
            fluids.add(newEid, {h, 0.f, 0.f});
        }
    }
}
