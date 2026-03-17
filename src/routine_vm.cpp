#include "routine_vm.hpp"

VMResult RoutineVM::step(AgentExecState& state, const Routine& routine,
                         Direction agentFacing, const uint8_t* stimuli) const {
    if (routine.empty() || state.pc >= routine.instructions.size())
        return { .halt = true };

    // Count down an active WAIT before consuming a new instruction.
    if (state.waitTicks > 0) {
        --state.waitTicks;
        return {};
    }

    const Instruction& instr = routine.instructions[state.pc];

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
            return { .halt = false, .wantMove = true, .moveDelta = delta,
                     .isStrafe = (instr.threshold != 0) };
        }

        case OpCode::DIG:
            ++state.pc;
            return { .wantDig = true };

        case OpCode::PLANT:
            ++state.pc;
            return { .wantPlant = true };

        case OpCode::SUMMON:
            ++state.pc;
            return { .wantSummon = true, .summonRoutineIdx = instr.addr };

        case OpCode::SCYTHE:
            ++state.pc;
            return { .wantScythe = true };

        case OpCode::MINE:
            ++state.pc;
            return { .wantMine = true };

        case OpCode::JUMP:
            state.pc = instr.addr;
            return {};

        case OpCode::JUMP_IF: {
            uint8_t val = stimuli ? stimuli[static_cast<int>(instr.cond)] : 0;
            state.pc = (val > instr.threshold) ? instr.addr : state.pc + 1;
            return {};
        }

        case OpCode::JUMP_IF_NOT: {
            uint8_t val = stimuli ? stimuli[static_cast<int>(instr.cond)] : 0;
            state.pc = (val <= instr.threshold) ? instr.addr : state.pc + 1;
            return {};
        }

        case OpCode::CALL:
            if (state.callDepth >= AgentExecState::CALL_STACK_DEPTH)
                return { .halt = true };   // overflow — safe stop
            state.callStack[state.callDepth++] = static_cast<uint16_t>(state.pc + 1);
            state.pc = instr.addr;
            return {};

        case OpCode::RET:
            if (state.callDepth == 0)
                return { .halt = true };   // unmatched RET — safe stop
            state.pc = state.callStack[--state.callDepth];
            return {};
    }

    return { .halt = true };
}
