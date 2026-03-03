#include "doctest.h"

#include "game.hpp"
#include "grid.hpp"
#include "entity.hpp"
#include "terrain.hpp"

// Helper: build a grid + registry and add an entity in one call.
static EntityID addEntity(Grid& grid, EntityRegistry& reg,
                           EntityType type, TilePos pos) {
    EntityID id = reg.spawn(type, pos);
    grid.add(id, *reg.get(id));
    return id;
}

// ─── tickFire: ignition timing ────────────────────────────────────────────────

TEST_CASE("grass adjacent to campfire does not catch fire before 50 ticks") {
    EntityRegistry reg;
    Grid grid(GRID_WORLD);

    addEntity(grid, reg, EntityType::Campfire, {5, 5});
    // {6,5} is adjacent — Grass by default.

    for (Tick t = 0; t < 49; ++t)
        tickFire(grid, reg, t);

    CHECK(grid.terrain.typeAt({6, 5}) == TileType::Grass);
}

TEST_CASE("grass adjacent to campfire catches fire at tick 50") {
    EntityRegistry reg;
    Grid grid(GRID_WORLD);

    addEntity(grid, reg, EntityType::Campfire, {5, 5});

    for (Tick t = 0; t < 50; ++t)
        tickFire(grid, reg, t);

    // At least one of the four neighbours must be on fire.
    bool anyFire =
        grid.terrain.typeAt({6, 5}) == TileType::Fire ||
        grid.terrain.typeAt({4, 5}) == TileType::Fire ||
        grid.terrain.typeAt({5, 6}) == TileType::Fire ||
        grid.terrain.typeAt({5, 4}) == TileType::Fire;
    CHECK(anyFire);
}

TEST_CASE("grass far from any fire is unaffected after 50 ticks") {
    EntityRegistry reg;
    Grid grid(GRID_WORLD);

    addEntity(grid, reg, EntityType::Campfire, {0, 0});

    for (Tick t = 0; t < 50; ++t)
        tickFire(grid, reg, t);

    // {10,10} is nowhere near the campfire.
    CHECK(grid.terrain.typeAt({10, 10}) == TileType::Grass);
}

// ─── tickFire: fire tile expiry ───────────────────────────────────────────────

TEST_CASE("fire tile expires to BareEarth after 150 ticks") {
    EntityRegistry reg;
    Grid grid(GRID_WORLD);

    // Manually ignite a tile at tick 0 with expiry at tick 150.
    grid.terrain.setType({3, 3}, TileType::Fire);
    grid.fireTileExpiry[{3, 3}] = 150;

    // One tick before expiry: still fire.
    tickFire(grid, reg, 149);
    CHECK(grid.terrain.typeAt({3, 3}) == TileType::Fire);

    // At expiry tick: extinguished.
    tickFire(grid, reg, 150);
    CHECK(grid.terrain.typeAt({3, 3}) == TileType::BareEarth);
    CHECK(!grid.fireTileExpiry.count({3, 3}));
}

TEST_CASE("extinguished fire tile is removed from fireTileExpiry") {
    EntityRegistry reg;
    Grid grid(GRID_WORLD);

    grid.terrain.setType({1, 1}, TileType::Fire);
    grid.fireTileExpiry[{1, 1}] = 10;

    tickFire(grid, reg, 10);
    CHECK(grid.fireTileExpiry.empty());
}

// ─── tickFire: entity burning ─────────────────────────────────────────────────

TEST_CASE("TreeStump adjacent to campfire does not ignite before 250 ticks") {
    EntityRegistry reg;
    Grid grid(GRID_WORLD);

    addEntity(grid, reg, EntityType::Campfire,  {0, 0});
    EntityID stump = addEntity(grid, reg, EntityType::TreeStump, {1, 0});

    for (Tick t = 0; t < 249; ++t)
        tickFire(grid, reg, t);

    CHECK(reg.get(stump) != nullptr);
    CHECK(!grid.entityBurnEnd.count(stump));
}

TEST_CASE("TreeStump adjacent to campfire starts burning at tick 250") {
    EntityRegistry reg;
    Grid grid(GRID_WORLD);

    addEntity(grid, reg, EntityType::Campfire,  {0, 0});
    EntityID stump = addEntity(grid, reg, EntityType::TreeStump, {1, 0});

    for (Tick t = 0; t < 250; ++t)
        tickFire(grid, reg, t);

    // Ignition fires on the 250th call (t=249); burnEnd = 249 + 500.
    CHECK(grid.entityBurnEnd.count(stump));
    CHECK(grid.entityBurnEnd[stump] == 249 + 500);
}

TEST_CASE("Log adjacent to campfire starts burning at tick 250") {
    EntityRegistry reg;
    Grid grid(GRID_WORLD);

    addEntity(grid, reg, EntityType::Campfire, {0, 0});
    EntityID log = addEntity(grid, reg, EntityType::Log, {0, 1});

    for (Tick t = 0; t < 250; ++t)
        tickFire(grid, reg, t);

    CHECK(grid.entityBurnEnd.count(log));
}

