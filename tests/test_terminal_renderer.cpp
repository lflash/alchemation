#include "doctest.h"
#include "terminal_renderer.hpp"
#include "terrain.hpp"

#include <algorithm>
#include <sstream>
#include <string>
#include <vector>

// ─── Helpers ─────────────────────────────────────────────────────────────────

// Parse the output of a single endFrame() call into a vector of rows.
// Strips the leading "\033[H" cursor-reset escape code (3 bytes).
static std::vector<std::string> parseFrame(const std::string& raw) {
    std::string s = (raw.size() >= 3 && raw[0] == '\033') ? raw.substr(3) : raw;
    std::vector<std::string> rows;
    std::istringstream ss(s);
    std::string line;
    while (std::getline(ss, line))
        rows.push_back(line);
    return rows;
}

// Render one frame and return parsed rows.
static std::vector<std::string> renderFrame(
    TerminalRenderer& r, const Terrain& terrain,
    std::ostringstream& oss)
{
    oss.str("");
    r.beginFrame();
    r.drawTerrain(terrain);
    r.endFrame();
    return parseFrame(oss.str());
}

// ─── charForTile ─────────────────────────────────────────────────────────────

TEST_CASE("charForTile: BareEarth always returns '#'") {
    CHECK(TerminalRenderer::charForTile(TileType::BareEarth, { 0,  0}) == '#');
    CHECK(TerminalRenderer::charForTile(TileType::BareEarth, { 5,  3}) == '#');
    CHECK(TerminalRenderer::charForTile(TileType::BareEarth, {-2, -7}) == '#');
}

TEST_CASE("charForTile: Grass alternates '.' and ',' in checkerboard") {
    // Even (x+y) → '.'
    CHECK(TerminalRenderer::charForTile(TileType::Grass, { 0,  0}) == '.');
    CHECK(TerminalRenderer::charForTile(TileType::Grass, { 1,  1}) == '.');
    CHECK(TerminalRenderer::charForTile(TileType::Grass, {-1, -1}) == '.');
    CHECK(TerminalRenderer::charForTile(TileType::Grass, { 2,  0}) == '.');
    // Odd (x+y) → ','
    CHECK(TerminalRenderer::charForTile(TileType::Grass, { 1,  0}) == ',');
    CHECK(TerminalRenderer::charForTile(TileType::Grass, { 0,  1}) == ',');
    CHECK(TerminalRenderer::charForTile(TileType::Grass, {-1,  0}) == ',');
    CHECK(TerminalRenderer::charForTile(TileType::Grass, { 0, -1}) == ',');
}

// ─── charForEntity ───────────────────────────────────────────────────────────

TEST_CASE("charForEntity returns correct characters") {
    CHECK(TerminalRenderer::charForEntity(EntityType::Player)   == '@');
    CHECK(TerminalRenderer::charForEntity(EntityType::Goblin)   == 'g');
    CHECK(TerminalRenderer::charForEntity(EntityType::Mushroom) == 'm');
    CHECK(TerminalRenderer::charForEntity(EntityType::Poop)     == '*');
}

// ─── Frame dimensions ────────────────────────────────────────────────────────

TEST_CASE("endFrame produces exactly GRID_HEIGHT rows") {
    std::ostringstream oss;
    TerminalRenderer r(oss);
    Terrain terrain;
    auto rows = renderFrame(r, terrain, oss);
    CHECK(static_cast<int>(rows.size()) == TerminalRenderer::GRID_HEIGHT);
}

TEST_CASE("each row has exactly GRID_WIDTH characters") {
    std::ostringstream oss;
    TerminalRenderer r(oss);
    Terrain terrain;
    auto rows = renderFrame(r, terrain, oss);
    for (const auto& row : rows)
        CHECK(static_cast<int>(row.size()) == TerminalRenderer::GRID_WIDTH);
}

// ─── Terrain rendering ───────────────────────────────────────────────────────

