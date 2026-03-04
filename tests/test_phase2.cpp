#include "doctest.h"
#include "entity.hpp"
#include "input.hpp"

// ─── Helpers ─────────────────────────────────────────────────────────────────

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

// ─── EntityRegistry ──────────────────────────────────────────────────────────

TEST_CASE("spawn returns a valid non-zero ID") {
    EntityRegistry reg;
    EntityID id = reg.spawn(EntityType::Player, {0, 0});
    CHECK(id != INVALID_ENTITY);
}

TEST_CASE("spawn returns unique IDs") {
    EntityRegistry reg;
    EntityID a = reg.spawn(EntityType::Player,   {0, 0});
    EntityID b = reg.spawn(EntityType::Goblin,   {1, 0});
    EntityID c = reg.spawn(EntityType::Mushroom, {2, 0});
    CHECK(a != b);
    CHECK(b != c);
    CHECK(a != c);
}

TEST_CASE("get returns correct entity after spawn") {
    EntityRegistry reg;
    EntityID id = reg.spawn(EntityType::Goblin, {3, 4});
    const Entity* e = reg.get(id);
    REQUIRE(e != nullptr);
    CHECK(e->type == EntityType::Goblin);
    CHECK(e->pos  == TilePos{3, 4});
}

TEST_CASE("get returns nullptr for unknown ID") {
    EntityRegistry reg;
    CHECK(reg.get(999) == nullptr);
}

TEST_CASE("destroy removes entity; get returns nullptr afterwards") {
    EntityRegistry reg;
    EntityID id = reg.spawn(EntityType::Player, {0, 0});
    REQUIRE(reg.get(id) != nullptr);
    reg.destroy(id);
    CHECK(reg.get(id) == nullptr);
}

TEST_CASE("destroy on invalid ID is a no-op") {
    EntityRegistry reg;
    reg.destroy(999);   // should not throw
}

TEST_CASE("drawOrder sorts entities by layer ascending") {
    EntityRegistry reg;
    // Mushroom layer=2, Goblin layer=1, Player layer=0
    reg.spawn(EntityType::Mushroom, {0, 0});
    reg.spawn(EntityType::Goblin,   {1, 0});
    reg.spawn(EntityType::Player,   {2, 0});

    auto ordered = reg.drawOrder();
    REQUIRE(ordered.size() == 3);
    CHECK(ordered[0]->layer <= ordered[1]->layer);
    CHECK(ordered[1]->layer <= ordered[2]->layer);
}

// ─── Entity defaults ─────────────────────────────────────────────────────────

TEST_CASE("spawned entity starts idle") {
    EntityRegistry reg;
    EntityID id = reg.spawn(EntityType::Player, {0, 0});
    CHECK(reg.get(id)->isIdle());
    CHECK(!reg.get(id)->isMoving());
}

TEST_CASE("spawned entity starts with correct position") {
    EntityRegistry reg;
    EntityID id = reg.spawn(EntityType::Player, {5, -3});
    const Entity* e = reg.get(id);
    CHECK(e->pos         == TilePos{5, -3});
    CHECK(e->destination == TilePos{5, -3});
    CHECK(e->moveT       == doctest::Approx(0.0f));
}

// ─── stepMovement ────────────────────────────────────────────────────────────

TEST_CASE("stepMovement does nothing when entity is idle") {
    EntityRegistry reg;
    EntityID id = reg.spawn(EntityType::Player, {0, 0});
    Entity* e = reg.get(id);
    bool arrived = stepMovement(*e);
    CHECK(!arrived);
    CHECK(e->isIdle());
    CHECK(e->moveT == doctest::Approx(0.0f));
}

TEST_CASE("stepMovement advances moveT each tick") {
    EntityRegistry reg;
    EntityID id = reg.spawn(EntityType::Player, {0, 0});
    Entity* e = reg.get(id);
    e->destination = {1, 0};

    stepMovement(*e);
    CHECK(e->moveT == doctest::Approx(e->speed));
    CHECK(e->isMoving());
}

TEST_CASE("stepMovement arrives after expected number of ticks") {
    EntityRegistry reg;
    EntityID id = reg.spawn(EntityType::Player, {0, 0});
    Entity* e = reg.get(id);
    e->destination = {1, 0};

    // speed = 0.1 → should arrive within 10–11 ticks
    int ticks = 0;
    while (e->isMoving() && ticks < 20) {
        stepMovement(*e);
        ticks++;
    }

    CHECK(ticks >= 10);
    CHECK(ticks <= 11);
    CHECK(e->isIdle());
    CHECK(e->pos == TilePos{1, 0});
    CHECK(e->moveT == doctest::Approx(0.0f));
}

TEST_CASE("stepMovement returns true exactly on arrival tick") {
    EntityRegistry reg;
    EntityID id = reg.spawn(EntityType::Player, {0, 0});
    Entity* e = reg.get(id);
    e->speed       = 0.5f;   // arrives in 2 ticks
    e->destination = {1, 0};

    bool arrived = stepMovement(*e);  // tick 1: moveT = 0.5
    CHECK(!arrived);
    arrived = stepMovement(*e);       // tick 2: moveT >= 1.0 → arrived
    CHECK(arrived);
    CHECK(e->isIdle());
}

// ─── Input ───────────────────────────────────────────────────────────────────

