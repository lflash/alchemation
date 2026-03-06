#include "doctest.h"
#include "routine.hpp"
#include "routine_vm.hpp"
#include "recorder.hpp"
#include "game.hpp"
#include "input.hpp"

// ─── Helpers ─────────────────────────────────────────────────────────────────

static Recording makeRec(std::initializer_list<Instruction> instrs) {
    Recording r;
    r.instructions = instrs;
    return r;
}

static SDL_Event makeKeyDown(SDL_Keycode k) {
    SDL_Event e{};
    e.type = SDL_KEYDOWN;
    e.key.keysym.sym = k;
    return e;
}

// Step the VM, advancing past any wait ticks. Returns the first non-wait result.
static VMResult stepSkipWait(RoutineVM& vm, AgentExecState& state,
                              const Recording& rec, Direction facing,
                              const uint8_t* stimuli = nullptr) {
    for (int i = 0; i < 1000; ++i) {
        VMResult r = vm.step(state, rec, facing, stimuli);
        if (r.halt || r.wantMove || r.wantDig || r.wantPlant) return r;
    }
    return { .halt = true };
}

// ─── DIG opcode ──────────────────────────────────────────────────────────────

TEST_CASE("VM DIG: sets wantDig, advances PC") {
    RoutineVM vm;
    AgentExecState state;
    Recording rec = makeRec({
        { .op = OpCode::DIG },
        { .op = OpCode::HALT },
    });
    VMResult r = vm.step(state, rec, Direction::N);
    CHECK(r.wantDig);
    CHECK_FALSE(r.halt);
    CHECK(state.pc == 1);
}

TEST_CASE("VM DIG: does not set wantMove or wantPlant") {
    RoutineVM vm;
    AgentExecState state;
    Recording rec = makeRec({ { .op = OpCode::DIG } });
    VMResult r = vm.step(state, rec, Direction::N);
    CHECK_FALSE(r.wantMove);
    CHECK_FALSE(r.wantPlant);
}

// ─── PLANT opcode ─────────────────────────────────────────────────────────────

TEST_CASE("VM PLANT: sets wantPlant, advances PC") {
    RoutineVM vm;
    AgentExecState state;
    Recording rec = makeRec({
        { .op = OpCode::PLANT },
        { .op = OpCode::HALT },
    });
    VMResult r = vm.step(state, rec, Direction::N);
    CHECK(r.wantPlant);
    CHECK_FALSE(r.halt);
    CHECK(state.pc == 1);
}

TEST_CASE("VM PLANT: does not set wantMove or wantDig") {
    RoutineVM vm;
    AgentExecState state;
    Recording rec = makeRec({ { .op = OpCode::PLANT } });
    VMResult r = vm.step(state, rec, Direction::N);
    CHECK_FALSE(r.wantMove);
    CHECK_FALSE(r.wantDig);
}

// ─── JUMP opcode ─────────────────────────────────────────────────────────────

TEST_CASE("VM JUMP: sets PC to target address") {
    RoutineVM vm;
    AgentExecState state;
    Recording rec = makeRec({
        { .op = OpCode::JUMP, .addr = 2 },   // 0: jump to 2
        { .op = OpCode::HALT },               // 1: skipped
        { .op = OpCode::DIG },                // 2: executed
        { .op = OpCode::HALT },
    });
    VMResult r = vm.step(state, rec, Direction::N);  // JUMP
    CHECK_FALSE(r.halt);
    CHECK_FALSE(r.wantMove);
    CHECK(state.pc == 2);

    VMResult r2 = vm.step(state, rec, Direction::N); // DIG
    CHECK(r2.wantDig);
}

TEST_CASE("VM JUMP: backward jump (loop)") {
    RoutineVM vm;
    AgentExecState state;
    state.pc = 2;
    Recording rec = makeRec({
        { .op = OpCode::HALT },               // 0
        { .op = OpCode::HALT },               // 1
        { .op = OpCode::JUMP, .addr = 0 },    // 2: jump back to 0
    });
    vm.step(state, rec, Direction::N);  // JUMP
    CHECK(state.pc == 0);
}

