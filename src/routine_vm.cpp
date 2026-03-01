#include "routine_vm.hpp"

VMResult RoutineVM::step(AgentExecState& state, const Recording& rec,
                         Direction agentFacing) const {
    if (rec.empty() || state.pc >= rec.instructions.size())
        return { .halt = true };

    // Count down an active WAIT before consuming a new instruction.
    if (state.waitTicks > 0) {
        --state.waitTicks;
        return {};
    }

    const Instruction& instr = rec.instructions[state.pc];

    switch (instr.op) {
        case OpCode::HALT:
            return { .halt = true };

        case OpCode::WAIT:
            state.waitTicks = instr.arg > 0 ? instr.arg - 1 : 0;
            ++state.pc;
            return {};

        case OpCode::MOVE_REL: {
            RelDir rel   = static_cast<RelDir>(instr.dir);
            TilePos delta = resolveRelDir(agentFacing, rel);
            ++state.pc;
            return { .halt = false, .wantMove = true, .moveDelta = delta };
        }
    }
    return { .halt = true };
}
