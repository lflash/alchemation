#pragma once

#include "irenderer.hpp"
#include <iostream>
#include <ostream>
#include <string>
#include <vector>

class Terrain;

// ─── TerminalRenderer ────────────────────────────────────────────────────────
//
// ASCII/ANSI renderer. Builds a character buffer each frame and writes it to
// a std::ostream (defaults to std::cout). Passing a std::ostringstream in tests
// allows output to be captured and inspected without a display.

class TerminalRenderer : public IRenderer {
public:
    static constexpr int GRID_WIDTH  = 20;
    static constexpr int GRID_HEIGHT = 20;

    explicit TerminalRenderer(std::ostream& out = std::cout);

    void beginFrame()                                override;
    void drawTerrain(const Terrain& terrain)         override;
    void drawSprite(Vec2f renderPos, EntityType type) override;
    void endFrame()                                  override;

    // Pure functions — exposed for unit testing.
    static char charForTile(TileType type, TilePos pos);
    static char charForEntity(EntityType type);

private:
    std::ostream&            out;
    std::vector<std::string> buffer;   // buffer[row][col], row 0 = top

    int  toCol(float tileX) const;
    int  toRow(float tileY) const;
    bool inBounds(int row, int col) const;
};
