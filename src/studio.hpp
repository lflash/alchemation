#pragma once

#include "routine.hpp"
#include "types.hpp"
#include <vector>
#include <cstdint>

// ─── PathStep ─────────────────────────────────────────────────────────────────
//
// One step in a simulated routine path.
//   MOVE_REL / DIG / PLANT → one step each (isWait = false).
//   WAIT N               → N steps           (isWait = true, shown as dot).
//   HALT / maxSteps cap  → sequence ends.

struct PathStep {
    TilePos   pos;
    Direction facing;
    int       instrIdx;  // index into Recording::instructions
    bool      isWait;    // true for WAIT ticks (dot, not arrow)
};

// ─── routinePath ─────────────────────────────────────────────────────────────
//
// Simulate rec from (origin, facing) without modifying any game state.
// Returns one PathStep per virtual tick.
// JUMP_IF   : null stimuli → 0 > threshold → false → not taken.
// JUMP_IF_NOT: null stimuli → 0 ≤ threshold → true  → taken.
// Capped at maxSteps to handle infinite loops.

std::vector<PathStep> routinePath(const Recording& rec,
                                  TilePos           origin,
                                  Direction         facing,
                                  int               maxSteps = 512);

// ─── AgentColor ───────────────────────────────────────────────────────────────

struct AgentColor { uint8_t r, g, b; };

// Distinct palette colour for studio agent index (cycles through 8 colours).
AgentColor agentPaletteColor(int index);

// ─── studioConflicts ─────────────────────────────────────────────────────────
//
// Given a collection of paths (one per agent), returns sorted tick indices
// where two or more agents share the same (x,y) TilePos.

std::vector<int> studioConflicts(
    const std::vector<std::vector<PathStep>>& paths);
