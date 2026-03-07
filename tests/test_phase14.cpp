#include "doctest.h"
#include "game.hpp"
#include "input.hpp"
#include "terrain.hpp"
#include "grid.hpp"

// ─── Helpers ─────────────────────────────────────────────────────────────────

static SDL_Event makeKeyDown(SDL_Keycode k) {
    SDL_Event e{};
    e.type = SDL_KEYDOWN;
    e.key.keysym.sym = k;
    return e;
}

// Run N game ticks with blank input.
static void runTicks(Game& game, int n) {
    Input input;
    for (int i = 0; i < n; ++i) {
        input.beginFrame();
        game.tick(input, static_cast<Tick>(i));
    }
}

// ─── Water tile type ─────────────────────────────────────────────────────────

TEST_CASE("Water tile type is a distinct TileType value") {
    CHECK(TileType::Water != TileType::Puddle);
    CHECK(TileType::Water != TileType::Grass);
    CHECK(TileType::Water != TileType::Fire);
}

TEST_CASE("Terrain setType can set and read Water") {
    Terrain t;
    TilePos p{0, 0, 0};
    t.setType(p, TileType::Water);
    CHECK(t.typeAt(p) == TileType::Water);
}

// ─── tickWater: flow ─────────────────────────────────────────────────────────

TEST_CASE("tickWater: water expands to adjacent same-level tile in one tick") {
    // Find two adjacent tiles at the same terrain level.
    Terrain ref;
    const TilePos kDirs4[] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0}};
    TilePos src{0,0,0}, dst{0,0,0};
    bool found = false;
    for (int x = -20; x <= 20 && !found; ++x) {
        for (int y = -20; y <= 20 && !found; ++y) {
            TilePos s{x, y, 0};
            int sl = ref.levelAt(s);
            for (const auto& d : kDirs4) {
                TilePos d2 = s + d;
                if (ref.levelAt(d2) == sl) { src = s; dst = d2; found = true; break; }
            }
        }
    }
    REQUIRE_MESSAGE(found, "could not find adjacent same-level tiles in scan");

    Grid grid{1};
    grid.terrain.setType(src, TileType::Water);
    grid.waterLevel[src] = 1.0f;
    tickWater(grid);
    // After one tick, water should have spread to the neighbour.
    CHECK(grid.terrain.typeAt(dst) == TileType::Water);
}

TEST_CASE("tickWater: high-level water stays Water after one tick") {
    // A tile with level 1.0 spreading to 1 neighbour keeps 0.5 — above threshold.
    Grid grid{1};
    TilePos p{5, 5, 0};
    grid.terrain.setType(p, TileType::Water);
    grid.waterLevel[p] = 1.0f;
    tickWater(grid);
    CHECK(grid.terrain.typeAt(p) == TileType::Water);
}

TEST_CASE("tickWater: low-level water converts to Puddle") {
    // A tile with level 0.05 (below 0.1 threshold) should convert to Puddle
    // without spreading.
    Grid grid{1};
    TilePos p{5, 5, 0};
    grid.terrain.setType(p, TileType::Water);
    grid.waterLevel[p] = 0.05f;
    tickWater(grid);
    CHECK(grid.terrain.typeAt(p) == TileType::Puddle);
    CHECK(grid.waterLevel.count(p) == 0);
}

TEST_CASE("tickWater: total volume is conserved after one tick") {
    // Seed a single water tile at level 1.0 in an area with same-level neighbours.
    Terrain ref;
    const TilePos kDirs4[] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0}};
    TilePos src{0,0,0};
    bool found = false;
    for (int x = -20; x <= 20 && !found; ++x) {
        for (int y = -20; y <= 20 && !found; ++y) {
            TilePos s{x, y, 0};
            int sl = ref.levelAt(s);
            int count = 0;
            for (const auto& d : kDirs4)
                if (ref.levelAt(s + d) == sl) ++count;
            if (count >= 1) { src = s; found = true; }
        }
    }
    REQUIRE(found);

    Grid grid{1};
    grid.terrain.setType(src, TileType::Water);
    grid.waterLevel[src] = 1.0f;
    tickWater(grid);

    float total = 0.0f;
    for (const auto& [pos, lvl] : grid.waterLevel) total += lvl;
    CHECK(total == doctest::Approx(1.0f).epsilon(0.001f));
}

TEST_CASE("tickWater: water does not overwrite Portal tiles") {
    Grid grid{1};
    TilePos water{0, 0, 0};
    TilePos portal{1, 0, 0};
    grid.terrain.setType(water,  TileType::Water);
    grid.waterLevel[water] = 1.0f;
    grid.terrain.setType(portal, TileType::Portal);

    tickWater(grid);

    CHECK(grid.terrain.typeAt(portal) == TileType::Portal);
}

TEST_CASE("tickWater: water does not overwrite Fire tiles") {
    Grid grid{1};
    TilePos water{0, 0, 0};
    TilePos fire{1, 0, 0};
    grid.terrain.setType(water, TileType::Water);
    grid.waterLevel[water] = 1.0f;
    grid.terrain.setType(fire,  TileType::Fire);

    tickWater(grid);

    CHECK(grid.terrain.typeAt(fire) == TileType::Fire);
}

// ─── Fire x Water extinguish ─────────────────────────────────────────────────

TEST_CASE("tickFire: Fire tile adjacent to Water is extinguished") {
    Grid grid{1};
    EntityRegistry registry;
    Tick tick = 0;

    TilePos fireTile{0, 0, 0};
    TilePos waterTile{1, 0, 0};

    // Plant a fire tile with a far-future expiry so it won't expire on its own.
    grid.terrain.setType(fireTile, TileType::Fire);
    grid.fireTileExpiry[fireTile] = 99999;

    grid.terrain.setType(waterTile, TileType::Water);

    tickFire(grid, registry, tick);

    CHECK(grid.terrain.typeAt(fireTile) == TileType::BareEarth);
    CHECK(grid.fireTileExpiry.count(fireTile) == 0);
}

TEST_CASE("tickFire: Fire tile NOT adjacent to Water is not prematurely extinguished") {
    Grid grid{1};
    EntityRegistry registry;
    Tick tick = 0;

    TilePos fireTile{0, 0, 0};
    grid.terrain.setType(fireTile, TileType::Fire);
    grid.fireTileExpiry[fireTile] = 99999;

    // Water is far away — not adjacent.
    grid.terrain.setType({10, 10, 0}, TileType::Water);

    tickFire(grid, registry, tick);

    CHECK(grid.terrain.typeAt(fireTile) == TileType::Fire);
}

// ─── Mana floor ──────────────────────────────────────────────────────────────

TEST_CASE("Player mana never drops below 1 after Plant action") {
    Game  game;
    Input input;

    // Give player exactly 1 mana, dig a tile ahead to get BareEarth, then plant.
    // Mana should not go below 1.

    // Dig ahead first (player faces N by default; tile at {0,-1}).
    input.beginFrame();
    input.handleEvent(makeKeyDown(SDLK_f));
    game.tick(input, 0);

    // Now plant — player has some mana (starts with 5 by default or similar).
    // Burn mana down by planting repeatedly. The floor should hold at 1.
    for (int i = 0; i < 20; ++i) {
        input.beginFrame();
        input.handleEvent(makeKeyDown(SDLK_c));
        game.tick(input, static_cast<Tick>(i + 1));
        CHECK(game.playerMana() >= 1);
    }
}

TEST_CASE("Player mana is at least 1 at game start") {
    Game game;
    CHECK(game.playerMana() >= 1);
}
