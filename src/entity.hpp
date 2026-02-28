#pragma once

#include "types.hpp"
#include <unordered_map>
#include <vector>

// ─── Entity ──────────────────────────────────────────────────────────────────

struct Entity {
    EntityID   id;
    EntityType type;

    TilePos    pos;          // current logical tile
    TilePos    destination;  // target tile (== pos when idle)
    float      moveT;        // 0.0→1.0 progress toward destination

    Vec2f      size;         // hitbox dimensions in tile units
    float      speed;        // moveT units added per tick
    Direction  facing;
    int        layer;        // draw order (lower drawn first)
    int        mana;
    int        health;

    bool isMoving() const { return pos != destination; }
    bool isIdle()   const { return pos == destination; }
};

// ─── Per-type defaults ────────────────────────────────────────────────────────

struct EntityConfig {
    float speed;
    Vec2f size;
    int   layer;
    int   health;
};

EntityConfig defaultConfig(EntityType type);

// ─── Movement ────────────────────────────────────────────────────────────────

// Advances moveT by entity speed. On arrival, snaps pos to destination and
// resets moveT to 0. Returns true if the entity arrived this tick.
bool stepMovement(Entity& e);

// ─── EntityRegistry ──────────────────────────────────────────────────────────

class EntityRegistry {
public:
    EntityID spawn(EntityType type, TilePos pos);

    // Returns nullptr if id is not found.
    Entity*       get(EntityID id);
    const Entity* get(EntityID id) const;

    // Removes entity. Safe to call with an invalid id.
    void destroy(EntityID id);

    // All entities as mutable pointers — for game logic iteration.
    // Note: do not spawn or destroy while iterating.
    std::vector<Entity*> all();

    // All entities sorted by layer ascending — for rendering.
    std::vector<const Entity*> drawOrder() const;

private:
    std::unordered_map<EntityID, Entity> entities;
    EntityID nextID = 1;   // 0 is INVALID_ENTITY
};
