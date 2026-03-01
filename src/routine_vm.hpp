#pragma once

#include "routine.hpp"
#include "entity.hpp"

// ─── RoutineVM ───────────────────────────────────────────────────────────────

struct VMResult {
    bool    halt     = false;   // agent should despawn
    bool    wantMove = false;   // moveDelta is valid
    TilePos moveDelta = {0, 0};
};

class RoutineVM {
public:
    // Advance one instruction for an agent. Uses agentFacing to resolve MOVE_REL.
    // Caller is responsible for updating agentFacing after a successful move.
    VMResult step(AgentExecState& state, const Recording& rec, Direction agentFacing) const;
};
