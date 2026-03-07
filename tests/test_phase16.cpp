#include "doctest.h"
#include "game.hpp"
#include "renderer.hpp"
#include "ui.hpp"
#include <cmath>

// ─── screenToTile ─────────────────────────────────────────────────────────────
//
// Round-trip: project a known tile to screen pixels, then invert back.
// The result must match the original tile (within floor-rounding).

TEST_CASE("screenToTile: round-trip at default camera") {
    Camera cam;
    cam.pos    = {0.0f, 0.0f};
    cam.target = {0.0f, 0.0f};
    cam.zoom   = 1.0f;
    cam.z      = 0.0f;
    cam.targetZ = 0.0f;

    // Project tile (3, 2) to screen pixels using the forward formula.
    // At z == cam.z, f == 1, so:
    //   px = VIEWPORT_W/2 + (tileX - cam.x) * TILE_SIZE * zoom
    //   py = VIEWPORT_H/2 + (tileY - cam.y) * TILE_H    * zoom
    float ts  = Renderer::TILE_SIZE * cam.zoom;
    float tsH = Renderer::TILE_H    * cam.zoom;
    int px = static_cast<int>(std::round(Renderer::VIEWPORT_W / 2.0f + (3.0f - cam.pos.x) * ts));
    int py = static_cast<int>(std::round(Renderer::VIEWPORT_H / 2.0f + (2.0f - cam.pos.y) * tsH));

    TilePos result = Renderer::screenToTile(px, py, cam);
    CHECK(result.x == 3);
    CHECK(result.y == 2);
}

TEST_CASE("screenToTile: round-trip with non-zero camera position") {
    Camera cam;
    cam.pos    = {5.0f, 3.0f};
    cam.target = cam.pos;
    cam.zoom   = 1.0f;
    cam.z      = 0.0f;
    cam.targetZ = 0.0f;

    float ts  = Renderer::TILE_SIZE * cam.zoom;
    float tsH = Renderer::TILE_H    * cam.zoom;
    // Tile at (-1, 4)
    int px = static_cast<int>(std::round(Renderer::VIEWPORT_W / 2.0f + (-1.0f - cam.pos.x) * ts));
    int py = static_cast<int>(std::round(Renderer::VIEWPORT_H / 2.0f + ( 4.0f - cam.pos.y) * tsH));

    TilePos result = Renderer::screenToTile(px, py, cam);
    CHECK(result.x == -1);
    CHECK(result.y == 4);
}

TEST_CASE("screenToTile: round-trip with zoom > 1") {
    Camera cam;
    cam.pos    = {0.0f, 0.0f};
    cam.target = cam.pos;
    cam.zoom   = 2.0f;
    cam.z      = 0.0f;
    cam.targetZ = 0.0f;

    float ts  = Renderer::TILE_SIZE * cam.zoom;
    float tsH = Renderer::TILE_H    * cam.zoom;
    int px = static_cast<int>(std::round(Renderer::VIEWPORT_W / 2.0f + (1.0f - cam.pos.x) * ts));
    int py = static_cast<int>(std::round(Renderer::VIEWPORT_H / 2.0f + (1.0f - cam.pos.y) * tsH));

    TilePos result = Renderer::screenToTile(px, py, cam);
    CHECK(result.x == 1);
    CHECK(result.y == 1);
}

TEST_CASE("screenToTile: z from camera") {
    Camera cam;
    cam.pos    = {0.0f, 0.0f};
    cam.target = cam.pos;
    cam.zoom   = 1.0f;
    cam.z      = 2.0f;   // elevated camera
    cam.targetZ = cam.z;

    TilePos result = Renderer::screenToTile(
        Renderer::VIEWPORT_W / 2,
        Renderer::VIEWPORT_H / 2, cam);
    // Centre of screen at cam position → tile near cam.pos
    CHECK(result.z == 2);
}

// ─── entityAtTile ─────────────────────────────────────────────────────────────

TEST_CASE("entityAtTile: returns player at player position") {
    Game game;
    TilePos pp = game.playerPos();
    const Entity* e = game.entityAtTile(pp);
    REQUIRE(e != nullptr);
    CHECK(e->type == EntityType::Player);
}

TEST_CASE("entityAtTile: returns nullptr for empty tile") {
    Game game;
    // A tile very far from any demo entity.
    const Entity* e = game.entityAtTile({100, 100, 0});
    CHECK(e == nullptr);
}

// ─── UIState ─────────────────────────────────────────────────────────────────

TEST_CASE("UIState: open sets panel, close clears it") {
    UIState ui;
    CHECK(!ui.isOpen());
    ui.open(ActivePanel::Controls);
    CHECK(ui.isOpen());
    CHECK(ui.is(ActivePanel::Controls));
    ui.close();
    CHECK(!ui.isOpen());
}

TEST_CASE("UIState: opening a panel closes the previous one") {
    UIState ui;
    ui.open(ActivePanel::Controls);
    ui.open(ActivePanel::Recordings);
    CHECK(ui.is(ActivePanel::Recordings));
    CHECK(!ui.is(ActivePanel::Controls));
}

// ─── Rect ─────────────────────────────────────────────────────────────────────

TEST_CASE("Rect: contains is correct") {
    Rect r{10, 20, 100, 50};
    CHECK( r.contains(10, 20));   // top-left
    CHECK( r.contains(50, 40));   // inside
    CHECK(!r.contains(110, 40));  // right edge (exclusive)
    CHECK(!r.contains(10, 70));   // bottom edge (exclusive)
    CHECK(!r.contains(9, 20));    // just outside left
}

// ─── ListWidget ───────────────────────────────────────────────────────────────

TEST_CASE("ListWidget: itemAt returns correct row index") {
    ListWidget lw;
    lw.bounds = {0, 0, 200, 100};
    lw.rowH   = 20;
    lw.rows   = {{"a"}, {"b"}, {"c"}};

    CHECK(lw.itemAt(5)  == 0);   // first row
    CHECK(lw.itemAt(25) == 1);   // second row
    CHECK(lw.itemAt(45) == 2);   // third row
    CHECK(lw.itemAt(-1) == -1);  // above
    CHECK(lw.itemAt(100) == -1); // below (exclusive)
}

TEST_CASE("ListWidget: scrollTo brings index into view") {
    ListWidget lw;
    lw.bounds = {0, 0, 200, 40};   // 2 visible rows (rowH=20)
    lw.rowH   = 20;
    lw.rows   = {{"a"}, {"b"}, {"c"}, {"d"}};

    lw.scrollTo(3);   // need to scroll to row 3
    CHECK(lw.scrollOffset == 2);  // rows 2+3 now visible

    lw.scrollTo(0);   // scroll back to top
    CHECK(lw.scrollOffset == 0);
}

// ─── queueClickMove ───────────────────────────────────────────────────────────

TEST_CASE("queueClickMove: player moves one step toward target on next tick") {
    Game  game;
    Input input;

    TilePos before = game.playerPos();
    // Click two tiles east and two tiles south.
    game.queueClickMove({before.x + 2, before.y + 2, before.z});

    input.beginFrame();
    game.tick(input, 0);

    TilePos after = game.playerDestination();
    // Destination should be one step diagonally SE.
    CHECK(after.x == before.x + 1);
    CHECK(after.y == before.y + 1);
}

TEST_CASE("queueClickMove: no-op when target equals current pos") {
    Game  game;
    Input input;

    TilePos before = game.playerPos();
    game.queueClickMove(before);   // same tile

    input.beginFrame();
    game.tick(input, 0);

    CHECK(game.playerDestination() == before);  // didn't move
}
