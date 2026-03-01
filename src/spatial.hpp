#pragma once

#include "types.hpp"
#include "entity.hpp"
#include <unordered_map>
#include <unordered_set>
#include <vector>

constexpr int TILE_ENTITY_CAP = 8;

// ─── SpatialGrid ─────────────────────────────────────────────────────────────

class SpatialGrid {
public:
    // Register entity in all cells its bounds cover.
    void add(EntityID id, TilePos pos, Vec2f size);

    // Remove entity from all cells its bounds cover.
    void remove(EntityID id, TilePos pos, Vec2f size);

    // Delta-update: removes cells only in old position, adds cells only in new
    // position. Cells covered by both are untouched. Use this on arrival instead
    // of remove+add to correctly handle multi-tile entities whose cell sets overlap.
    void move(EntityID id, TilePos from, TilePos to, Vec2f size);

    // Entities registered at a specific tile.
    std::vector<EntityID> at(TilePos pos) const;

    // Broad phase: all unique EntityIDs in any cell overlapping the given bounds
    // at the specified z level. Defaults to z=0 for backward compatibility.
    std::vector<EntityID> query(Bounds bounds, int z = 0) const;

    // Cells covered by an entity at pos with given size.
    static std::vector<TilePos> cellsFor(TilePos pos, Vec2f size);

private:
    std::unordered_map<TilePos, std::vector<EntityID>, TilePosHash> cells;

    void addToCell(EntityID id, TilePos cell);
    void removeFromCell(EntityID id, TilePos cell);
};

// ─── AABB helpers ─────────────────────────────────────────────────────────────

inline Bounds boundsAt(TilePos pos, Vec2f size) {
    return {
        toVec(pos),
        { static_cast<float>(pos.x) + size.x, static_cast<float>(pos.y) + size.y }
    };
}

// Strict overlap: adjacent (touching) boxes return false.
inline bool overlaps(Bounds a, Bounds b) {
    return a.max.x > b.min.x && b.max.x > a.min.x
        && a.max.y > b.min.y && b.max.y > a.min.y;
}

// ─── Collision resolution ─────────────────────────────────────────────────────

enum class CollisionResult { Pass, Block, Collect, Combat, Hit };

// Lookup table: (mover type, occupant type) → result.
//
//         │ Player   Goblin   Mushroom  Poop
// ────────┼──────────────────────────────────
// Player  │  —       Block*   Collect   Pass     *bump combat: push fires on block
// Goblin  │ Combat   Block    Pass      Pass
// Poop    │  Pass    Hit      Pass      Pass
CollisionResult resolveCollision(EntityType mover, EntityType occupant);

// ─── Two-phase move resolution ────────────────────────────────────────────────

struct MoveIntention {
    EntityID   id;
    TilePos    from;
    TilePos    to;
    EntityType type;
    Vec2f      size;
};

// Resolves all intentions in one pass. For each intention:
//   1. Rejects swap (A→B's tile, B→A's tile in same tick).
//   2. Broad phase: queries spatial at destination bounds.
//   3. Narrow phase: AABB overlap check per candidate.
//   4. Rejects if any occupant yields Block.
//
// Returns IDs of entities allowed to move.
// Dual-registers allowed movers at their destination in the spatial grid.
std::unordered_set<EntityID> resolveMoves(
    const std::vector<MoveIntention>& intentions,
    SpatialGrid& spatial,
    const EntityRegistry& registry
);