TEST_CASE("burning entity despawns after 500 ticks") {
    EntityRegistry reg;
    Grid grid(GRID_WORLD);

    EntityID stump = addEntity(grid, reg, EntityType::TreeStump, {2, 2});

    // Manually mark as burning from tick 0, expires at tick 500.
    grid.entityBurnEnd[stump] = 500;

    tickFire(grid, reg, 499);
    CHECK(reg.get(stump) != nullptr);   // still alive

    tickFire(grid, reg, 500);
    CHECK(reg.get(stump) == nullptr);   // despawned
    CHECK(!grid.hasEntity(stump));
}

// ─── tickFire: exposure resets when heat source removed ───────────────────────

TEST_CASE("grass exposure resets when no longer adjacent to fire") {
    EntityRegistry reg;
    Grid grid(GRID_WORLD);

    // Manually warm up {1,0} for 30 ticks by seeding a fire at {0,0}.
    grid.terrain.setType({0, 0}, TileType::Fire);
    grid.fireTileExpiry[{0, 0}] = 1000;   // won't expire during test

    for (Tick t = 0; t < 30; ++t)
        tickFire(grid, reg, t);

    CHECK(grid.tileFireExp.count({1, 0}));
    CHECK(grid.tileFireExp[{1, 0}] > 0);

    // Remove the fire source; exposure should decay to 0.
    grid.terrain.setType({0, 0}, TileType::BareEarth);
    grid.fireTileExpiry.erase({0, 0});

    tickFire(grid, reg, 30);
    CHECK(!grid.tileFireExp.count({1, 0}));
}

// ─── tickFire: chain spreading ────────────────────────────────────────────────

TEST_CASE("fire spreads from a fire tile to adjacent grass") {
    EntityRegistry reg;
    Grid grid(GRID_WORLD);

    // Fire tile at {0,0}; {1,0} is Grass — should catch after 50 ticks.
    grid.terrain.setType({0, 0}, TileType::Fire);
    grid.fireTileExpiry[{0, 0}] = 1000;

    for (Tick t = 0; t < 50; ++t)
        tickFire(grid, reg, t);

    CHECK(grid.terrain.typeAt({1, 0}) == TileType::Fire);
}

// ─── tickVoltage: no batteries ────────────────────────────────────────────────

TEST_CASE("no voltage anywhere when there are no batteries") {
    EntityRegistry reg;
    Grid grid(GRID_WORLD);

    grid.terrain.setType({0, 0}, TileType::Puddle);
    EntityID bulb = addEntity(grid, reg, EntityType::Lightbulb, {0, 0});

    tickVoltage(grid, reg);

    CHECK(grid.voltage.empty());
    CHECK(reg.get(bulb)->lit == false);
}

// ─── tickVoltage: battery with no adjacent puddles ────────────────────────────

TEST_CASE("battery on grass with no adjacent puddles produces no voltage") {
    EntityRegistry reg;
    Grid grid(GRID_WORLD);

    addEntity(grid, reg, EntityType::Battery, {5, 5});
    // No puddles placed.

    tickVoltage(grid, reg);
    CHECK(grid.voltage.empty());
}

// ─── tickVoltage: adjacent puddle gets 4V ─────────────────────────────────────

TEST_CASE("puddle adjacent to battery receives 4V") {
    EntityRegistry reg;
    Grid grid(GRID_WORLD);

    addEntity(grid, reg, EntityType::Battery, {0, 0});
    grid.terrain.setType({1, 0}, TileType::Puddle);

    tickVoltage(grid, reg);

    REQUIRE(grid.voltage.count({1, 0}));
    CHECK(grid.voltage[{1, 0}] == 4);
}

// ─── tickVoltage: puddle chain propagation ────────────────────────────────────

TEST_CASE("voltage decrements by 1 per puddle hop") {
    EntityRegistry reg;
    Grid grid(GRID_WORLD);

    addEntity(grid, reg, EntityType::Battery, {0, 0});
    grid.terrain.setType({1, 0}, TileType::Puddle);
    grid.terrain.setType({2, 0}, TileType::Puddle);
    grid.terrain.setType({3, 0}, TileType::Puddle);
    grid.terrain.setType({4, 0}, TileType::Puddle);

    tickVoltage(grid, reg);

    CHECK(grid.voltage[{1, 0}] == 4);
    CHECK(grid.voltage[{2, 0}] == 3);
    CHECK(grid.voltage[{3, 0}] == 2);
    CHECK(grid.voltage[{4, 0}] == 1);
}

TEST_CASE("puddle 5 hops from battery receives no voltage") {
    EntityRegistry reg;
    Grid grid(GRID_WORLD);

    addEntity(grid, reg, EntityType::Battery, {0, 0});
    for (int x = 1; x <= 5; ++x)
        grid.terrain.setType({x, 0}, TileType::Puddle);

    tickVoltage(grid, reg);

    CHECK(!grid.voltage.count({5, 0}));
}

