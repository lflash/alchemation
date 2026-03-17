#include "terrain.hpp"
#include "FastNoiseLite.h"

#include <algorithm>
#include <unordered_map>

struct Terrain::Impl {
    FastNoiseLite noise;
    FastNoiseLite biomeNoise;
    mutable std::unordered_map<TilePos, float, TilePosHash> heightCache;
    mutable std::unordered_map<TilePos, Biome, TilePosHash> biomeCache;
};

Terrain::Terrain() : impl(std::make_unique<Impl>()) {
    impl->noise.SetNoiseType(FastNoiseLite::NoiseType_Perlin);
    impl->noise.SetFrequency(0.08f);
    impl->noise.SetFractalType(FastNoiseLite::FractalType_FBm);
    impl->noise.SetFractalOctaves(4);
    impl->noise.SetFractalLacunarity(2.0f);
    impl->noise.SetFractalGain(0.5f);

    impl->biomeNoise.SetSeed(42);
    impl->biomeNoise.SetNoiseType(FastNoiseLite::NoiseType_Perlin);
    impl->biomeNoise.SetFrequency(0.015f);
    impl->biomeNoise.SetFractalType(FastNoiseLite::FractalType_FBm);
    impl->biomeNoise.SetFractalOctaves(3);
    impl->biomeNoise.SetFractalLacunarity(2.0f);
    impl->biomeNoise.SetFractalGain(0.5f);
}

Terrain::~Terrain() = default;

float Terrain::heightAt(TilePos p) const {
    // Normalise z to 0: height is a 2D (x, y) property.
    TilePos key = {p.x, p.y, 0};
    auto it = impl->heightCache.find(key);
    if (it != impl->heightCache.end())
        return it->second;

    float base = impl->noise.GetNoise(static_cast<float>(p.x), static_cast<float>(p.y));
    float bv   = impl->biomeNoise.GetNoise(static_cast<float>(p.x), static_cast<float>(p.y));

    float h;
    if (bv < -0.5f) {
        // Lake: depressed and nearly flat
        h = base * 0.2f - 0.3f;
    } else if (bv < -0.1f) {
        // Forest: moderately rough
        h = base * 0.55f;
    } else if (bv < 0.4f) {
        // Grassland: very flat
        h = base * 0.25f;
    } else {
        // Volcanic: dome profile — biome noise drives a central peak
        float t = (bv - 0.4f) / 0.6f;  // 0 at edge, 1 at centre
        h = base * 0.4f + t * 1.5f;
    }
    h = std::clamp(h, -1.0f, 1.0f);

    impl->heightCache[key] = h;
    return h;
}

int Terrain::levelAt(TilePos p) const {
    return static_cast<int>(std::round(heightAt(p) * 4));
}

Biome Terrain::biomeAt(TilePos p) const {
    TilePos key = {p.x, p.y, 0};
    auto it = impl->biomeCache.find(key);
    if (it != impl->biomeCache.end()) return it->second;

    // Mountains override based on height threshold.
    static constexpr int MOUNTAIN_THRESHOLD = 3;
    if (levelAt(p) >= MOUNTAIN_THRESHOLD) {
        impl->biomeCache[key] = Biome::Mountains;
        return Biome::Mountains;
    }

    float v = impl->biomeNoise.GetNoise(static_cast<float>(p.x), static_cast<float>(p.y));
    Biome b;
    if      (v < -0.5f) b = Biome::Lake;
    else if (v < -0.1f) b = Biome::Forest;
    else if (v <  0.4f) b = Biome::Grassland;
    else                b = Biome::Volcanic;

    impl->biomeCache[key] = b;
    return b;
}



