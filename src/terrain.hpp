#pragma once

#include "types.hpp"
#include <memory>

class Terrain {
public:
    Terrain();
    ~Terrain();

    // Returns Perlin height in [-1, 1] for the given tile. Cached.
    // Only the (x, y) components are used; z is ignored.
    float heightAt(TilePos p) const;

    // Returns integer height level for the given tile: round(heightAt * 4).
    // Used for movement blocking: moves with |levelAt(dest) - levelAt(src)| > 1
    // are blocked. Rendering uses the raw float instead.
    int   levelAt(TilePos p) const;

    // Returns the biome at the given tile (deterministic, cached).
    // Mountains override all other biomes when levelAt(p) >= MOUNTAIN_THRESHOLD.
    Biome biomeAt(TilePos p) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
