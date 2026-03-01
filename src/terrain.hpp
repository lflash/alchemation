#pragma once

#include "types.hpp"
#include <unordered_map>
#include <memory>

class Terrain {
public:
    Terrain();
    ~Terrain();

    // Returns Perlin height in [-1, 1] for the given tile. Cached.
    float    heightAt(TilePos p) const;

    // Returns terrain type. Checks manual overrides first, defaults to Grass.
    TileType typeAt(TilePos p) const;

    void dig(TilePos p);                    // marks tile as BareEarth
    void restore(TilePos p);               // removes override, tile reverts to Grass
    void setType(TilePos p, TileType t);   // generic override setter
    void clearOverrides();                 // removes all overrides (used by save/load)

    // Read-only view of the overrides map — for serialisation.
    const std::unordered_map<TilePos, TileType, TilePosHash>& overrides() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
