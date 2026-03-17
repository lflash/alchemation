#include "doctest.h"
#include "alchemy.hpp"
#include "game.hpp"
#include "input.hpp"
#include <cmath>

// ─── PrincipleProfile ─────────────────────────────────────────────────────────

TEST_CASE("PrincipleProfile: default-constructed is all zero") {
    PrincipleProfile p{};
    CHECK(p.heat == 0); CHECK(p.cold == 0);
    CHECK(p.wet  == 0); CHECK(p.dry  == 0);
    CHECK(p.life == 0); CHECK(p.death == 0);
    CHECK(p.pos  == 0); CHECK(p.neg   == 0);
    CHECK(p.adhesive == 0); CHECK(p.repellent == 0);
}

// ─── principleProfile factory ─────────────────────────────────────────────────

TEST_CASE("principleProfile: unknown entity type is all zero") {
    PrincipleProfile p = principleProfile(EntityType::Rock);
    CHECK(p.heat == 0); CHECK(p.life == 0); CHECK(p.wet == 0);
}

TEST_CASE("principleProfile: Campfire is hot and dry") {
    PrincipleProfile p = principleProfile(EntityType::Campfire);
    CHECK(p.heat  == 100);
    CHECK(p.dry   ==  40);
    CHECK(p.life  ==   0);
    CHECK(p.cold  ==   0);
}

TEST_CASE("principleProfile: Rabbit has life and adhesive") {
    PrincipleProfile p = principleProfile(EntityType::Rabbit);
    CHECK(p.life     == 80);
    CHECK(p.adhesive == 40);
    CHECK(p.heat     ==  0);
}

TEST_CASE("principleProfile: Water is fully wet") {
    PrincipleProfile p = principleProfile(EntityType::Water);
    CHECK(p.wet  == 100);
    CHECK(p.life ==   0);
}

TEST_CASE("principleProfile: Battery emits positive galvanic field") {
    PrincipleProfile p = principleProfile(EntityType::Battery);
    CHECK(p.pos  == 100);
    CHECK(p.neg  ==   0);
    CHECK(p.life ==   0);
}

TEST_CASE("principleProfile: Player and Goblin emit repellent field") {
    PrincipleProfile player = principleProfile(EntityType::Player);
    PrincipleProfile goblin = principleProfile(EntityType::Goblin);
    CHECK(player.repellent > 0);
    CHECK(goblin.repellent > 0);
}

TEST_CASE("principleProfile: Meat has life; CookedMeat has life and heat") {
    PrincipleProfile meat   = principleProfile(EntityType::Meat);
    PrincipleProfile cooked = principleProfile(EntityType::CookedMeat);
    CHECK(meat.life   > 0);
    CHECK(meat.heat   == 0);
    CHECK(cooked.life > 0);
    CHECK(cooked.heat > 0);
}

// ─── responseProfile factory ──────────────────────────────────────────────────

TEST_CASE("responseProfile: non-behaving entity is all-zero response") {
    ResponseProfile r = responseProfile(EntityType::Rock);
    CHECK(r.heat == 0); CHECK(r.cold == 0);
    CHECK(r.wet  == 0); CHECK(r.dry  == 0);
    CHECK(r.life == 0); CHECK(r.death == 0);
    CHECK(r.pos  == 0); CHECK(r.neg   == 0);
    CHECK(r.adhesive == 0); CHECK(r.repellent == 0);
}

TEST_CASE("responseProfile: Player has all-zero response (not driven by fields)") {
    ResponseProfile r = responseProfile(EntityType::Player);
    CHECK(r.heat == 0); CHECK(r.life == 0); CHECK(r.adhesive == 0);
}

TEST_CASE("responseProfile: Goblin unloaded seeks life and flees repellent") {
    ResponseProfile r = responseProfile(EntityType::Goblin, /*loaded=*/false);
    CHECK(r.life      >  0);   // attracted to Life field (prey/food)
    CHECK(r.repellent <  0);   // repelled by Repellent field (threats)
    CHECK(r.heat      == 0);   // doesn't seek warmth when unloaded
    CHECK(r.manaMax   >  0);   // has hunger scaling
}

