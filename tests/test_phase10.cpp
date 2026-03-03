#include "doctest.h"
#include "effects.hpp"

// ─── Animation::frameAt ──────────────────────────────────────────────────────

TEST_CASE("Animation::frameAt returns 0 for empty animation") {
    Animation anim;
    CHECK(anim.frameAt(0)   == 0);
    CHECK(anim.frameAt(100) == 0);
}

TEST_CASE("Animation::frameAt: looping, two equal-duration frames") {
    Animation anim;
    anim.frames = { {0,0,0,0, 5}, {0,0,0,0, 5} };
    anim.loop = true;

    CHECK(anim.frameAt(0)  == 0);
    CHECK(anim.frameAt(4)  == 0);
    CHECK(anim.frameAt(5)  == 1);
    CHECK(anim.frameAt(9)  == 1);
    CHECK(anim.frameAt(10) == 0);   // wraps back to frame 0
    CHECK(anim.frameAt(15) == 1);   // second period, frame 1
}

TEST_CASE("Animation::frameAt: looping, three frames with different durations") {
    Animation anim;
    anim.frames = { {0,0,0,0, 2}, {0,0,0,0, 3}, {0,0,0,0, 1} };
    anim.loop = true;

    // Period = 6
    CHECK(anim.frameAt(0) == 0);
    CHECK(anim.frameAt(1) == 0);
    CHECK(anim.frameAt(2) == 1);
    CHECK(anim.frameAt(4) == 1);
    CHECK(anim.frameAt(5) == 2);
    CHECK(anim.frameAt(6) == 0);   // wraps
}

TEST_CASE("Animation::frameAt: one-shot clamps to last frame") {
    Animation anim;
    anim.frames = { {0,0,0,0, 3}, {0,0,0,0, 3} };
    anim.loop = false;

    CHECK(anim.frameAt(0)   == 0);
    CHECK(anim.frameAt(2)   == 0);
    CHECK(anim.frameAt(3)   == 1);
    CHECK(anim.frameAt(5)   == 1);
    CHECK(anim.frameAt(6)   == 1);    // clamps — does NOT wrap
    CHECK(anim.frameAt(100) == 1);    // still last frame
}

TEST_CASE("Animation::frameAt: one-shot single frame always returns 0") {
    Animation anim;
    anim.frames = { {0,0,0,0, 10} };
    anim.loop = false;
    CHECK(anim.frameAt(0)   == 0);
    CHECK(anim.frameAt(9)   == 0);
    CHECK(anim.frameAt(999) == 0);
}

// ─── Particle::tick ──────────────────────────────────────────────────────────

TEST_CASE("Particle::tick reduces life by dt") {
    Particle p;
    p.life = 1.0f; p.maxLife = 1.0f;
    p.vel  = {0, 0};
    bool alive = p.tick(0.3f);
    CHECK(alive);
    CHECK(p.life == doctest::Approx(0.7f));
}

TEST_CASE("Particle::tick returns false when life reaches zero") {
    Particle p;
    p.life = 0.1f; p.maxLife = 1.0f;
    bool alive = p.tick(0.2f);
    CHECK(!alive);
}

TEST_CASE("Particle::tick returns false on exact zero") {
    Particle p;
    p.life = 0.5f; p.maxLife = 1.0f; p.vel = {0, 0};
    bool alive = p.tick(0.5f);
    CHECK(!alive);
}

TEST_CASE("Particle::tick moves pos by vel * dt") {
    Particle p;
    p.pos = {0, 0}; p.vel = {2.0f, -1.0f};
    p.life = 1.0f; p.maxLife = 1.0f;
    p.tick(0.5f);
    CHECK(p.pos.x == doctest::Approx(1.0f));
    CHECK(p.pos.y == doctest::Approx(-0.5f));
}

TEST_CASE("Particle alive after first tick, dead after second") {
    Particle p;
    p.life = 0.05f; p.maxLife = 1.0f; p.vel = {0, 0};
    CHECK( p.tick(0.03f));   // 0.02 remaining → alive
    CHECK(!p.tick(0.03f));   // negative → dead
}

// ─── shakeDecay ──────────────────────────────────────────────────────────────

TEST_CASE("shakeDecay: magnitude strictly decreases each frame") {
    float shake = 10.0f;
    float dt    = 1.0f / 60.0f;
    for (int i = 0; i < 60; ++i) {
        float next = shakeDecay(shake, dt);
        CHECK(next < shake);
        shake = next;
    }
}

TEST_CASE("shakeDecay: magnitude remains positive") {
    float shake = 10.0f;
    float dt    = 1.0f / 60.0f;
    for (int i = 0; i < 120; ++i)
        shake = shakeDecay(shake, dt);
    CHECK(shake > 0.0f);
}

TEST_CASE("shakeDecay: magnitude reaches near-zero after many frames") {
    float shake = 10.0f;
    float dt    = 1.0f / 60.0f;
    for (int i = 0; i < 300; ++i)
        shake = shakeDecay(shake, dt);
    CHECK(shake < 0.01f);
}

TEST_CASE("shakeDecay: zero input stays zero") {
    CHECK(shakeDecay(0.0f, 1.0f / 60.0f) == doctest::Approx(0.0f));
}
