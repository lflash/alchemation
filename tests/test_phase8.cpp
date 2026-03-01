#include "doctest.h"
#include "grid.hpp"

// ─── Grid::add / remove ───────────────────────────────────────────────────────

TEST_CASE("Grid::add registers entity in spatial") {
    EntityRegistry reg;
    Grid grid(1);

    EntityID id = reg.spawn(EntityType::Goblin, {3, 4});
    grid.add(id, *reg.get(id));

    CHECK(grid.hasEntity(id));
    auto at = grid.spatial.at({3, 4});
    CHECK(std::find(at.begin(), at.end(), id) != at.end());
}

TEST_CASE("Grid::remove unregisters entity from spatial and entity list") {
    EntityRegistry reg;
    Grid grid(1);

    EntityID id = reg.spawn(EntityType::Goblin, {1, 1});
    grid.add(id, *reg.get(id));
    grid.remove(id, *reg.get(id));

    CHECK(!grid.hasEntity(id));
    CHECK(grid.spatial.at({1, 1}).empty());
}

TEST_CASE("Grid::remove cleans up mid-move dual registration") {
    EntityRegistry reg;
    Grid grid(1);

    EntityID id = reg.spawn(EntityType::Goblin, {0, 0});
    Entity* e   = reg.get(id);
    grid.add(id, *e);

    // Simulate mid-move: dual-register at destination
    e->destination = {1, 0};
    grid.spatial.add(id, e->destination, e->size);

    grid.remove(id, *e);

    CHECK(grid.spatial.at({0, 0}).empty());
    CHECK(grid.spatial.at({1, 0}).empty());
}

TEST_CASE("Grid::hasEntity returns false for unknown entity") {
    Grid grid(1);
    CHECK(!grid.hasEntity(42));
}

// ─── Independent terrain ──────────────────────────────────────────────────────

TEST_CASE("terrain in grid A is independent from terrain in grid B") {
    Grid a(1), b(2);

    a.terrain.dig({5, 5});

    CHECK(a.terrain.typeAt({5, 5}) == TileType::BareEarth);
    CHECK(b.terrain.typeAt({5, 5}) == TileType::Grass);
}

// ─── Multiple entities ────────────────────────────────────────────────────────

TEST_CASE("multiple entities can be added and removed independently") {
    EntityRegistry reg;
    Grid grid(1);

    EntityID a = reg.spawn(EntityType::Player, {0, 0});
    EntityID b = reg.spawn(EntityType::Goblin, {1, 0});
    grid.add(a, *reg.get(a));
    grid.add(b, *reg.get(b));

    CHECK(grid.hasEntity(a));
    CHECK(grid.hasEntity(b));

    grid.remove(a, *reg.get(a));
    CHECK(!grid.hasEntity(a));
    CHECK(grid.hasEntity(b));
}