TEST_CASE("responseProfile: Goblin loaded seeks heat and ignores life") {
    ResponseProfile r = responseProfile(EntityType::Goblin, /*loaded=*/true);
    CHECK(r.heat   > 0);    // seeks campfire to cook
    CHECK(r.life   == 0);   // no prey-seeking when loaded
    CHECK(r.manaMax == 0);  // no hunger scaling when loaded
}

TEST_CASE("responseProfile: Rabbit seeks life and adhesive, flees repellent") {
    ResponseProfile r = responseProfile(EntityType::Rabbit);
    CHECK(r.life      > 0);   // attracted to Life field (grass)
    CHECK(r.adhesive  > 0);   // drawn toward warren
    CHECK(r.repellent < 0);   // flees repellent sources
}

TEST_CASE("responseProfile: wander/urgency parameters are set for behaving entities") {
    ResponseProfile g = responseProfile(EntityType::Goblin);
    ResponseProfile rb = responseProfile(EntityType::Rabbit);
    CHECK(g.wanderRate       > 0);
    CHECK(g.urgencyThreshold > 0);
    CHECK(rb.wanderRate       > 0);
    CHECK(rb.urgencyThreshold > 0);
}

// ─── Behavioural consistency (net field score = resp · pp) ───────────────────
//
// Compute the raw dot product of a ResponseProfile against a PrincipleProfile.
// A positive value means the entity is attracted toward the source at any distance.

static float netAttraction(const ResponseProfile& resp, const PrincipleProfile& src) {
    return float(resp.heat)      * float(src.heat)
         + float(resp.cold)      * float(src.cold)
         + float(resp.wet)       * float(src.wet)
         + float(resp.dry)       * float(src.dry)
         + float(resp.life)      * float(src.life)
         + float(resp.death)     * float(src.death)
         + float(resp.pos)       * float(src.pos)
         + float(resp.neg)       * float(src.neg)
         + float(resp.adhesive)  * float(src.adhesive)
         + float(resp.repellent) * float(src.repellent);
}

TEST_CASE("net attraction: unloaded goblin FLEES from player") {
    ResponseProfile resp = responseProfile(EntityType::Goblin, false);
    PrincipleProfile src = principleProfile(EntityType::Player);
    CHECK(netAttraction(resp, src) < 0.f);
}

TEST_CASE("net attraction: unloaded goblin is ATTRACTED to rabbit") {
    ResponseProfile resp = responseProfile(EntityType::Goblin, false);
    PrincipleProfile src = principleProfile(EntityType::Rabbit);
    CHECK(netAttraction(resp, src) > 0.f);
}

TEST_CASE("net attraction: unloaded goblin is ATTRACTED to loose meat") {
    ResponseProfile resp = responseProfile(EntityType::Goblin, false);
    PrincipleProfile src = principleProfile(EntityType::Meat);
    CHECK(netAttraction(resp, src) > 0.f);
}

TEST_CASE("net attraction: loaded goblin is ATTRACTED to campfire") {
    ResponseProfile resp = responseProfile(EntityType::Goblin, true);
    PrincipleProfile src = principleProfile(EntityType::Campfire);
    CHECK(netAttraction(resp, src) > 0.f);
}

TEST_CASE("net attraction: loaded goblin seeks campfire MORE than rabbit") {
    ResponseProfile resp = responseProfile(EntityType::Goblin, true);
    PrincipleProfile fire   = principleProfile(EntityType::Campfire);
    PrincipleProfile rabbit = principleProfile(EntityType::Rabbit);
    // Campfire has high Heat — loaded goblin's heat response should pull it
    // toward fire much more strongly than toward a rabbit.
    CHECK(netAttraction(resp, fire) > netAttraction(resp, rabbit));
}

TEST_CASE("net attraction: rabbit FLEES from player") {
    ResponseProfile resp = responseProfile(EntityType::Rabbit);
    PrincipleProfile src = principleProfile(EntityType::Player);
    CHECK(netAttraction(resp, src) < 0.f);
}

TEST_CASE("net attraction: rabbit FLEES from goblin") {
    ResponseProfile resp = responseProfile(EntityType::Rabbit);
    PrincipleProfile src = principleProfile(EntityType::Goblin);
    CHECK(netAttraction(resp, src) < 0.f);
}

