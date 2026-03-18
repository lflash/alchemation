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

    for (int y = camY_ - halfH; y < camY_ + halfH; ++y) {
        for (int x = camX_ - halfW; x < camX_ + halfW; ++x) {
            TilePos p   = {x, y};
            int     col = toCol(static_cast<float>(x));
            int     row = toRow(static_cast<float>(y));
            if (inBounds(row, col))
                buffer[row][col] = charForTile(p);
        }
    }
}

void TerminalRenderer::drawSprite(Vec2f renderPos, float /*renderZ*/, EntityType type,
                                   EntityID /*eid*/, float /*moveProgress*/, bool /*lit*/,
                                   int /*zHeight*/) {
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

char TerminalRenderer::charForTile(TilePos pos) {
    // Checkerboard: use bitwise & to handle negative coords correctly.
    // Tile-state entities (BareEarth, Fire, Puddle, Straw, Portal) are drawn
    // on top of this background by charForEntity via drawSprite.
    return ((pos.x + pos.y) & 1) == 0 ? '.' : ',';
}

char TerminalRenderer::charForEntity(EntityType type) {
    switch (type) {
        case EntityType::Player:     return '@';
        case EntityType::Goblin:     return 'g';
        case EntityType::Mushroom:   return 'm';
        case EntityType::Campfire:   return 'f';
        case EntityType::TreeStump:  return 'T';
        case EntityType::Log:        return 'l';
        case EntityType::Battery:    return 'B';
        case EntityType::Lightbulb:  return 'L';
        case EntityType::Tree:       return 't';
        case EntityType::Rock:       return 'o';
        case EntityType::Chest:      return 'x';
        case EntityType::MudGolem:   return 'G';
        case EntityType::StoneGolem: return 'S';
        case EntityType::ClayGolem:  return 'C';
        case EntityType::WaterGolem: return 'A';
        case EntityType::BushGolem:  return 'U';
        case EntityType::WoodGolem:  return 'J';
        case EntityType::IronGolem:  return 'I';
        case EntityType::CopperGolem:return 'P';
        case EntityType::Water:      return 'w';
        case EntityType::Rabbit:     return 'r';
        case EntityType::Warren:     return 'W';
        case EntityType::IronOre:    return 'i';
        case EntityType::CopperOre:  return 'u';
        case EntityType::CoalOre:    return 'k';
        case EntityType::SulphurOre: return 'y';
        case EntityType::LongGrass:  return '+';
        case EntityType::Meat:       return 'M';
        case EntityType::CookedMeat: return 'K';
        case EntityType::Spark:      return '!';
        case EntityType::Mud:        return ':';
        case EntityType::Stone:      return '=';
        case EntityType::Clay:       return 'c';
        case EntityType::Bush:       return '*';
        case EntityType::Wood:       return '"';
        case EntityType::Iron:       return 'i';
        case EntityType::Copper:     return 'u';
        case EntityType::BareEarth:  return '#';
        case EntityType::Fire:       return '^';
        case EntityType::Puddle:     return '~';
        case EntityType::Straw:      return '_';
        case EntityType::Portal:     return 'O';
    }
    return '?';
}

int TerminalRenderer::toCol(float tileX) const {
    return static_cast<int>(std::round(tileX)) - camX_ + GRID_WIDTH / 2;
}

int TerminalRenderer::toRow(float tileY) const {
    return static_cast<int>(std::round(tileY)) - camY_ + GRID_HEIGHT / 2;
}

bool TerminalRenderer::inBounds(int row, int col) const {
    return row >= 0 && row < GRID_HEIGHT && col >= 0 && col < GRID_WIDTH;
}