TEST_CASE("held() is true while key is down") {
    Input input;
    input.beginFrame();
    input.handleEvent(makeKeyDown(SDLK_w));
    CHECK(input.held(Action::MoveUp));
}

TEST_CASE("held() is false before key is pressed") {
    Input input;
    input.beginFrame();
    CHECK(!input.held(Action::MoveUp));
}

TEST_CASE("held() is false after key is released") {
    Input input;
    input.beginFrame();
    input.handleEvent(makeKeyDown(SDLK_w));
    input.beginFrame();
    input.handleEvent(makeKeyUp(SDLK_w));
    CHECK(!input.held(Action::MoveUp));
}

TEST_CASE("pressed() is true only on the first frame a key is down") {
    Input input;

    // Frame 1: key goes down
    input.beginFrame();
    input.handleEvent(makeKeyDown(SDLK_w));
    CHECK(input.pressed(Action::MoveUp));

    // Frame 2: key still held, no new event
    input.beginFrame();
    CHECK(!input.pressed(Action::MoveUp));
    CHECK(input.held(Action::MoveUp));
}

TEST_CASE("pressed() ignores key-repeat events") {
    Input input;
    input.beginFrame();
    input.handleEvent(makeKeyDown(SDLK_w));

    // Simulate OS key-repeat: SDL_KEYDOWN with repeat != 0
    SDL_Event repeat{};
    repeat.type           = SDL_KEYDOWN;
    repeat.key.keysym.sym = SDLK_w;
    repeat.key.repeat     = 1;

    input.beginFrame();
    input.handleEvent(repeat);
    CHECK(!input.pressed(Action::MoveUp));
    CHECK(input.held(Action::MoveUp));
}

TEST_CASE("released() is true only on the frame the key goes up") {
    Input input;

    input.beginFrame();
    input.handleEvent(makeKeyDown(SDLK_s));

    input.beginFrame();
    input.handleEvent(makeKeyUp(SDLK_s));
    CHECK(input.released(Action::MoveDown));

    // Released fires only once
    input.beginFrame();
    CHECK(!input.released(Action::MoveDown));
}

TEST_CASE("multiple keys are tracked independently") {
    Input input;
    input.beginFrame();
    input.handleEvent(makeKeyDown(SDLK_w));
    input.handleEvent(makeKeyDown(SDLK_d));

    CHECK(input.held(Action::MoveUp));
    CHECK(input.held(Action::MoveRight));
    CHECK(!input.held(Action::MoveLeft));

    input.beginFrame();
    input.handleEvent(makeKeyUp(SDLK_w));

    CHECK(!input.held(Action::MoveUp));
    CHECK(input.held(Action::MoveRight));
    CHECK(input.released(Action::MoveUp));
    CHECK(!input.released(Action::MoveRight));
}

// ─── toDirection ─────────────────────────────────────────────────────────────

TEST_CASE("toDirection maps cardinal deltas correctly") {
    CHECK(toDirection({0, -1}) == Direction::N);
    CHECK(toDirection({0,  1}) == Direction::S);
    CHECK(toDirection({-1, 0}) == Direction::W);
    CHECK(toDirection({1,  0}) == Direction::E);
}

TEST_CASE("toDirection maps diagonal deltas correctly") {
    CHECK(toDirection({1, -1})  == Direction::NE);
    CHECK(toDirection({1,  1})  == Direction::SE);
    CHECK(toDirection({-1, 1})  == Direction::SW);
    CHECK(toDirection({-1, -1}) == Direction::NW);
}

// ─── Shift / strafe ──────────────────────────────────────────────────────────

static SDL_Event makeKeyDown(SDL_Keycode key);  // defined above

TEST_CASE("held(Action::Strafe) is true when left shift is down") {
    Input input;
    input.beginFrame();
    input.handleEvent(makeKeyDown(SDLK_LSHIFT));
    CHECK(input.held(Action::Strafe));
}

TEST_CASE("held(Action::Strafe) is true when right shift is down") {
    Input input;
    input.beginFrame();
    input.handleEvent(makeKeyDown(SDLK_RSHIFT));
    CHECK(input.held(Action::Strafe));
}

// Mirrors the strafe logic from main.cpp: facing only updates when shift is not held.
static Direction applyFacing(Direction current, TilePos delta, bool shiftHeld) {
    if (!shiftHeld) return toDirection(delta);
    return current;
}

TEST_CASE("facing updates to match movement direction without shift") {
    CHECK(applyFacing(Direction::N, {1,  0}, false) == Direction::E);
    CHECK(applyFacing(Direction::N, {-1, 0}, false) == Direction::W);
    CHECK(applyFacing(Direction::N, {0,  1}, false) == Direction::S);
    CHECK(applyFacing(Direction::N, {0, -1}, false) == Direction::N);
}

TEST_CASE("facing unchanged when strafing with shift held") {
    static const TilePos dirs[] = {{1,0},{-1,0},{0,1},{0,-1}};
    for (auto& d : dirs)
        CHECK(applyFacing(Direction::N, d, true) == Direction::N);
}

TEST_CASE("strafe preserves any initial facing, not just north") {
    CHECK(applyFacing(Direction::E, {0, 1}, true) == Direction::E);
    CHECK(applyFacing(Direction::W, {1, 0}, true) == Direction::W);
    CHECK(applyFacing(Direction::S, {-1, 0}, true) == Direction::S);
}
