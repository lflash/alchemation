#include "doctest.h"
#include "entity.hpp"
#include "spatial.hpp"
#include <algorithm>

// ─── Helpers ─────────────────────────────────────────────────────────────────

// Runs the combat/push action (Arrived handler logic) for a player hitting a goblin.
static void doCombat(EntityID playerID, EntityID goblinID,
                     EntityRegistry& registry, SpatialGrid& spatial) {
    Entity* player = registry.get(playerID);
    Entity* goblin = registry.get(goblinID);
    if (!player || !goblin) return;

    goblin->health -= player->mana;
    if (goblin->health <= 0) {
        spatial.remove(goblinID, goblin->pos, goblin->size);
        if (goblin->isMoving())
            spatial.remove(goblinID, goblin->destination, goblin->size);
        registry.destroy(goblinID);
        return;
    }

    TilePos pushBase = goblin->isMoving() ? goblin->destination : goblin->pos;
    TilePos pushDest = pushBase + dirToDelta(player->facing);
    if (goblin->isMoving()) {
        Bounds pushBounds = boundsAt(pushDest, goblin->size);
        bool pushBlocked = false;
        for (EntityID oid : spatial.query(pushBounds)) {
            if (oid == goblinID) continue;
            const Entity* occ = registry.get(oid);
            if (!occ || !overlaps(pushBounds, boundsAt(occ->pos, occ->size))) continue;
            if (resolveCollision(goblin->type, occ->type) == CollisionResult::Block) {
                pushBlocked = true;
                break;
            }
        }
        if (!pushBlocked) {
            spatial.remove(goblinID, goblin->destination, goblin->size);
            spatial.add(goblinID, pushDest, goblin->size);
            goblin->destination = pushDest;
        }
    } else {
        std::vector<MoveIntention> intentions = {{
            goblinID, goblin->pos, pushDest, goblin->type, goblin->size
        }};
        auto allowed = resolveMoves(intentions, spatial, registry);
        if (allowed.count(goblinID))
            goblin->destination = pushDest;
    }
}

// ─── Health defaults ─────────────────────────────────────────────────────────

TEST_CASE("goblin spawns with 5 health") {
    EntityRegistry registry;
    EntityID gid = registry.spawn(EntityType::Goblin, {0, 0});
    CHECK(registry.get(gid)->health == 5);
}

TEST_CASE("player spawns with 0 health (health unused for player)") {
    EntityRegistry registry;
    EntityID pid = registry.spawn(EntityType::Player, {0, 0});
    CHECK(registry.get(pid)->health == 0);
}

// ─── Combat & despawn ────────────────────────────────────────────────────────

TEST_CASE("goblin despawns when health reaches 0") {
    EntityRegistry registry;
    SpatialGrid    spatial;

    EntityID pid = registry.spawn(EntityType::Player, {0, 0});
    EntityID gid = registry.spawn(EntityType::Goblin, {1, 0});
    Entity*  p   = registry.get(pid);
    Entity*  g   = registry.get(gid);

    spatial.add(pid, p->pos, p->size);
    spatial.add(gid, g->pos, g->size);

    p->mana    = 10;   // more than goblin's 5 health
    p->facing  = Direction::E;
    g->pos     = {1, 0};  // same tile player arrived at

    doCombat(pid, gid, registry, spatial);

    CHECK(registry.get(gid) == nullptr);
    CHECK(spatial.at({1, 0}).empty());
}

TEST_CASE("combat with 0 mana deals 0 damage") {
    EntityRegistry registry;
    SpatialGrid    spatial;

    EntityID pid = registry.spawn(EntityType::Player, {0, 0});
    EntityID gid = registry.spawn(EntityType::Goblin, {1, 0});
    Entity*  p   = registry.get(pid);
    Entity*  g   = registry.get(gid);

    spatial.add(pid, p->pos, p->size);
    spatial.add(gid, g->pos, g->size);

    p->mana   = 0;
    p->facing = Direction::E;

    int healthBefore = g->health;
    doCombat(pid, gid, registry, spatial);

    CHECK(registry.get(gid) != nullptr);   // still alive
    CHECK(g->health == healthBefore);      // no damage
}

