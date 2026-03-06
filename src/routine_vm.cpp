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
            state.waitTicks = instr.ticks > 0 ? instr.ticks - 1 : 0;
            ++state.pc;
            return {};

        case OpCode::MOVE_REL: {
            TilePos delta = resolveRelDir(agentFacing, instr.dir);
            ++state.pc;
            return { .halt = false, .wantMove = true, .moveDelta = delta };
        }
    }
    return { .halt = true };
}
