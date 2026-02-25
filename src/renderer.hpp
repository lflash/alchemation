#pragma once

#include "types.hpp"
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <unordered_map>
#include <string>

class Terrain;

// ─── SpriteCache ─────────────────────────────────────────────────────────────

class SpriteCache {
public:
    explicit SpriteCache(SDL_Renderer* sdl);
    ~SpriteCache();

    // Loads all sprites from assets/sprites/. Called once at startup.
    void loadAll();

    // Returns texture for the given entity type, or nullptr if not loaded.
    SDL_Texture* get(EntityType type) const;

private:
    SDL_Renderer* sdl;
    std::unordered_map<EntityType, SDL_Texture*> textures;
};

// ─── Renderer ────────────────────────────────────────────────────────────────

class Renderer {
public:
    static constexpr int TILE_SIZE   = 32;
    static constexpr int GRID_WIDTH  = 20;
    static constexpr int GRID_HEIGHT = 20;

    Renderer();
    ~Renderer();

    // Non-copyable
    Renderer(const Renderer&)            = delete;
    Renderer& operator=(const Renderer&) = delete;

    void beginFrame();
    void drawTerrain(const Terrain& terrain);

    // Draws a sprite at a tile position.
    // renderPos is the interpolated visual position (tile units); used for smooth movement.
    void drawSprite(Vec2f renderPos, EntityType type);

    void endFrame();

private:
    SDL_Window*   window;
    SDL_Renderer* sdl;
    SpriteCache   sprites;

    SDL_Color tileColor(float height, TilePos pos, TileType type) const;

    // Convert tile units to screen pixels (tile (0,0) maps to screen centre).
    int toPixelX(float tileX) const;
    int toPixelY(float tileY) const;
};
