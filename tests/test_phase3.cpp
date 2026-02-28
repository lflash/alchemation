#include "doctest.h"
#include "spatial.hpp"
#include <algorithm>

static bool contains(const std::vector<EntityID>& v, EntityID id) {
    return std::find(v.begin(), v.end(), id) != v.end();
}

// ─── SpatialGrid::at() ───────────────────────────────────────────────────────

TEST_CASE("at(): single-tile entity appears in exactly one cell") {
    SpatialGrid spatial;
    EntityID id = 1;
    spatial.add(id, {2, 3}, {0.8f, 0.8f});

    CHECK( contains(spatial.at({2, 3}), id));
    CHECK(!contains(spatial.at({3, 3}), id));
    CHECK(!contains(spatial.at({2, 4}), id));
    CHECK(!contains(spatial.at({1, 3}), id));
}

TEST_CASE("at(): entity does not appear after remove") {
    SpatialGrid spatial;
    EntityID id = 1;
    spatial.add(id, {0, 0}, {0.8f, 0.8f});
    spatial.remove(id, {0, 0}, {0.8f, 0.8f});
    CHECK(!contains(spatial.at({0, 0}), id));
}

TEST_CASE("at(): multiple entities in the same cell") {
    SpatialGrid spatial;
    spatial.add(1, {0, 0}, {0.8f, 0.8f});
    spatial.add(2, {0, 0}, {0.8f, 0.8f});
    CHECK(contains(spatial.at({0, 0}), 1));
    CHECK(contains(spatial.at({0, 0}), 2));
}

// ─── Multi-tile entity ───────────────────────────────────────────────────────

TEST_CASE("multi-tile entity (2x1) registers in exactly 2 cells") {
    SpatialGrid spatial;
    EntityID id = 1;
    spatial.add(id, {0, 0}, {2.0f, 1.0f});

    CHECK( contains(spatial.at({0, 0}), id));
    CHECK( contains(spatial.at({1, 0}), id));
    CHECK(!contains(spatial.at({2, 0}), id));
    CHECK(!contains(spatial.at({0, 1}), id));
}

TEST_CASE("multi-tile entity moving east updates only delta cells") {
    SpatialGrid spatial;
    EntityID id = 1;
    Vec2f    size = {2.0f, 1.0f};
    TilePos  pos  = {0, 0};
    TilePos  dest = {1, 0};

    spatial.add(id, pos, size);    // cells: (0,0), (1,0)
    spatial.add(id, dest, size);   // dual-register: adds (2,0); (1,0) already present
    spatial.move(id, pos, dest, size);  // arrival: removes (0,0), (1,0) and (2,0) are kept

    CHECK(!contains(spatial.at({0, 0}), id));  // pos-only → removed
    CHECK( contains(spatial.at({1, 0}), id));  // in both  → kept
    CHECK( contains(spatial.at({2, 0}), id));  // dest-only → kept
}

// ─── Dual registration ───────────────────────────────────────────────────────

TEST_CASE("dual registration: entity in both pos and dest during movement") {
    SpatialGrid spatial;
    EntityID id   = 1;
    Vec2f    size = {0.8f, 0.8f};
    TilePos  pos  = {0, 0};
    TilePos  dest = {1, 0};

    spatial.add(id, pos,  size);  // spawn
    spatial.add(id, dest, size);  // movement starts

    CHECK(contains(spatial.at(pos),  id));
    CHECK(contains(spatial.at(dest), id));
}

TEST_CASE("dual registration: only destination after arrival") {
    SpatialGrid spatial;
    EntityID id   = 1;
    Vec2f    size = {0.8f, 0.8f};
    TilePos  pos  = {0, 0};
    TilePos  dest = {1, 0};

    spatial.add(id, pos,  size);
    spatial.add(id, dest, size);
    spatial.move(id, pos, dest, size);  // arrival

    CHECK(!contains(spatial.at(pos),  id));
    CHECK( contains(spatial.at(dest), id));
}

// ─── AABB narrow phase ───────────────────────────────────────────────────────

TEST_CASE("AABB: overlapping boxes return true") {
    Bounds a = { {0.0f, 0.0f}, {1.0f, 1.0f} };
    Bounds b = { {0.5f, 0.5f}, {1.5f, 1.5f} };
    CHECK(overlaps(a, b));
    CHECK(overlaps(b, a));
}

TEST_CASE("AABB: adjacent boxes return false") {
    Bounds a = { {0.0f, 0.0f}, {1.0f, 1.0f} };
    Bounds b = { {1.0f, 0.0f}, {2.0f, 1.0f} };
    CHECK(!overlaps(a, b));
    CHECK(!overlaps(b, a));
}

TEST_CASE("AABB: partial overlap returns true") {
    Bounds a = { {0.0f, 0.0f}, {0.8f, 0.8f} };
    Bounds b = { {0.5f, 0.5f}, {1.3f, 1.3f} };
    CHECK(overlaps(a, b));
}

