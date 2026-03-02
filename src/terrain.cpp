#include "terrain.hpp"
#include "FastNoiseLite.h"

#include <unordered_map>

struct Terrain::Impl {
    FastNoiseLite noise;
    mutable std::unordered_map<TilePos, float,      TilePosHash> heightCache;
            std::unordered_map<TilePos, TileType,   TilePosHash> overrides;
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
}

const std::unordered_map<TilePos, TileType, TilePosHash>& Terrain::overrides() const {
    return impl->overrides;
}



