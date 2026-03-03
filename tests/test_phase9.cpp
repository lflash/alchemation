#include "doctest.h"

#include "spatial.hpp"
#include "entity.hpp"
#include "terrain.hpp"
#include "game.hpp"
#include "input.hpp"

#include <algorithm>
#include <cmath>

// ─── z-level collision isolation ─────────────────────────────────────────────

TEST_CASE("entities at same (x,y) but different z do not collide") {
    EntityRegistry registry;
    SpatialGrid    spatial;

    // Mover at z=0 heading to (1,0,1) — different z from blocker at (1,0,0).
    EntityID mover   = registry.spawn(EntityType::Goblin, {0, 0, 0});
    EntityID blocker = registry.spawn(EntityType::Goblin, {1, 0, 0});
    Entity*  em = registry.get(mover);
    Entity*  eb = registry.get(blocker);

    spatial.add(mover,   em->pos, em->size);
    spatial.add(blocker, eb->pos, eb->size);

    // Mover targets (1,0,1) — z=1 plane, where blocker is NOT registered.
    std::vector<MoveIntention> intentions = {
        { mover, {0,0,0}, {1,0,1}, em->type, em->size },
    };

    auto allowed = resolveMoves(intentions, spatial, registry);
    CHECK(allowed.count(mover));   // not blocked — different z levels
}

TEST_CASE("entities at same (x,y,z) still collide") {
    EntityRegistry registry;
    SpatialGrid    spatial;

    EntityID mover   = registry.spawn(EntityType::Goblin, {0, 0, 0});
    EntityID blocker = registry.spawn(EntityType::Goblin, {1, 0, 0});
    Entity*  em = registry.get(mover);
    Entity*  eb = registry.get(blocker);

    spatial.add(mover,   em->pos, em->size);
    spatial.add(blocker, eb->pos, eb->size);

    std::vector<MoveIntention> intentions = {
        { mover, {0,0,0}, {1,0,0}, em->type, em->size },
    };

    auto allowed = resolveMoves(intentions, spatial, registry);
    CHECK(!allowed.count(mover));  // blocked — same z level
}

// ─── Terrain::heightAt z-invariance ──────────────────────────────────────────

TEST_CASE("heightAt ignores z coordinate") {
    Terrain t;
    CHECK(t.heightAt({5, 10, 0}) == doctest::Approx(t.heightAt({5, 10, 3})));
    CHECK(t.heightAt({0,  0, 0}) == doctest::Approx(t.heightAt({0,  0, -2})));
}

// ─── Terrain::levelAt ────────────────────────────────────────────────────────

TEST_CASE("levelAt is round(heightAt * 4)") {
    Terrain t;
    const TilePos pts[] = {{0, 0}, {3, 7}, {-5, 2}, {10, -3}};
    for (auto p : pts)
        CHECK(t.levelAt(p) == static_cast<int>(std::round(t.heightAt(p) * 4.0f)));
}

TEST_CASE("levelAt returns consistent value for same TilePos") {
    Terrain t;
    CHECK(t.levelAt({3, 7}) == t.levelAt({3, 7}));
    CHECK(t.levelAt({0, 0}) == t.levelAt({0, 0}));
}

TEST_CASE("levelAt ignores z coordinate") {
    Terrain t;
    CHECK(t.levelAt({5, 10, 0}) == t.levelAt({5, 10, 3}));
    CHECK(t.levelAt({0,  0, 0}) == t.levelAt({0,  0, -2}));
}

// ─── SpatialGrid::query z-plane filtering ────────────────────────────────────

TEST_CASE("query returns entity at matching z level") {
    SpatialGrid    spatial;
    EntityRegistry registry;

    EntityID id = registry.spawn(EntityType::Goblin, {2, 2, 1});
    spatial.add(id, {2, 2, 1}, {1.0f, 1.0f});

    auto found = spatial.query({{2.0f, 2.0f}, {3.0f, 3.0f}}, 1);
    CHECK(std::find(found.begin(), found.end(), id) != found.end());
}

