#include "entity.hpp"
#include <algorithm>

// ─── Per-type defaults ────────────────────────────────────────────────────────

EntityConfig defaultConfig(EntityType type) {
    switch (type) {
        case EntityType::Player:   return { 0.1f, {0.8f, 0.8f}, 0,  0 };
        case EntityType::Goblin:   return { 0.1f, {0.8f, 0.8f}, 1,  5 };
        case EntityType::Mushroom: return { 0.0f, {0.6f, 0.6f}, 2,  0 };
        case EntityType::Poop:     return { 0.2f, {0.5f, 0.5f}, 1,  0 };
    }
    return { 0.1f, {0.8f, 0.8f}, 0, 0 };
}

// ─── Movement ────────────────────────────────────────────────────────────────

bool stepMovement(Entity& e) {
    if (e.isIdle()) return false;

    e.moveT += e.speed;
    if (e.moveT >= 1.0f) {
        e.pos      = e.destination;
        e.moveT    = 0.0f;
        return true;
    }
    return false;
}

// ─── EntityRegistry ──────────────────────────────────────────────────────────

EntityID EntityRegistry::spawn(EntityType type, TilePos pos) {
    EntityID id = nextID++;
    EntityConfig cfg = defaultConfig(type);

    entities[id] = Entity{
        .id          = id,
        .type        = type,
        .pos         = pos,
        .destination = pos,
        .moveT       = 0.0f,
        .size        = cfg.size,
        .speed       = cfg.speed,
        .facing      = Direction::N,
        .layer       = cfg.layer,
        .mana        = (type == EntityType::Player ? 3 : 0),
        .health      = cfg.health,
    };
    return id;
}

Entity* EntityRegistry::get(EntityID id) {
    auto it = entities.find(id);
    return (it != entities.end()) ? &it->second : nullptr;
}

const Entity* EntityRegistry::get(EntityID id) const {
    auto it = entities.find(id);
    return (it != entities.end()) ? &it->second : nullptr;
}

void EntityRegistry::destroy(EntityID id) {
    entities.erase(id);
}

std::vector<Entity*> EntityRegistry::all() {
    std::vector<Entity*> out;
    out.reserve(entities.size());
    for (auto& [id, e] : entities)
        out.push_back(&e);
    return out;
}

std::vector<const Entity*> EntityRegistry::drawOrder() const {
    std::vector<const Entity*> out;
    out.reserve(entities.size());
    for (const auto& [id, e] : entities)
        out.push_back(&e);
    std::sort(out.begin(), out.end(),
        [](const Entity* a, const Entity* b) { return a->layer < b->layer; });
    return out;
}
