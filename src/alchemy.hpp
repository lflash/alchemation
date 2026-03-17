#pragma once

#include "types.hpp"
#include <cstdint>
#include <optional>

// ─── PrincipleProfile ────────────────────────────────────────────────────────
//
// 10 signed 8-bit values, one per alchemical principle.  Stored as an ECS
// component (ComponentStore<PrincipleProfile>) — not every entity needs one.
// Values express the entity's alignment with each principle; magnitude indicates
// how strongly the entity is defined along that axis.  Opposite poles can
// coexist on the same entity (e.g. an entity can be both Hot and Cold).

struct PrincipleProfile {
    // Caloric pair
    int8_t heat = 0, cold = 0;
    // Aqueous pair
    int8_t wet  = 0, dry  = 0;
    // Vital pair  (mana == life)
    int8_t life = 0, death = 0;
    // Galvanic pair
    int8_t pos  = 0, neg  = 0;
    // Cohesion pair
    int8_t adhesive = 0, repellent = 0;
};

// ─── ResponseProfile ─────────────────────────────────────────────────────────
//
// Per-entity-type constants that replace hardcoded AI scoring equations.
// Each signed value indicates how strongly the entity is attracted to (+) or
// repelled by (-) the gradient of the corresponding principle field.
// Zero means the entity ignores that field entirely.
//
// wanderRate       — average ticks between passive (non-urgent) move attempts.
// urgencyThreshold — if |bestScore| exceeds this, the entity moves every tick.
// manaMax          — if > 0, the entity has hunger; life response is scaled by
//                    clamp((manaMax - mana) / manaMax, 0, 1).

struct ResponseProfile {
    // Caloric
    int8_t heat = 0, cold = 0;
    // Aqueous
    int8_t wet  = 0, dry  = 0;
    // Vital
    int8_t life = 0, death = 0;
    // Galvanic
    int8_t pos  = 0, neg  = 0;
    // Cohesion
    int8_t adhesive = 0, repellent = 0;

    int   wanderRate        = 80;
    float urgencyThreshold  = 150.f;
    int   manaMax           = 0;    // 0 = no hunger scaling
};

// ─── Factory functions ───────────────────────────────────────────────────────
//
// principleProfile() — canonical PrincipleProfile for an entity type.
//   Used when lazily adding entities to the field simulation.
//
// responseProfile()  — movement response constants for a behaving entity type.
//   loaded = true when the entity is carrying something (e.g. goblin with food).

PrincipleProfile principleProfile(EntityType type);
ResponseProfile  responseProfile(EntityType type, bool loaded = false);

// ─── alchemyReact ─────────────────────────────────────────────────────────────
//
// Given a medium entity type, returns the golem type it produces when catalysed
// by a Spark.  Returns nullopt if the entity type is not a summoning medium.
// The Summon action defaults to MudGolem when no medium entity is present.

std::optional<EntityType> alchemyReact(EntityType medium);
