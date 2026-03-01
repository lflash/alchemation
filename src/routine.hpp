#pragma once

#include "types.hpp"
#include <vector>
#include <cstdint>

// ─── OpCode ──────────────────────────────────────────────────────────────────

enum class OpCode : uint8_t {
    MOVE_REL,   // move one tile relative to agent facing; dir = RelDir
    WAIT,       // pause for arg ticks
    HALT,       // stop; agent despawns
};

// ─── RelDir ──────────────────────────────────────────────────────────────────

// Direction relative to the agent's current facing. Stored in Instruction::dir.
enum class RelDir : uint8_t { Forward = 0, Right = 1, Back = 2, Left = 3 };

// ─── Instruction ─────────────────────────────────────────────────────────────

struct Instruction {
    OpCode   op;
    uint8_t  dir;    // RelDir for MOVE_REL; unused for WAIT/HALT
    uint32_t arg;    // tick count for WAIT; unused otherwise
};

// ─── AgentExecState ──────────────────────────────────────────────────────────

struct AgentExecState {
    uint32_t pc        = 0;   // index into Recording::instructions
    uint32_t waitTicks = 0;   // remaining ticks on a WAIT
};

// ─── Recording ───────────────────────────────────────────────────────────────

struct Recording {
    std::vector<Instruction> instructions;
    bool empty() const { return instructions.empty(); }
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
