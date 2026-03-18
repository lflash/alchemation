#include "doctest.h"

#include "game.hpp"
#include "field.hpp"
#include "entity.hpp"

// Helper: build a grid + registry and add an entity in one call.
static EntityID addEntity(Field& grid, EntityRegistry& reg,
                           EntityType type, TilePos pos) {
    EntityID id = reg.spawn(type, pos);
    grid.add(id, *reg.get(id));
    return id;
}

// Helper: add a Fire entity + fireTileExpiry entry.
static EntityID addFire(Field& grid, EntityRegistry& reg,
                         TilePos pos, Tick expiry = 999999) {
    EntityID feid = reg.spawn(EntityType::Fire, pos);
    grid.add(feid, *reg.get(feid));
    grid.fireTileExpiry[pos] = expiry;
    return feid;
}

// Helper: add a Puddle entity (no z adjustment needed for flat test grids).
static EntityID addPuddle(Field& grid, EntityRegistry& reg, TilePos pos) {
    return addEntity(grid, reg, EntityType::Puddle, pos);
}

// Helper: check Fire entity exists at pos.
static bool hasFire(Field& grid, EntityRegistry& reg, TilePos pos) {
    for (EntityID eid : grid.spatial.at(pos)) {
        const Entity* e = reg.get(eid);
        if (e && e->type == EntityType::Fire) return true;
    }
    return false;
}

// Helper: check BareEarth entity exists at pos.
static bool hasBareEarth(Field& grid, EntityRegistry& reg, TilePos pos) {
    for (EntityID eid : grid.spatial.at(pos)) {
        const Entity* e = reg.get(eid);
        if (e && e->type == EntityType::BareEarth) return true;
    }
    return false;
}

// Helper: remove all Fire entities at pos (simulate manually removing fire source).
static void removeFire(Field& grid, EntityRegistry& reg, TilePos pos) {
    for (EntityID eid : std::vector<EntityID>(grid.spatial.at(pos))) {
        Entity* e = reg.get(eid);
        if (e && e->type == EntityType::Fire) {
            grid.remove(eid, *e); reg.destroy(eid);
        }
    }
    grid.fireTileExpiry.erase(pos);
}

// Helper: remove all Puddle entities at pos.
static void removePuddle(Field& grid, EntityRegistry& reg, TilePos pos) {
    for (EntityID eid : std::vector<EntityID>(grid.spatial.at(pos))) {
        Entity* e = reg.get(eid);
        if (e && e->type == EntityType::Puddle) {
            grid.remove(eid, *e); reg.destroy(eid);
        }
    }
}

// ─── tickFire: ignition timing ────────────────────────────────────────────────

TEST_CASE("grass adjacent to campfire does not catch fire before 50 ticks") {
    EntityRegistry reg;
    Field grid(FIELD_WORLD);

    addEntity(grid, reg, EntityType::Campfire, {5, 5});
    // {6,5} is adjacent — Grass by default.

    for (Tick t = 0; t < 49; ++t)
        tickFire(grid, reg, t);

    CHECK(!hasFire(grid, reg, {6, 5}));
}

TEST_CASE("long grass adjacent to campfire catches fire at tick 50") {
    EntityRegistry reg;
    Field grid(FIELD_WORLD);

    addEntity(grid, reg, EntityType::Campfire,  {5, 5});
    addEntity(grid, reg, EntityType::LongGrass, {6, 5});  // adjacent

    for (Tick t = 0; t < 50; ++t)
        tickFire(grid, reg, t);

    // The LongGrass at {6,5} must have ignited.
    CHECK(hasFire(grid, reg, {6, 5}));
}

TEST_CASE("grass far from any fire is unaffected after 50 ticks") {
    EntityRegistry reg;
    Field grid(FIELD_WORLD);

    addEntity(grid, reg, EntityType::Campfire, {0, 0});

    for (Tick t = 0; t < 50; ++t)
        tickFire(grid, reg, t);

    // {10,10} is nowhere near the campfire — no Fire entity there.
    CHECK(!hasFire(grid, reg, {10, 10}));
}

