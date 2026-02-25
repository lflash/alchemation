#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "types.hpp"
#include "terrain.hpp"

// ─── TilePos ─────────────────────────────────────────────────────────────────

TEST_CASE("TilePos addition") {
    TilePos a = {1, 2};
    TilePos b = {3, -1};
    TilePos c = a + b;
    CHECK(c.x == 4);
    CHECK(c.y == 1);
}

TEST_CASE("TilePos subtraction") {
    TilePos a = {5, 3};
    TilePos b = {2, 4};
    TilePos c = a - b;
    CHECK(c.x == 3);
    CHECK(c.y == -1);
}

TEST_CASE("TilePos equality") {
    CHECK(TilePos{1, 2} == TilePos{1, 2});
    CHECK(TilePos{1, 2} != TilePos{1, 3});
}

TEST_CASE("TilePos scalar multiply") {
    TilePos a = {3, -2};
    TilePos b = a * 2;
    CHECK(b.x == 6);
    CHECK(b.y == -4);
}

// ─── Vec2f lerp ──────────────────────────────────────────────────────────────

TEST_CASE("lerp at t=0 returns a") {
    Vec2f result = lerp({1.0f, 2.0f}, {5.0f, 6.0f}, 0.0f);
    CHECK(result.x == doctest::Approx(1.0f));
    CHECK(result.y == doctest::Approx(2.0f));
}

TEST_CASE("lerp at t=1 returns b") {
    Vec2f result = lerp({1.0f, 2.0f}, {5.0f, 6.0f}, 1.0f);
    CHECK(result.x == doctest::Approx(5.0f));
    CHECK(result.y == doctest::Approx(6.0f));
}

TEST_CASE("lerp at t=0.5 returns midpoint") {
    Vec2f result = lerp({0.0f, 0.0f}, {4.0f, 8.0f}, 0.5f);
    CHECK(result.x == doctest::Approx(2.0f));
    CHECK(result.y == doctest::Approx(4.0f));
}

// ─── TilePosHash ─────────────────────────────────────────────────────────────

TEST_CASE("TilePosHash: equal positions produce equal hashes") {
    TilePosHash h;
    CHECK(h({3, 7}) == h({3, 7}));
}

TEST_CASE("TilePosHash: different positions have different hashes (spot check)") {
    TilePosHash h;
    // Not a guarantee for all inputs, but should hold for these simple cases
    CHECK(h({0, 0}) != h({1, 0}));
    CHECK(h({0, 0}) != h({0, 1}));
    CHECK(h({1, 0}) != h({0, 1}));
}

// ─── Terrain ─────────────────────────────────────────────────────────────────

TEST_CASE("heightAt returns consistent value for same TilePos") {
    Terrain t;
    float first  = t.heightAt({5, 10});
    float second = t.heightAt({5, 10});
    CHECK(first == doctest::Approx(second));
}

TEST_CASE("heightAt returns value in expected range") {
    Terrain t;
    // Perlin noise output is in [-1, 1]; FBm with 4 octaves can approach but
    // not reliably exceed this. Check a spread of tiles.
    for (int x = -10; x <= 10; x++) {
        for (int y = -10; y <= 10; y++) {
            float h = t.heightAt({x, y});
            CHECK(h >= -2.0f);   // generous bound
            CHECK(h <=  2.0f);
        }
    }
}

TEST_CASE("heightAt produces variation across tiles") {
    Terrain t;
    // Classic Perlin returns 0 at integer grid points, so don't test specific pairs.
    // Instead verify that across a spread of tiles the output is not constant.
    float first = t.heightAt({0, 0});
    bool anyDiffers = false;
    for (int x = -5; x <= 5 && !anyDiffers; x++)
        for (int y = -5; y <= 5 && !anyDiffers; y++)
            if (t.heightAt({x, y}) != doctest::Approx(first))
                anyDiffers = true;
    CHECK(anyDiffers);
}

TEST_CASE("typeAt returns Grass by default") {
    Terrain t;
    CHECK(t.typeAt({0,  0}) == TileType::Grass);
    CHECK(t.typeAt({3, -5}) == TileType::Grass);
}

TEST_CASE("typeAt returns BareEarth after dig") {
    Terrain t;
    t.dig({2, 3});
    CHECK(t.typeAt({2, 3}) == TileType::BareEarth);
}

TEST_CASE("dig does not affect other tiles") {
    Terrain t;
    t.dig({2, 3});
    CHECK(t.typeAt({2, 4}) == TileType::Grass);
    CHECK(t.typeAt({3, 3}) == TileType::Grass);
}

TEST_CASE("restore reverts BareEarth to Grass") {
    Terrain t;
    t.dig({1, 1});
    REQUIRE(t.typeAt({1, 1}) == TileType::BareEarth);
    t.restore({1, 1});
    CHECK(t.typeAt({1, 1}) == TileType::Grass);
}

TEST_CASE("restore on unmodified tile is a no-op") {
    Terrain t;
    t.restore({0, 0});   // should not throw or change anything
    CHECK(t.typeAt({0, 0}) == TileType::Grass);
}
