#include "doctest.h"
#include "alchemy.hpp"
#include "game.hpp"
#include "input.hpp"

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
