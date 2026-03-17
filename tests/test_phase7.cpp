#include "doctest.h"
#include "recorder.hpp"
#include "routine_vm.hpp"
#include "entity.hpp"
#include "spatial.hpp"

// ─── Recorder ────────────────────────────────────────────────────────────────

TEST_CASE("recording ends with HALT") {
    Recorder rec;
    rec.start();
    Routine r = rec.stop();
    REQUIRE(!r.empty());
    CHECK(r.instructions.back().op == OpCode::HALT);
}

TEST_CASE("move north while facing north emits MOVE_REL Forward") {
    Recorder rec;
    rec.start();
    rec.recordMove({0, -1}, Direction::N);
    Routine r = rec.stop();
    REQUIRE(r.instructions.size() >= 2);
    CHECK(r.instructions[0].op  == OpCode::MOVE_REL);
    CHECK(static_cast<RelDir>(r.instructions[0].dir) == RelDir::Forward);
}

TEST_CASE("move east while facing north emits MOVE_REL Right") {
    Recorder rec;
    rec.start();
    rec.recordMove({1, 0}, Direction::N);
    Routine r = rec.stop();
    REQUIRE(r.instructions.size() >= 2);
    CHECK(r.instructions[0].op  == OpCode::MOVE_REL);
    CHECK(static_cast<RelDir>(r.instructions[0].dir) == RelDir::Right);
}

TEST_CASE("move south while facing north emits MOVE_REL Back") {
    Recorder rec;
    rec.start();
    rec.recordMove({0, 1}, Direction::N);
    Routine r = rec.stop();
    CHECK(static_cast<RelDir>(r.instructions[0].dir) == RelDir::Back);
}

TEST_CASE("move west while facing north emits MOVE_REL Left") {
    Recorder rec;
    rec.start();
    rec.recordMove({-1, 0}, Direction::N);
    Routine r = rec.stop();
    CHECK(static_cast<RelDir>(r.instructions[0].dir) == RelDir::Left);
}

TEST_CASE("move forward while facing east emits MOVE_REL Forward") {
    Recorder rec;
    rec.start();
    rec.recordMove({1, 0}, Direction::E);
    Routine r = rec.stop();
    CHECK(static_cast<RelDir>(r.instructions[0].dir) == RelDir::Forward);
}

TEST_CASE("pause between moves emits WAIT with correct tick count") {
    Recorder rec;
    rec.start();
    rec.tick();  // tick 1
    rec.tick();  // tick 2
    rec.tick();  // tick 3
    rec.recordMove({0, -1}, Direction::N);
    Routine r = rec.stop();

    REQUIRE(r.instructions.size() >= 3);
    CHECK(r.instructions[0].op  == OpCode::WAIT);
    CHECK(r.instructions[0].ticks == 3);
    CHECK(r.instructions[1].op  == OpCode::MOVE_REL);
}

TEST_CASE("no WAIT emitted when move immediately follows start") {
    Recorder rec;
    rec.start();
    // No tick() calls — move happens on the same tick as start
    rec.recordMove({0, -1}, Direction::N);
    Routine r = rec.stop();
    CHECK(r.instructions[0].op == OpCode::MOVE_REL);
}

TEST_CASE("two moves separated by a pause emit WAIT between them") {
    Recorder rec;
    rec.start();
    rec.recordMove({0, -1}, Direction::N);   // immediate first move
    rec.tick();
    rec.tick();
    rec.recordMove({1, 0}, Direction::N);    // move after 2-tick pause
    Routine r = rec.stop();

    // Expected: MOVE_REL, WAIT(2), MOVE_REL, HALT
    REQUIRE(r.instructions.size() == 4);
    CHECK(r.instructions[0].op  == OpCode::MOVE_REL);
    CHECK(r.instructions[1].op  == OpCode::WAIT);
    CHECK(r.instructions[1].ticks == 2);
    CHECK(r.instructions[2].op  == OpCode::MOVE_REL);
    CHECK(r.instructions[3].op  == OpCode::HALT);
}

TEST_CASE("stop() saves recording and resets; second recording is independent") {
    Recorder rec;
    rec.start();
    rec.recordMove({0, -1}, Direction::N);
    rec.stop();

    rec.start();
    rec.recordMove({1, 0}, Direction::E);
    rec.stop();

    REQUIRE(rec.routines.size() == 2);
    // First recording: MOVE_REL Forward (N facing N)
    CHECK(rec.routines[0].instructions[0].dir == RelDir::Forward);
    // Second recording: MOVE_REL Forward (E facing E)
    CHECK(rec.routines[1].instructions[0].dir == RelDir::Forward);
}

