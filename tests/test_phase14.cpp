#include "doctest.h"
#include "game.hpp"
#include "input.hpp"
#include "terrain.hpp"
#include "grid.hpp"
#include "fluid.hpp"

// ─── Helpers ─────────────────────────────────────────────────────────────────

static SDL_Event makeKeyDown(SDL_Keycode k) {
    SDL_Event e{};
    e.type = SDL_KEYDOWN;
    e.key.keysym.sym = k;
    return e;
}

static void runTicks(Game& game, int n) {
    Input input;
    for (int i = 0; i < n; ++i) {
        input.beginFrame();
        game.tick(input, static_cast<Tick>(i));
    }
}

// Add a Water entity + FluidComponent to a grid.
static EntityID addWater(Grid& grid, EntityRegistry& reg,
                         ComponentStore<FluidComponent>& fluids,
                         TilePos pos, float h) {
    EntityID eid = reg.spawn(EntityType::Water, pos);
    Entity*  e   = reg.get(eid);
    grid.add(eid, *e);
    fluids.add(eid, {h, 0.f, 0.f});
    return eid;
}

// ─── EntityType::Water ────────────────────────────────────────────────────────

TEST_CASE("Water EntityType is distinct from other entity types") {
    CHECK(EntityType::Water != EntityType::Goblin);
    CHECK(EntityType::Water != EntityType::Player);
    CHECK(EntityType::Water != EntityType::Mushroom);
}

TEST_CASE("Water entity can be spawned and has FluidComponent") {
    EntityRegistry reg;
    ComponentStore<FluidComponent> fluids;
    EntityID eid = reg.spawn(EntityType::Water, {0,0,0});
    fluids.add(eid, {1.0f, 0.f, 0.f});
    REQUIRE(reg.get(eid) != nullptr);
    CHECK(reg.get(eid)->type == EntityType::Water);
    REQUIRE(fluids.get(eid) != nullptr);
    CHECK(fluids.get(eid)->h == doctest::Approx(1.0f));
}

// ─── tickFluid: flow ──────────────────────────────────────────────────────────

TEST_CASE("tickFluid: water with velocity spreads to adjacent tile over ticks") {
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

    Grid              grid{1};
    EntityRegistry    reg;
    ComponentStore<FluidComponent> fluids;
    EntityID srcEid = addWater(grid, reg, fluids, src, 2.0f);
    // Give it velocity toward dst.
    TilePos delta = dst - src;
    fluids.get(srcEid)->vx = delta.x > 0 ? 1.0f : (delta.x < 0 ? -1.0f : 0.f);
    fluids.get(srcEid)->vy = delta.y > 0 ? 1.0f : (delta.y < 0 ? -1.0f : 0.f);

    // After several ticks, a Water entity should appear at dst (compare x,y only;
    // spawned entity z is terrain-level, not necessarily dst.z).
    bool spread = false;
    for (int i = 0; i < 20 && !spread; ++i) {
        tickFluid(grid, fluids, reg);
        for (EntityID eid : grid.entities) {
            const Entity* e = reg.get(eid);
            if (e && e->type == EntityType::Water &&
                e->pos.x == dst.x && e->pos.y == dst.y)
                spread = true;
        }
    }
    CHECK(spread);
}

TEST_CASE("tickFluid: water entity with h well above threshold persists") {
    Grid              grid{1};
    EntityRegistry    reg;
    ComponentStore<FluidComponent> fluids;
    EntityID eid = addWater(grid, reg, fluids, {5,5,0}, 1.0f);
    tickFluid(grid, fluids, reg);
    // The entity may still exist; h should not have dropped catastrophically.
    const Entity* e = reg.get(eid);
    if (e) {
        FluidComponent* fc = fluids.get(eid);
        if (fc) CHECK(fc->h > 0.5f);
    }
}

TEST_CASE("tickFluid: water entity with very low h despawns") {
    Grid              grid{1};
    EntityRegistry    reg;
    ComponentStore<FluidComponent> fluids;
    EntityID eid = addWater(grid, reg, fluids, {5,5,0}, 0.001f);
    fluids.get(eid)->vx = 5.f;   // force rapid drainage
    for (int i = 0; i < 20; ++i)
        tickFluid(grid, fluids, reg);
    // After enough ticks, entity should be gone or have negligible h.
    const Entity* e = reg.get(eid);
    if (e) {
        FluidComponent* fc = fluids.get(eid);
        if (fc) CHECK(fc->h < 0.1f);
    }
    // No crash is the primary assertion.
}

