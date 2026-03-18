#pragma once

#include "types.hpp"

class Terrain;

// ─── IRenderer ───────────────────────────────────────────────────────────────
//
// Abstract rendering interface. The simulation has no dependency on any
// specific frontend. Implementations: Renderer (SDL2), TerminalRenderer (ASCII).

class IRenderer {
public:
    virtual ~IRenderer() = default;

    virtual void beginFrame()                               = 0;
    virtual void drawTerrain(const Terrain& terrain)        = 0;
    virtual void drawSprite(Vec2f renderPos, float renderZ, EntityType type,
                            EntityID eid, float moveProgress, bool lit = true,
                            int zHeight = 1) = 0;
    virtual void endFrame()                                 = 0;
};
