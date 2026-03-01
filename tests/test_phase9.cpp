#include "doctest.h"

#include "terrain.hpp"
#include "spatial.hpp"
#include "entity.hpp"
#include <algorithm>
#include <initializer_list>
#include <vector>

// ─── walkPath helper ─────────────────────────────────────────────────────────
//
// Simulates entity movement through terrain using resolveZ only (no collision).
// Returns each position after applying the corresponding move; the initial
// position is element [0], after move[0] is element [1], etc.

static TilePos step(Direction d) {
    switch (d) {
        case Direction::N: return {0,-1,0};
        case Direction::S: return {0, 1,0};
        case Direction::E: return {1, 0,0};
        case Direction::W: return {-1,0,0};
        default:           return {0, 0,0};
    }
}

static std::vector<TilePos> walkPath(
    const Terrain& terrain, TilePos start,
    std::initializer_list<Direction> moves)
{
    std::vector<TilePos> path = {start};
    TilePos pos = start;
    for (Direction d : moves) {
        TilePos tentative = pos + step(d);
        auto result = resolveZ(pos, tentative, terrain);
        if (result) pos = *result;
        path.push_back(pos);
    }
    return path;
}

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

TEST_CASE("resolveZ: perpendicular slope -> pass through at z unchanged") {
    Terrain t;
    // SlopeN at destination: ascends North. Moving East is perpendicular ->
    // entity enters the tile but does not ascend (z unchanged).
    t.setShape({1, 0, 0}, TileShape::SlopeN);
    auto dest = resolveZ({0, 0, 0}, {1, 0, 0}, t);  // moving E
    REQUIRE(dest.has_value());
    CHECK(dest->z == 0);
    CHECK(dest->x == 1);
}

TEST_CASE("resolveZ: back face of slope -> pass through at z unchanged") {
    Terrain t;
    // SlopeN ascends North. Moving South into it from the high side ->
    // entity enters the tile but does not descend (no ramp below at z-1).
    t.setShape({0, 0, 0}, TileShape::SlopeN);
    auto dest = resolveZ({0, -1, 0}, {0, 0, 0}, t);  // moving S
    REQUIRE(dest.has_value());
    CHECK(dest->z == 0);
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

// ─── walkPath scenarios ───────────────────────────────────────────────────────

TEST_CASE("walk: ascend SlopeN heading north") {
    // Layout: flat ground at y=2, SlopeN at y=1, elevated flat at y=0.
    // Ascending the slope should jump from z=0 to z=1.
    Terrain t;
    t.setShape({0,1,0}, TileShape::SlopeN);
    auto p = walkPath(t, {0,2,0}, {Direction::N, Direction::N});
    // after 1st N: on slope tile, z=1
    CHECK(p[1] == TilePos{0,1,1});
    // after 2nd N: flat tile above slope, still z=1
    CHECK(p[2] == TilePos{0,0,1});
}

TEST_CASE("walk: descend via SlopeN (approach from north at z=1)") {
    // Entity on elevated tile walks south toward SlopeN, should descend to z=0.
    Terrain t;
    t.setShape({0,1,0}, TileShape::SlopeN);
    // Start at elevated tile, walk south twice
    auto p = walkPath(t, {0,0,1}, {Direction::S, Direction::S});
    // after 1st S: SlopeN at (0,1), from z=1 heading S → a_zm1=SlopeN(=N), oppositeDir(N,S)=true → z=0
    CHECK(p[1] == TilePos{0,1,0});
    // after 2nd S: flat ground
    CHECK(p[2] == TilePos{0,2,0});
}

TEST_CASE("walk: east through a row of SlopeN tiles stays at z=0") {
    // Walking east along the foot of a north cliff — should pass through freely.
    Terrain t;
    t.setShape({0,1,0}, TileShape::SlopeN);
    t.setShape({1,1,0}, TileShape::SlopeN);
    t.setShape({2,1,0}, TileShape::SlopeN);
    auto p = walkPath(t, {0,1,0}, {Direction::E, Direction::E});
    CHECK(p[1] == TilePos{1,1,0});
    CHECK(p[2] == TilePos{2,1,0});
}

TEST_CASE("walk: east along elevated plateau at z=1") {
    // Once on top, walking east across flat elevated tiles should stay at z=1.
    Terrain t;
    t.setShape({0,1,0}, TileShape::SlopeN);  // ramp to get up
    // Elevated tiles above are plain Flat at z=0 (their elevation is visual only)
    auto p = walkPath(t, {0,0,1}, {Direction::E, Direction::E});
    CHECK(p[1] == TilePos{1,0,1});
    CHECK(p[2] == TilePos{2,0,1});
}

TEST_CASE("walk: corner slope SlopeNE passable from east") {
    Terrain t;
    t.setShape({0,0,0}, TileShape::SlopeNE);
    auto p = walkPath(t, {1,0,0}, {Direction::W});
    CHECK(p[1] == TilePos{0,0,0});  // entered at z=0, no block
}

TEST_CASE("walk: corner slope SlopeNE passable from south") {
    Terrain t;
    t.setShape({0,0,0}, TileShape::SlopeNE);
    auto p = walkPath(t, {0,1,0}, {Direction::N});
    CHECK(p[1] == TilePos{0,0,0});
}

TEST_CASE("walk: mixed cliff face east — cardinal then corner slope") {
    // Simulates the inconsistency: a cliff base row with alternating slope types.
    // Walking east through SlopeN then SlopeNE should both pass at z=0.
    Terrain t;
    t.setShape({0,1,0}, TileShape::SlopeN);   // cardinal — perpendicular block?
    t.setShape({1,1,0}, TileShape::SlopeNE);  // corner   — always passable
    t.setShape({2,1,0}, TileShape::SlopeN);   // cardinal again
    auto p = walkPath(t, {0,1,0}, {Direction::E, Direction::E, Direction::E});
    CHECK(p[1] == TilePos{1,1,0});
    CHECK(p[2] == TilePos{2,1,0});
    CHECK(p[3] == TilePos{3,1,0});
}

TEST_CASE("walk: full traverse — up slope, along top, down other side") {
    // SlopeN at y=1 (ascend going N), SlopeS at y=-1 (descend going N).
    Terrain t;
    t.setShape({ 0, 1, 0}, TileShape::SlopeN);
    t.setShape({ 0,-1, 0}, TileShape::SlopeS);
    auto p = walkPath(t, {0,2,0}, {Direction::N, Direction::N, Direction::N, Direction::N});
    CHECK(p[1] == TilePos{ 0, 1, 1});  // ascended
    CHECK(p[2] == TilePos{ 0, 0, 1});  // plateau
    CHECK(p[3] == TilePos{ 0,-1, 0});  // descended via SlopeS
    CHECK(p[4] == TilePos{ 0,-2, 0});  // ground on far side
}