// ─── tickFire: fire tile expiry ───────────────────────────────────────────────

TEST_CASE("fire tile expires to BareEarth after 150 ticks") {
    EntityRegistry reg;
    Field grid(FIELD_WORLD);

    // Manually ignite a tile at tick 0 with expiry at tick 150.
    addFire(grid, reg, {3, 3}, 150);

    // One tick before expiry: still fire.
    tickFire(grid, reg, 149);
    CHECK(hasFire(grid, reg, {3, 3}));

    // At expiry tick: extinguished → BareEarth.
    tickFire(grid, reg, 150);
    CHECK(!hasFire(grid, reg, {3, 3}));
    CHECK(hasBareEarth(grid, reg, {3, 3}));
    CHECK(!grid.fireTileExpiry.count({3, 3}));
}

TEST_CASE("extinguished fire tile is removed from fireTileExpiry") {
    EntityRegistry reg;
    Field grid(FIELD_WORLD);

    addFire(grid, reg, {1, 1}, 10);

    tickFire(grid, reg, 10);
    CHECK(grid.fireTileExpiry.empty());
}

// ─── tickFire: entity burning ─────────────────────────────────────────────────

TEST_CASE("TreeStump adjacent to campfire does not ignite before 250 ticks") {
    EntityRegistry reg;
    Field grid(FIELD_WORLD);

    addEntity(grid, reg, EntityType::Campfire,  {0, 0});
    EntityID stump = addEntity(grid, reg, EntityType::TreeStump, {1, 0});

    for (Tick t = 0; t < 249; ++t)
        tickFire(grid, reg, t);

    CHECK(reg.get(stump) != nullptr);
    CHECK(!grid.entityBurnEnd.count(stump));
}

TEST_CASE("TreeStump adjacent to campfire starts burning at tick 250") {
    EntityRegistry reg;
    Field grid(FIELD_WORLD);

    addEntity(grid, reg, EntityType::Campfire,  {0, 0});
    EntityID stump = addEntity(grid, reg, EntityType::TreeStump, {1, 0});

    for (Tick t = 0; t < 250; ++t)
        tickFire(grid, reg, t);

    // Ignition fires on the 250th call (t=249); burnEnd = 249 + 500 * mass.
    // TreeStump has mass=2, so burnEnd = 249 + 1000 = 1249.
    CHECK(grid.entityBurnEnd.count(stump));
    EntityConfig cfg = defaultConfig(EntityType::TreeStump);
    CHECK(grid.entityBurnEnd[stump] == 249 + 500 * cfg.mass);
}

TEST_CASE("Log adjacent to campfire starts burning at tick 250") {
    EntityRegistry reg;
    Field grid(FIELD_WORLD);

    addEntity(grid, reg, EntityType::Campfire, {0, 0});
    EntityID log = addEntity(grid, reg, EntityType::Log, {0, 1});

    for (Tick t = 0; t < 250; ++t)
        tickFire(grid, reg, t);

    CHECK(grid.entityBurnEnd.count(log));
}

