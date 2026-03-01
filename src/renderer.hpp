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
    static constexpr int TILE_SIZE    = 32;    // width of one tile (pixels, unzoomed)
    static constexpr int TILE_H       = 20;    // height of one tile (oblique squish)
    static constexpr int Z_STEP       = 12;    // pixels per z-level (unzoomed)
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

    // Controls terrain colour palette. Call before drawTerrain().
    void setStudioMode(bool s) { studioMode_ = s; }

    // Set bounded grid dimensions (0,0 = unbounded/infinite world).
    void setGridBounds(int w, int h) { gridW_ = w; gridH_ = h; }

    void beginFrame();
    void setTitle(const std::string& title);
    void drawTerrain(const Terrain& terrain);

    // Draws a sprite at a tile position.
    // renderPos is the interpolated visual position (tile units, XY only).
    // renderZ is the interpolated z (for oblique vertical offset).
    void drawSprite(Vec2f renderPos, float renderZ, EntityType type);

    void endFrame();

    // Draws the always-on HUD (mana counter + recording indicator) top-left.
    void drawHUD(int mana, bool isRecording);

    // Draws a small directional triangle at the edge of a tile in the entity's facing direction.
    void drawFacingIndicator(Vec2f renderPos, float renderZ, Direction facing);

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
    bool          studioMode_ = false;
    int           gridW_      = 0;   // 0 = unbounded
    int           gridH_      = 0;

    SDL_Color tileColor(float height, TilePos pos, TileType type) const;

    // Convert world tile coordinates to screen pixels.
    // toPixelY uses oblique projection: higher z = higher on screen.
    int toPixelX(float tileX) const;
    int toPixelY(float tileY, float tileZ = 0.0f) const;

    // Render a UTF-8 string at screen pixel (x, y).
    void drawText(const std::string& text, int x, int y, SDL_Color col) const;
};
