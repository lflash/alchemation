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
//
// Call setCamera(playerPos) before beginFrame() to centre the grid on the
// player. drawSprite() receives renderPos in world tile coordinates; the
// camera offset is applied internally.

class TerminalRenderer : public IRenderer {
public:
    static constexpr int GRID_WIDTH  = 30;
    static constexpr int GRID_HEIGHT = 20;

    explicit TerminalRenderer(std::ostream& out = std::cout);

    // Set the world tile position to centre the view on before each frame.
    void setCamera(TilePos cam) { camX_ = cam.x; camY_ = cam.y; }

    void beginFrame()                                override;
    void drawTerrain(const Terrain& terrain)         override;
    void drawSprite(Vec2f renderPos, float renderZ, EntityType type,
                    EntityID eid, float moveProgress, bool lit = true,
                    int zHeight = 1) override;
    void endFrame()                                  override;

    // Pure functions — exposed for unit testing.
    static char charForTile(TilePos pos);
    static char charForEntity(EntityType type);

private:
    std::ostream&            out;
    std::vector<std::string> buffer;   // buffer[row][col], row 0 = top
    int                      camX_ = 0;
    int                      camY_ = 0;

    int  toCol(float tileX) const;
    int  toRow(float tileY) const;
    bool inBounds(int row, int col) const;
};