TEST_CASE("AABB: one box contained in another returns true") {
    Bounds outer = { {0.0f, 0.0f}, {2.0f, 2.0f} };
    Bounds inner = { {0.5f, 0.5f}, {1.5f, 1.5f} };
    CHECK(overlaps(outer, inner));
    CHECK(overlaps(inner, outer));
}

TEST_CASE("AABB: separated boxes return false") {
    Bounds a = { {0.0f, 0.0f}, {0.8f, 0.8f} };
    Bounds b = { {2.0f, 2.0f}, {2.8f, 2.8f} };
    CHECK(!overlaps(a, b));
}

// ─── Collision resolution table ──────────────────────────────────────────────

TEST_CASE("resolveCollision: player + mushroom → Collect") {
    CHECK(resolveCollision(EntityType::Player, EntityType::Mushroom) == CollisionResult::Collect);
}

TEST_CASE("resolveCollision: player + goblin → Combat") {
    CHECK(resolveCollision(EntityType::Player, EntityType::Goblin) == CollisionResult::Combat);
}

TEST_CASE("resolveCollision: goblin + goblin → Block") {
    CHECK(resolveCollision(EntityType::Goblin, EntityType::Goblin) == CollisionResult::Block);
}

TEST_CASE("resolveCollision: poop + goblin → Hit") {
    CHECK(resolveCollision(EntityType::Poop, EntityType::Goblin) == CollisionResult::Hit);
}

TEST_CASE("resolveCollision: player + poop → Pass") {
    CHECK(resolveCollision(EntityType::Player, EntityType::Poop) == CollisionResult::Pass);
}

TEST_CASE("resolveCollision: goblin + mushroom → Pass") {
    CHECK(resolveCollision(EntityType::Goblin, EntityType::Mushroom) == CollisionResult::Pass);
}

// ─── Swap detection ──────────────────────────────────────────────────────────

TEST_CASE("swap detection: both entities blocked when swapping tiles") {
    EntityRegistry registry;
    SpatialGrid    spatial;

    EntityID a = registry.spawn(EntityType::Player, {0, 0});
    EntityID b = registry.spawn(EntityType::Goblin, {1, 0});
    Entity* ea  = registry.get(a);
    Entity* eb  = registry.get(b);

    spatial.add(a, ea->pos, ea->size);
    spatial.add(b, eb->pos, eb->size);

    std::vector<MoveIntention> intentions = {
        { a, {0,0}, {1,0}, ea->type, ea->size },
        { b, {1,0}, {0,0}, eb->type, eb->size },
    };

    auto allowed = resolveMoves(intentions, spatial, registry);

    CHECK(!allowed.count(a));
    CHECK(!allowed.count(b));
}

TEST_CASE("resolveMoves: unobstructed move is allowed") {
    EntityRegistry registry;
    SpatialGrid    spatial;

    EntityID a = registry.spawn(EntityType::Player, {0, 0});
    Entity*  ea = registry.get(a);
    spatial.add(a, ea->pos, ea->size);

    std::vector<MoveIntention> intentions = {
        { a, {0,0}, {1,0}, ea->type, ea->size },
    };

    auto allowed = resolveMoves(intentions, spatial, registry);
    CHECK(allowed.count(a));
}

TEST_CASE("resolveMoves: goblin blocked by goblin at destination") {
    EntityRegistry registry;
    SpatialGrid    spatial;

    EntityID mover   = registry.spawn(EntityType::Goblin, {0, 0});
    EntityID blocker = registry.spawn(EntityType::Goblin, {1, 0});
    Entity*  em = registry.get(mover);
    Entity*  eb = registry.get(blocker);

    spatial.add(mover,   em->pos, em->size);
    spatial.add(blocker, eb->pos, eb->size);

    std::vector<MoveIntention> intentions = {
        { mover, {0,0}, {1,0}, em->type, em->size },
    };

    auto allowed = resolveMoves(intentions, spatial, registry);
    CHECK(!allowed.count(mover));
}

TEST_CASE("resolveMoves: player not blocked by mushroom") {
    EntityRegistry registry;
    SpatialGrid    spatial;

    EntityID player   = registry.spawn(EntityType::Player,   {0, 0});
    EntityID mushroom = registry.spawn(EntityType::Mushroom, {1, 0});
    Entity*  ep = registry.get(player);
    Entity*  em = registry.get(mushroom);

    spatial.add(player,   ep->pos, ep->size);
    spatial.add(mushroom, em->pos, em->size);

    std::vector<MoveIntention> intentions = {
        { player, {0,0}, {1,0}, ep->type, ep->size },
    };

    auto allowed = resolveMoves(intentions, spatial, registry);
    CHECK(allowed.count(player));
}
