#pragma once

#include "component_store.hpp"
#include "types.hpp"
#include <vector>

class Grid;
class EntityRegistry;

// ─── FluidComponent ───────────────────────────────────────────────────────────
//
// Shallow-water state for a Water entity. One per wet tile.
// h  = water depth (arbitrary units; entity despawns when h < H_MIN)
// vx = horizontal velocity in the +x direction
// vy = horizontal velocity in the +y direction
//
// Updated each tick by tickFluid() using simplified shallow water equations:
//   - gravity accelerates velocity toward lower surface height (h + terrain level)
//   - velocity advects mass to neighbouring tiles
//   - volume is approximately conserved (small numerical loss at despawn threshold)

struct FluidComponent {
    float h  = 0.f;
    float vx = 0.f;
    float vy = 0.f;
};

// ─── FluidOverlay ─────────────────────────────────────────────────────────────
//
// Passed from Game to Renderer each frame so the renderer can draw water
// without knowing about entities or components directly.

struct FluidOverlay {
    TilePos pos;
    float   h;
};

// ─── tickFluid ────────────────────────────────────────────────────────────────

void tickFluid(Grid& grid, ComponentStore<FluidComponent>& fluids,
               EntityRegistry& registry);