// ─── JUMP_IF opcode ──────────────────────────────────────────────────────────

TEST_CASE("VM JUMP_IF: jumps when stimulus > threshold") {
    RoutineVM vm;
    AgentExecState state;
    Recording rec = makeRec({
        { .op = OpCode::JUMP_IF, .addr = 2, .cond = Condition::Fire, .threshold = 0 },
        { .op = OpCode::HALT },   // 1: skipped
        { .op = OpCode::DIG },    // 2: target
        { .op = OpCode::HALT },
    });
    uint8_t stimuli[8] = {};
    stimuli[static_cast<int>(Condition::Fire)] = 1;

    vm.step(state, rec, Direction::N, stimuli);
    CHECK(state.pc == 2);
}

TEST_CASE("VM JUMP_IF: falls through when stimulus == threshold") {
    RoutineVM vm;
    AgentExecState state;
    Recording rec = makeRec({
        { .op = OpCode::JUMP_IF, .addr = 2, .cond = Condition::Fire, .threshold = 0 },
        { .op = OpCode::DIG },    // 1: fall-through
        { .op = OpCode::HALT },
    });
    uint8_t stimuli[8] = {};  // Fire = 0

    vm.step(state, rec, Direction::N, stimuli);
    CHECK(state.pc == 1);
}

TEST_CASE("VM JUMP_IF: null stimuli treated as all-zero") {
    RoutineVM vm;
    AgentExecState state;
    Recording rec = makeRec({
        { .op = OpCode::JUMP_IF, .addr = 2, .cond = Condition::Fire, .threshold = 0 },
        { .op = OpCode::DIG },
        { .op = OpCode::HALT },
    });
    vm.step(state, rec, Direction::N, nullptr);
    CHECK(state.pc == 1);  // fell through; no jump
}

// ─── JUMP_IF_NOT opcode ───────────────────────────────────────────────────────

TEST_CASE("VM JUMP_IF_NOT: jumps when stimulus == threshold (i.e. <= threshold)") {
    RoutineVM vm;
    AgentExecState state;
    Recording rec = makeRec({
        { .op = OpCode::JUMP_IF_NOT, .addr = 2, .cond = Condition::Wet, .threshold = 0 },
        { .op = OpCode::HALT },
        { .op = OpCode::DIG },
        { .op = OpCode::HALT },
    });
    uint8_t stimuli[8] = {};  // Wet = 0 (not wet)
    vm.step(state, rec, Direction::N, stimuli);
    CHECK(state.pc == 2);
}

TEST_CASE("VM JUMP_IF_NOT: falls through when stimulus > threshold") {
    RoutineVM vm;
    AgentExecState state;
    Recording rec = makeRec({
        { .op = OpCode::JUMP_IF_NOT, .addr = 2, .cond = Condition::Wet, .threshold = 0 },
        { .op = OpCode::DIG },
        { .op = OpCode::HALT },
    });
    uint8_t stimuli[8] = {};
    stimuli[static_cast<int>(Condition::Wet)] = 1;
    vm.step(state, rec, Direction::N, stimuli);
    CHECK(state.pc == 1);
}

// ─── CALL / RET ──────────────────────────────────────────────────────────────

TEST_CASE("VM CALL: pushes return addr and jumps to target") {
    RoutineVM vm;
    AgentExecState state;
    Recording rec = makeRec({
        { .op = OpCode::CALL, .addr = 3 },   // 0: call sub at 3
        { .op = OpCode::HALT },               // 1: return lands here
        { .op = OpCode::HALT },               // 2
        { .op = OpCode::RET },                // 3: subroutine body
    });
    vm.step(state, rec, Direction::N);  // CALL
    CHECK(state.pc == 3);
    CHECK(state.callDepth == 1);
    CHECK(state.callStack[0] == 1);

    vm.step(state, rec, Direction::N);  // RET
    CHECK(state.pc == 1);
    CHECK(state.callDepth == 0);
}

