#include "doctest.h"

#include "spatial.hpp"
#include "entity.hpp"

// ─── z-level collision isolation ─────────────────────────────────────────────

TEST_CASE("entities at same (x,y) but different z do not collide") {
    EntityRegistry registry;
    SpatialGrid    spatial;

    // Mover at z=0 heading to (1,0,1) — different z from blocker at (1,0,0).
    EntityID mover   = registry.spawn(EntityType::Goblin, {0, 0, 0});
    EntityID blocker = registry.spawn(EntityType::Goblin, {1, 0, 0});
    Entity*  em = registry.get(mover);
    Entity*  eb = registry.get(blocker);

    spatial.add(mover,   em->pos, em->size);
    spatial.add(blocker, eb->pos, eb->size);

    // Mover targets (1,0,1) — z=1 plane, where blocker is NOT registered.
    std::vector<MoveIntention> intentions = {
        { mover, {0,0,0}, {1,0,1}, em->type, em->size },
    };

    auto allowed = resolveMoves(intentions, spatial, registry);
    CHECK(allowed.count(mover));   // not blocked — different z levels
}

TEST_CASE("entities at same (x,y,z) still collide") {
    EntityRegistry registry;
    SpatialGrid    spatial;

    EntityID mover   = registry.spawn(EntityType::Goblin, {0, 0, 0});
    EntityID blocker = registry.spawn(EntityType::Goblin, {1, 0, 0});
    Entity*  em = registry.get(mover);
    Entity*  eb = registry.get(blocker);

    spatial.add(mover,   em->pos, em->size);
    spatial.add(blocker, eb->pos, eb->size);

    std::vector<MoveIntention> intentions = {
        { mover, {0,0,0}, {1,0,0}, em->type, em->size },
    };

    auto allowed = resolveMoves(intentions, spatial, registry);
    CHECK(!allowed.count(mover));  // blocked — same z level
}
