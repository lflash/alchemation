#pragma once

#include "irenderer.hpp"
#include "effects.hpp"
#include "game.hpp"
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>
#include <unordered_map>
#include <string>
#include <vector>

class Terrain;

// ─── EntityFlash ─────────────────────────────────────────────────────────────
//
// Per-entity hit flash: tints the sprite for a few renderer frames.

struct EntityFlash {
    RGBA color;
    int  ticksLeft;
};

// ─── DyingEntity ─────────────────────────────────────────────────────────────
//
// Cached entity data rendered with fading alpha after the entity is destroyed.

struct DyingEntity {
    Vec2f      pos;
    float      z;
    EntityType type;
    float      life;
    float      maxLife;
};

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
    static constexpr int Z_STEP       = 12;    // pixels per z-level, vertical (unzoomed)
    static constexpr int Z_PERSP      = 30;    // virtual camera height in z-levels; controls
                                               // perspective strength (VP = Z_PERSP*Z_STEP px below centre)
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
    void drawShadow(Vec2f renderPos, float renderZ);
    void drawSprite(Vec2f renderPos, float renderZ, EntityType type,
                    EntityID eid, float moveT, bool lit = true);

    void endFrame();

    // ── Visual effects ────────────────────────────────────────────────────────
    // Call updateEffects() once per render frame (before beginFrame) with the
    // real-time delta so that particles/shake/fade advance at wall-clock speed.

    void updateEffects(float fdt);

    // Spawn a radial burst of particles at a world-tile position.
    void spawnBurst(Vec2f pos, float z, RGBA color,
                    int count, float speed, float lifeMax, float size);

    // Trigger a camera shake of the given magnitude (decays exponentially).
    void triggerShake(float amount);

    // Start a fade (alpha 0→1 driven by delta, then auto-reverses).
    void triggerFade(float startAlpha, float delta);

    // Flash an entity sprite for the given number of renderer ticks.
    void flashEntity(EntityID eid, RGBA color, int ticks);

    // Cache an entity for a death-fade after it is destroyed.
    void addDyingEntity(Vec2f pos, float z, EntityType type, float lifeMax = 0.35f);

    // Draw persistent per-entity effects (fire overlay + sparks, electric overlay + sparks).
    // Call once per entity, after drawSprite, using the same renderPos/renderZ.
    void drawEntityEffects(Vec2f pos, float z, bool burning, bool electrified);

    // Draw all live particles (call after entity sprites, before HUD).
    void drawParticles();

    // Draw all fading death-sprites (call after drawParticles).
    void drawDyingEntities();

    // Draws the always-on HUD (mana counter + recording indicator) top-left.
    void drawHUD(int mana, bool isRecording);

    // Draws a summon preview tooltip at the bottom of the screen.
    // No-op if preview.active is false.
    void drawSummonPreview(const SummonPreview& preview);

    // Draws a small directional triangle at the edge of a tile in the entity's facing direction.
    void drawFacingIndicator(Vec2f renderPos, float renderZ, Direction facing);

    // Draws the recordings panel in the top-right corner (replaces controls when open).
    void drawRecordingsPanel(const std::vector<RecordingInfo>& list,
                             bool renaming, const std::string& renameBuffer);

    // Draws the controls reference panel in the top-right corner.
    void drawControlsMenu();

    // Draws the key-rebind panel.  selectedRow is the highlighted Action index
    // (0-based, matching enum order).  listening = waiting for a new keypress.
    void drawRebindPanel(const InputMap& map, int selectedRow, bool listening);

private:
    SDL_Window*   window;
    SDL_Renderer* sdl;
    SpriteCache   sprites;
    Camera        camera_;   // updated each frame via setCamera()
    TTF_Font*     font_;
    bool          studioMode_ = false;
    int           gridW_      = 0;   // 0 = unbounded
    int           gridH_      = 0;

    // ── Effect state ──────────────────────────────────────────────────────────
    std::vector<Particle>                           particles_;
    std::unordered_map<EntityID, EntityFlash>       entityFlashes_;
    std::vector<DyingEntity>                        dying_;
    float    shakeAmt_    = 0.0f;
    float    shakeOffX_   = 0.0f;
    float    shakeOffY_   = 0.0f;
    float    fadeAlpha_   = 0.0f;
    float    fadeDelta_   = 0.0f;
    float    dayNightT_   = 0.0f;
    float    dustAccum_   = 0.0f;
    float    lastFdt_     = 0.0f;
    int      rendererTick_ = 0;

    SDL_Color tileColor(float height, TilePos pos, TileType type) const;

    // Convert world tile coordinates to screen pixels using one-point perspective.
    // Vertical world lines converge to a VP at (centre, Z_PERSP*Z_STEP px below centre).
    // shakeOffX_/shakeOffY_ are added here so all draw calls benefit automatically.
    int toPixelX(float tileX, float tileZ = 0.0f) const;
    int toPixelY(float tileY, float tileZ = 0.0f) const;

    // Render a UTF-8 string at screen pixel (x, y).
    void drawText(const std::string& text, int x, int y, SDL_Color col) const;
};
