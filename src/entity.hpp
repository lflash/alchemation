#pragma once

#include "types.hpp"
#include <unordered_map>
#include <vector>

// ─── Capability flags ────────────────────────────────────────────────────────
//
// Bitfield stored on each Entity. Initialised from entityCaps() at spawn time
// but can be modified per-instance (e.g. upgraded golems in future phases).

enum Capability : uint32_t {
    CanExecuteRoutine = 1u << 0,  // runs the Routine VM
    Pushable          = 1u << 1,  // can be shoved one tile by bumping
    CanFight          = 1u << 2,  // deals damage in combat interactions
    ImmuneFire        = 1u << 3,  // unaffected by fire stimulus
    ImmuneWet         = 1u << 4,  // unaffected by wet stimulus
    Carriable         = 1u << 5,  // can be picked up and carried
};

// Default capability set for each entity type.
uint32_t entityCaps(EntityType type);

// True if the type is one of the eight golem types.
bool isGolem(EntityType type);

// ─── Entity ──────────────────────────────────────────────────────────────────

struct Entity {
    EntityID   id;
    EntityType type;

    TilePos    pos;           // current logical tile
    TilePos    destination;   // target tile (== pos when idle)
    float      moveProgress;  // 0.0→1.0 progress toward destination

    Vec2f      size;          // hitbox dimensions in tile units
    float      speed;         // moveProgress units added per tick
    Direction  facing;
    int        drawOrder;     // draw order (lower drawn first)
    int        mana;
    int        health;
    bool       lit          = false;  // Lightbulb: true when powered (≥1V on puddle tile)
    bool       burning      = false;  // TreeStump/Log: true while in entityBurnEnd
    bool       electrified  = false;  // Any entity: true while standing on a charged puddle
    uint32_t   capabilities = 0;      // bitfield of Capability flags
    EntityID   carrying     = INVALID_ENTITY;  // entity this entity is carrying (or INVALID)
    EntityID   carriedBy    = INVALID_ENTITY;  // entity carrying this one (or INVALID)

    bool isMoving()                const { return pos != destination; }
    bool isIdle()                  const { return pos == destination; }
    bool hasCapability(Capability c) const {
        return (capabilities & static_cast<uint32_t>(c)) != 0;
    }
};

// ─── Per-type defaults ────────────────────────────────────────────────────────

struct EntityConfig {
    float speed;
    Vec2f size;
    int   drawOrder;
    int   health;
    int   mana = 0;
};

EntityConfig defaultConfig(EntityType type);

// ─── Movement ────────────────────────────────────────────────────────────────

// Advances moveProgress by entity speed. On arrival, snaps pos to destination and
// resets moveProgress to 0. Returns true if the entity arrived this tick.
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
