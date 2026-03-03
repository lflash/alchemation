#pragma once

#include "types.hpp"
#include <cmath>
#include <cstdint>
#include <vector>

// ─── AnimFrame ────────────────────────────────────────────────────────────────
//
// One frame within an animation strip.  srcX/srcY/srcW/srcH select the
// sub-region of the sprite sheet (srcW==0 means use the full texture).
// duration is how many renderer ticks to display this frame.

struct AnimFrame {
    int srcX = 0, srcY = 0;
    int srcW = 0, srcH = 0;
    int duration = 1;
};

// ─── Animation ────────────────────────────────────────────────────────────────

struct Animation {
    std::vector<AnimFrame> frames;
    bool loop = true;

    // Returns the frame index to show after elapsedTicks renderer ticks.
    // Looping: wraps the sequence. One-shot: clamps to the last frame.
    int frameAt(int elapsedTicks) const {
        if (frames.empty()) return 0;
        int total = 0;
        for (const auto& f : frames) total += f.duration;
        if (total <= 0) return 0;
        if (loop) {
            elapsedTicks %= total;
        } else {
            elapsedTicks = std::min(elapsedTicks, total - 1);
        }
        int t = 0;
        for (int i = 0; i < (int)frames.size(); ++i) {
            t += frames[i].duration;
            if (elapsedTicks < t) return i;
        }
        return (int)frames.size() - 1;
    }
};

// ─── AnimState ────────────────────────────────────────────────────────────────

enum class AnimState { Idle, Walk, Hit, Die };

// ─── RGBA ─────────────────────────────────────────────────────────────────────
//
// SDL-free colour type used in effects.  Renderer converts to SDL_Color.

struct RGBA { uint8_t r, g, b, a; };

// ─── Particle ─────────────────────────────────────────────────────────────────

struct Particle {
    Vec2f pos;
    Vec2f vel;
    float z       = 0.0f;
    float life    = 1.0f;    // seconds remaining
    float maxLife = 1.0f;    // original duration (used for alpha fall-off)
    float size    = 3.0f;    // radius in pixels (unscaled by zoom)
    RGBA  color   = {255, 255, 255, 255};

    // Advance by dt seconds.  Returns true while still alive.
    bool tick(float dt) {
        pos.x += vel.x * dt;
        pos.y += vel.y * dt;
        life  -= dt;
        return life > 0.0f;
    }
};

// ─── Screen shake ─────────────────────────────────────────────────────────────
//
// Free function so tests can verify decay behaviour without SDL.
// Returns the updated shake magnitude after one render frame of dt seconds.

inline float shakeDecay(float shakeAmt, float dt) {
    return shakeAmt * std::exp(-8.0f * dt);
}
