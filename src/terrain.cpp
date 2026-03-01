#include "terrain.hpp"
#include "FastNoiseLite.h"

#include <unordered_map>
#include <optional>

struct Terrain::Impl {
    FastNoiseLite noise;
    mutable std::unordered_map<TilePos, float,      TilePosHash> heightCache;
            std::unordered_map<TilePos, TileType,   TilePosHash> overrides;
            std::unordered_map<TilePos, TileShape,  TilePosHash> shapeOverrides;
};

Terrain::Terrain() : impl(std::make_unique<Impl>()) {
    impl->noise.SetNoiseType(FastNoiseLite::NoiseType_Perlin);
    impl->noise.SetFrequency(0.08f);
    impl->noise.SetFractalType(FastNoiseLite::FractalType_FBm);
    impl->noise.SetFractalOctaves(4);
    impl->noise.SetFractalLacunarity(2.0f);
    impl->noise.SetFractalGain(0.5f);
}

Terrain::~Terrain() = default;

float Terrain::heightAt(TilePos p) const {
    auto it = impl->heightCache.find(p);
    if (it != impl->heightCache.end())
        return it->second;

    float h = impl->noise.GetNoise(static_cast<float>(p.x), static_cast<float>(p.y));
    impl->heightCache[p] = h;
    return h;
}

TileType Terrain::typeAt(TilePos p) const {
    auto it = impl->overrides.find(p);
    if (it != impl->overrides.end())
        return it->second;
    return TileType::Grass;
}

void Terrain::dig(TilePos p) {
    impl->overrides[p] = TileType::BareEarth;
}

void Terrain::restore(TilePos p) {
    impl->overrides.erase(p);
}

void Terrain::setType(TilePos p, TileType t) {
    if (t == TileType::Grass)
        impl->overrides.erase(p);
    else
        impl->overrides[p] = t;
}

void Terrain::clearOverrides() {
    impl->overrides.clear();
    impl->shapeOverrides.clear();
}

const std::unordered_map<TilePos, TileType, TilePosHash>& Terrain::overrides() const {
    return impl->overrides;
}

TileShape Terrain::shapeAt(TilePos p) const {
    auto it = impl->shapeOverrides.find(p);
    if (it != impl->shapeOverrides.end())
        return it->second;
    return TileShape::Flat;
}

void Terrain::setShape(TilePos p, TileShape s) {
    if (s == TileShape::Flat)
        impl->shapeOverrides.erase(p);
    else
        impl->shapeOverrides[p] = s;
}

const std::unordered_map<TilePos, TileShape, TilePosHash>& Terrain::shapes() const {
    return impl->shapeOverrides;
}

void Terrain::generateSlopes(int radius, int safeRadius) {
    constexpr float HIGH = 0.30f;

    // A tile counts as "high" if its Perlin value exceeds the threshold,
    // unless it falls within the safe zone around the origin.
    auto isHigh = [&](int x, int y) -> bool {
        if (std::abs(x) <= safeRadius && std::abs(y) <= safeRadius) return false;
        return heightAt({x, y, 0}) > HIGH;
    };

    for (int y = -radius; y <= radius; ++y) {
        for (int x = -radius; x <= radius; ++x) {
            if (isHigh(x, y)) continue;  // slopes go on low tiles only

            bool hN = isHigh(x, y - 1);
            bool hS = isHigh(x, y + 1);
            bool hE = isHigh(x + 1, y);
            bool hW = isHigh(x - 1, y);
            int  n  = (hN ? 1 : 0) + (hS ? 1 : 0) + (hE ? 1 : 0) + (hW ? 1 : 0);

            TileShape slope = TileShape::Flat;
            if (n == 1) {
                // Cardinal ramp — single high neighbour.
                if      (hN) slope = TileShape::SlopeN;
                else if (hS) slope = TileShape::SlopeS;
                else if (hE) slope = TileShape::SlopeE;
                else         slope = TileShape::SlopeW;
            } else if (n == 2) {
                // Corner ramp — two perpendicular high neighbours.
                if      (hN && hE) slope = TileShape::SlopeNE;
                else if (hN && hW) slope = TileShape::SlopeNW;
                else if (hS && hE) slope = TileShape::SlopeSE;
                else if (hS && hW) slope = TileShape::SlopeSW;
                // Opposite pairs (ridge/pass) → leave flat.
            }

            if (slope != TileShape::Flat)
                setShape({x, y, 0}, slope);
        }
    }
}

// ─── resolveZ ────────────────────────────────────────────────────────────────

namespace {
    std::optional<Direction> slopeAscent(TileShape s) {
        switch (s) {
            case TileShape::SlopeN: return Direction::N;
            case TileShape::SlopeS: return Direction::S;
            case TileShape::SlopeE: return Direction::E;
            case TileShape::SlopeW: return Direction::W;
            default:                return std::nullopt;  // Flat + corner slopes
        }
    }

    bool oppositeDir(Direction a, Direction b) {
        return (a == Direction::N && b == Direction::S) ||
               (a == Direction::S && b == Direction::N) ||
               (a == Direction::E && b == Direction::W) ||
               (a == Direction::W && b == Direction::E);
    }
}

std::optional<TilePos> resolveZ(TilePos from, TilePos to, const Terrain& terrain) {
    int dx = to.x - from.x;
    int dy = to.y - from.y;

    // Only cardinal moves interact with slopes.
    bool cardinal = (dx == 0) != (dy == 0);
    if (!cardinal) return to;

    Direction dir = toDirection({dx, dy});

    auto a_z   = slopeAscent(terrain.shapeAt({to.x, to.y, from.z}));
    auto a_zm1 = slopeAscent(terrain.shapeAt({to.x, to.y, from.z - 1}));

    // Slope at destination z level
    if (a_z) {
        if (*a_z == dir)
            return TilePos{to.x, to.y, from.z + 1};   // ascending
        return to;                                     // perpendicular/back-face: pass through at z
    }

    // Flat at destination z: check for descent ramp one level below
    if (a_zm1 && oppositeDir(*a_zm1, dir))
        return TilePos{to.x, to.y, from.z - 1};       // descending via destination slope

    // Still flat: check if we're stepping backward off the source slope
    auto a_src = slopeAscent(terrain.shapeAt({from.x, from.y, from.z - 1}));
    if (a_src && oppositeDir(*a_src, dir))
        return TilePos{to.x, to.y, from.z - 1};       // descending off source slope

    return to;  // flat: z unchanged
}
