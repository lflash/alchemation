#include "renderer.hpp"
#include "terrain.hpp"

#include <stdexcept>
#include <algorithm>
#include <cstdio>

// ─── Sprite paths ─────────────────────────────────────────────────────────────

static const std::unordered_map<EntityType, std::string> SPRITE_PATHS = {
    { EntityType::Player,   "assets/sprites/player.png"   },
    { EntityType::Goblin,   "assets/sprites/goblin.png"   },
    { EntityType::Mushroom, "assets/sprites/mushroom.png" },
    { EntityType::Poop,     "assets/sprites/poop.png"     },
};

// ─── SpriteCache ─────────────────────────────────────────────────────────────

SpriteCache::SpriteCache(SDL_Renderer* sdl) : sdl(sdl) {}

SpriteCache::~SpriteCache() {
    for (auto& [type, tex] : textures)
        SDL_DestroyTexture(tex);
}

void SpriteCache::loadAll() {
    for (auto& [type, path] : SPRITE_PATHS) {
        SDL_Texture* tex = IMG_LoadTexture(sdl, path.c_str());
        if (!tex) {
            std::fprintf(stderr, "Warning: could not load sprite '%s': %s\n",
                         path.c_str(), IMG_GetError());
            continue;
        }
        textures[type] = tex;
    }
}

SDL_Texture* SpriteCache::get(EntityType type) const {
    auto it = textures.find(type);
    return (it != textures.end()) ? it->second : nullptr;
}

// ─── Renderer ────────────────────────────────────────────────────────────────

Renderer::Renderer() : sprites(nullptr) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0)
        throw std::runtime_error(SDL_GetError());

    if (!(IMG_Init(IMG_INIT_PNG) & IMG_INIT_PNG))
        throw std::runtime_error(IMG_GetError());

    window = SDL_CreateWindow(
        "Grid Game",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        GRID_WIDTH  * TILE_SIZE,
        GRID_HEIGHT * TILE_SIZE,
        SDL_WINDOW_SHOWN
    );
    if (!window) throw std::runtime_error(SDL_GetError());

    sdl = SDL_CreateRenderer(window, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!sdl) throw std::runtime_error(SDL_GetError());

    sprites = SpriteCache(sdl);
    sprites.loadAll();
}

Renderer::~Renderer() {
    SDL_DestroyRenderer(sdl);
    SDL_DestroyWindow(window);
    IMG_Quit();
    SDL_Quit();
}

void Renderer::setTitle(const std::string& title) {
    SDL_SetWindowTitle(window, title.c_str());
}

void Renderer::beginFrame() {
    SDL_SetRenderDrawColor(sdl, 0, 0, 0, 255);
    SDL_RenderClear(sdl);
}

void Renderer::drawTerrain(const Terrain& terrain) {
    const int halfW = GRID_WIDTH  / 2;
    const int halfH = GRID_HEIGHT / 2;

    for (int y = -halfH; y < halfH; y++) {
        for (int x = -halfW; x < halfW; x++) {
            TilePos  p     = {x, y};
            float    h     = terrain.heightAt(p);
            TileType type  = terrain.typeAt(p);
            SDL_Color color = tileColor(h, p, type);

            SDL_SetRenderDrawColor(sdl, color.r, color.g, color.b, color.a);
            SDL_Rect rect = { toPixelX(x), toPixelY(y), TILE_SIZE, TILE_SIZE };
            SDL_RenderFillRect(sdl, &rect);
        }
    }
}

void Renderer::drawSprite(Vec2f renderPos, EntityType type) {
    SDL_Texture* tex = sprites.get(type);
    if (!tex) return;

    SDL_Rect dst = {
        toPixelX(renderPos.x),
        toPixelY(renderPos.y),
        TILE_SIZE,
        TILE_SIZE
    };
    SDL_RenderCopy(sdl, tex, nullptr, &dst);
}

void Renderer::endFrame() {
    SDL_RenderPresent(sdl);
}

SDL_Color Renderer::tileColor(float height, TilePos pos, TileType type) const {
    if (type == TileType::BareEarth)
        return { 139, 90, 43, 255 };

    // Map Perlin height [-1,1] → green channel [72, 184]
    auto g = static_cast<int>(128.0f + height * 56.0f);
    g = std::clamp(g, 64, 220);

    // Checkerboard: slightly lighter on even tiles
    if ((pos.x + pos.y) % 2 == 0)
        g = std::min(g + 6, 255);

    return { 0, static_cast<uint8_t>(g), 0, 255 };
}

int Renderer::toPixelX(float tileX) const {
    return static_cast<int>((tileX + GRID_WIDTH  / 2.0f) * TILE_SIZE);
}

int Renderer::toPixelY(float tileY) const {
    return static_cast<int>((tileY + GRID_HEIGHT / 2.0f) * TILE_SIZE);
}
