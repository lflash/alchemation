#pragma once

#include "routine.hpp"
#include <deque>

// ─── Recorder ────────────────────────────────────────────────────────────────
//
// Records player actions as a flat Instruction stream.
//
// Usage per tick:
//   if (recorder.isRecording()) recorder.tick();
//   if (player moved)           recorder.recordMove(delta, player.facing);
//   if (r pressed)              toggle: start() or recordings.push_back(stop())

class Recorder {
public:
    void start();

    // Call once per tick while recording (tracks time between moves for WAIT).
    void tick();

    // Emit a WAIT (if ticks elapsed since last move) then a MOVE_REL.
    // facing is the player's facing AFTER the move direction has been applied
    // (i.e. the facing used to compute the RelDir, which is the pre-move facing
    // for non-strafing movement but may differ when strafing).
    void recordMove(TilePos delta, Direction facingBeforeMove);

    // Emit a WAIT (if any pause) then a DIG instruction.
    void recordDig();

    // Emit a WAIT (if any pause) then a PLANT instruction.
    void recordPlant();

    // Append HALT and return the completed Recording. Resets internal state.
    Recording stop();

    bool isRecording() const { return recording_; }

    // All completed recordings, oldest first.
    std::deque<Recording> recordings;

private:
    bool     recording_   = false;
    uint32_t ticksSinceLastMove_ = 0;
    Recording current_;
};