// ─── RoutineVM ───────────────────────────────────────────────────────────────

static Routine makeRecording(std::vector<Instruction> instrs) {
    Routine r;
    r.instructions = std::move(instrs);
    return r;
}

TEST_CASE("VM: HALT on first instruction returns halt immediately") {
    RoutineVM vm;
    AgentExecState state;
    Routine rec = makeRecording({ {.op = OpCode::HALT} });
    auto result = vm.step(state, rec, Direction::N);
    CHECK(result.halt);
    CHECK(!result.wantMove);
}

TEST_CASE("VM: empty recording returns halt") {
    RoutineVM vm;
    AgentExecState state;
    Routine rec;
    CHECK(vm.step(state, rec, Direction::N).halt);
}

TEST_CASE("VM: MOVE_REL Forward facing N moves north") {
    RoutineVM vm;
    AgentExecState state;
    Routine rec = makeRecording({
        {.op = OpCode::MOVE_REL, .dir = RelDir::Forward},
        {.op = OpCode::HALT},
    });
    auto result = vm.step(state, rec, Direction::N);
    CHECK(!result.halt);
    CHECK(result.wantMove);
    CHECK(result.moveDelta == TilePos{0, -1});
    CHECK(state.pc == 1);
}

TEST_CASE("VM: MOVE_REL Forward facing E moves east") {
    RoutineVM vm;
    AgentExecState state;
    Routine rec = makeRecording({
        {.op = OpCode::MOVE_REL, .dir = RelDir::Forward},
        {.op = OpCode::HALT},
    });
    auto result = vm.step(state, rec, Direction::E);
    CHECK(result.wantMove);
    CHECK(result.moveDelta == TilePos{1, 0});
}

TEST_CASE("VM: MOVE_REL Right facing N moves east") {
    RoutineVM vm;
    AgentExecState state;
    Routine rec = makeRecording({
        {.op = OpCode::MOVE_REL, .dir = RelDir::Right},
        {.op = OpCode::HALT},
    });
    auto result = vm.step(state, rec, Direction::N);
    CHECK(result.wantMove);
    CHECK(result.moveDelta == TilePos{1, 0});
}

TEST_CASE("VM: WAIT holds for correct number of ticks then continues") {
    RoutineVM vm;
    AgentExecState state;
    Routine rec = makeRecording({
        {.op = OpCode::WAIT, .ticks = 3},
        {.op = OpCode::HALT},
    });

    // Tick 1: WAIT consumed, 2 remaining
    auto r1 = vm.step(state, rec, Direction::N);
    CHECK(!r1.halt); CHECK(!r1.wantMove);

    // Ticks 2–3: counting down
    auto r2 = vm.step(state, rec, Direction::N);
    CHECK(!r2.halt); CHECK(!r2.wantMove);
    auto r3 = vm.step(state, rec, Direction::N);
    CHECK(!r3.halt); CHECK(!r3.wantMove);

    // Tick 4: WAIT expired, HALT executes
    auto r4 = vm.step(state, rec, Direction::N);
    CHECK(r4.halt);
}

TEST_CASE("VM: pc advances correctly through a linear sequence") {
    RoutineVM vm;
    AgentExecState state;
    Routine rec = makeRecording({
        {.op = OpCode::MOVE_REL, .dir = RelDir::Forward},
        {.op = OpCode::MOVE_REL, .dir = RelDir::Right},
        {.op = OpCode::HALT},
    });

    CHECK(state.pc == 0);
    vm.step(state, rec, Direction::N);
    CHECK(state.pc == 1);
    vm.step(state, rec, Direction::N);
    CHECK(state.pc == 2);
    CHECK(vm.step(state, rec, Direction::N).halt);
}

// ─── Direction rotation helpers ──────────────────────────────────────────────

TEST_CASE("rotCW cycles N→E→S→W→N") {
    CHECK(rotCW(Direction::N) == Direction::E);
    CHECK(rotCW(Direction::E) == Direction::S);
    CHECK(rotCW(Direction::S) == Direction::W);
    CHECK(rotCW(Direction::W) == Direction::N);
}

TEST_CASE("rotCCW cycles N→W→S→E→N") {
    CHECK(rotCCW(Direction::N) == Direction::W);
    CHECK(rotCCW(Direction::W) == Direction::S);
    CHECK(rotCCW(Direction::S) == Direction::E);
    CHECK(rotCCW(Direction::E) == Direction::N);
}

TEST_CASE("opposite gives 180-degree reverse") {
    CHECK(opposite(Direction::N) == Direction::S);
    CHECK(opposite(Direction::E) == Direction::W);
    CHECK(opposite(Direction::NE) == Direction::SW);
}
