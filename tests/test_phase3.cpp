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

// ─── SpatialGrid::atAnyZ ─────────────────────────────────────────────────────

TEST_CASE("atAnyZ: finds entity at z=0") {
    SpatialGrid spatial;
    spatial.add(1, {3, 4, 0}, {0.8f, 0.8f});
    CHECK(contains(spatial.atAnyZ(3, 4), 1));
}

TEST_CASE("atAnyZ: finds entity at z=2") {
    SpatialGrid spatial;
    spatial.add(1, {3, 4, 2}, {0.8f, 0.8f});
    CHECK(contains(spatial.atAnyZ(3, 4), 1));
}

TEST_CASE("atAnyZ: returns entities across multiple z levels") {
    SpatialGrid spatial;
    spatial.add(1, {5, 5, 0}, {0.8f, 0.8f});
    spatial.add(2, {5, 5, 1}, {0.8f, 0.8f});
    spatial.add(3, {5, 5, 3}, {0.8f, 0.8f});
    auto result = spatial.atAnyZ(5, 5);
    CHECK(contains(result, 1));
    CHECK(contains(result, 2));
    CHECK(contains(result, 3));
}

TEST_CASE("atAnyZ: empty when no entity at (x,y)") {
    SpatialGrid spatial;
    spatial.add(1, {0, 0, 0}, {0.8f, 0.8f});
    CHECK(spatial.atAnyZ(9, 9).empty());
}

TEST_CASE("atAnyZ: does not return entities at different xy") {
    SpatialGrid spatial;
    spatial.add(1, {1, 0, 0}, {0.8f, 0.8f});
    spatial.add(2, {0, 1, 0}, {0.8f, 0.8f});
    auto result = spatial.atAnyZ(0, 0);
    CHECK(!contains(result, 1));
    CHECK(!contains(result, 2));
}

TEST_CASE("atAnyZ: no duplicates for multi-tile entity spanning two z-matching cells") {
    SpatialGrid spatial;
    // 2-wide entity at z=0: registers in (0,0,0) and (1,0,0)
    spatial.add(1, {0, 0, 0}, {2.0f, 1.0f});
    auto result = spatial.atAnyZ(0, 0);
    int count = static_cast<int>(std::count(result.begin(), result.end(), 1));
    CHECK(count == 1);
}

// ─── Collision resolution table ──────────────────────────────────────────────

TEST_CASE("resolveCollision: player + mushroom → Collect") {
    CHECK(resolveCollision(EntityType::Player, EntityType::Mushroom) == CollisionResult::Collect);
}

TEST_CASE("resolveCollision: player + goblin → Block (bump combat)") {
    CHECK(resolveCollision(EntityType::Player, EntityType::Goblin) == CollisionResult::Block);
}

TEST_CASE("resolveCollision: goblin + goblin → Block") {
    CHECK(resolveCollision(EntityType::Goblin, EntityType::Goblin) == CollisionResult::Block);
}

TEST_CASE("resolveCollision: mud golem + goblin → Hit") {
    CHECK(resolveCollision(EntityType::MudGolem, EntityType::Goblin) == CollisionResult::Hit);
}