TEST_CASE("net attraction: rabbit is ATTRACTED to warren") {
    ResponseProfile resp = responseProfile(EntityType::Rabbit);
    PrincipleProfile src = principleProfile(EntityType::Warren);
    CHECK(netAttraction(resp, src) > 0.f);
}

TEST_CASE("net attraction: rabbit is ATTRACTED to long grass (food)") {
    ResponseProfile resp = responseProfile(EntityType::Rabbit);
    PrincipleProfile src = principleProfile(EntityType::LongGrass);
    CHECK(netAttraction(resp, src) > 0.f);
}

// ─── Integration: player position unaffected by tickResponseMovement ──────────
//
// The player has no response profile (all zeros), so tickResponseMovement must
// never queue a move for the player.

TEST_CASE("tickResponseMovement: player does not wander") {
    Game g;
    Input idle;
    TilePos start = g.playerPos();

    // Run enough ticks that any entity with a wander rate would have moved,
    // but the player should stay exactly at its starting position.
    for (int i = 0; i < 200; ++i) {
        idle.beginFrame();
        g.tick(idle, static_cast<Tick>(i));
    }

    CHECK(g.playerPos() == start);
}

// ─── Simulation helpers ───────────────────────────────────────────────────────

static const Entity* findByType(const Game& g, EntityType type) {
    for (const Entity* e : g.drawOrder())
        if (e->type == type) return e;
    return nullptr;
}

static float dist2(TilePos a, TilePos b) {
    float dx = float(a.x - b.x), dy = float(a.y - b.y);
    return dx*dx + dy*dy;
}

// Key helpers reused from test_phase18.
static SDL_Event simKeyDown(SDL_Keycode sym) {
    SDL_Event e{};
    e.type = SDL_KEYDOWN;
    e.key.keysym.sym = sym;
    return e;
}
static SDL_Event simKeyUp(SDL_Keycode sym) {
    SDL_Event e{};
    e.type = SDL_KEYUP;
    e.key.keysym.sym = sym;
    return e;
}

// Press a movement key for one tick, release it, then idle until the player
// animation settles (same pattern as pressMove in test_phase18).
static void simMove(Game& g, Input& inp, SDL_Keycode key, Tick& t,
                    int maxWait = 30) {
    inp.beginFrame();
    inp.handleEvent(simKeyDown(key));
    g.tick(inp, t++);

    inp.beginFrame();
    inp.handleEvent(simKeyUp(key));
    g.tick(inp, t++);

    Input idle;
    for (int i = 0; i < maxWait; ++i) {
        idle.beginFrame();
        g.tick(idle, t++);
    }
}

// ─── Simulation: goblin field-repulsion from idle player ─────────────────────
//
// The default world spawns a goblin at {5,5} and the player at {0,0}.
// The goblin's unloaded response profile has resp.repellent = -80, and the
// player emits repellent = 80.  Net dot-product per unit = 80*50 + (-80)*80
// = 4000 - 6400 = -2400 (strongly negative → flee).  After many idle ticks the
// goblin should wander away, never reducing its distance to the player.

TEST_CASE("simulation: goblin drifts away from idle player over 500 ticks") {
    Game g;
    Input idle;

    // Settle one tick so entities are fully registered.
    idle.beginFrame();
    g.tick(idle, 0);

    const Entity* goblin = findByType(g, EntityType::Goblin);
    REQUIRE(goblin != nullptr);

    float startDist2 = dist2(goblin->pos, g.playerPos());

    for (Tick t = 1; t <= 500; ++t) {
        idle.beginFrame();
        g.tick(idle, t);
    }

    // Re-query goblin after ticking (pointer from drawOrder() is into the live
    // registry; re-fetch to be safe).
    goblin = findByType(g, EntityType::Goblin);
    REQUIRE(goblin != nullptr);

    float finalDist2 = dist2(goblin->pos, g.playerPos());

    // The goblin should never have moved meaningfully closer to the player.
    // Allow a 4-unit slack (within one tile) in case a wander step briefly
    // moved it sideways before the best direction was chosen.
    CHECK(finalDist2 >= startDist2 - 4.f);
}

