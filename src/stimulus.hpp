#pragma once

#include "grid.hpp"
#include "entity.hpp"
#include "types.hpp"

// ─── Environmental stimulus simulations ───────────────────────────────────────
//
// Free functions called by Game::tick() for every non-paused grid.
// Kept separate from game.cpp so that tests can exercise them directly.

void tickFire(Grid& grid, EntityRegistry& registry, Tick currentTick);
void tickVoltage(Grid& grid, EntityRegistry& registry);
void tickWater(Grid& grid);
