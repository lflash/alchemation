#include "spatial.hpp"
#include <algorithm>
#include <cmath>

// ─── Cell helpers ─────────────────────────────────────────────────────────────

// Returns all tile cells whose spatial range overlaps [pos, pos+size).
// A size of exactly N.0 covers N tiles (uses epsilon to exclude the far edge).
std::vector<TilePos> SpatialGrid::cellsFor(TilePos pos, Vec2f size) {
    int x0 = pos.x;
    int y0 = pos.y;
    int x1 = static_cast<int>(std::floor(pos.x + size.x - 1e-6f));
    int y1 = static_cast<int>(std::floor(pos.y + size.y - 1e-6f));

    std::vector<TilePos> result;
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x)
            result.push_back({x, y, pos.z});
    return result;
}

void SpatialGrid::addToCell(EntityID id, TilePos cell) {
    auto& vec = cells[cell];
    if (std::find(vec.begin(), vec.end(), id) == vec.end())
        vec.push_back(id);
}

void SpatialGrid::removeFromCell(EntityID id, TilePos cell) {
    auto it = cells.find(cell);
    if (it == cells.end()) return;
    auto& vec = it->second;
    vec.erase(std::remove(vec.begin(), vec.end(), id), vec.end());
    if (vec.empty()) cells.erase(it);
}

// ─── SpatialGrid ─────────────────────────────────────────────────────────────

void SpatialGrid::add(EntityID id, TilePos pos, Vec2f size) {
    for (TilePos cell : cellsFor(pos, size))
        addToCell(id, cell);
}

void SpatialGrid::remove(EntityID id, TilePos pos, Vec2f size) {
    for (TilePos cell : cellsFor(pos, size))
        removeFromCell(id, cell);
}

void SpatialGrid::move(EntityID id, TilePos from, TilePos to, Vec2f size) {
    auto oldCells = cellsFor(from, size);
    auto newCells = cellsFor(to,   size);

    for (TilePos cell : oldCells) {
        bool stillCovered = std::find(newCells.begin(), newCells.end(), cell) != newCells.end();
        if (!stillCovered) removeFromCell(id, cell);
    }
    for (TilePos cell : newCells) {
        bool alreadyCovered = std::find(oldCells.begin(), oldCells.end(), cell) != oldCells.end();
        if (!alreadyCovered) addToCell(id, cell);
    }
}

std::vector<EntityID> SpatialGrid::at(TilePos pos) const {
    auto it = cells.find(pos);
    if (it == cells.end()) return {};
    return it->second;
}

std::vector<EntityID> SpatialGrid::query(Bounds bounds, int z) const {
    int x0 = static_cast<int>(std::floor(bounds.min.x));
    int y0 = static_cast<int>(std::floor(bounds.min.y));
    int x1 = static_cast<int>(std::floor(bounds.max.x - 1e-6f));
    int y1 = static_cast<int>(std::floor(bounds.max.y - 1e-6f));

    std::vector<EntityID> result;
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            auto it = cells.find({x, y, z});
            if (it == cells.end()) continue;
            for (EntityID id : it->second) {
                if (std::find(result.begin(), result.end(), id) == result.end())
                    result.push_back(id);
            }
        }
    }
    return result;
}

// ─── Collision resolution ─────────────────────────────────────────────────────

CollisionResult resolveCollision(EntityType mover, EntityType occupant) {
    using ET = EntityType;
    using CR = CollisionResult;

    // Static terrain objects — always block movers that reach them.
    auto isStatic = [](ET t) {
        return t == ET::Tree || t == ET::Rock || t == ET::Campfire ||
               t == ET::TreeStump || t == ET::Battery || t == ET::Lightbulb;
    };

    switch (mover) {
        case ET::Player:
            if (occupant == ET::Goblin)   return CR::Block;    // bump → combat
            if (occupant == ET::Mushroom) return CR::Collect;
            if (occupant == ET::Chest)    return CR::Collect;
            if (occupant == ET::Log)      return CR::Block;    // bump → push
            if (occupant == ET::Rock)     return CR::Block;    // bump → push
            if (isGolem(occupant))        return CR::Block;
            if (isStatic(occupant))       return CR::Block;
            return CR::Pass;

        case ET::Goblin:
            if (occupant == ET::Player)   return CR::Combat;
            if (occupant == ET::Goblin)   return CR::Block;
            if (isGolem(occupant))        return CR::Block;
            if (isStatic(occupant))       return CR::Block;
            return CR::Pass;

        case ET::Poop:
            if (occupant == ET::Goblin)   return CR::Hit;
            return CR::Pass;

        case ET::Mushroom:
            return CR::Pass;

        // Golems
        default:
            if (!isGolem(mover)) return CR::Pass;
            if (occupant == ET::Player)   return CR::Block;
            if (occupant == ET::Goblin) {
                // Fighting golems strike goblins; others are blocked
                if (mover == ET::IronGolem || mover == ET::WoodGolem)
                    return CR::Hit;
                return CR::Block;
            }
            if (occupant == ET::Mushroom) return CR::Pass;
            if (occupant == ET::Goblin)   return CR::Block;
            if (isGolem(occupant))        return CR::Block;
            if (isStatic(occupant))       return CR::Block;
            if (occupant == ET::Log || occupant == ET::Rock) return CR::Block;
            return CR::Pass;
    }
}

// ─── Two-phase move resolution ────────────────────────────────────────────────

std::unordered_set<EntityID> resolveMoves(
    const std::vector<MoveIntention>& intentions,
    SpatialGrid& spatial,
    const EntityRegistry& registry
) {
    std::unordered_set<EntityID> allowed;

    for (const auto& intent : intentions) {
        bool blocked = false;

        // Swap detection: another entity wants to come from our destination to our source.
        for (const auto& other : intentions) {
            if (other.id == intent.id) continue;
            if (other.from == intent.to && other.to == intent.from) {
                blocked = true;
                break;
            }
        }

        if (blocked) continue;

        // Broad phase: candidates in cells overlapping the destination bounds,
        // at the same z level as the intended destination.
        Bounds destBounds = boundsAt(intent.to, intent.size);
        auto candidates = spatial.query(destBounds, intent.to.z);

        // Narrow phase + collision resolution.
        for (EntityID occupantID : candidates) {
            if (occupantID == intent.id) continue;
            const Entity* occupant = registry.get(occupantID);
            if (!occupant) continue;

            if (!overlaps(destBounds, boundsAt(occupant->pos, occupant->size)))
                continue;

            if (resolveCollision(intent.type, occupant->type) == CollisionResult::Block) {
                blocked = true;
                break;
            }
        }

        if (!blocked)
            allowed.insert(intent.id);
    }

    // Dual-register allowed movers at their destination (still at source for now).
    for (const auto& intent : intentions) {
        if (allowed.count(intent.id))
            spatial.add(intent.id, intent.to, intent.size);
    }

    return allowed;
}
