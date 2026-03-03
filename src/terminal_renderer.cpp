#include "terminal_renderer.hpp"
#include "terrain.hpp"

#include <cmath>

TerminalRenderer::TerminalRenderer(std::ostream& out)
    : out(out)
    , buffer(GRID_HEIGHT, std::string(GRID_WIDTH, ' '))
{}

void TerminalRenderer::beginFrame() {
    for (auto& row : buffer)
        std::fill(row.begin(), row.end(), ' ');
}

void TerminalRenderer::drawTerrain(const Terrain& terrain) {
    const int halfW = GRID_WIDTH  / 2;
    const int halfH = GRID_HEIGHT / 2;

    for (int y = -halfH; y < halfH; ++y) {
        for (int x = -halfW; x < halfW; ++x) {
            TilePos p   = {x, y};
            int     col = toCol(static_cast<float>(x));
            int     row = toRow(static_cast<float>(y));
            if (inBounds(row, col))
                buffer[row][col] = charForTile(terrain.typeAt(p), p);
        }
    }
}

void TerminalRenderer::drawSprite(Vec2f renderPos, float /*renderZ*/, EntityType type, bool /*lit*/) {
    int col = toCol(renderPos.x);
    int row = toRow(renderPos.y);
    if (inBounds(row, col))
        buffer[row][col] = charForEntity(type);
}

void TerminalRenderer::endFrame() {
    out << "\033[H";
    for (const auto& row : buffer)
        out << row << '\n';
    out.flush();
}

char TerminalRenderer::charForTile(TileType type, TilePos pos) {
    switch (type) {
        case TileType::BareEarth: return '#';
        case TileType::Portal:    return 'O';
        case TileType::Fire:      return '^';
        case TileType::Puddle:    return '~';
        case TileType::Grass:     break;
    }
    // Checkerboard: use bitwise & to handle negative coords correctly
    return ((pos.x + pos.y) & 1) == 0 ? '.' : ',';
}

char TerminalRenderer::charForEntity(EntityType type) {
    switch (type) {
        case EntityType::Player:    return '@';
        case EntityType::Goblin:    return 'g';
        case EntityType::Mushroom:  return 'm';
        case EntityType::Poop:      return '*';
        case EntityType::Campfire:  return 'f';
        case EntityType::TreeStump: return 'T';
        case EntityType::Log:       return 'l';
        case EntityType::Battery:   return 'B';
        case EntityType::Lightbulb: return 'L';
    }
    return '?';
}

int TerminalRenderer::toCol(float tileX) const {
    return static_cast<int>(std::round(tileX)) + GRID_WIDTH / 2;
}

int TerminalRenderer::toRow(float tileY) const {
    return static_cast<int>(std::round(tileY)) + GRID_HEIGHT / 2;
}

bool TerminalRenderer::inBounds(int row, int col) const {
    return row >= 0 && row < GRID_HEIGHT && col >= 0 && col < GRID_WIDTH;
}
