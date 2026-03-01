#pragma once

#include "irenderer.hpp"
#include "game.hpp"
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>
#include <unordered_map>
#include <string>
#include <vector>

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

class Renderer : public IRenderer {
public:
    static constexpr int TILE_SIZE    = 32;
    // Viewport dimensions in pixels (fixed window size).
    static constexpr int VIEWPORT_W   = 640;
    static constexpr int VIEWPORT_H   = 640;

    Renderer();
    ~Renderer();

    // Non-copyable
    Renderer(const Renderer&)            = delete;
    Renderer& operator=(const Renderer&) = delete;

    // Must be called once per frame before any draw calls.
    void setCamera(const Camera& cam) { camera_ = cam; }

    void beginFrame();
    void setTitle(const std::string& title);
    void drawTerrain(const Terrain& terrain);

    // Draws a sprite at a tile position.
    // renderPos is the interpolated visual position (tile units); used for smooth movement.
    void drawSprite(Vec2f renderPos, EntityType type);

    void endFrame();

    // Draws the always-on HUD (mana counter + recording indicator) top-left.
    void drawHUD(int mana, bool isRecording);

    // Draws the recordings panel in the top-right corner (replaces controls when open).
    void drawRecordingsPanel(const std::vector<RecordingInfo>& list,
                             bool renaming, const std::string& renameBuffer);

    // Draws the controls reference panel in the top-right corner.
    void drawControlsMenu();

private:
    SDL_Window*   window;
    SDL_Renderer* sdl;
    SpriteCache   sprites;
    Camera        camera_;   // updated each frame via setCamera()
    TTF_Font*     font_;

    SDL_Color tileColor(float height, TilePos pos, TileType type) const;

    // Convert a world tile coordinate to screen pixels.
    // (camera_.pos is the tile at screen centre.)
    int toPixelX(float tileX) const;
    int toPixelY(float tileY) const;

    // Render a UTF-8 string at screen pixel (x, y).
    void drawText(const std::string& text, int x, int y, SDL_Color col) const;
};
