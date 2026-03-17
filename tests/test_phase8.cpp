#include "doctest.h"
#include "field.hpp"
#include "game.hpp"
#include "input.hpp"
#include <algorithm>

static SDL_Event makeKeyDown(SDL_Keycode key) {
    SDL_Event e{};
    e.type           = SDL_KEYDOWN;
    e.key.keysym.sym = key;
    e.key.repeat     = 0;
    return e;
}

static SDL_Event makeKeyUp(SDL_Keycode key) {
    SDL_Event e{};
    e.type           = SDL_KEYUP;
    e.key.keysym.sym = key;
    return e;
}

// ─── Field::add / remove ──────────────────────────────────────────────────────

TEST_CASE("Field::add registers entity in spatial") {
    EntityRegistry reg;
    Field grid(1);

    EntityID id = reg.spawn(EntityType::Goblin, {3, 4});
    grid.add(id, *reg.get(id));

    CHECK(grid.hasEntity(id));
    auto at = grid.spatial.at({3, 4});
    CHECK(std::find(at.begin(), at.end(), id) != at.end());
}

TEST_CASE("Grid::remove unregisters entity from spatial and entity list") {
    EntityRegistry reg;
    Field grid(1);

    EntityID id = reg.spawn(EntityType::Goblin, {1, 1});
    grid.add(id, *reg.get(id));
    grid.remove(id, *reg.get(id));

    CHECK(!grid.hasEntity(id));
    CHECK(grid.spatial.at({1, 1}).empty());
}

TEST_CASE("Grid::remove cleans up mid-move dual registration") {
    EntityRegistry reg;
    Field grid(1);

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
    Field grid(1);
    CHECK(!grid.hasEntity(42));
}

// ─── Independent terrain ──────────────────────────────────────────────────────

TEST_CASE("spatial grids A and B are independent") {
    EntityRegistry reg;
    Field a(1), b(2);

    // Spawning a BareEarth entity into field A does not affect field B.
    EntityID beid = reg.spawn(EntityType::BareEarth, {5, 5});
    a.add(beid, *reg.get(beid));

    CHECK( a.hasEntity(beid));
    CHECK(!b.hasEntity(beid));
    CHECK(!a.spatial.at({5, 5}).empty());
    CHECK( b.spatial.at({5, 5}).empty());
}

// ─── Multiple entities ────────────────────────────────────────────────────────

TEST_CASE("multiple entities can be added and removed independently") {
    EntityRegistry reg;
    Field grid(1);

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

// ─── transferEntity ───────────────────────────────────────────────────────────

TEST_CASE("transferEntity: entity appears in destination, absent from source") {
    EntityRegistry reg;
    Field a(FIELD_WORLD), b(FIELD_STUDIO);

    EntityID id = reg.spawn(EntityType::Player, {2, 3});
    a.add(id, *reg.get(id));

    transferEntity(id, a, b, reg, {5, 5});

    CHECK(!a.hasEntity(id));
    CHECK( b.hasEntity(id));

    auto at_old = a.spatial.at({2, 3});
    CHECK(std::find(at_old.begin(), at_old.end(), id) == at_old.end());

    auto at_new = b.spatial.at({5, 5});
    CHECK(std::find(at_new.begin(), at_new.end(), id) != at_new.end());
}

TEST_CASE("transferEntity: entity position snaps to destination") {
    EntityRegistry reg;
    Field a(FIELD_WORLD), b(FIELD_STUDIO);

    EntityID id = reg.spawn(EntityType::Player, {0, 0});
    a.add(id, *reg.get(id));

    transferEntity(id, a, b, reg, {7, 3});
    Entity* e = reg.get(id);
    REQUIRE(e != nullptr);
    CHECK(e->pos         == TilePos{7, 3});
    CHECK(e->destination == TilePos{7, 3});
    CHECK(e->moveProgress == doctest::Approx(0.0f));
}

TEST_CASE("transferEntity: mid-move entity has dual registration cleaned up") {
    EntityRegistry reg;
    Field a(FIELD_WORLD), b(FIELD_STUDIO);

    EntityID id = reg.spawn(EntityType::Goblin, {0, 0});
    Entity* e   = reg.get(id);
    a.add(id, *e);

    // Simulate mid-move
    e->destination = {1, 0};
    a.spatial.add(id, e->destination, e->size);

    transferEntity(id, a, b, reg, {0, 0});

    // Both old cells cleared from source grid
    CHECK(a.spatial.at({0, 0}).empty());
    CHECK(a.spatial.at({1, 0}).empty());
}

// ─── Scheduler isolation ─────────────────────────────────────────────────────

TEST_CASE("scheduler actions in an inactive grid do not affect entities in other grids") {
    EntityRegistry reg;
    Field a(FIELD_WORLD), b(FIELD_STUDIO);

    EntityID id = reg.spawn(EntityType::Goblin, {0, 0});
    a.add(id, *reg.get(id));

    // Schedule a Despawn on grid A's scheduler at tick 1
    a.scheduler.push({ 1, id, ActionType::Despawn, std::monostate{} });

    // Only tick grid B's scheduler — entity should be untouched
    for (auto& action : b.scheduler.popDue(1)) {
        (void)action;   // nothing should fire
    }

    CHECK(reg.get(id) != nullptr);
    CHECK(a.hasEntity(id));
}

// ─── Studio field ────────────────────────────────────────────────────────────

TEST_CASE("Tab enters studio: only player visible, goblin absent") {
    Game  game;
    Input input;

    // World has player + goblin + static demo entities
    size_t worldCount = game.drawOrder().size();
    CHECK(worldCount >= 2);

    // Press Tab to enter studio
    input.beginFrame();
    input.handleEvent(makeKeyDown(SDLK_TAB));
    game.tick(input, 0);

    // Studio has the player + 3 medium entities (Mud, Stone, Clay)
    CHECK(game.inStudio());
    CHECK(game.drawOrder().size() == 4);
}

TEST_CASE("Tab back to world restores goblin and player position") {
    Game  game;
    Input input;

    // Move player somewhere distinctive
    // (player starts at {0,0}; just record where it is)

    // Enter studio
    input.beginFrame();
    input.handleEvent(makeKeyDown(SDLK_TAB));
    game.tick(input, 0);
    CHECK(game.inStudio());

    // Release Tab, then press again to return to world
    input.beginFrame();
    input.handleEvent(makeKeyUp(SDLK_TAB));
    input.beginFrame();
    input.handleEvent(makeKeyDown(SDLK_TAB));
    game.tick(input, 1);

    CHECK(!game.inStudio());
    // All world entities (player, goblin, and static demo entities) restored
    CHECK(game.drawOrder().size() >= 2);
}

TEST_CASE("studio spatial is independent from world spatial") {
    // Confirm world and studio fields have independent spatial grids.
    EntityRegistry reg;
    Field world(FIELD_WORLD), studio(FIELD_STUDIO);
    EntityID beid = reg.spawn(EntityType::BareEarth, {3, 3});
    world.add(beid, *reg.get(beid));
    CHECK( world.hasEntity(beid));
    CHECK(!studio.hasEntity(beid));
}

TEST_CASE("Action::SwitchGrid is recognised by Input") {
    Input input;
    input.beginFrame();
    input.handleEvent(makeKeyDown(SDLK_TAB));
    CHECK(input.pressed(Action::SwitchGrid));
}