// ─── tickVoltage: lightbulb lit state ────────────────────────────────────────

TEST_CASE("lightbulb on charged puddle is lit") {
    EntityRegistry reg;
    Grid grid(GRID_WORLD);

    addEntity(grid, reg, EntityType::Battery, {0, 0});
    grid.terrain.setType({1, 0}, TileType::Puddle);
    EntityID bulb = addEntity(grid, reg, EntityType::Lightbulb, {1, 0});

    tickVoltage(grid, reg);

    CHECK(reg.get(bulb)->lit == true);
}

TEST_CASE("lightbulb on uncharged puddle (too far) is not lit") {
    EntityRegistry reg;
    Grid grid(GRID_WORLD);

    addEntity(grid, reg, EntityType::Battery, {0, 0});
    for (int x = 1; x <= 5; ++x)
        grid.terrain.setType({x, 0}, TileType::Puddle);
    EntityID bulb = addEntity(grid, reg, EntityType::Lightbulb, {5, 0});

    tickVoltage(grid, reg);

    CHECK(reg.get(bulb)->lit == false);
}

TEST_CASE("lightbulb on grass (no puddle) is not lit even near battery") {
    EntityRegistry reg;
    Grid grid(GRID_WORLD);

    addEntity(grid, reg, EntityType::Battery, {0, 0});
    // Bulb is adjacent to battery but terrain is Grass, not Puddle.
    EntityID bulb = addEntity(grid, reg, EntityType::Lightbulb, {1, 0});

    tickVoltage(grid, reg);

    CHECK(reg.get(bulb)->lit == false);
}

TEST_CASE("lightbulb lit state updates to off when voltage is removed") {
    EntityRegistry reg;
    Grid grid(GRID_WORLD);

    EntityID bat  = addEntity(grid, reg, EntityType::Battery,   {0, 0});
    grid.terrain.setType({1, 0}, TileType::Puddle);
    EntityID bulb = addEntity(grid, reg, EntityType::Lightbulb, {1, 0});

    tickVoltage(grid, reg);
    CHECK(reg.get(bulb)->lit == true);

    // Remove the battery.
    Entity* b = reg.get(bat);
    grid.remove(bat, *b);
    reg.destroy(bat);

    tickVoltage(grid, reg);
    CHECK(reg.get(bulb)->lit == false);
}

// ─── tickVoltage: multiple batteries ─────────────────────────────────────────

TEST_CASE("two batteries reaching same puddle: puddle gets max voltage") {
    EntityRegistry reg;
    Grid grid(GRID_WORLD);

    // Battery A at {0,2}, Battery B at {4,2}; shared puddle at {2,2}.
    // Path A: {1,2}=puddle (4V), {2,2}=puddle (3V)
    // Path B: {3,2}=puddle (4V), {2,2}=puddle (3V)
    // Both paths reach {2,2} at 3V — consistent result.
    addEntity(grid, reg, EntityType::Battery, {0, 2});
    addEntity(grid, reg, EntityType::Battery, {4, 2});
    grid.terrain.setType({1, 2}, TileType::Puddle);
    grid.terrain.setType({2, 2}, TileType::Puddle);
    grid.terrain.setType({3, 2}, TileType::Puddle);

    tickVoltage(grid, reg);

    CHECK(grid.voltage[{1, 2}] == 4);
    CHECK(grid.voltage[{2, 2}] == 3);
    CHECK(grid.voltage[{3, 2}] == 4);
}

TEST_CASE("closer battery wins: puddle gets higher voltage") {
    EntityRegistry reg;
    Grid grid(GRID_WORLD);

    // Battery A at {0,0} — {1,0} is 1 hop away → 4V
    // Battery B at {5,0} — {1,0} is 4 hops away → would be 1V
    // BFS should assign max(4V, 1V) = 4V.
    addEntity(grid, reg, EntityType::Battery, {0, 0});
    addEntity(grid, reg, EntityType::Battery, {5, 0});
    for (int x = 1; x <= 4; ++x)
        grid.terrain.setType({x, 0}, TileType::Puddle);

    tickVoltage(grid, reg);

    CHECK(grid.voltage[{1, 0}] == 4);
    CHECK(grid.voltage[{4, 0}] == 4);   // 1 hop from Battery B
}

// ─── tickVoltage: voltage recomputed fresh each tick ─────────────────────────

TEST_CASE("voltage map is cleared and recomputed each tick") {
    EntityRegistry reg;
    Grid grid(GRID_WORLD);

    addEntity(grid, reg, EntityType::Battery, {0, 0});
    grid.terrain.setType({1, 0}, TileType::Puddle);

    tickVoltage(grid, reg);
    CHECK(grid.voltage.count({1, 0}));

    // Replace puddle with grass — voltage should disappear next tick.
    grid.terrain.setType({1, 0}, TileType::Grass);
    tickVoltage(grid, reg);
    CHECK(!grid.voltage.count({1, 0}));
}
