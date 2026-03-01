#pragma once

#include "types.hpp"
#include "terrain.hpp"
#include "spatial.hpp"
#include "scheduler.hpp"
#include "events.hpp"
#include "entity.hpp"
#include <vector>
#include <algorithm>

// ─── Grid ────────────────────────────────────────────────────────────────────
//
// One independent simulation space. Owns the per-world systems (terrain,
// spatial index, scheduler, event bus) and tracks which entities live here.
// The EntityRegistry is shared across all grids and lives in Game.

class Grid {
public:
    explicit Grid(GridID gid) : id(gid) {}

    GridID      id;
    Terrain     terrain;
    SpatialGrid spatial;
    Scheduler   scheduler;
    EventBus    events;

    std::vector<EntityID> entities;

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
