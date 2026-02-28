#include "doctest.h"
#include "types.hpp"
#include "terrain.hpp"
#include "entity.hpp"
#include "spatial.hpp"

// ─── Helpers ─────────────────────────────────────────────────────────────────

// Runs the plant action (Key::C logic) and returns the spawned EntityID,
// or INVALID_ENTITY if the action was rejected.
static EntityID doPlant(Entity& player, Terrain& terrain,
                        EntityRegistry& registry, SpatialGrid& spatial) {
    TilePos ahead = player.pos + dirToDelta(player.facing);
    if (terrain.typeAt(ahead) != TileType::BareEarth) return INVALID_ENTITY;
    if (player.mana < 1)                               return INVALID_ENTITY;

    EntityID mid = registry.spawn(EntityType::Mushroom, ahead);
    Entity*  m   = registry.get(mid);
    spatial.add(mid, m->pos, m->size);
    terrain.restore(ahead);
    player.mana--;
    return mid;
}

// Runs the collect action (Arrived handler logic) for the given player.
// Returns true if a mushroom was collected.
static bool doCollect(EntityID playerID, EntityRegistry& registry, SpatialGrid& spatial) {
    Entity* player = registry.get(playerID);
    if (!player) return false;

    for (EntityID cid : spatial.at(player->pos)) {
        if (cid == playerID) continue;
        Entity* cand = registry.get(cid);
        if (!cand || cand->type != EntityType::Mushroom) continue;

        player->mana += 3;
        spatial.remove(cid, cand->pos, cand->size);
        registry.destroy(cid);
        return true;
    }
    return false;
}

// ─── dirToDelta ───────────────────────────────────────────────────────────────

TEST_CASE("dirToDelta is the inverse of toDirection for cardinal directions") {
    for (auto [dir, delta] : std::initializer_list<std::pair<Direction, TilePos>>{
            {Direction::N, { 0,-1}}, {Direction::S, { 0, 1}},
            {Direction::E, { 1, 0}}, {Direction::W, {-1, 0}},
    }) {
        CHECK(dirToDelta(dir) == delta);
        CHECK(toDirection(delta) == dir);
    }
}

TEST_CASE("dirToDelta is the inverse of toDirection for diagonal directions") {
    for (auto [dir, delta] : std::initializer_list<std::pair<Direction, TilePos>>{
            {Direction::NE, { 1,-1}}, {Direction::SE, { 1, 1}},
            {Direction::SW, {-1, 1}}, {Direction::NW, {-1,-1}},
    }) {
        CHECK(dirToDelta(dir) == delta);
        CHECK(toDirection(delta) == dir);
    }
}

// ─── Plant action ─────────────────────────────────────────────────────────────

TEST_CASE("plant on Grass tile does nothing") {
    EntityRegistry registry;
    SpatialGrid    spatial;
    Terrain        terrain;

    EntityID pid = registry.spawn(EntityType::Player, {0, 0});
    Entity*  p   = registry.get(pid);
    p->facing = Direction::E;
    p->mana   = 5;

    // Tile ahead is Grass by default
    EntityID mid = doPlant(*p, terrain, registry, spatial);

    CHECK(mid == INVALID_ENTITY);
    CHECK(p->mana == 5);  // mana unchanged
}

TEST_CASE("plant on BareEarth with 0 mana does nothing") {
    EntityRegistry registry;
    SpatialGrid    spatial;
    Terrain        terrain;

    EntityID pid = registry.spawn(EntityType::Player, {0, 0});
    Entity*  p   = registry.get(pid);
    p->facing = Direction::E;
    p->mana   = 0;

    TilePos ahead = p->pos + dirToDelta(p->facing);
    terrain.dig(ahead);

    EntityID mid = doPlant(*p, terrain, registry, spatial);

    CHECK(mid == INVALID_ENTITY);
    CHECK(terrain.typeAt(ahead) == TileType::BareEarth);  // terrain unchanged
    CHECK(p->mana == 0);
}

