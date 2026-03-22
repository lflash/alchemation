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

std::vector<EntityID> SpatialGrid::atAnyZ(int x, int y) const {
    std::vector<EntityID> result;
    for (const auto& [pos, ids] : cells) {
        if (pos.x != x || pos.y != y) continue;
        for (EntityID id : ids)
            if (std::find(result.begin(), result.end(), id) == result.end())
                result.push_back(id);
    }
    return result;
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

// (mover, occupant) → CollisionResult lookup table.
// Initialised once; resolveCollision() is a single array lookup.
//
// Row (mover) / column (occupant) layout mirrors the comment in spatial.hpp:
//
//         │ Player  Goblin  Mushroom  Golem  Static  Log   ...
// ────────┼──────────────────────────────────────────────────────
// Player  │  —      Block   Collect   Block  Block   Block  Pass
// Goblin  │ Combat  Block    Pass     Block  Block   Pass   Pass
// Golem   │ Block   Hit/Blk  Pass     Block  Block   Block  Pass
// other   │  Pass   Pass     Pass      Pass   Pass   Pass   Pass

namespace {
    constexpr int ET_COUNT = static_cast<int>(EntityType::Portal) + 1;

    using CR = CollisionResult;
    using ET = EntityType;

    std::array<std::array<CR, ET_COUNT>, ET_COUNT> buildCollisionTable() {
        std::array<std::array<CR, ET_COUNT>, ET_COUNT> t{};
        for (auto& row : t) row.fill(CR::Pass);

        auto idx = [](ET e) { return static_cast<int>(e); };
        auto set = [&](ET m, ET o, CR r) { t[idx(m)][idx(o)] = r; };

        // Static occupants block all active movers.
        const ET statics[] = {
            ET::Tree, ET::Rock, ET::Campfire, ET::TreeStump,
            ET::Battery, ET::Lightbulb, ET::Warren,
            ET::IronOre, ET::CopperOre, ET::CoalOre, ET::SulphurOre,
        };
        const ET golems[] = {
            ET::MudGolem, ET::StoneGolem, ET::ClayGolem, ET::WaterGolem,
            ET::BushGolem, ET::WoodGolem, ET::IronGolem, ET::CopperGolem,
        };
        // Movers that interact with the world (Mushroom and inert types always pass).
        const ET activeMovers[] = {
            ET::Player, ET::Goblin,
            ET::MudGolem, ET::StoneGolem, ET::ClayGolem, ET::WaterGolem,
            ET::BushGolem, ET::WoodGolem, ET::IronGolem, ET::CopperGolem,
        };

        for (ET m : activeMovers) {
            for (ET o : statics) set(m, o, CR::Block);
            for (ET o : golems)  set(m, o, CR::Block);
        }

        // Player specifics (some override the blocks set above).
        set(ET::Player, ET::Goblin,   CR::Block);    // bump → combat event
        set(ET::Player, ET::Mushroom, CR::Collect);
        set(ET::Player, ET::Chest,    CR::Collect);
        set(ET::Player, ET::Rabbit,   CR::Block);
        set(ET::Player, ET::Warren,   CR::Pass);     // enter warren via portal
        set(ET::Player, ET::Log,      CR::Block);    // bump → push

        // Goblin specifics.
        set(ET::Goblin, ET::Player, CR::Combat);
        set(ET::Goblin, ET::Goblin, CR::Block);

        // Golem specifics.
        for (ET m : golems) {
            set(m, ET::Player,   CR::Block);
            set(m, ET::Goblin,   CR::Block);   // default; fighting golems override below
            set(m, ET::Mushroom, CR::Pass);
            set(m, ET::Log,      CR::Block);
        }
        // Fighting golems strike goblins instead of blocking.
        for (ET m : {ET::MudGolem, ET::IronGolem, ET::WoodGolem})
            set(m, ET::Goblin, CR::Hit);

        return t;
    }

    const auto kCollisionTable = buildCollisionTable();
} // namespace

CollisionResult resolveCollision(EntityType mover, EntityType occupant) {
    int mi = static_cast<int>(mover);
    int oi = static_cast<int>(occupant);
    if (mi < 0 || mi >= ET_COUNT || oi < 0 || oi >= ET_COUNT)
        return CollisionResult::Pass;
    return kCollisionTable[mi][oi];
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