// ─── Simulation: goblin urgently flees when player is within urgency range ────
//
// urgencyThreshold = 200.  Field score at distance d is 2400/(d²+1).
// This exceeds 200 when d² < 11 (i.e., within ~3.3 tiles).
// Navigate the player to {4,1} (dist² from goblin {5,5} = 1+16 = 17 — score
// ≈ 133 per direction, and the worst direction score ≈ 218 > 200, triggering
// urgent movement every tick).  Tick several more times and verify the goblin
// has moved away from its spawn position.

TEST_CASE("simulation: goblin urgently flees player within urgency range") {
    Game g;
    Input inp;
    Tick  t = 0;

    // Settle.
    inp.beginFrame();
    g.tick(inp, t++);

    const Entity* goblin = findByType(g, EntityType::Goblin);
    REQUIRE(goblin != nullptr);
    TilePos goblinStart = goblin->pos;   // {5,5,z}

    // Navigate player from {0,0} east ×4 → {4,0}, then south ×1 → {4,1}.
    // This puts the player ~sqrt(17) ≈ 4.1 tiles from the goblin, inside the
    // urgency radius, so the goblin will move every tick it gets a chance.
    for (int i = 0; i < 4; ++i) simMove(g, inp, SDLK_d, t);
    simMove(g, inp, SDLK_s, t);

    // Idle 30 more ticks to let the goblin react.
    for (int i = 0; i < 30; ++i) {
        inp.beginFrame();
        g.tick(inp, t++);
    }

    goblin = findByType(g, EntityType::Goblin);
    REQUIRE(goblin != nullptr);

    // The goblin must have moved from its spawn tile.
    CHECK(goblin->pos != goblinStart);

    // And it should be further from the player than it started.
    float distAfter = dist2(goblin->pos, g.playerPos());
    float distBefore = dist2(goblinStart, g.playerPos());
    CHECK(distAfter > distBefore);
}

// ─── Simulation: rabbit flees repellent sources ───────────────────────────────
//
// Verify profile constants mean a rabbit scores negatively against both the
// player and a goblin at any distance.  This is a pure-maths check executed at
// simulation time so it sits with the behavioural tests above.

TEST_CASE("simulation: rabbit net-score is negative toward player and goblin") {
    // At distance d, the raw score per principle = resp_p * src_p / (d²+1).
    // Summing over all principles gives the net attraction.  We want it < 0.
    ResponseProfile  rabbitResp = responseProfile(EntityType::Rabbit);
    PrincipleProfile playerSrc  = principleProfile(EntityType::Player);
    PrincipleProfile goblinSrc  = principleProfile(EntityType::Goblin);

    // Check at several distances.
    for (float d2 : { 1.f, 4.f, 9.f, 25.f }) {
        float inv = 1.f / (d2 + 1.f);
        auto score = [&](const PrincipleProfile& src) {
            return ( float(rabbitResp.heat)      * float(src.heat)
                   + float(rabbitResp.life)      * float(src.life)
                   + float(rabbitResp.repellent) * float(src.repellent)
                   + float(rabbitResp.adhesive)  * float(src.adhesive) ) * inv;
        };
        CHECK(score(playerSrc) < 0.f);
        CHECK(score(goblinSrc) < 0.f);
    }
}

// ─── Simulation: goblin urgency radius is consistent with profile values ──────
//
// The urgency threshold (200) and the net field strength (-2400 for
// goblin/player) determine the "panic radius".  Confirm that at dist²=10 the
// score magnitude exceeds the threshold, and at dist²=15 it does not.

TEST_CASE("simulation: goblin urgency radius matches profile constants") {
    ResponseProfile  goblinResp = responseProfile(EntityType::Goblin, false);
    PrincipleProfile playerSrc  = principleProfile(EntityType::Player);

    float threshold = goblinResp.urgencyThreshold;

    auto netScore = [&](float d2) {
        float inv = 1.f / (d2 + 1.f);
        return ( float(goblinResp.life)      * float(playerSrc.life)
               + float(goblinResp.repellent) * float(playerSrc.repellent) ) * inv;
    };

    // At dist²=10 (within urgency range): |score| should exceed threshold.
    CHECK(std::abs(netScore(10.f)) > threshold);

    // At dist²=15 (outside urgency range): |score| should be below threshold.
    CHECK(std::abs(netScore(15.f)) < threshold);
}