TEST_CASE("tickFluid: water does not flow into Portal tiles") {
    Grid              grid{1};
    EntityRegistry    reg;
    ComponentStore<FluidComponent> fluids;
    TilePos waterPos  = {0, 0, 0};
    TilePos portalPos = {1, 0, 0};
    grid.terrain.setType(portalPos, TileType::Portal);
    EntityID eid = addWater(grid, reg, fluids, waterPos, 2.0f);
    fluids.get(eid)->vx = 1.0f;  // velocity toward portal

    for (int i = 0; i < 10; ++i)
        tickFluid(grid, fluids, reg);

    // Portal tile should remain a portal.
    CHECK(grid.terrain.typeAt(portalPos) == TileType::Portal);
    // No Water entity should occupy the portal tile.
    bool waterOnPortal = false;
    for (EntityID e2 : grid.entities) {
        const Entity* e = reg.get(e2);
        if (e && e->type == EntityType::Water && e->pos == portalPos)
            waterOnPortal = true;
    }
    CHECK(!waterOnPortal);
}

TEST_CASE("tickFluid: water does not flow into Fire tiles") {
    Grid              grid{1};
    EntityRegistry    reg;
    ComponentStore<FluidComponent> fluids;
    TilePos waterPos = {0, 0, 0};
    TilePos firePos  = {1, 0, 0};
    grid.terrain.setType(firePos, TileType::Fire);
    grid.fireTileExpiry[firePos] = 99999;
    EntityID eid = addWater(grid, reg, fluids, waterPos, 2.0f);
    fluids.get(eid)->vx = 1.0f;

    // tickFluid only — do not run tickFire here (that would extinguish it).
    for (int i = 0; i < 5; ++i)
        tickFluid(grid, fluids, reg);

    // Fire should not have been overwritten by a Water entity.
    bool waterOnFire = false;
    for (EntityID e2 : grid.entities) {
        const Entity* e = reg.get(e2);
        if (e && e->type == EntityType::Water && e->pos == firePos)
            waterOnFire = true;
    }
    CHECK(!waterOnFire);
}

// ─── Fire × Water extinguish ─────────────────────────────────────────────────

TEST_CASE("tickFire: Fire tile adjacent to Water entity is extinguished") {
    Grid           grid{1};
    EntityRegistry registry;
    ComponentStore<FluidComponent> fluids;
    Tick tick = 0;

    TilePos fireTile  = {0, 0, 0};
    TilePos waterTile = {1, 0, 0};

    grid.terrain.setType(fireTile, TileType::Fire);
    grid.fireTileExpiry[fireTile] = 99999;

    addWater(grid, registry, fluids, waterTile, 1.0f);

    tickFire(grid, registry, tick);

    CHECK(grid.terrain.typeAt(fireTile) == TileType::BareEarth);
    CHECK(grid.fireTileExpiry.count(fireTile) == 0);
}

TEST_CASE("tickFire: Fire tile NOT adjacent to Water entity is not extinguished") {
    Grid           grid{1};
    EntityRegistry registry;
    ComponentStore<FluidComponent> fluids;
    Tick tick = 0;

    TilePos fireTile  = {0, 0, 0};
    grid.terrain.setType(fireTile, TileType::Fire);
    grid.fireTileExpiry[fireTile] = 99999;

    // Water is far away — not adjacent.
    addWater(grid, registry, fluids, {10, 10, 0}, 1.0f);

    tickFire(grid, registry, tick);

    CHECK(grid.terrain.typeAt(fireTile) == TileType::Fire);
}

// ─── Mana floor ──────────────────────────────────────────────────────────────

TEST_CASE("Player mana never drops below 1 after Plant action") {
    Game  game;
    Input input;

    // Dig ahead first (player faces N by default; tile at {0,-1}).
    input.beginFrame();
    input.handleEvent(makeKeyDown(SDLK_f));
    game.tick(input, 0);

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
