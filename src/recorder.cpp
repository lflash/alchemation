#include "recorder.hpp"

void Recorder::start() {
    recording_           = true;
    ticksSinceLastMove_  = 0;
    current_             = {};
}

void Recorder::tick() {
    if (recording_) ++ticksSinceLastMove_;
}

void Recorder::recordMove(TilePos delta, Direction facingBeforeMove) {
    if (!recording_) return;

    // Emit WAIT for any pause since the last move (or since start).
    if (ticksSinceLastMove_ > 0) {
        current_.instructions.push_back({ .op = OpCode::WAIT, .ticks = static_cast<uint16_t>(ticksSinceLastMove_) });
        ticksSinceLastMove_ = 0;
    }

    RelDir rel = toRelDir(facingBeforeMove, delta);
    current_.instructions.push_back({ .op = OpCode::MOVE_REL, .dir = rel });
}

Recording Recorder::stop() {
    current_.instructions.push_back({ .op = OpCode::HALT });
    current_.name = "Script " + std::to_string(recordings.size() + 1);
    recording_ = false;
    recordings.push_back(current_);
    current_ = {};
    return recordings.back();
}
