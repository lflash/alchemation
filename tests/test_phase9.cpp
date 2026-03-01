#include "doctest.h"

#include "terrain.hpp"
#include "spatial.hpp"
#include "entity.hpp"
#include <algorithm>

// ─── resolveZ ────────────────────────────────────────────────────────────────

TEST_CASE("resolveZ: flat tile -> z unchanged") {
    Terrain t;
    // Default terrain is all Flat; moving N from (0,1,0) to (0,0,0) stays at z=0.
    auto dest = resolveZ({0, 1, 0}, {0, 0, 0}, t);
    REQUIRE(dest.has_value());
    CHECK(dest->z == 0);
    CHECK(dest->x == 0);
    CHECK(dest->y == 0);
}

TEST_CASE("resolveZ: ascending slope in movement direction -> z+1") {
    Terrain t;
    // SlopeN at (0,0,0): walking North ascends to z=1.
    t.setShape({0, 0, 0}, TileShape::SlopeN);
    auto dest = resolveZ({0, 1, 0}, {0, 0, 0}, t);  // moving N
    REQUIRE(dest.has_value());
    CHECK(dest->z == 1);
}

TEST_CASE("resolveZ: descending slope at z-1 -> z-1") {
    Terrain t;
    // Entity at z=1. SlopeN at (0,1,0): ascending direction is N = opposite of S.
    // Walking South from z=1 to (0,1) finds SlopeN at z=0 -> descend to z=0.
    t.setShape({0, 1, 0}, TileShape::SlopeN);
    auto dest = resolveZ({0, 0, 1}, {0, 1, 1}, t);  // moving S from z=1
    REQUIRE(dest.has_value());
    CHECK(dest->z == 0);
}

TEST_CASE("resolveZ: perpendicular slope -> blocked (nullopt)") {
    Terrain t;
    // SlopeN at destination: ascends North. Moving East is perpendicular -> blocked.
    t.setShape({1, 0, 0}, TileShape::SlopeN);
    auto dest = resolveZ({0, 0, 0}, {1, 0, 0}, t);  // moving E
    CHECK(!dest.has_value());
}

TEST_CASE("resolveZ: back face of slope -> blocked (nullopt)") {
    Terrain t;
    // SlopeN ascends North. Moving South into it hits the back face -> blocked.
    t.setShape({0, 0, 0}, TileShape::SlopeN);
    auto dest = resolveZ({0, -1, 0}, {0, 0, 0}, t);  // moving S
    CHECK(!dest.has_value());
}

TEST_CASE("resolveZ: diagonal move ignores slope (no z change, no block)") {
    Terrain t;
    // SlopeN at destination; diagonal NE move should pass through unaffected.
    t.setShape({1, 0, 0}, TileShape::SlopeN);
    auto dest = resolveZ({0, 1, 0}, {1, 0, 0}, t);  // moving NE (diagonal)
    REQUIRE(dest.has_value());
    CHECK(dest->z == 0);
}

TEST_CASE("resolveZ: SlopeE ascending East -> z+1") {
    Terrain t;
    t.setShape({1, 0, 0}, TileShape::SlopeE);
    auto dest = resolveZ({0, 0, 0}, {1, 0, 0}, t);  // moving E
    REQUIRE(dest.has_value());
    CHECK(dest->z == 1);
}

TEST_CASE("resolveZ: SlopeS ascending South -> z+1") {
    Terrain t;
    t.setShape({0, 1, 0}, TileShape::SlopeS);
    auto dest = resolveZ({0, 0, 0}, {0, 1, 0}, t);  // moving S
    REQUIRE(dest.has_value());
    CHECK(dest->z == 1);
}

TEST_CASE("resolveZ: SlopeW ascending West -> z+1") {
    Terrain t;
    t.setShape({-1, 0, 0}, TileShape::SlopeW);
    auto dest = resolveZ({0, 0, 0}, {-1, 0, 0}, t);  // moving W
    REQUIRE(dest.has_value());
    CHECK(dest->z == 1);
}

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
