#include "doctest.h"
#include "terrain.hpp"
#include "types.hpp"
#include "routine.hpp"
#include "routine_vm.hpp"
#include "recorder.hpp"
#include "game.hpp"
#include "input.hpp"

// ─── Biome ────────────────────────────────────────────────────────────────────

TEST_CASE("biomeAt: deterministic — same tile returns same biome") {
    Terrain t;
    Biome b1 = t.biomeAt({100, 200});
    Biome b2 = t.biomeAt({100, 200});
    CHECK(b1 == b2);
}

TEST_CASE("biomeAt: caches correctly — repeated calls consistent") {
    Terrain t;
    // Call many different tiles to fill the cache, then re-check one.
    for (int x = -10; x <= 10; ++x)
        for (int y = -10; y <= 10; ++y)
            t.biomeAt({x, y});
    Biome b1 = t.biomeAt({5, 3});
    Biome b2 = t.biomeAt({5, 3});
    CHECK(b1 == b2);
}

TEST_CASE("biomeAt: mountain override applies at high terrain") {
    // Find a tile with levelAt >= 3 and verify it returns Mountains.
    Terrain t;
    // Brute-force search a tile with level >= 3.
    bool found = false;
    for (int x = -200; x <= 200 && !found; ++x) {
        for (int y = -200; y <= 200 && !found; ++y) {
            if (t.levelAt({x, y}) >= 3) {
                CHECK(t.biomeAt({x, y}) == Biome::Mountains);
                found = true;
            }
        }
    }
    // The Perlin noise used has FBm with 4 octaves; high tiles do exist.
    CHECK_MESSAGE(found, "Could not find a mountain tile in search area");
}

TEST_CASE("biomeAt: low terrain is not Mountains") {
    Terrain t;
    // Find a tile with levelAt <= 0 and verify it is not Mountains.
    bool found = false;
    for (int x = -50; x <= 50 && !found; ++x) {
        for (int y = -50; y <= 50 && !found; ++y) {
            if (t.levelAt({x, y}) <= 0) {
                CHECK(t.biomeAt({x, y}) != Biome::Mountains);
                found = true;
            }
        }
    }
    CHECK_MESSAGE(found, "Could not find a low tile in search area");
}

// ─── SCYTHE opcode ────────────────────────────────────────────────────────────

TEST_CASE("instrCost: SCYTHE costs 2") {
    CHECK(instrCost(OpCode::SCYTHE) == 2);
}

TEST_CASE("instrCost: MINE costs 3") {
    CHECK(instrCost(OpCode::MINE) == 3);
}

TEST_CASE("RoutineVM: SCYTHE emits wantScythe") {
    Recording rec;
    rec.instructions.push_back({ .op = OpCode::SCYTHE });

    AgentExecState state;
    RoutineVM vm;
    VMResult res = vm.step(state, rec, Direction::N);
    CHECK(res.wantScythe);
    CHECK(!res.halt);
    CHECK(!res.wantMine);
}

TEST_CASE("RoutineVM: MINE emits wantMine") {
    Recording rec;
    rec.instructions.push_back({ .op = OpCode::MINE });

    AgentExecState state;
    RoutineVM vm;
    VMResult res = vm.step(state, rec, Direction::N);
    CHECK(res.wantMine);
    CHECK(!res.halt);
    CHECK(!res.wantScythe);
}

TEST_CASE("Recorder: recordScythe appends SCYTHE instruction") {
    Recorder rec;
    rec.start();
    rec.recordScythe();
    Recording r = rec.stop();
    REQUIRE(r.instructions.size() >= 2);   // SCYTHE + HALT
    CHECK(r.instructions[0].op == OpCode::SCYTHE);
    CHECK(r.instructions.back().op == OpCode::HALT);
}

TEST_CASE("Recorder: recordMine appends MINE instruction") {
    Recorder rec;
    rec.start();
    rec.recordMine();
    Recording r = rec.stop();
    REQUIRE(r.instructions.size() >= 2);   // MINE + HALT
    CHECK(r.instructions[0].op == OpCode::MINE);
    CHECK(r.instructions.back().op == OpCode::HALT);
}

TEST_CASE("Recorder: manaCost counts SCYTHE at 2 and MINE at 3") {
    Recorder rec;
    rec.start();
    rec.recordScythe();
    rec.recordMine();
    Recording r = rec.stop();
    CHECK(r.manaCost() == 5);   // 2 + 3
}

// ─── Input: Scythe and Mine actions exist ─────────────────────────────────────

TEST_CASE("InputMap defaults: Scythe and Mine are bound") {
    InputMap m = InputMap::defaults();
    CHECK(m.get(Action::Scythe) != SDLK_UNKNOWN);
    CHECK(m.get(Action::Mine)   != SDLK_UNKNOWN);
}

// ─── Chunk generation ─────────────────────────────────────────────────────────

TEST_CASE("Game: chunk generation is idempotent — re-visiting same area doesn't double-spawn") {
    Game g;
    // The game already pre-marks a few chunks; check entity count stays stable
    // when tick is called repeatedly with a static player (no movement input).
    Input input;
    g.tick(input, 0);
    size_t count0 = g.drawOrder().size();
    g.tick(input, 1);
    size_t count1 = g.drawOrder().size();
    // No movement → no new chunks → entity count stable.
    CHECK(count0 == count1);
}

TEST_CASE("CHUNK_SIZE constant is 16") {
    CHECK(CHUNK_SIZE == 16);
}