TEST_CASE("query excludes entities at different z level") {
    SpatialGrid    spatial;
    EntityRegistry registry;

    EntityID id = registry.spawn(EntityType::Goblin, {2, 2, 0});
    spatial.add(id, {2, 2, 0}, {1.0f, 1.0f});

    // z=1 query: entity registered at z=0 must not appear
    auto at1 = spatial.query({{2.0f, 2.0f}, {3.0f, 3.0f}}, 1);
    CHECK(std::find(at1.begin(), at1.end(), id) == at1.end());

    // z=0 query: entity must appear
    auto at0 = spatial.query({{2.0f, 2.0f}, {3.0f, 3.0f}}, 0);
    CHECK(std::find(at0.begin(), at0.end(), id) != at0.end());
}

// ─── Game integration: z-level tracking & height blocking ────────────────────

static SDL_Event makeKeyDown(SDL_Keycode key) {
    SDL_Event e{};
    e.type           = SDL_KEYDOWN;
    e.key.keysym.sym = key;
    e.key.repeat     = 0;
    return e;
}

static SDL_Event makeKeyUp(SDL_Keycode key) {
    SDL_Event e{};
    e.type           = SDL_KEYUP;
    e.key.keysym.sym = key;
    return e;
}

// Move the player one tile and wait for arrival (speed=0.1 → 10 ticks).
static void walkOne(Game& game, Input& input, Tick& tick, SDL_Keycode key) {
    input.beginFrame();
    input.handleEvent(makeKeyDown(key));
    game.tick(input, tick++);

    input.beginFrame();
    input.handleEvent(makeKeyUp(key));
    for (int w = 0; w < 15; ++w) {
        if (game.playerPos() == game.playerDestination()) break;
        game.tick(input, tick++);
    }
}

TEST_CASE("player z matches terrain level at spawn") {
    Game    game;
    TilePos pos = game.playerPos();
    CHECK(pos.z == game.terrain().levelAt(pos));
}

TEST_CASE("player destination z is set from terrain when initiating a move") {
    Game  game;
    Input input;

    input.beginFrame();
    input.handleEvent(makeKeyDown(SDLK_d));
    game.tick(input, 0);

    TilePos dest = game.playerDestination();
    CHECK(dest.z == game.terrain().levelAt(dest));
}

TEST_CASE("player is blocked from moving to tile with height diff > 1") {
    // Phase 1: find the first steep adjacent pair anywhere in a 201×201 scan.
    struct SteepPair { TilePos src; TilePos dst; TilePos delta; };
    std::optional<SteepPair> steep;
    {
        Terrain t;
        const TilePos dirs4[] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0}};
        for (int x = -100; x <= 100 && !steep; ++x) {
            for (int y = -100; y <= 100 && !steep; ++y) {
                TilePos s{x, y, 0};
                s.z = t.levelAt(s);
                for (auto& d : dirs4) {
                    TilePos dst{x + d.x, y + d.y, 0};
                    dst.z = t.levelAt(dst);
                    if (std::abs(dst.z - s.z) > 1) {
                        steep = SteepPair{s, dst, d};
                        break;
                    }
                }
            }
        }
    }
    REQUIRE_MESSAGE(steep.has_value(), "no steep terrain found in 201x201 scan");

    // Phase 2: navigate the player to steep.src via manhattan walk (X then Y).
    Game  game;
    Input input;
    Tick  tick = 0;

    auto navTo = [&](int tx, int ty) -> bool {
        while (game.playerPos().x != tx || game.playerPos().y != ty) {
            TilePos before = game.playerPos();
            SDL_Keycode key;
            if      (before.x < tx) key = SDLK_d;
            else if (before.x > tx) key = SDLK_a;
            else if (before.y < ty) key = SDLK_s;
            else                    key = SDLK_w;

            walkOne(game, input, tick, key);
            if (game.playerPos() == before) return false;  // blocked mid-path
            if (tick > 5000) return false;                 // timeout
        }
        return true;
    };

    if (!navTo(steep->src.x, steep->src.y)) {
        MESSAGE("could not navigate to steep terrain src — path itself blocked");
        return;
    }

    // Phase 3: attempt the steep step and verify the player does not move.
    SDL_Keycode steep_key;
    if      (steep->delta.x ==  1) steep_key = SDLK_d;
    else if (steep->delta.x == -1) steep_key = SDLK_a;
    else if (steep->delta.y ==  1) steep_key = SDLK_s;
    else                           steep_key = SDLK_w;

    TilePos src = game.playerPos();
    input.beginFrame();
    input.handleEvent(makeKeyDown(steep_key));
    game.tick(input, tick);

    CHECK(game.playerPos()         == src);
    CHECK(game.playerDestination() == src);
}