TEST_CASE("combat reduces goblin health by player mana") {
    EntityRegistry registry;
    SpatialGrid    spatial;

    EntityID pid = registry.spawn(EntityType::Player, {0, 0});
    EntityID gid = registry.spawn(EntityType::Goblin, {1, 0});
    Entity*  p   = registry.get(pid);
    Entity*  g   = registry.get(gid);

    spatial.add(pid, p->pos, p->size);
    spatial.add(gid, g->pos, g->size);

    p->mana   = 2;
    p->facing = Direction::E;

    doCombat(pid, gid, registry, spatial);

    REQUIRE(registry.get(gid) != nullptr);
    CHECK(g->health == 3);   // 5 - 2
}

// ─── Push mechanic ───────────────────────────────────────────────────────────

TEST_CASE("goblin is pushed in player facing direction when tile is free") {
    EntityRegistry registry;
    SpatialGrid    spatial;

    EntityID pid = registry.spawn(EntityType::Player, {0, 0});
    EntityID gid = registry.spawn(EntityType::Goblin, {1, 0});
    Entity*  p   = registry.get(pid);
    Entity*  g   = registry.get(gid);

    spatial.add(pid, p->pos, p->size);
    spatial.add(gid, g->pos, g->size);

    p->mana   = 1;    // small damage, goblin survives
    p->facing = Direction::E;

    doCombat(pid, gid, registry, spatial);

    REQUIRE(registry.get(gid) != nullptr);
    CHECK(g->destination == TilePos{2, 0});  // pushed east
}

TEST_CASE("goblin is not pushed when destination is blocked") {
    EntityRegistry registry;
    SpatialGrid    spatial;

    EntityID pid  = registry.spawn(EntityType::Player, {0, 0});
    EntityID gid  = registry.spawn(EntityType::Goblin, {1, 0});
    EntityID gid2 = registry.spawn(EntityType::Goblin, {2, 0});  // blocker
    Entity*  p    = registry.get(pid);
    Entity*  g    = registry.get(gid);
    Entity*  g2   = registry.get(gid2);

    spatial.add(pid,  p->pos,  p->size);
    spatial.add(gid,  g->pos,  g->size);
    spatial.add(gid2, g2->pos, g2->size);

    p->mana   = 1;
    p->facing = Direction::E;

    doCombat(pid, gid, registry, spatial);

    REQUIRE(registry.get(gid) != nullptr);
    CHECK(g->destination == g->pos);  // not pushed, still idle
}

TEST_CASE("goblin pushed south when player faces south") {
    EntityRegistry registry;
    SpatialGrid    spatial;

    EntityID pid = registry.spawn(EntityType::Player, {0, 0});
    EntityID gid = registry.spawn(EntityType::Goblin, {0, 1});
    Entity*  p   = registry.get(pid);
    Entity*  g   = registry.get(gid);

    spatial.add(pid, p->pos, p->size);
    spatial.add(gid, g->pos, g->size);

    p->mana   = 1;
    p->facing = Direction::S;

    doCombat(pid, gid, registry, spatial);

    REQUIRE(registry.get(gid) != nullptr);
    CHECK(g->destination == TilePos{0, 2});
}

TEST_CASE("moving goblin push redirects destination, not pos") {
    EntityRegistry registry;
    SpatialGrid    spatial;

    EntityID pid = registry.spawn(EntityType::Player, {0, 0});
    EntityID gid = registry.spawn(EntityType::Goblin, {1, 0});
    Entity*  p   = registry.get(pid);
    Entity*  g   = registry.get(gid);

    spatial.add(pid, p->pos, p->size);
    spatial.add(gid, g->pos, g->size);

    // Goblin mid-move: heading east from {1,0} to {2,0}
    g->destination = {2, 0};
    g->moveT       = 0.5f;
    spatial.add(gid, g->destination, g->size);  // dual-register

    p->mana   = 1;   // small damage, goblin survives
    p->facing = Direction::E;

    doCombat(pid, gid, registry, spatial);

    REQUIRE(registry.get(gid) != nullptr);
    // Push base is old destination {2,0}, so new destination = {3,0}
    CHECK(g->destination == TilePos{3, 0});
    // Old destination {2,0} should be cleared; new {3,0} registered
    CHECK(spatial.at({2, 0}).empty());
    CHECK(spatial.at({3, 0}).size() == 1);
}
