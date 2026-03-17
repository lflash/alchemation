#pragma once

#include "types.hpp"
#include <vector>
#include <string>
#include <cstdint>

// ─── OpCode ──────────────────────────────────────────────────────────────────

enum class OpCode : uint8_t {
    MOVE_REL,       // move one tile relative to agent facing; dir = RelDir
    WAIT,           // pause for ticks ticks
    HALT,           // stop; agent despawns
    DIG,            // dig tile in facing direction
    PLANT,          // plant mushroom in facing direction
    JUMP,           // unconditional jump; addr = target PC
    JUMP_IF,        // jump if stimulus[cond] > threshold; addr, cond, threshold
    JUMP_IF_NOT,    // jump if stimulus[cond] <= threshold; addr, cond, threshold
    CALL,           // push return addr, jump; addr = target PC
    RET,            // pop return addr, jump back
    SUMMON,         // summon golem from medium tile ahead; new golem inherits agent's routine
    SCYTHE,         // convert Grass tile ahead to Straw
    MINE,           // make ore entity ahead Pushable
};

// ─── RelDir ──────────────────────────────────────────────────────────────────

// Direction relative to the agent's current facing. Stored in Instruction::dir.
enum class RelDir : uint8_t { Forward = 0, Right = 1, Back = 2, Left = 3 };

// ─── Condition ───────────────────────────────────────────────────────────────

// Stimulus conditions testable by JUMP_IF / JUMP_IF_NOT.
enum class Condition : uint8_t { None = 0, Fire, Wet, EntityAhead, AtEdge };

// ─── Instruction ─────────────────────────────────────────────────────────────
//
// Flat layout — all fields always present; unused fields default to zero.
// Mapping by opcode:
//   MOVE_REL              → dir
//   WAIT                  → ticks
//   HALT                  → (none)
//   DIG / PLANT           → (none; agent uses its current facing)
//   JUMP / CALL           → addr
//   JUMP_IF / JUMP_IF_NOT → addr, cond, threshold
//   RET                   → (none)

struct Instruction {
    OpCode    op        = OpCode::HALT;
    RelDir    dir       = RelDir::Forward;
    uint16_t  ticks     = 0;
    uint16_t  addr      = 0;
    Condition cond      = Condition::None;
    uint8_t   threshold = 0;
};

// ─── AgentExecState ──────────────────────────────────────────────────────────

struct AgentExecState {
    static constexpr int CALL_STACK_DEPTH = 8;

    uint32_t pc        = 0;   // index into Routine::instructions
    uint32_t waitTicks = 0;   // remaining ticks on a WAIT
    uint16_t callStack[CALL_STACK_DEPTH] = {};   // CALL return addresses
    int      callDepth = 0;                      // current stack depth
};

// ─── Instruction cost table ───────────────────────────────────────────────────
//
// Mana cost per instruction. Defined here so it is trivially testable and easy
// to rebalance — this is the single source of truth.
//
// Phase 13 opcodes (DIG, PLANT, JUMP*, CALL, RET) are priced now so the table
// is complete; their game-logic implementations land in Phase 13.

constexpr int instrCost(OpCode op, RelDir dir = RelDir::Forward) {
    switch (op) {
        case OpCode::MOVE_REL: return 1;
        case OpCode::DIG:      return 1;
        case OpCode::PLANT:    return 1;
        case OpCode::SUMMON:   return 5;
        case OpCode::SCYTHE:   return 2;
        case OpCode::MINE:     return 3;
        default:               return 0;   // WAIT, HALT, JUMP*, CALL, RET
    }
}

// ─── Routine ─────────────────────────────────────────────────────────────────

struct Routine {
    std::string              name;
    std::vector<Instruction> instructions;
    bool empty() const { return instructions.empty(); }

    // Total mana cost to deploy this routine as an agent.
    int manaCost() const {
        int total = 0;
        for (const auto& ins : instructions)
            total += instrCost(ins.op, ins.dir);
        return total;
    }
};

// ─── Direction rotation helpers ───────────────────────────────────────────────
//
// Used by the Recorder (to encode RelDir) and the VM (to decode MOVE_REL).

// Rotate a Direction 90° clockwise: N→E→S→W→N, NE→SE→SW→NW→NE
inline Direction rotCW(Direction d) {
    constexpr Direction cw[] = {
        Direction::E,   // N
        Direction::SE,  // NE
        Direction::S,   // E
        Direction::SW,  // SE
        Direction::W,   // S
        Direction::NW,  // SW
        Direction::N,   // W
        Direction::NE,  // NW
    };
    return cw[static_cast<int>(d)];
}

// Rotate a Direction 90° counter-clockwise.
inline Direction rotCCW(Direction d) {
    constexpr Direction ccw[] = {
        Direction::W,   // N
        Direction::NW,  // NE
        Direction::N,   // E
        Direction::NE,  // SE
        Direction::E,   // S
        Direction::SE,  // SW
        Direction::S,   // W
        Direction::SW,  // NW
    };
    return ccw[static_cast<int>(d)];
}

// Reverse a Direction (180°).
inline Direction opposite(Direction d) { return rotCW(rotCW(d)); }

// Given an agent's facing and a RelDir, return the absolute movement delta.
inline TilePos resolveRelDir(Direction facing, RelDir rel) {
    switch (rel) {
        case RelDir::Forward: return dirToDelta(facing);
        case RelDir::Right:   return dirToDelta(rotCW(facing));
        case RelDir::Back:    return dirToDelta(opposite(facing));
        case RelDir::Left:    return dirToDelta(rotCCW(facing));
    }
    return {0, 0};
}

// Given a player's facing and an absolute movement delta, compute the RelDir.
// Falls back to Forward if the delta doesn't map to a recognisable relative direction.
inline RelDir toRelDir(Direction facing, TilePos delta) {
    Direction abs = toDirection(delta);
    if (abs == facing)           return RelDir::Forward;
    if (abs == rotCW(facing))    return RelDir::Right;
    if (abs == opposite(facing)) return RelDir::Back;
    if (abs == rotCCW(facing))   return RelDir::Left;
    return RelDir::Forward;
}
