#pragma once

#include "irenderer.hpp"
#include "effects.hpp"
#include "fluid.hpp"
#include "game.hpp"
#include "ui.hpp"
#include "studio.hpp"
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>
#include <unordered_map>
#include <string>
#include <vector>
#include <cmath>

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
                    EntityID eid, float moveProgress, bool lit = true);

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

    // Draws the active-action bar below the HUD. actionName is e.g. "Dig", "Summon".
    void drawActionBar(const char* actionName);

    // Draws a summon preview tooltip at the bottom of the screen.
    // No-op if preview.active is false.
    void drawSummonPreview(const SummonPreview& preview);

    // Draws a small directional triangle at the edge of a tile in the entity's facing direction.
    void drawFacingIndicator(Vec2f renderPos, float renderZ, Direction facing);

    // Draws the recordings panel in the top-right corner (replaces controls when open).
    void drawRecordingsPanel(const std::vector<RoutineInfo>& list,
                             bool renaming, const std::string& renameBuffer);

    // Draws the controls reference panel in the top-right corner.
    void drawControlsMenu();

    // Draws the key-rebind panel.  selectedRow is the highlighted Action index
    // (0-based, matching enum order).  listening = waiting for a new keypress.
    void drawRebindPanel(const InputMap& map, int selectedRow, bool listening);

    // ── Phase 15: Studio overlays ─────────────────────────────────────────────

    // Draw per-recording path overlays on the studio floor.
    // paths[i] is the precomputed PathStep sequence for recording i.
    // conflicts is the list of tick indices where two or more paths share a tile.
    // scrubTick / selectedRec identify the scrub position on the selected path.
    struct StudioPathView {
        const std::vector<PathStep>* path;   // non-owning pointer
        AgentColor                   color;
    };
    void drawStudioPaths(const std::vector<StudioPathView>& views,
                         const std::vector<int>& conflicts,
                         int scrubTick, int selectedRec);

    // Draw a translucent ghost entity at the given tile position.
    void drawGhostEntity(TilePos pos, Direction facing, EntityType type);

    // Draw the instruction list panel on the right side of the screen.
    void drawInstructionPanel(const Routine& routine, int selectedRow, int scrubInstrIdx,
                              bool insertingWait, bool insertingMove,
                              const std::string& insertBuffer, RelDir insertDir);

    // Draw the timeline bar at the bottom of the screen.
    // scrubInstrIdx: instruction index at the current scrub position (-1 = none).
    // conflictInstrs: instruction indices that have path conflicts.
    void drawTimeline(const Routine& routine, int scrubInstrIdx,
                      const std::vector<int>& conflictInstrs);

    // ── Phase 16: Mouse interaction ───────────────────────────────────────────

    // Inverse perspective projection: maps screen pixel (px, py) to the world
    // tile at the camera's z-level. Static so tests can call it without SDL.
    static TilePos screenToTile(int px, int py, const Camera& cam) {
        float ts  = TILE_SIZE * cam.zoom;
        float tsH = TILE_H    * cam.zoom;
        float tileX = cam.pos.x + (px - VIEWPORT_W * 0.5f) / ts;
        float tileY = cam.pos.y + (py - VIEWPORT_H * 0.5f) / tsH;
        return { static_cast<int>(std::floor(tileX)),
                 static_cast<int>(std::floor(tileY)),
                 static_cast<int>(std::round(cam.z)) };
    }

    // Draw water depth overlays on tiles (call after drawTerrain, before entities).
    void drawFluidOverlay(const std::vector<FluidOverlay>& overlay);

    // Draw a translucent white overlay on the hovered tile.
    void drawHoverHighlight(TilePos tile);

    // Draw a small name tooltip anchored just above a screen position.
    void drawEntityTooltip(const std::string& name, int screenX, int screenY);

    // Draw a right-click context menu.
    void drawContextMenu(const ContextMenu& menu);

    // Swap between arrow and hand OS cursor.
    void setHandCursor(bool hand);

private:
    SDL_Window*   window;
    SDL_Renderer* sdl;
    SpriteCache   sprites;
    Camera        camera_;   // updated each frame via setCamera()
    TTF_Font*     font_;
    bool          studioMode_ = false;
    int           gridW_      = 0;   // 0 = unbounded
    int           gridH_      = 0;

    // ── Text cache ────────────────────────────────────────────────────────────
    // Maps (text, packed-RGBA-color) → SDL_Texture*. Created on first use;
    // destroyed on ~Renderer(). drawText() is mutable to populate the cache.
    struct TextKey {
        std::string text;
        uint32_t    color = 0;
        bool operator==(const TextKey& o) const {
            return text == o.text && color == o.color;
        }
    };
    struct TextKeyHash {
        size_t operator()(const TextKey& k) const {
            size_t h = std::hash<std::string>{}(k.text);
            h ^= std::hash<uint32_t>{}(k.color) + 0x9e3779b9u + (h << 6) + (h >> 2);
            return h;
        }
    };
    mutable std::unordered_map<TextKey, SDL_Texture*, TextKeyHash> textCache_;

    // ── OS cursors ────────────────────────────────────────────────────────────
    SDL_Cursor* cursorArrow_ = nullptr;
    SDL_Cursor* cursorHand_  = nullptr;

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

    SDL_Color tileColor(float height, TilePos pos, Biome biome) const;

    // Convert world tile coordinates to screen pixels using one-point perspective.
    // Vertical world lines converge to a VP at (centre, Z_PERSP*Z_STEP px below centre).
    // shakeOffX_/shakeOffY_ are added here so all draw calls benefit automatically.
    int toPixelX(float tileX, float tileZ = 0.0f) const;
    int toPixelY(float tileY, float tileZ = 0.0f) const;

    // Render a UTF-8 string at screen pixel (x, y).
    void drawText(const std::string& text, int x, int y, SDL_Color col) const;
};
