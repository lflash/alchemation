#pragma once

#include <cstdint>
#include <cmath>
#include <functional>

// ─── Coordinate types ────────────────────────────────────────────────────────

struct TilePos {
    int x, y;
    int z = 0;   // vertical level; 0 = ground. Defaults to 0 so existing
                 // two-field aggregate initialisers {x, y} remain valid.

    bool operator==(const TilePos&) const = default;
    bool operator!=(const TilePos&) const = default;

    TilePos operator+(const TilePos& o) const { return {x + o.x, y + o.y, z + o.z}; }
    TilePos operator-(const TilePos& o) const { return {x - o.x, y - o.y, z - o.z}; }
    TilePos operator*(int s)            const { return {x * s,   y * s,   z * s};   }
};

struct Vec2f {
    float x, y;
};

struct Bounds {
    Vec2f min, max;   // in tile units
};

// ─── Camera ──────────────────────────────────────────────────────────────────
//
// Viewport state for the renderer. pos is the world-space tile coordinate at
// the centre of the screen; zoom scales TILE_SIZE pixels per tile.
// Owned by main.cpp, passed to the renderer each frame via setCamera().

struct Camera {
    Vec2f pos    = {0.0f, 0.0f};   // current smoothed position (tile coords)
    Vec2f target = {0.0f, 0.0f};   // lerp target
    float zoom   = 1.0f;           // multiplier on TILE_SIZE
    float z      = 0.0f;           // current smoothed z (tile units, step 6 drives this)
    float targetZ = 0.0f;          // lerp target for z
};

// ─── ID types ────────────────────────────────────────────────────────────────

using EntityID    = uint32_t;
using GridID      = uint32_t;
using RecordingID = uint32_t;
using Tick        = uint64_t;

constexpr EntityID INVALID_ENTITY = 0;

// ─── Enums ───────────────────────────────────────────────────────────────────

enum class EntityType {
    // Core
    Player, Goblin, Mushroom, Poop,
    // Static stimulus sources
    Campfire, TreeStump, Log, Battery, Lightbulb,
    // Terrain objects (Phase 12)
    Tree, Rock, Chest,
    // Golems — summoned from medium tiles (Phase 12)
    MudGolem, StoneGolem, ClayGolem, WaterGolem,
    BushGolem, WoodGolem, IronGolem, CopperGolem,
    // Fluid (Phase 17) — one entity per wet tile; carries FluidComponent
    Water,
    // World entities (Phase 18) — spawned by biome world gen; AI deferred
    Rabbit, Warren,
    IronOre, CopperOre, CoalOre, SulphurOre,
};
// ─── World generation ─────────────────────────────────────────────────────────

enum class Biome { Grassland, Forest, Volcanic, Lake, Mountains };

// Chunk size for lazy world generation (tiles per side). Easy to change here.
inline constexpr int CHUNK_SIZE = 16;

enum class Direction  { N, NE, E, SE, S, SW, W, NW };
enum class ActionType { Move, Spawn, Despawn, ChangeMana, Dig, Plant, Summon };
enum class EventType  { Arrived, Collided, Despawned };
enum class TileType {
    Grass, BareEarth, Portal, Fire, Puddle,
    // Summoning mediums — each yields one golem type (Phase 12)
    Mud, Stone, Clay, Bush, Wood, Iron, Copper,
    // Straw — scythed grass (Phase 18)
    Straw,
};


// ─── Math helpers ────────────────────────────────────────────────────────────

inline Vec2f lerp(Vec2f a, Vec2f b, float t) {
    return { a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t };
}

inline Vec2f toVec(TilePos p) {
    return { static_cast<float>(p.x), static_cast<float>(p.y) };
}

// ─── Direction helpers ───────────────────────────────────────────────────────

inline TilePos dirToDelta(Direction d) {
    switch (d) {
        case Direction::N:  return { 0, -1};
        case Direction::NE: return { 1, -1};
        case Direction::E:  return { 1,  0};
        case Direction::SE: return { 1,  1};
        case Direction::S:  return { 0,  1};
        case Direction::SW: return {-1,  1};
        case Direction::W:  return {-1,  0};
        case Direction::NW: return {-1, -1};
    }
    return {0, 0};
}

inline Direction toDirection(TilePos delta) {
    if (delta.x ==  0 && delta.y == -1) return Direction::N;
    if (delta.x ==  1 && delta.y == -1) return Direction::NE;
    if (delta.x ==  1 && delta.y ==  0) return Direction::E;
    if (delta.x ==  1 && delta.y ==  1) return Direction::SE;
    if (delta.x ==  0 && delta.y ==  1) return Direction::S;
    if (delta.x == -1 && delta.y ==  1) return Direction::SW;
    if (delta.x == -1 && delta.y ==  0) return Direction::W;
    if (delta.x == -1 && delta.y == -1) return Direction::NW;
    return Direction::N;
}

// ─── Hash for TilePos (for use in unordered_map) ─────────────────────────────

struct TilePosHash {
    size_t operator()(const TilePos& p) const {
        size_t seed = 0;
        seed ^= std::hash<int>{}(p.x) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        seed ^= std::hash<int>{}(p.y) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        seed ^= std::hash<int>{}(p.z) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        return seed;
    }
};
