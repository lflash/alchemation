#pragma once

#include "types.hpp"
#include "terrain.hpp"
#include "spatial.hpp"
#include "scheduler.hpp"
#include "events.hpp"
#include "entity.hpp"
#include <vector>
#include <algorithm>
#include <unordered_map>

// ─── Portal ───────────────────────────────────────────────────────────────────
//
// Stored per-tile in Grid::portals. When an entity arrives on the tile,
// it is transferred to targetGrid at targetPos.

struct Portal {
    GridID  targetGrid;
    TilePos targetPos;
};

// ─── Grid ────────────────────────────────────────────────────────────────────
//
// One independent simulation space. Owns the per-world systems (terrain,
// spatial index, scheduler, event bus) and tracks which entities live here.
// The EntityRegistry is shared across all grids and lives in Game.
//
// width/height == 0 means unbounded (infinite). Otherwise the grid is
// treated as an NxM room; tiles outside [0,width)×[0,height) are void.

class Grid {
public:
    explicit Grid(GridID gid, int w = 0, int h = 0)
        : id(gid), width(w), height(h) {}

    GridID id;
    int    width  = 0;   // 0 = unbounded
    int    height = 0;
    bool   paused = false;

    std::unordered_map<TilePos, Portal, TilePosHash> portals;

    Terrain     terrain;
    SpatialGrid spatial;
    Scheduler   scheduler;
    EventBus    events;

    std::vector<EntityID> entities;

    // ── Fire simulation ───────────────────────────────────────────────────────
    // tileFireExp:    ticks a Grass tile has been adjacent to fire (→ catches at 50)
    // fireTileExpiry: tick at which each active Fire tile turns to BareEarth
    // entityFireExp:  ticks a TreeStump/Log has been adjacent to fire (→ burns at 250)
    // entityBurnEnd:  tick at which a burning entity is despawned (500 ticks after ignition)
    std::unordered_map<TilePos, int,  TilePosHash> tileFireExp;
    std::unordered_map<TilePos, Tick, TilePosHash> fireTileExpiry;
    std::unordered_map<EntityID, int>              entityFireExp;
    std::unordered_map<EntityID, Tick>             entityBurnEnd;

    // ── Voltage (computed fresh each tick by BFS from Battery entities) ───────
    std::unordered_map<TilePos, int, TilePosHash>  voltage;

    // ── Water levels (Phase 14) ───────────────────────────────────────────────
    // Per-tile depth of water (arbitrary units). Updated by tickWater().
    // Tiles with level ≤ 0.1 are converted to Puddle and removed from this map.
    // Total volume is conserved as water spreads.
    std::unordered_map<TilePos, float, TilePosHash> waterLevel;

    bool isBounded() const { return width > 0 && height > 0; }
    bool inBounds(TilePos p) const {
        return !isBounded() || (p.x >= 0 && p.x < width &&
                                p.y >= 0 && p.y < height);
    }

    // Register an entity in this grid: adds to entity list and spatial index.
    void add(EntityID eid, Entity& e) {
        entities.push_back(eid);
        spatial.add(eid, e.pos, e.size);
    }

    // Unregister an entity: removes from entity list and spatial index.
    // Also cleans up dual registration if the entity is mid-move.
    void remove(EntityID eid, Entity& e) {
        spatial.remove(eid, e.pos, e.size);
        if (e.isMoving())
            spatial.remove(eid, e.destination, e.size);
        entities.erase(std::remove(entities.begin(), entities.end(), eid),
                       entities.end());
    }

    bool hasEntity(EntityID eid) const {
        return std::find(entities.begin(), entities.end(), eid) != entities.end();
    }
};
