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

    void dig(TilePos p);       // marks tile as BareEarth
    void restore(TilePos p);   // removes override, tile reverts to Grass

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
