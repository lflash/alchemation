#pragma once

#include "types.hpp"
#include <unordered_map>
#include <memory>

class Terrain {
public:
    Terrain();
    ~Terrain();

    // Returns Perlin height in [-1, 1] for the given tile. Cached.
    // Only the (x, y) components are used; z is ignored.
    float    heightAt(TilePos p) const;

    // Returns integer height level for the given tile: round(heightAt * 4).
    // Used for movement blocking: moves with |levelAt(dest) - levelAt(src)| > 1
    // are blocked. Rendering uses the raw float instead.
    int      levelAt(TilePos p) const;

    // Returns terrain type. Checks manual overrides first, defaults to Grass.
    TileType typeAt(TilePos p) const;

    // Returns the biome at the given tile (deterministic, cached).
    // Mountains override all other biomes when levelAt(p) >= MOUNTAIN_THRESHOLD.
    Biome biomeAt(TilePos p) const;

    void dig(TilePos p);                    // marks tile as BareEarth
    void restore(TilePos p);               // removes override, tile reverts to Grass
    void setType(TilePos p, TileType t);   // generic type setter
    void clearOverrides();                 // removes all type overrides

    // Read-only view for serialisation.
    const std::unordered_map<TilePos, TileType, TilePosHash>& overrides() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