TEST_CASE("plant on BareEarth with mana >= 1 spawns mushroom, deducts 1 mana, restores terrain") {
    EntityRegistry registry;
    SpatialGrid    spatial;
    Terrain        terrain;

    EntityID pid = registry.spawn(EntityType::Player, {0, 0});
    Entity*  p   = registry.get(pid);
    spatial.add(pid, p->pos, p->size);
    p->facing = Direction::E;
    p->mana   = 3;

    TilePos ahead = p->pos + dirToDelta(p->facing);
    terrain.dig(ahead);

    EntityID mid = doPlant(*p, terrain, registry, spatial);

    CHECK(mid != INVALID_ENTITY);
    CHECK(p->mana == 2);
    CHECK(terrain.typeAt(ahead) == TileType::Grass);  // restored

    // Mushroom exists at the tile
    Entity* m = registry.get(mid);
    REQUIRE(m != nullptr);
    CHECK(m->type == EntityType::Mushroom);
    CHECK(m->pos  == ahead);
}

TEST_CASE("planting twice with enough mana spawns two mushrooms") {
    EntityRegistry registry;
    SpatialGrid    spatial;
    Terrain        terrain;

    EntityID pid = registry.spawn(EntityType::Player, {0, 0});
    Entity*  p   = registry.get(pid);
    spatial.add(pid, p->pos, p->size);
    p->facing = Direction::E;
    p->mana   = 4;

    TilePos east  = {1, 0};
    TilePos north = {0, -1};
    terrain.dig(east);
    terrain.dig(north);

    doPlant(*p, terrain, registry, spatial);  // plants east
    p->facing = Direction::N;
    doPlant(*p, terrain, registry, spatial);  // plants north

    CHECK(p->mana == 2);
    CHECK(terrain.typeAt(east)  == TileType::Grass);
    CHECK(terrain.typeAt(north) == TileType::Grass);
}

// ─── Collect action ───────────────────────────────────────────────────────────

TEST_CASE("collecting mushroom increments mana by 3 and removes mushroom") {
    EntityRegistry registry;
    SpatialGrid    spatial;

    EntityID pid = registry.spawn(EntityType::Player,   {2, 2});
    EntityID mid = registry.spawn(EntityType::Mushroom, {2, 2});
    Entity*  p   = registry.get(pid);
    Entity*  m   = registry.get(mid);

    spatial.add(pid, p->pos, p->size);
    spatial.add(mid, m->pos, m->size);
    p->mana = 1;

    bool collected = doCollect(pid, registry, spatial);

    CHECK(collected);
    CHECK(p->mana == 4);
    CHECK(registry.get(mid) == nullptr);
    CHECK(spatial.at({2, 2}).size() == 1);  // only player remains
}

TEST_CASE("collect with no mushroom present does nothing") {
    EntityRegistry registry;
    SpatialGrid    spatial;

    EntityID pid = registry.spawn(EntityType::Player, {0, 0});
    Entity*  p   = registry.get(pid);
    spatial.add(pid, p->pos, p->size);
    p->mana = 5;

    bool collected = doCollect(pid, registry, spatial);

    CHECK(!collected);
    CHECK(p->mana == 5);
}

TEST_CASE("collect ignores goblin at same tile") {
    EntityRegistry registry;
    SpatialGrid    spatial;

    EntityID pid = registry.spawn(EntityType::Player, {0, 0});
    EntityID gid = registry.spawn(EntityType::Goblin, {0, 0});
    Entity*  p   = registry.get(pid);
    Entity*  g   = registry.get(gid);
    spatial.add(pid, p->pos, p->size);
    spatial.add(gid, g->pos, g->size);
    p->mana = 0;

    bool collected = doCollect(pid, registry, spatial);

    CHECK(!collected);
    CHECK(p->mana == 0);
}

TEST_CASE("collect only removes one mushroom even if two are stacked") {
    EntityRegistry registry;
    SpatialGrid    spatial;

    EntityID pid  = registry.spawn(EntityType::Player,   {0, 0});
    EntityID mid1 = registry.spawn(EntityType::Mushroom, {0, 0});
    EntityID mid2 = registry.spawn(EntityType::Mushroom, {0, 0});
    Entity*  p    = registry.get(pid);
    spatial.add(pid,  p->pos, p->size);
    spatial.add(mid1, registry.get(mid1)->pos, registry.get(mid1)->size);
    spatial.add(mid2, registry.get(mid2)->pos, registry.get(mid2)->size);
    p->mana = 0;

    doCollect(pid, registry, spatial);

    CHECK(p->mana == 3);
    // Exactly one mushroom should remain
    int mushrooms = 0;
    for (EntityID cid : spatial.at({0, 0})) {
        Entity* e = registry.get(cid);
        if (e && e->type == EntityType::Mushroom) mushrooms++;
    }
    CHECK(mushrooms == 1);
}
