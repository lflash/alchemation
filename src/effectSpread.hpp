#pragma once

#include "field.hpp"
#include "entity.hpp"
#include "types.hpp"
#include <random>

// ─── Environmental effectSpread simulations ───────────────────────────────────
//
// Free functions called by Game::tick() for every non-paused field.
// Kept separate from game.cpp so that tests can exercise them directly.

void tickFire(Field& field, EntityRegistry& registry, Tick currentTick);
void tickVoltage(Field& field, EntityRegistry& registry);

// Slowly spreads LongGrass entities to adjacent Grass tiles in the Grassland biome.
// Call once per world-field tick.
void tickLongGrass(Field& field, EntityRegistry& registry, std::mt19937& rng);
