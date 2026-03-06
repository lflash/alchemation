#pragma once

#include "routine.hpp"
#include "entity.hpp"

// ─── RoutineVM ───────────────────────────────────────────────────────────────

struct VMResult {
    bool    halt      = false;   // agent should stop executing (despawn if Poop)
    bool    wantMove  = false;   // moveDelta is valid
    TilePos moveDelta = {0, 0};
    bool    wantDig   = false;   // dig tile in agent's facing direction
    bool    wantPlant = false;   // plant mushroom in agent's facing direction
};

class RoutineVM {
public:
    // Advance one instruction for an agent. Uses agentFacing to resolve MOVE_REL
    // and direction-relative opcodes.
    //
    // stimuli: optional array indexed by Condition enum values. Non-null only when
    //          a JUMP_IF / JUMP_IF_NOT instruction may be executed. Values are 0/1
    //          (condition false/true). Computed by the caller (game.cpp tickVM).
    //
    // Caller is responsible for updating agentFacing after a successful move.
    VMResult step(AgentExecState& state, const Recording& rec, Direction agentFacing,
                  const uint8_t* stimuli = nullptr) const;
};