TEST_CASE("drawTerrain fills all tiles with non-space characters") {
    std::ostringstream oss;
    TerminalRenderer r(oss);
    Terrain terrain;
    auto rows = renderFrame(r, terrain, oss);
    for (const auto& row : rows)
        for (char c : row)
            CHECK(c != ' ');
}

TEST_CASE("dug tile renders as '#'") {
    std::ostringstream oss;
    TerminalRenderer r(oss);
    Terrain terrain;
    terrain.dig({0, 0});
    auto rows = renderFrame(r, terrain, oss);
    // tile (0,0) → col = 0 + 10 = 10, row = 0 + 10 = 10
    CHECK(rows[10][10] == '#');
}

TEST_CASE("restored tile renders as grass again") {
    std::ostringstream oss;
    TerminalRenderer r(oss);
    Terrain terrain;
    terrain.dig({0, 0});
    terrain.restore({0, 0});
    auto rows = renderFrame(r, terrain, oss);
    char c = rows[10][10];
    CHECK((c == '.' || c == ','));
}

// ─── Sprite rendering ────────────────────────────────────────────────────────

TEST_CASE("drawSprite at tile (0,0) places char at buffer centre") {
    std::ostringstream oss;
    TerminalRenderer r(oss);
    Terrain terrain;

    oss.str("");
    r.beginFrame();
    r.drawTerrain(terrain);
    r.drawSprite({0.0f, 0.0f}, EntityType::Player);
    r.endFrame();

    auto rows = parseFrame(oss.str());
    CHECK(rows[10][10] == '@');
}

TEST_CASE("drawSprite overwrites terrain char at same tile") {
    std::ostringstream oss;
    TerminalRenderer r(oss);
    Terrain terrain;

    oss.str("");
    r.beginFrame();
    r.drawTerrain(terrain);
    r.drawSprite({0.0f, 0.0f}, EntityType::Goblin);
    r.endFrame();

    auto rows = parseFrame(oss.str());
    CHECK(rows[10][10] == 'g');
}

TEST_CASE("drawSprite at non-origin tile maps to correct buffer position") {
    std::ostringstream oss;
    TerminalRenderer r(oss);
    Terrain terrain;

    oss.str("");
    r.beginFrame();
    r.drawTerrain(terrain);
    r.drawSprite({-5.0f, -3.0f}, EntityType::Mushroom);
    r.endFrame();

    auto rows = parseFrame(oss.str());
    // col = round(-5) + 10 = 5,  row = round(-3) + 10 = 7
    CHECK(rows[7][5] == 'm');
}

TEST_CASE("drawSprite with interpolated position rounds to nearest tile") {
    std::ostringstream oss;
    TerminalRenderer r(oss);
    Terrain terrain;

    oss.str("");
    r.beginFrame();
    r.drawTerrain(terrain);
    // 0.4 rounds to 0 → col 10, row 10
    r.drawSprite({0.4f, 0.4f}, EntityType::Poop);
    r.endFrame();

    auto rows = parseFrame(oss.str());
    CHECK(rows[10][10] == '*');
}

TEST_CASE("out-of-bounds sprite is silently ignored") {
    std::ostringstream oss;
    TerminalRenderer r(oss);
    Terrain terrain;

    oss.str("");
    r.beginFrame();
    r.drawTerrain(terrain);
    r.drawSprite({100.0f, 100.0f}, EntityType::Player);
    r.endFrame();

    auto rows = parseFrame(oss.str());
    CHECK(static_cast<int>(rows.size()) == TerminalRenderer::GRID_HEIGHT);
}

// ─── Frame clearing ──────────────────────────────────────────────────────────

TEST_CASE("beginFrame clears sprite from previous frame") {
    std::ostringstream oss;
    TerminalRenderer r(oss);
    Terrain terrain;

    // Frame 1: draw player at (0,0)
    oss.str("");
    r.beginFrame();
    r.drawTerrain(terrain);
    r.drawSprite({0.0f, 0.0f}, EntityType::Player);
    r.endFrame();

    // Frame 2: draw terrain only — player should be gone
    auto rows = renderFrame(r, terrain, oss);
    CHECK(rows[10][10] != '@');
}
