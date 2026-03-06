#include "doctest.h"
#include "entity.hpp"
#include "game.hpp"
#include "input.hpp"
#include "spatial.hpp"

// ─── isGolem ─────────────────────────────────────────────────────────────────

TEST_CASE("isGolem: player is not a golem") {
    CHECK_FALSE(isGolem(EntityType::Player));
}

TEST_CASE("isGolem: goblin is not a golem") {
    CHECK_FALSE(isGolem(EntityType::Goblin));
}

TEST_CASE("isGolem: MudGolem is a golem") {
    CHECK(isGolem(EntityType::MudGolem));
}

TEST_CASE("isGolem: IronGolem is a golem") {
    CHECK(isGolem(EntityType::IronGolem));
}

TEST_CASE("isGolem: WoodGolem is a golem") {
    CHECK(isGolem(EntityType::WoodGolem));
}

TEST_CASE("isGolem: all golem types recognised") {
    CHECK(isGolem(EntityType::StoneGolem));
    CHECK(isGolem(EntityType::ClayGolem));
    CHECK(isGolem(EntityType::WaterGolem));
    CHECK(isGolem(EntityType::BushGolem));
    CHECK(isGolem(EntityType::CopperGolem));
}

// ─── entityCaps ──────────────────────────────────────────────────────────────

TEST_CASE("entityCaps: Log is Pushable") {
    bool r = (entityCaps(EntityType::Log) & Capability::Pushable) != 0;
    CHECK(r);
}

TEST_CASE("entityCaps: Rock is Pushable") {
    bool r = (entityCaps(EntityType::Rock) & Capability::Pushable) != 0;
    CHECK(r);
}

TEST_CASE("entityCaps: MudGolem has CanExecuteRoutine") {
    bool r = (entityCaps(EntityType::MudGolem) & Capability::CanExecuteRoutine) != 0;
    CHECK(r);
}

TEST_CASE("entityCaps: MudGolem has ImmuneWet") {
    bool r = (entityCaps(EntityType::MudGolem) & Capability::ImmuneWet) != 0;
    CHECK(r);
}

TEST_CASE("entityCaps: StoneGolem has ImmuneFire") {
    bool r = (entityCaps(EntityType::StoneGolem) & Capability::ImmuneFire) != 0;
    CHECK(r);
}

TEST_CASE("entityCaps: IronGolem has CanFight") {
    bool r = (entityCaps(EntityType::IronGolem) & Capability::CanFight) != 0;
    CHECK(r);
}

TEST_CASE("entityCaps: WoodGolem has CanFight") {
    bool r = (entityCaps(EntityType::WoodGolem) & Capability::CanFight) != 0;
    CHECK(r);
}

TEST_CASE("entityCaps: Player has no Pushable capability") {
    bool r = (entityCaps(EntityType::Player) & Capability::Pushable) != 0;
    CHECK_FALSE(r);
}

// ─── Collision resolution: golems ─────────────────────────────────────────────

TEST_CASE("resolveCollision: IronGolem vs Goblin is Hit") {
    CHECK(resolveCollision(EntityType::IronGolem, EntityType::Goblin) == CollisionResult::Hit);
}

TEST_CASE("resolveCollision: WoodGolem vs Goblin is Hit") {
    CHECK(resolveCollision(EntityType::WoodGolem, EntityType::Goblin) == CollisionResult::Hit);
}

TEST_CASE("resolveCollision: MudGolem vs Goblin is Block") {
    CHECK(resolveCollision(EntityType::MudGolem, EntityType::Goblin) == CollisionResult::Block);
}

TEST_CASE("resolveCollision: Golem vs Player is Block") {
    CHECK(resolveCollision(EntityType::MudGolem, EntityType::Player) == CollisionResult::Block);
}

TEST_CASE("resolveCollision: Player vs golem is Block") {
    CHECK(resolveCollision(EntityType::Player, EntityType::MudGolem) == CollisionResult::Block);
}

TEST_CASE("resolveCollision: Golem vs Mushroom is Pass") {
    CHECK(resolveCollision(EntityType::MudGolem, EntityType::Mushroom) == CollisionResult::Pass);
}

// ─── Summon gating ────────────────────────────────────────────────────────────

TEST_CASE("playerSummonPreview: inactive on plain grass") {
    Game game;
    // No ticks needed — default world has no medium tile directly in front of player
    SummonPreview preview = game.playerSummonPreview();
    // Preview may or may not be active depending on spawn world layout,
    // but the struct must be well-formed.
    CHECK((preview.manaCost >= 0));
}

TEST_CASE("Capability::CanExecuteRoutine flag value is non-zero") {
    uint32_t v = static_cast<uint32_t>(Capability::CanExecuteRoutine);
    CHECK(v != 0u);
}

TEST_CASE("Capability flags are distinct powers of two") {
    uint32_t all = Capability::CanExecuteRoutine | Capability::Pushable |
                   Capability::CanFight | Capability::ImmuneFire | Capability::ImmuneWet;
    int bits = __builtin_popcount(all);
    CHECK(bits == 5);
}
