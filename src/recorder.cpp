#include "recorder.hpp"

void Recorder::start() {
    recording_           = true;
    ticksSinceLastMove_  = 0;
    current_             = {};
}

void Recorder::tick() {
    if (recording_) ++ticksSinceLastMove_;
}

void Recorder::recordMove(TilePos delta, Direction facingBeforeMove, bool strafe) {
    if (!recording_) return;

    // Emit WAIT for any pause since the last move (or since start).
    if (ticksSinceLastMove_ > 0) {
        current_.instructions.push_back({ .op = OpCode::WAIT, .ticks = static_cast<uint16_t>(ticksSinceLastMove_) });
        ticksSinceLastMove_ = 0;
    }

    RelDir rel = toRelDir(facingBeforeMove, delta);
    // threshold != 0 signals "strafe" to the VM (don't update agent facing on playback).
    current_.instructions.push_back({ .op = OpCode::MOVE_REL, .dir = rel,
                                      .threshold = static_cast<uint8_t>(strafe ? 1 : 0) });
}

void Recorder::recordDig() {
    if (!recording_) return;
    if (ticksSinceLastMove_ > 0) {
        current_.instructions.push_back({ .op = OpCode::WAIT, .ticks = static_cast<uint16_t>(ticksSinceLastMove_) });
        ticksSinceLastMove_ = 0;
    }
    current_.instructions.push_back({ .op = OpCode::DIG });
}

void Recorder::recordPlant() {
    if (!recording_) return;
    if (ticksSinceLastMove_ > 0) {
        current_.instructions.push_back({ .op = OpCode::WAIT, .ticks = static_cast<uint16_t>(ticksSinceLastMove_) });
        ticksSinceLastMove_ = 0;
    }
    current_.instructions.push_back({ .op = OpCode::PLANT });
}

void Recorder::recordSummon(size_t targetRecIdx) {
    if (!recording_) return;
    if (ticksSinceLastMove_ > 0) {
        current_.instructions.push_back({ .op = OpCode::WAIT, .ticks = static_cast<uint16_t>(ticksSinceLastMove_) });
        ticksSinceLastMove_ = 0;
    }
    // addr stores which recording index to assign to the summoned golem.
    current_.instructions.push_back({ .op = OpCode::SUMMON,
                                      .addr = static_cast<uint16_t>(targetRecIdx) });
}

void Recorder::recordScythe() {
    if (!recording_) return;
    if (ticksSinceLastMove_ > 0) {
        current_.instructions.push_back({ .op = OpCode::WAIT, .ticks = static_cast<uint16_t>(ticksSinceLastMove_) });
        ticksSinceLastMove_ = 0;
    }
    current_.instructions.push_back({ .op = OpCode::SCYTHE });
}

void Recorder::recordMine() {
    if (!recording_) return;
    if (ticksSinceLastMove_ > 0) {
        current_.instructions.push_back({ .op = OpCode::WAIT, .ticks = static_cast<uint16_t>(ticksSinceLastMove_) });
        ticksSinceLastMove_ = 0;
    }
    current_.instructions.push_back({ .op = OpCode::MINE });
}

Recording Recorder::stop() {
    current_.instructions.push_back({ .op = OpCode::HALT });
    current_.name = "Script " + std::to_string(recordings.size() + 1);
    recording_ = false;
    recordings.push_back(current_);
    current_ = {};
    return recordings.back();
}
