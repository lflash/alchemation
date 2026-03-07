#include "studio.hpp"
#include <unordered_map>

// ─── routinePath ─────────────────────────────────────────────────────────────

std::vector<PathStep> routinePath(const Recording& rec,
                                  TilePos origin, Direction facing,
                                  int maxSteps) {
    std::vector<PathStep> path;
    if (rec.instructions.empty()) return path;

    TilePos   pos       = origin;
    Direction dir       = facing;
    int       pc        = 0;
    int       steps     = 0;
    int       callStack[AgentExecState::CALL_STACK_DEPTH] = {};
    int       callDepth = 0;

    while (steps < maxSteps) {
        if (pc < 0 || pc >= (int)rec.instructions.size()) break;

        const Instruction& instr = rec.instructions[pc];
        int instrIdx = pc;

        switch (instr.op) {
            case OpCode::HALT:
                return path;

            case OpCode::MOVE_REL: {
                TilePos delta = resolveRelDir(dir, instr.dir);
                pos = pos + delta;
                bool strafe = (instr.threshold != 0);
                if (!strafe) dir = toDirection(delta);
                path.push_back({ pos, dir, instrIdx, false });
                ++steps;
                ++pc;
                break;
            }

            case OpCode::WAIT: {
                int n = instr.ticks > 0 ? (int)instr.ticks : 1;
                for (int i = 0; i < n && steps < maxSteps; ++i, ++steps)
                    path.push_back({ pos, dir, instrIdx, true });
                ++pc;
                break;
            }

            case OpCode::DIG:
            case OpCode::PLANT:
            case OpCode::SUMMON:
                path.push_back({ pos, dir, instrIdx, false });
                ++steps;
                ++pc;
                break;

            case OpCode::JUMP:
                if (instr.addr == (uint16_t)pc) return path; // self-loop
                pc = (int)instr.addr;
                break;

            case OpCode::JUMP_IF:
                // null stimuli → 0 > threshold → false → not taken
                ++pc;
                break;

            case OpCode::JUMP_IF_NOT:
                // null stimuli → 0 ≤ threshold → true → taken
                if (instr.addr == (uint16_t)pc) return path; // self-loop
                pc = (int)instr.addr;
                break;

            case OpCode::CALL:
                if (callDepth >= AgentExecState::CALL_STACK_DEPTH) return path;
                callStack[callDepth++] = pc + 1;
                pc = (int)instr.addr;
                break;

            case OpCode::RET:
                if (callDepth <= 0) return path;
                pc = callStack[--callDepth];
                break;
        }
    }

    return path;
}

// ─── agentPaletteColor ────────────────────────────────────────────────────────

AgentColor agentPaletteColor(int index) {
    static const AgentColor palette[] = {
        {100, 200, 255},   // cyan-blue
        {255, 200,  80},   // gold
        {180, 255, 120},   // lime
        {255, 120, 180},   // pink
        {200, 120, 255},   // purple
        { 80, 220, 180},   // teal
        {255, 160,  80},   // orange
        {220, 220,  80},   // yellow
    };
    return palette[index % 8];
}

// ─── studioConflicts ─────────────────────────────────────────────────────────

std::vector<int> studioConflicts(
    const std::vector<std::vector<PathStep>>& paths) {

    std::vector<int> conflicts;
    if (paths.size() < 2) return conflicts;

    int maxTick = 0;
    for (const auto& p : paths)
        if ((int)p.size() > maxTick) maxTick = (int)p.size();

    for (int tick = 0; tick < maxTick; ++tick) {
        std::unordered_map<int, int> posCount;
        bool found = false;
        for (const auto& p : paths) {
            if (tick >= (int)p.size()) continue;
            const TilePos& tp = p[tick].pos;
            int key = tp.x * 100003 + tp.y;
            if (++posCount[key] >= 2) { found = true; break; }
        }
        if (found) conflicts.push_back(tick);
    }

    return conflicts;
}