TEST_CASE("VM CALL/RET round-trip leaves call stack clean") {
    RoutineVM vm;
    AgentExecState state;
    Recording rec = makeRec({
        { .op = OpCode::CALL, .addr = 2 },   // 0
        { .op = OpCode::HALT },               // 1: return target
        { .op = OpCode::RET },                // 2: sub body
    });
    vm.step(state, rec, Direction::N);  // CALL → pc=2, depth=1
    vm.step(state, rec, Direction::N);  // RET  → pc=1, depth=0
    CHECK(state.callDepth == 0);
    CHECK(state.pc == 1);
}

TEST_CASE("VM CALL overflow (depth 9) halts safely") {
    RoutineVM vm;
    AgentExecState state;
    // Single CALL that jumps to itself — will overflow after 8 pushes.
    Recording rec = makeRec({
        { .op = OpCode::CALL, .addr = 0 },
    });
    VMResult r;
    for (int i = 0; i < AgentExecState::CALL_STACK_DEPTH; ++i)
        r = vm.step(state, rec, Direction::N);  // fill stack
    // One more push should overflow and halt.
    r = vm.step(state, rec, Direction::N);
    CHECK(r.halt);
}

TEST_CASE("VM RET with empty stack halts safely") {
    RoutineVM vm;
    AgentExecState state;
    Recording rec = makeRec({ { .op = OpCode::RET } });
    VMResult r = vm.step(state, rec, Direction::N);
    CHECK(r.halt);
}

// ─── Recorder: recordDig / recordPlant ───────────────────────────────────────

TEST_CASE("Recorder::recordDig emits DIG instruction") {
    Recorder rec;
    rec.start();
    rec.recordDig();
    Recording r = rec.stop();
    // Expect: DIG, HALT (no leading WAIT since ticksSinceLastMove_ == 0)
    REQUIRE(r.instructions.size() >= 2);
    CHECK(r.instructions[0].op == OpCode::DIG);
    CHECK(r.instructions.back().op == OpCode::HALT);
}

TEST_CASE("Recorder::recordDig emits WAIT before DIG after a pause") {
    Recorder rec;
    rec.start();
    rec.tick();
    rec.tick();
    rec.tick();
    rec.recordDig();
    Recording r = rec.stop();
    REQUIRE(r.instructions.size() >= 3);
    CHECK(r.instructions[0].op == OpCode::WAIT);
    CHECK(r.instructions[0].ticks == 3);
    CHECK(r.instructions[1].op == OpCode::DIG);
}

TEST_CASE("Recorder::recordPlant emits PLANT instruction") {
    Recorder rec;
    rec.start();
    rec.recordPlant();
    Recording r = rec.stop();
    REQUIRE(r.instructions.size() >= 2);
    CHECK(r.instructions[0].op == OpCode::PLANT);
    CHECK(r.instructions.back().op == OpCode::HALT);
}

TEST_CASE("Recorder: DIG then move sequence emits correct instructions") {
    Recorder rec;
    rec.start();
    rec.recordDig();
    rec.tick();   // one tick pause
    rec.recordMove({1, 0}, Direction::N);
    Recording r = rec.stop();
    // Expected: DIG, WAIT(1), MOVE_REL, HALT
    REQUIRE(r.instructions.size() == 4);
    CHECK(r.instructions[0].op == OpCode::DIG);
    CHECK(r.instructions[1].op == OpCode::WAIT);
    CHECK(r.instructions[1].ticks == 1);
    CHECK(r.instructions[2].op == OpCode::MOVE_REL);
    CHECK(r.instructions[3].op == OpCode::HALT);
}

// ─── instrCost: new opcodes ───────────────────────────────────────────────────

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