TEST_CASE("burning entity despawns after 500 ticks") {
    EntityRegistry reg;
    Field grid(FIELD_WORLD);

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

TEST_CASE("long grass exposure resets when no longer adjacent to fire") {
    EntityRegistry reg;
    Field grid(FIELD_WORLD);

    // Manually warm up {1,0} for 30 ticks by seeding a fire at {0,0}.
    addFire(grid, reg, {0, 0}, 1000);   // won't expire during test
    addEntity(grid, reg, EntityType::LongGrass, {1, 0});  // fuel at {1,0}

    for (Tick t = 0; t < 30; ++t)
        tickFire(grid, reg, t);

    CHECK(grid.tileFireExp.count({1, 0}));
    CHECK(grid.tileFireExp[{1, 0}] > 0);

    // Remove the fire source; exposure should decay to 0.
    removeFire(grid, reg, {0, 0});

    tickFire(grid, reg, 30);
    CHECK(!grid.tileFireExp.count({1, 0}));
}

// ─── tickFire: chain spreading ────────────────────────────────────────────────

TEST_CASE("fire spreads from a fire tile to adjacent long grass") {
    EntityRegistry reg;
    Field grid(FIELD_WORLD);

    // Fire tile at {0,0}; LongGrass at {1,0} — should catch after 50 ticks.
    addFire(grid, reg, {0, 0}, 1000);
    addEntity(grid, reg, EntityType::LongGrass, {1, 0});

    for (Tick t = 0; t < 50; ++t)
        tickFire(grid, reg, t);

    CHECK(hasFire(grid, reg, {1, 0}));
}

// ─── tickVoltage: no batteries ────────────────────────────────────────────────

TEST_CASE("no voltage anywhere when there are no batteries") {
    EntityRegistry reg;
    Field grid(FIELD_WORLD);

    addPuddle(grid, reg, {0, 0});
    EntityID bulb = addEntity(grid, reg, EntityType::Lightbulb, {0, 0});

    tickVoltage(grid, reg);

    CHECK(grid.voltage.empty());
    CHECK(reg.get(bulb)->lit == false);
}

// ─── tickVoltage: battery with no adjacent puddles ────────────────────────────

TEST_CASE("battery on grass with no adjacent puddles produces no voltage") {
    EntityRegistry reg;
    Field grid(FIELD_WORLD);

    addEntity(grid, reg, EntityType::Battery, {5, 5});
    // No puddles placed.

    tickVoltage(grid, reg);
    CHECK(grid.voltage.empty());
}

// ─── tickVoltage: adjacent puddle gets 4V ─────────────────────────────────────

TEST_CASE("puddle adjacent to battery receives 4V") {
    EntityRegistry reg;
    Field grid(FIELD_WORLD);

    addEntity(grid, reg, EntityType::Battery, {0, 0});
    addPuddle(grid, reg, {1, 0});

    tickVoltage(grid, reg);

    REQUIRE(grid.voltage.count({1, 0}));
    CHECK(grid.voltage[{1, 0}] == 4);
}

// ─── tickVoltage: puddle chain propagation ────────────────────────────────────

TEST_CASE("voltage decrements by 1 per puddle hop") {
    EntityRegistry reg;
    Field grid(FIELD_WORLD);

    addEntity(grid, reg, EntityType::Battery, {0, 0});
    addPuddle(grid, reg, {1, 0});
    addPuddle(grid, reg, {2, 0});
    addPuddle(grid, reg, {3, 0});
    addPuddle(grid, reg, {4, 0});

    tickVoltage(grid, reg);

    CHECK(grid.voltage[{1, 0}] == 4);
    CHECK(grid.voltage[{2, 0}] == 3);
    CHECK(grid.voltage[{3, 0}] == 2);
    CHECK(grid.voltage[{4, 0}] == 1);
}

TEST_CASE("puddle 5 hops from battery receives no voltage") {
    EntityRegistry reg;
    Field grid(FIELD_WORLD);

    addEntity(grid, reg, EntityType::Battery, {0, 0});
    for (int x = 1; x <= 5; ++x)
        addPuddle(grid, reg, {x, 0});

    tickVoltage(grid, reg);

    CHECK(!grid.voltage.count({5, 0}));
}

// ─── tickVoltage: lightbulb lit state ────────────────────────────────────────

TEST_CASE("lightbulb on charged puddle is lit") {
    EntityRegistry reg;
    Field grid(FIELD_WORLD);

    addEntity(grid, reg, EntityType::Battery, {0, 0});
    addPuddle(grid, reg, {1, 0});
    EntityID bulb = addEntity(grid, reg, EntityType::Lightbulb, {1, 0});

    tickVoltage(grid, reg);

    CHECK(reg.get(bulb)->lit == true);
}

TEST_CASE("lightbulb on uncharged puddle (too far) is not lit") {
    EntityRegistry reg;
    Field grid(FIELD_WORLD);

    addEntity(grid, reg, EntityType::Battery, {0, 0});
    for (int x = 1; x <= 5; ++x)
        addPuddle(grid, reg, {x, 0});
    EntityID bulb = addEntity(grid, reg, EntityType::Lightbulb, {5, 0});

    tickVoltage(grid, reg);

    CHECK(reg.get(bulb)->lit == false);
}

TEST_CASE("lightbulb on grass (no puddle) is not lit even near battery") {
    EntityRegistry reg;
    Field grid(FIELD_WORLD);

    addEntity(grid, reg, EntityType::Battery, {0, 0});
    // Bulb is adjacent to battery but terrain is Grass, not Puddle.
    EntityID bulb = addEntity(grid, reg, EntityType::Lightbulb, {1, 0});

    tickVoltage(grid, reg);

    CHECK(reg.get(bulb)->lit == false);
}

TEST_CASE("lightbulb lit state updates to off when voltage is removed") {
    EntityRegistry reg;
    Field grid(FIELD_WORLD);

    EntityID bat  = addEntity(grid, reg, EntityType::Battery,   {0, 0});
    addPuddle(grid, reg, {1, 0});
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
    Field grid(FIELD_WORLD);

    // Battery A at {0,2}, Battery B at {4,2}; shared puddle at {2,2}.
    // Path A: {1,2}=puddle (4V), {2,2}=puddle (3V)
    // Path B: {3,2}=puddle (4V), {2,2}=puddle (3V)
    // Both paths reach {2,2} at 3V — consistent result.
    addEntity(grid, reg, EntityType::Battery, {0, 2});
    addEntity(grid, reg, EntityType::Battery, {4, 2});
    addPuddle(grid, reg, {1, 2});
    addPuddle(grid, reg, {2, 2});
    addPuddle(grid, reg, {3, 2});

    tickVoltage(grid, reg);

    CHECK(grid.voltage[{1, 2}] == 4);
    CHECK(grid.voltage[{2, 2}] == 3);
    CHECK(grid.voltage[{3, 2}] == 4);
}

TEST_CASE("closer battery wins: puddle gets higher voltage") {
    EntityRegistry reg;
    Field grid(FIELD_WORLD);

    // Battery A at {0,0} — {1,0} is 1 hop away → 4V
    // Battery B at {5,0} — {1,0} is 4 hops away → would be 1V
    // BFS should assign max(4V, 1V) = 4V.
    addEntity(grid, reg, EntityType::Battery, {0, 0});
    addEntity(grid, reg, EntityType::Battery, {5, 0});
    for (int x = 1; x <= 4; ++x)
        addPuddle(grid, reg, {x, 0});

    tickVoltage(grid, reg);

    CHECK(grid.voltage[{1, 0}] == 4);
    CHECK(grid.voltage[{4, 0}] == 4);   // 1 hop from Battery B
}

// ─── tickVoltage: voltage recomputed fresh each tick ─────────────────────────

TEST_CASE("voltage map is cleared and recomputed each tick") {
    EntityRegistry reg;
    Field grid(FIELD_WORLD);

    addEntity(grid, reg, EntityType::Battery, {0, 0});
    addPuddle(grid, reg, {1, 0});

    tickVoltage(grid, reg);
    CHECK(grid.voltage.count({1, 0}));

    // Replace puddle with grass — voltage should disappear next tick.
    removePuddle(grid, reg, {1, 0});
    tickVoltage(grid, reg);
    CHECK(!grid.voltage.count({1, 0}));
}

// ─── burning flag ─────────────────────────────────────────────────────────────

TEST_CASE("entity burning flag is false before ignition threshold") {
    EntityRegistry reg;
    Field grid(FIELD_WORLD);

    addEntity(grid, reg, EntityType::Campfire,  {0, 0});
    EntityID stump = addEntity(grid, reg, EntityType::TreeStump, {1, 0});

    for (Tick t = 0; t < 249; ++t)
        tickFire(grid, reg, t);

    CHECK(reg.get(stump)->burning == false);
}

TEST_CASE("entity burning flag is true after ignition") {
    EntityRegistry reg;
    Field grid(FIELD_WORLD);

    addEntity(grid, reg, EntityType::Campfire,  {0, 0});
    EntityID stump = addEntity(grid, reg, EntityType::TreeStump, {1, 0});

    for (Tick t = 0; t < 250; ++t)
        tickFire(grid, reg, t);

    REQUIRE(reg.get(stump) != nullptr);
    CHECK(reg.get(stump)->burning == true);
}

TEST_CASE("burning flag persists on subsequent ticks") {
    EntityRegistry reg;
    Field grid(FIELD_WORLD);

    EntityID stump = addEntity(grid, reg, EntityType::TreeStump, {0, 0});
    grid.entityBurnEnd[stump] = 9999;
    reg.get(stump)->burning = true;

    // Run several ticks well before burn-end — entity stays alive and burning.
    for (Tick t = 0; t < 10; ++t)
        tickFire(grid, reg, t);

    REQUIRE(reg.get(stump) != nullptr);
    CHECK(reg.get(stump)->burning == true);
}

// ─── electrified flag ─────────────────────────────────────────────────────────

TEST_CASE("entity on uncharged grass is not electrified") {
    EntityRegistry reg;
    Field grid(FIELD_WORLD);

    EntityID goblin = addEntity(grid, reg, EntityType::Goblin, {3, 3});
    tickVoltage(grid, reg);
    CHECK(reg.get(goblin)->electrified == false);
}

TEST_CASE("entity on charged puddle is electrified") {
    EntityRegistry reg;
    Field grid(FIELD_WORLD);

    addEntity(grid, reg, EntityType::Battery, {0, 0});
    addPuddle(grid, reg, {1, 0});
    EntityID goblin = addEntity(grid, reg, EntityType::Goblin, {1, 0});

    tickVoltage(grid, reg);

    CHECK(reg.get(goblin)->electrified == true);
}

TEST_CASE("entity on uncharged puddle (out of range) is not electrified") {
    EntityRegistry reg;
    Field grid(FIELD_WORLD);

    addEntity(grid, reg, EntityType::Battery, {0, 0});
    for (int x = 1; x <= 5; ++x)
        addPuddle(grid, reg, {x, 0});
    EntityID goblin = addEntity(grid, reg, EntityType::Goblin, {5, 0});

    tickVoltage(grid, reg);

    CHECK(reg.get(goblin)->electrified == false);
}

TEST_CASE("electrified flag clears when battery is removed") {
    EntityRegistry reg;
    Field grid(FIELD_WORLD);

    EntityID bat    = addEntity(grid, reg, EntityType::Battery, {0, 0});
    addPuddle(grid, reg, {1, 0});
    EntityID goblin = addEntity(grid, reg, EntityType::Goblin, {1, 0});

    tickVoltage(grid, reg);
    CHECK(reg.get(goblin)->electrified == true);

    Entity* b = reg.get(bat);
    grid.remove(bat, *b);
    reg.destroy(bat);

    tickVoltage(grid, reg);
    CHECK(reg.get(goblin)->electrified == false);
}

TEST_CASE("lightbulb uses lit flag, not electrified") {
    EntityRegistry reg;
    Field grid(FIELD_WORLD);

    addEntity(grid, reg, EntityType::Battery, {0, 0});
    addPuddle(grid, reg, {1, 0});
    EntityID bulb = addEntity(grid, reg, EntityType::Lightbulb, {1, 0});

    tickVoltage(grid, reg);

    CHECK(reg.get(bulb)->lit         == true);
    CHECK(reg.get(bulb)->electrified == false);  // Lightbulb is exempt
}
