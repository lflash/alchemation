#pragma once

#include "types.hpp"
#include <unordered_map>
#include <memory>
#include <optional>

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
    void setType(TilePos p, TileType t);   // generic type setter
    void clearOverrides();                 // removes all type + shape overrides

    // Shape (slope) accessors.
    TileShape shapeAt(TilePos p) const;             // defaults to Flat
    void      setShape(TilePos p, TileShape s);     // Flat removes the override

    // Read-only views for serialisation.
    const std::unordered_map<TilePos, TileType,  TilePosHash>& overrides() const;
    const std::unordered_map<TilePos, TileShape, TilePosHash>& shapes()    const;

    // Seed slopes from Perlin height within a square of half-side `radius`.
    // Tiles within `safeRadius` of the origin are left flat (safe spawn area).
    void generateSlopes(int radius, int safeRadius = 4);

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

// ─── Slope movement resolver ──────────────────────────────────────────────────
//
// Computes the actual destination (including z) for an entity at `from` moving
// to `to` (where to.z == from.z initially). Slope rules applied in order:
//   - Slope ascending in dir at (to.xy, from.z)       → arrive at z+1
//   - Any other slope at (to.xy, from.z)              → blocked (nullopt)
//   - Slope ascending opposite dir at (to.xy, from.z-1) → arrive at z-1
//   - Flat tile (no slope at z or z-1)                → arrive at z unchanged
//
// Only cardinal (N/S/E/W) moves interact with slopes.
// Diagonal moves pass through with z unchanged.
std::optional<TilePos> resolveZ(TilePos from, TilePos to, const Terrain& terrain);
