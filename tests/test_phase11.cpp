#include "doctest.h"
#include "routine.hpp"
#include "game.hpp"
#include "input.hpp"

// ─── instrCost ───────────────────────────────────────────────────────────────

TEST_CASE("instrCost: MOVE_REL Forward costs 1") {
    CHECK(instrCost(OpCode::MOVE_REL, RelDir::Forward) == 1);
}

TEST_CASE("instrCost: MOVE_REL Left costs 1") {
    CHECK(instrCost(OpCode::MOVE_REL, RelDir::Left) == 1);
}

TEST_CASE("instrCost: MOVE_REL Right costs 1") {
    CHECK(instrCost(OpCode::MOVE_REL, RelDir::Right) == 1);
}

TEST_CASE("instrCost: MOVE_REL Back costs 2") {
    CHECK(instrCost(OpCode::MOVE_REL, RelDir::Back) == 2);
}

TEST_CASE("instrCost: WAIT costs 0") {
    CHECK(instrCost(OpCode::WAIT) == 0);
}

TEST_CASE("instrCost: HALT costs 0") {
    CHECK(instrCost(OpCode::HALT) == 0);
}

TEST_CASE("instrCost: DIG costs 3") {
    CHECK(instrCost(OpCode::DIG) == 3);
}

TEST_CASE("instrCost: PLANT costs 2") {
    CHECK(instrCost(OpCode::PLANT) == 2);
}

TEST_CASE("instrCost: JUMP costs 0") {
    CHECK(instrCost(OpCode::JUMP) == 0);
}

TEST_CASE("instrCost: JUMP_IF costs 0") {
    CHECK(instrCost(OpCode::JUMP_IF) == 0);
}

TEST_CASE("instrCost: CALL costs 0") {
    CHECK(instrCost(OpCode::CALL) == 0);
}

TEST_CASE("instrCost: RET costs 0") {
    CHECK(instrCost(OpCode::RET) == 0);
}

// ─── Recording::manaCost ─────────────────────────────────────────────────────

TEST_CASE("manaCost: empty recording costs 0") {
    Recording rec;
    CHECK(rec.manaCost() == 0);
}

TEST_CASE("manaCost: HALT-only recording costs 0") {
    Recording rec;
    rec.instructions = { {.op = OpCode::HALT} };
    CHECK(rec.manaCost() == 0);
}

TEST_CASE("manaCost: one MOVE_REL Forward costs 1") {
    Recording rec;
    rec.instructions = {
        {.op = OpCode::MOVE_REL, .dir = RelDir::Forward},
        {.op = OpCode::HALT},
    };
    CHECK(rec.manaCost() == 1);
}

TEST_CASE("manaCost: one MOVE_REL Back costs 2") {
    Recording rec;
    rec.instructions = {
        {.op = OpCode::MOVE_REL, .dir = RelDir::Back},
        {.op = OpCode::HALT},
    };
    CHECK(rec.manaCost() == 2);
}

TEST_CASE("manaCost: mixed sequence sums correctly") {
    // Forward(1) + Right(1) + Back(2) + WAIT(0) + HALT(0) = 4
    Recording rec;
    rec.instructions = {
        {.op = OpCode::MOVE_REL, .dir = RelDir::Forward},
        {.op = OpCode::MOVE_REL, .dir = RelDir::Right},
        {.op = OpCode::MOVE_REL, .dir = RelDir::Back},
        {.op = OpCode::WAIT, .ticks = 5},
        {.op = OpCode::HALT},
    };
    CHECK(rec.manaCost() == 4);
}

TEST_CASE("manaCost: HALT not counted, WAIT not counted") {
    Recording rec;
    rec.instructions = {
        {.op = OpCode::WAIT, .ticks = 10},
        {.op = OpCode::WAIT, .ticks = 20},
        {.op = OpCode::HALT},
    };
    CHECK(rec.manaCost() == 0);
}

// ─── Deploy gating (via Game + Input) ────────────────────────────────────────
//
// Helper: inject a single-frame keydown, tick once, then release.

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

// Press a key for exactly one tick, then release it.
static void pressKey(Input& input, Game& game, SDL_Keycode key, Tick tick) {
    input.beginFrame();
    input.handleEvent(makeKeyDown(key));
    game.tick(input, tick);
    input.beginFrame();
    input.handleEvent(makeKeyUp(key));
}

// Advance N ticks with no input (lets movement / animations complete).
static void idleTicks(Input& input, Game& game, int n, Tick& tick) {
    for (int i = 0; i < n; ++i) {
        input.beginFrame();
        game.tick(input, tick++);
    }
}

TEST_CASE("deploy succeeds and deducts mana when mana >= cost") {
    // Player starts with 3 mana (see entity.cpp defaultConfig).
    // Record a HALT-only script (cost = 0) then a one-move script (cost = 1).
    //
    // Phase: build a one-move recording via R → W → R, then deploy with E.
    // Player speed = 0.1 so movement takes 10 ticks to complete.

    Game  game;
    Input input;
    Tick  tick = 0;

    int manaBefore = game.playerMana();   // 3

    // Start recording
    pressKey(input, game, SDLK_r, tick++);

    // Move north (W key) — records MOVE_REL; player needs 10 ticks to arrive
    pressKey(input, game, SDLK_w, tick++);
    idleTicks(input, game, 10, tick);    // wait for movement to complete

    // Stop recording → creates Recording with MOVE_REL Forward + HALT, cost = 1
    pressKey(input, game, SDLK_r, tick++);

    REQUIRE(!game.recordingList().empty());
    CHECK(game.recordingList()[0].manaCost == 1);

    // Deploy
    pressKey(input, game, SDLK_e, tick++);

    CHECK(game.playerMana() == manaBefore - 1);
}

TEST_CASE("deploy blocked and mana unchanged when mana insufficient") {
    // Build a recording with manaCost == 3 (three forward moves).
    // Player starts with 3 mana, so we first spend it all, then attempt deploy.

    Game  game;
    Input input;
    Tick  tick = 0;

    // Record three forward (north) moves: cost = 3
    pressKey(input, game, SDLK_r, tick++);
    for (int i = 0; i < 3; ++i) {
        pressKey(input, game, SDLK_w, tick++);
        idleTicks(input, game, 10, tick);
    }
    pressKey(input, game, SDLK_r, tick++);

    REQUIRE(!game.recordingList().empty());
    CHECK(game.recordingList()[0].manaCost == 3);

    // Drain mana to near-zero by deploying once (cost 3, player has 3).
    // Mana floor keeps it at 1.
    pressKey(input, game, SDLK_e, tick++);
    CHECK(game.playerMana() >= 1);
    CHECK(game.playerMana() < 3);

    // Second deploy attempt should be blocked — mana stays below 3
    int manaAfterFirst = game.playerMana();
    pressKey(input, game, SDLK_e, tick++);
    CHECK(game.playerMana() == manaAfterFirst);
}
