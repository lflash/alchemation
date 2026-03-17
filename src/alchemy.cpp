#include "alchemy.hpp"

// ─── principleProfile ────────────────────────────────────────────────────────
//
// Canonical principle profiles for each entity type.  These are the "natural"
// principle values an entity carries before any field exposure modifies them.
// Substance profiles TBD — values below are provisional design estimates.

PrincipleProfile principleProfile(EntityType type) {
    PrincipleProfile p{};
    switch (type) {
        case EntityType::Campfire:   p.heat = 100; p.dry  = 40;  break;
        case EntityType::Rabbit:     p.life =  80; p.adhesive = 40; break;
        case EntityType::Warren:     p.life =  20; p.adhesive = 100; break;
        case EntityType::LongGrass:  p.life =  50; break;
        case EntityType::Meat:       p.life =  50; break;
        case EntityType::CookedMeat: p.life =  80; p.heat = 20;  break;
        case EntityType::Mushroom:   p.life =  60; break;
        case EntityType::Goblin:     p.life =  40; p.repellent = 80; break;
        case EntityType::Player:     p.life =  50; p.repellent = 80; break;
        case EntityType::Water:      p.wet  = 100; break;
        case EntityType::Battery:    p.pos  = 100; break;
        // Summoning mediums — inert until catalysed; emit no field so they
        // don't influence AI movement.  Principle data lives in alchemyReact only.
        // Spark likewise has no emission (it is consumed instantly on summon).
        default: break;
    }
    return p;
}

// ─── alchemyReact ────────────────────────────────────────────────────────────
//
// Spark + medium entity → golem type.  Returns nullopt if `medium` is not a
// summoning medium; the caller defaults to MudGolem when no medium is present.

std::optional<EntityType> alchemyReact(EntityType medium) {
    switch (medium) {
        case EntityType::Mud:    return EntityType::MudGolem;
        case EntityType::Stone:  return EntityType::StoneGolem;
        case EntityType::Clay:   return EntityType::ClayGolem;
        case EntityType::Bush:   return EntityType::BushGolem;
        case EntityType::Wood:   return EntityType::WoodGolem;
        case EntityType::Iron:   return EntityType::IronGolem;
        case EntityType::Copper: return EntityType::CopperGolem;
        default:                 return std::nullopt;
    }
}

// ─── responseProfile ─────────────────────────────────────────────────────────
//
// Movement response constants for behaving entities.
//
// Goblin (unloaded): attracted to Life (prey, food), Adhesive (pack).
// Goblin (loaded):   attracted to Heat (campfire to cook food), Adhesive (pack).
// Rabbit:            attracted to Life (grass), Adhesive (warren).
//                    strongly repelled by Repellent field (players, goblins).

ResponseProfile responseProfile(EntityType type, bool loaded) {
    ResponseProfile r{};
    switch (type) {
        case EntityType::Goblin:
            if (loaded) {
                // Carrying food → seek fire to cook and eat.
                r.heat       =  100;
                r.adhesive   =   20;
            } else {
                // Hungry → seek Life (prey/food), flee threats, cluster with pack.
                r.life       =   80;
                r.repellent  =  -80;   // flee from players and other threatening sources
                r.adhesive   =   30;
                r.manaMax    =   20;
            }
            r.wanderRate       = 60;
            r.urgencyThreshold = 200.f;
            break;

        case EntityType::Rabbit:
            r.life             =   60;   // seek grass
            r.adhesive         =   80;   // drawn toward warren
            r.repellent        = -100;   // flee repellent sources (players, goblins)
            r.wanderRate       = 80;
            r.urgencyThreshold = 150.f;
            break;

        default: break;
    }
    return r;
}