TEST_CASE("resolveCollision: player + mud golem → Block") {
    CHECK(resolveCollision(EntityType::Player, EntityType::MudGolem) == CollisionResult::Block);
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

// ─── Extended collision table coverage ───────────────────────────────────────

// Player mover
TEST_CASE("resolveCollision: player + water → Pass") {
    CHECK(resolveCollision(EntityType::Player, EntityType::Water) == CollisionResult::Pass);
}
TEST_CASE("resolveCollision: player + warren → Pass (enter via portal)") {
    CHECK(resolveCollision(EntityType::Player, EntityType::Warren) == CollisionResult::Pass);
}
TEST_CASE("resolveCollision: player + log → Block (bump-push)") {
    CHECK(resolveCollision(EntityType::Player, EntityType::Log) == CollisionResult::Block);
}
TEST_CASE("resolveCollision: player + rabbit → Block") {
    CHECK(resolveCollision(EntityType::Player, EntityType::Rabbit) == CollisionResult::Block);
}
TEST_CASE("resolveCollision: player + chest → Collect") {
    CHECK(resolveCollision(EntityType::Player, EntityType::Chest) == CollisionResult::Collect);
}
TEST_CASE("resolveCollision: player + tree → Block (static)") {
    CHECK(resolveCollision(EntityType::Player, EntityType::Tree) == CollisionResult::Block);
}
TEST_CASE("resolveCollision: player + campfire → Block (static)") {
    CHECK(resolveCollision(EntityType::Player, EntityType::Campfire) == CollisionResult::Block);
}
TEST_CASE("resolveCollision: player + fire → Pass") {
    CHECK(resolveCollision(EntityType::Player, EntityType::Fire) == CollisionResult::Pass);
}
TEST_CASE("resolveCollision: player + bare earth → Pass") {
    CHECK(resolveCollision(EntityType::Player, EntityType::BareEarth) == CollisionResult::Pass);
}

// Goblin mover
TEST_CASE("resolveCollision: goblin + player → Combat") {
    CHECK(resolveCollision(EntityType::Goblin, EntityType::Player) == CollisionResult::Combat);
}
TEST_CASE("resolveCollision: goblin + tree → Block (static)") {
    CHECK(resolveCollision(EntityType::Goblin, EntityType::Tree) == CollisionResult::Block);
}
TEST_CASE("resolveCollision: goblin + mud golem → Block") {
    CHECK(resolveCollision(EntityType::Goblin, EntityType::MudGolem) == CollisionResult::Block);
}
TEST_CASE("resolveCollision: goblin + water → Pass") {
    CHECK(resolveCollision(EntityType::Goblin, EntityType::Water) == CollisionResult::Pass);
}

// Golem movers — fighting vs non-fighting
TEST_CASE("resolveCollision: iron golem + goblin → Hit") {
    CHECK(resolveCollision(EntityType::IronGolem, EntityType::Goblin) == CollisionResult::Hit);
}
TEST_CASE("resolveCollision: wood golem + goblin → Hit") {
    CHECK(resolveCollision(EntityType::WoodGolem, EntityType::Goblin) == CollisionResult::Hit);
}
TEST_CASE("resolveCollision: mud golem + goblin → Hit") {
    CHECK(resolveCollision(EntityType::MudGolem, EntityType::Goblin) == CollisionResult::Hit);
}
TEST_CASE("resolveCollision: stone golem + goblin → Block (non-fighting)") {
    CHECK(resolveCollision(EntityType::StoneGolem, EntityType::Goblin) == CollisionResult::Block);
}
TEST_CASE("resolveCollision: clay golem + goblin → Block (non-fighting)") {
    CHECK(resolveCollision(EntityType::ClayGolem, EntityType::Goblin) == CollisionResult::Block);
}
TEST_CASE("resolveCollision: mud golem + player → Block") {
    CHECK(resolveCollision(EntityType::MudGolem, EntityType::Player) == CollisionResult::Block);
}
TEST_CASE("resolveCollision: iron golem + player → Block") {
    CHECK(resolveCollision(EntityType::IronGolem, EntityType::Player) == CollisionResult::Block);
}
TEST_CASE("resolveCollision: mud golem + mushroom → Pass") {
    CHECK(resolveCollision(EntityType::MudGolem, EntityType::Mushroom) == CollisionResult::Pass);
}
TEST_CASE("resolveCollision: mud golem + log → Block") {
    CHECK(resolveCollision(EntityType::MudGolem, EntityType::Log) == CollisionResult::Block);
}
TEST_CASE("resolveCollision: mud golem + campfire → Block (static)") {
    CHECK(resolveCollision(EntityType::MudGolem, EntityType::Campfire) == CollisionResult::Block);
}
TEST_CASE("resolveCollision: mud golem + iron golem → Block (golem occupant)") {
    CHECK(resolveCollision(EntityType::MudGolem, EntityType::IronGolem) == CollisionResult::Block);
}
TEST_CASE("resolveCollision: mud golem + water → Pass") {
    CHECK(resolveCollision(EntityType::MudGolem, EntityType::Water) == CollisionResult::Pass);
}

// Passive / inert movers
TEST_CASE("resolveCollision: mushroom mover → always Pass") {
    CHECK(resolveCollision(EntityType::Mushroom, EntityType::Player)  == CollisionResult::Pass);
    CHECK(resolveCollision(EntityType::Mushroom, EntityType::Goblin)  == CollisionResult::Pass);
    CHECK(resolveCollision(EntityType::Mushroom, EntityType::Tree)    == CollisionResult::Pass);
}
TEST_CASE("resolveCollision: non-mover type → always Pass") {
    // Meat, LongGrass etc. are never movers; table should return Pass
    CHECK(resolveCollision(EntityType::Meat,      EntityType::Player) == CollisionResult::Pass);
    CHECK(resolveCollision(EntityType::LongGrass, EntityType::Tree)   == CollisionResult::Pass);
}
