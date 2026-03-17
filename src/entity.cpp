#include "entity.hpp"
#include <algorithm>

// ─── Per-type defaults ────────────────────────────────────────────────────────

EntityConfig defaultConfig(EntityType type) {
    switch (type) {
        // Core
        case EntityType::Player:    return { 0.1f,  {0.8f, 0.8f}, 0, 0, 3  };
        case EntityType::Goblin:    return { 0.17f, {0.8f, 0.8f}, 1, 5, 5  };
        case EntityType::Mushroom:  return { 0.0f,  {0.6f, 0.6f}, 2, 0, 5  };
        // Static effectSpread sources
        case EntityType::Campfire:  return { 0.0f,  {0.8f, 0.8f}, 2, 0  };
        case EntityType::TreeStump: return { 0.0f,  {0.8f, 0.8f}, 2, 5  };
        case EntityType::Log:       return { 0.0f,  {0.8f, 0.8f}, 2, 0  };
        case EntityType::Battery:   return { 0.0f,  {0.6f, 0.6f}, 2, 0  };
        case EntityType::Lightbulb: return { 0.0f,  {0.6f, 0.6f}, 2, 0  };
        // Terrain objects
        case EntityType::Tree:      return { 0.0f,  {0.9f, 0.9f}, 2, 5  };
        case EntityType::Rock:      return { 0.0f,  {0.8f, 0.8f}, 2, 0  };
        case EntityType::Chest:     return { 0.0f,  {0.7f, 0.7f}, 2, 0  };
        // Golems (speed, size, drawOrder, health)
        case EntityType::MudGolem:    return { 0.07f, {0.8f, 0.8f}, 1, 8  };
        case EntityType::StoneGolem:  return { 0.06f, {0.8f, 0.8f}, 1, 12 };
        case EntityType::ClayGolem:   return { 0.08f, {0.8f, 0.8f}, 1, 6  };
        case EntityType::WaterGolem:  return { 0.12f, {0.8f, 0.8f}, 1, 4  };
        case EntityType::BushGolem:   return { 0.12f, {0.8f, 0.8f}, 1, 4  };
        case EntityType::WoodGolem:   return { 0.08f, {0.8f, 0.8f}, 1, 6  };
        case EntityType::IronGolem:   return { 0.06f, {0.8f, 0.8f}, 1, 10 };
        case EntityType::CopperGolem: return { 0.12f, {0.8f, 0.8f}, 1, 4  };
        // Fluid
        case EntityType::Water:       return { 0.0f,  {1.0f, 1.0f}, -1, 0 };
        // World entities (Phase 18)
        case EntityType::Rabbit:      return { 0.15f, {0.6f, 0.6f}, 1, 2  };
        case EntityType::Warren:      return { 0.0f,  {0.8f, 0.8f}, 2, 0  };
        case EntityType::IronOre:     return { 0.0f,  {0.8f, 0.8f}, 2, 0  };
        case EntityType::CopperOre:   return { 0.0f,  {0.8f, 0.8f}, 2, 0  };
        case EntityType::CoalOre:     return { 0.0f,  {0.8f, 0.8f}, 2, 0  };
        case EntityType::SulphurOre:  return { 0.0f,  {0.8f, 0.8f}, 2, 0  };
        case EntityType::LongGrass:   return { 0.0f,  {0.9f, 0.9f}, 0, 0  };
        case EntityType::Meat:        return { 0.0f,  {0.6f, 0.6f}, 0, 0  };
        case EntityType::CookedMeat:  return { 0.0f,  {0.6f, 0.6f}, 0, 0  };
    }
    return { 0.1f, {0.8f, 0.8f}, 0, 0 };
}

// ─── Capability table ─────────────────────────────────────────────────────────

bool isGolem(EntityType type) {
    switch (type) {
        case EntityType::MudGolem: case EntityType::StoneGolem:
        case EntityType::ClayGolem: case EntityType::WaterGolem:
        case EntityType::BushGolem: case EntityType::WoodGolem:
        case EntityType::IronGolem: case EntityType::CopperGolem:
            return true;
        default: return false;
    }
}

uint32_t entityCaps(EntityType type) {
    using C = Capability;
    switch (type) {
        case EntityType::Log:         return C::Pushable | C::Carriable;
        case EntityType::Rock:        return C::Pushable | C::Carriable;
        case EntityType::Mushroom:    return C::Carriable;
        case EntityType::Rabbit:      return C::Carriable;
        case EntityType::Meat:        return C::Carriable;
        case EntityType::CookedMeat:  return C::Carriable;
        case EntityType::MudGolem:    return C::CanExecuteRoutine | C::ImmuneWet;
        case EntityType::StoneGolem:  return C::CanExecuteRoutine | C::ImmuneFire;
        case EntityType::ClayGolem:   return C::CanExecuteRoutine;
        case EntityType::WaterGolem:  return C::CanExecuteRoutine;
        case EntityType::BushGolem:   return C::CanExecuteRoutine;
        case EntityType::WoodGolem:   return C::CanExecuteRoutine | C::CanFight;
        case EntityType::IronGolem:   return C::CanExecuteRoutine | C::CanFight;
        case EntityType::CopperGolem: return C::CanExecuteRoutine;
        // World entities: ores start inert; MINE grants Pushable at runtime.
        default: return 0;
    }
}

// ─── Movement ────────────────────────────────────────────────────────────────

bool stepMovement(Entity& e) {
    if (e.isIdle()) return false;

    // Speed-0 entities (rocks, trees, etc.) teleport instantly when pushed.
    if (e.speed <= 0.0f) {
        e.pos          = e.destination;
        e.moveProgress = 0.0f;
        return true;
    }

    e.moveProgress += e.speed;
    if (e.moveProgress >= 1.0f) {
        e.pos          = e.destination;
        e.moveProgress = 0.0f;
        return true;
    }
    return false;
}

// ─── EntityRegistry ──────────────────────────────────────────────────────────

EntityID EntityRegistry::spawn(EntityType type, TilePos pos) {
    EntityID id = nextID++;
    EntityConfig cfg = defaultConfig(type);

    entities[id] = Entity{
        .id           = id,
        .type         = type,
        .pos          = pos,
        .destination  = pos,
        .moveProgress = 0.0f,
        .size         = cfg.size,
        .speed        = cfg.speed,
        .facing       = Direction::N,
        .drawOrder    = cfg.drawOrder,
        .mana         = cfg.mana,
        .health       = cfg.health,
        .capabilities = entityCaps(type),
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
        [](const Entity* a, const Entity* b) { return a->drawOrder < b->drawOrder; });
    return out;
}
