#include "game.hpp"
#include "terrain.hpp"
#include "alchemy.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <unordered_set>
#include <vector>

// ─── Height helper (local to movement subsystem) ──────────────────────────────

static bool resolveDestHeight(TilePos& dest, const TilePos& from, const Field& field) {
    if (field.isBounded()) return true;   // flat floor; no height check
    dest.z = field.terrain.levelAt(dest);
    return std::abs(dest.z - from.z) <= 1;
}

// ─── Goblin AI ────────────────────────────────────────────────────────────────
//
// Conditionless scoring equation (no explicit state machine):
//
//   score(d) = hunger * (1-loaded) * approach(d, nearest_prey)  * PREY_W
//            + loaded              * approach(d, nearest_fire)   * HOME_W
//            + (1-loaded)         * approach(d, pack_centroid)   * PACK_W
//            + ε * noise
//
// where:
//   hunger  = clamp((MANA_MAX - mana) / MANA_MAX, 0, 1)
//   loaded  = 1 if goblin carries food, else 0
//   prey    = nearest idle Rabbit, loose Meat, or player carrying food
//             (within DETECT_RADIUS tiles, Manhattan distance)
//
// Side-effects (per idle tick):
//   - Hit adjacent Rabbit → -1 mana; destroy if mana==0, drop Meat
//   - Pick up adjacent loose Meat (or steal from adjacent player)
//   - Eat carried Meat when adjacent to Campfire: mana += meat.mana, destroy meat
//   - Drop Meat beside (but not on) fire when loaded and at fire
//
// Mana decays every DECAY_RATE ticks; hunger drives prey-seeking.

void Game::tickGoblinAI(Field& grid, Tick currentTick) {
    static constexpr int   MANA_MAX     = 20;
    static constexpr int   DECAY_RATE   = 300;  // ~6 s at 50 Hz
    static constexpr int   DETECT_RADIUS = 20;
    static constexpr float PREY_W       = 3.0f;
    static constexpr float HOME_W       = 2.5f;
    static constexpr float PACK_W       = 0.6f;
    static constexpr float NOISE_EPS    = 0.15f;
    static constexpr int   WANDER_RATE  = 60;   // ticks between passive moves
    static const TilePos   kDirs4[]     = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0}};

    // Approach score: dot product of step direction with normalised vector to target.
    auto approach = [](TilePos dir, TilePos from, TilePos to) -> float {
        float dx = float(to.x - from.x), dy = float(to.y - from.y);
        float dist = std::sqrt(dx*dx + dy*dy);
        if (dist < 0.5f) return 0.f;
        return (dir.x * dx + dir.y * dy) / dist;
    };

    auto manhat = [](TilePos a, TilePos b) -> int {
        return std::abs(a.x - b.x) + std::abs(a.y - b.y);
    };

    bool decayTick = (currentTick % DECAY_RATE == 0);

    // ── Snapshot of non-goblin entity positions ───────────────────────────────
    // Collect all rabbits, loose meat, campfires, and the player once for the
    // whole tick (avoids O(N²) per-goblin iteration over the entity list).

    std::vector<TilePos>  rabbitPositions;
    std::vector<TilePos>  meatPositions;
    std::vector<EntityID> meatIDs;
    std::vector<TilePos>  firePositions;
    TilePos               playerPos = {0, 0, 0};
    bool                  playerHasFood = false;
    EntityID              playerEID = playerID_;

    // Pack centroid (average goblin position) computed after listing goblins.
    std::vector<TilePos>  goblinPositions;

    for (EntityID eid : grid.entities) {
        const Entity* e = registry_.get(eid);
        if (!e) continue;
        switch (e->type) {
            case EntityType::Rabbit:
                rabbitPositions.push_back(e->pos);
                break;
            case EntityType::Meat:
            case EntityType::CookedMeat:
                if (e->carriedBy == INVALID_ENTITY) {
                    meatPositions.push_back(e->pos);
                    meatIDs.push_back(eid);
                }
                break;
            case EntityType::Campfire:
            case EntityType::Fire:
                firePositions.push_back(e->pos);
                break;
            case EntityType::Player:
                playerPos = e->pos;
                playerHasFood = (e->carrying != INVALID_ENTITY &&
                                 registry_.get(e->carrying) &&
                                 (registry_.get(e->carrying)->type == EntityType::Meat ||
                                  registry_.get(e->carrying)->type == EntityType::CookedMeat));
                break;
            case EntityType::Goblin:
                goblinPositions.push_back(e->pos);
                break;
            default: break;
        }
    }

    // Terrain fires count as fire positions too.
    // (Campfire entities are already collected above.)

    // Pack centroid.
    TilePos packCentroid = {0, 0, 0};
    if (!goblinPositions.empty()) {
        int sx = 0, sy = 0;
        for (const auto& p : goblinPositions) { sx += p.x; sy += p.y; }
        packCentroid = { sx / int(goblinPositions.size()),
                         sy / int(goblinPositions.size()), 0 };
    }

    // ── Nearest campfire (shared reference for whole pack) ────────────────────
    // (Goblins score toward their nearest fire individually below.)

    std::uniform_real_distribution<float> noiseDist(0.f, NOISE_EPS);

    // ── Per-goblin loop ───────────────────────────────────────────────────────
    std::vector<EntityID> snapshot(grid.entities.begin(), grid.entities.end());

    for (EntityID eid : snapshot) {
        Entity* ent = registry_.get(eid);
        if (!ent || ent->type != EntityType::Goblin) continue;

        // ── Mana decay ────────────────────────────────────────────────────────
        if (decayTick && ent->mana > 0) --ent->mana;

        if (!ent->isIdle()) continue;

        // ── Determine loaded state ────────────────────────────────────────────
        bool loaded = (ent->carrying != INVALID_ENTITY);
        Entity* carriedEnt = loaded ? registry_.get(ent->carrying) : nullptr;

        // ── Find nearest fire ─────────────────────────────────────────────────
        // firePositions already includes both Campfire and Fire entities.
        TilePos nearestFire = {0, 0, 0};
        bool    hasFire     = false;
        int bestFireDist = INT_MAX;
        for (const auto& fp : firePositions) {
            int d = manhat(ent->pos, fp);
            if (d < bestFireDist) { bestFireDist = d; nearestFire = fp; hasFire = true; }
        }

        // ── Side-effect: eat carried food when adjacent to fire ───────────────
        if (loaded && carriedEnt && hasFire && manhat(ent->pos, nearestFire) <= 1) {
            ent->mana = std::min(ent->mana + carriedEnt->mana, MANA_MAX);
            grid.remove(ent->carrying, *carriedEnt);
            registry_.destroy(ent->carrying);
            ent->carrying = INVALID_ENTITY;
            loaded = false;
            carriedEnt = nullptr;
        }


        // ── Side-effect: hit adjacent rabbit ─────────────────────────────────
        if (!loaded) {
            for (const auto& d : kDirs4) {
                TilePos adj = ent->pos + d;
                for (EntityID cid : std::vector<EntityID>(grid.spatial.at(adj))) {
                    Entity* cand = registry_.get(cid);
                    if (!cand || cand->type != EntityType::Rabbit) continue;
                    if (cand->mana > 0) --cand->mana;
                    if (cand->mana == 0) {
                        // Rabbit dies → drop Meat.
                        int dropMana = 5 + cand->mana; // mana already 0, so 5
                        TilePos deathPos = cand->pos;
                        grid.remove(cid, *cand);
                        registry_.destroy(cid);
                        onRabbitDied(cid);
                        EntityID mid = registry_.spawn(EntityType::Meat, deathPos);
                        Entity*  me  = registry_.get(mid);
                        me->mana = dropMana;
                        grid.add(mid, *me);
                        // Update meat list for this tick.
                        meatPositions.push_back(deathPos);
                        meatIDs.push_back(mid);
                    }
                    break;
                }
            }
        }

        // ── Side-effect: pick up adjacent loose meat ──────────────────────────
        if (!loaded) {
            for (const auto& d : kDirs4) {
                TilePos adj = ent->pos + d;
                for (EntityID cid : std::vector<EntityID>(grid.spatial.at(adj))) {
                    Entity* cand = registry_.get(cid);
                    if (!cand) continue;
                    if (cand->type != EntityType::Meat && cand->type != EntityType::CookedMeat) continue;
                    if (cand->carriedBy != INVALID_ENTITY) continue;
                    // Pick it up.
                    grid.spatial.remove(cid, cand->pos, cand->size);
                    cand->carriedBy = eid;
                    ent->carrying   = cid;
                    loaded = true;
                    break;
                }
                if (loaded) break;
            }
        }

        // ── Side-effect: steal food from adjacent player ──────────────────────
        if (!loaded) {
            Entity* player = registry_.get(playerEID);
            if (player && manhat(ent->pos, player->pos) == 1 && player->carrying != INVALID_ENTITY) {
                Entity* item = registry_.get(player->carrying);
                if (item && (item->type == EntityType::Meat || item->type == EntityType::CookedMeat)) {
                    // Transfer from player to goblin.
                    item->carriedBy   = eid;
                    ent->carrying     = player->carrying;
                    player->carrying  = INVALID_ENTITY;
                    loaded = true;
                }
            }
        }

        // Movement is now handled by tickResponseMovement.
        (void)loaded;
    }
}

// ─── Rabbit AI ───────────────────────────────────────────────────────────────
//
// Movement is chosen by scoring each of the 4 directions with a single equation:
//
//   score(d) = hunger  * grass_at(pos+d)
//            + satiety * approach(d, warren)
//            + fear    * approach(d, warren)
//            + ε * noise
//
// where:
//   hunger  = clamp((FULL  - mana) / FULL,              0, 1)
//   satiety = clamp((mana  - HUNGRY) / (FULL - HUNGRY), 0, 1)
//   fear    = clamp((FLEE_RADIUS - nearest_threat) / FLEE_RADIUS, 0, 1)
//   approach(d, target) = dot(d, normalise(target - pos))  ∈ [-1, 1]
//
// Move only if bestScore > INERTIA (keeps rabbit still when nothing pulls it).
// Warren entry/exit and starvation remain explicit (unavoidable side-effects).

void Game::tickRabbitAI(Field& grid, Tick currentTick) {
    static constexpr int   FLEE_RADIUS  = 6;
    static const TilePos   kDirs4[]    = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0}};

    // ── Mana decay ───────────────────────────────────────────────────────────
    bool decayTick = (currentTick % RABBIT_DECAY_RATE == 0);

    // ── Collect threat positions (world grid only) ────────────────────────────
    bool isWorldGrid = (grid.id == FIELD_WORLD);
    std::vector<TilePos> threats;
    if (isWorldGrid) {
        for (EntityID eid : grid.entities) {
            const Entity* e = registry_.get(eid);
            if (!e) continue;
            if (e->type == EntityType::Player || e->type == EntityType::Goblin)
                threats.push_back(e->pos);
        }
    }

    // ── Collect LongGrass positions (world grid only) ─────────────────────────
    std::unordered_map<TilePos, EntityID, TilePosHash> grassAt;
    if (isWorldGrid) {
        for (EntityID eid : grid.entities) {
            const Entity* e = registry_.get(eid);
            if (e && e->type == EntityType::LongGrass) grassAt[e->pos] = eid;
        }
    }

    std::vector<EntityID> toEat;               // LongGrass entities to destroy
    std::vector<PendingTransfer> transfers;    // warren in/out

    std::vector<EntityID> snapshot(grid.entities.begin(), grid.entities.end());

    for (EntityID eid : snapshot) {
        Entity* ent = registry_.get(eid);
        if (!ent || ent->type != EntityType::Rabbit) continue;

        // ── Mana decay / starvation ───────────────────────────────────────────
        if (decayTick && ent->mana > 0) {
            --ent->mana;
            if (ent->mana == 0) {
                // Drop meat at the rabbit's position before destroying it.
                TilePos deathPos   = ent->pos;
                int     rabbitMana = ent->mana;
                EntityID meatID = registry_.spawn(EntityType::Meat, deathPos);
                Entity*  meat   = registry_.get(meatID);
                meat->mana = 5 + rabbitMana;
                grid.add(meatID, *meat);
                // Rabbit starves — defer cleanup via onRabbitDied.
                grid.remove(eid, *ent);
                registry_.destroy(eid);
                onRabbitDied(eid);
                continue;
            }
        }

        // ── Warren interior: emerge when hungry ───────────────────────────────
        if (!isWorldGrid) {
            if (ent->mana < RABBIT_MANA_HUNGRY && ent->isIdle()) {
                auto sit = rabbitSlots_.find(eid);
                if (sit != rabbitSlots_.end()) {
                    auto wit = warrenData_.find(sit->second.warrenEid);
                    if (wit != warrenData_.end()) {
                        TilePos wp = wit->second.worldPos;
                        TilePos emergePos = { wp.x, wp.y + 1, wp.z };
                        transfers.push_back({ eid, grid.id, FIELD_WORLD, emergePos });
                        sit->second.emergedAt = currentTick;  // start cooldown
                    }
                }
            }
            continue;
        }

        if (!ent->isIdle()) continue;   // world grid: only act when idle

        // ── Eat adjacent LongGrass if hungry ──────────────────────────────────
        if (ent->mana < RABBIT_MANA_FULL) {
            for (const auto& d : kDirs4) {
                auto git = grassAt.find(ent->pos + d);
                if (git == grassAt.end()) continue;
                toEat.push_back(git->second);
                grassAt.erase(git);
                ent->mana = std::min(ent->mana + RABBIT_EAT_GAIN, RABBIT_MANA_MAX);
                break;
            }
        }

        // ── Find warren info ──────────────────────────────────────────────────
        auto sit = rabbitSlots_.find(eid);
        if (sit == rabbitSlots_.end()) continue;
        auto wit = warrenData_.find(sit->second.warrenEid);
        if (wit == warrenData_.end()) continue;
        const TilePos  warrenPos = wit->second.worldPos;
        const FieldID  warrenGID = sit->second.warrenFieldID;

        // ── Weights ───────────────────────────────────────────────────────────
        float hunger  = std::clamp(float(RABBIT_MANA_FULL  - ent->mana) / RABBIT_MANA_FULL, 0.f, 1.f);
        float satiety = std::clamp(float(ent->mana - RABBIT_MANA_HUNGRY) / (RABBIT_MANA_FULL - RABBIT_MANA_HUNGRY), 0.f, 1.f);

        float nearestThreat = float(FLEE_RADIUS + 1);
        for (const TilePos& t : threats)
            nearestThreat = std::min(nearestThreat, float(std::abs(t.x - ent->pos.x) + std::abs(t.y - ent->pos.y)));
        float fear = std::clamp((FLEE_RADIUS - nearestThreat) / FLEE_RADIUS, 0.f, 1.f);

        float warren_pull = satiety + fear;

        // ── At warren entrance → enter warren (side-effect, unavoidable) ─────
        // Cooldown prevents immediate re-entry after emergence (e.g. player is
        // standing nearby and fear would pull the rabbit straight back in).
        static constexpr Tick EMERGE_COOLDOWN = 400;  // ~8 s at 50 Hz
        bool cooledDown = (currentTick - sit->second.emergedAt >= EMERGE_COOLDOWN);
        if (warren_pull > 0.f && ent->pos == warrenPos && cooledDown) {
            transfers.push_back({ eid, FIELD_WORLD, warrenGID,
                                   { WARREN_W / 2, WARREN_H / 2 - 1, 0 } });
            continue;
        }

        // Movement is now handled by tickResponseMovement.
        (void)hunger;
    }

    // ── Apply eats ───────────────────────────────────────────────────────────
    for (EntityID geid : toEat) {
        Entity* ge = registry_.get(geid);
        if (ge) grid.remove(geid, *ge);
        registry_.destroy(geid);
    }

    // ── Apply transfers ───────────────────────────────────────────────────────
    for (auto& t : transfers)
        pendingTransfers_.push_back(t);
}

// ─── Rabbit breeding ─────────────────────────────────────────────────────────
//
// Runs on warren interior grids only.
// Two full rabbits in the same warren → spawn a new hungry rabbit inside.
// Warren population cap: 8 rabbits per warren.

void Game::tickRabbitBreeding(Field& grid) {
    static constexpr int BREED_RATE = 2000;
    static constexpr int POP_CAP    = 8;
    static const TilePos kDirs4[]   = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0}};

    // Only run on warren interior grids.
    bool isWarren = false;
    EntityID warrenEid = INVALID_ENTITY;
    for (auto& [weid, wd] : warrenData_) {
        if (wd.fieldID == grid.id) { isWarren = true; warrenEid = weid; break; }
    }
    if (!isWarren) return;

    // Count rabbits and find full idle ones.
    std::vector<EntityID> fullRabbits;
    int total = 0;
    for (EntityID eid : grid.entities) {
        const Entity* e = registry_.get(eid);
        if (!e || e->type != EntityType::Rabbit) continue;
        ++total;
        if (e->mana >= RABBIT_MANA_FULL && e->isIdle()) fullRabbits.push_back(eid);
    }
    if (total >= POP_CAP || fullRabbits.size() < 2) return;
    if (static_cast<int>(worldRng_() % BREED_RATE) != 0) return;

    // Find a free tile adjacent to one of the full rabbits.
    std::shuffle(fullRabbits.begin(), fullRabbits.end(), worldRng_);
    for (EntityID parent : fullRabbits) {
        const Entity* pe = registry_.get(parent);
        if (!pe) continue;
        int dirs[4] = {0,1,2,3};
        std::shuffle(std::begin(dirs), std::end(dirs), worldRng_);
        for (int di : dirs) {
            TilePos adj = pe->pos + kDirs4[di];
            adj.x = std::clamp(adj.x, 0, WARREN_W - 1);
            adj.y = std::clamp(adj.y, 0, WARREN_H - 1);
            if (!grid.spatial.at(adj).empty()) continue;
            EntityID kid = registry_.spawn(EntityType::Rabbit, adj);
            Entity* ke = registry_.get(kid);
            ke->mana = RABBIT_MANA_HUNGRY - 1;  // born hungry → will emerge
            grid.add(kid, *ke);
            rabbitSlots_[kid] = rabbitSlots_[parent];  // inherits warren
            return;
        }
    }
}

// ─── Movement ────────────────────────────────────────────────────────────────

void Game::tickMovement(Field& grid) {
    std::vector<EntityID> snapshot = grid.entities;

    for (EntityID eid : snapshot) {
        Entity* ent = registry_.get(eid);
        if (!ent) continue;
        if (ent->carriedBy != INVALID_ENTITY) continue;  // carried; skip independent movement
        TilePos oldPos  = ent->pos;
        bool    arrived = stepMovement(*ent);
        if (arrived) {
            grid.spatial.move(eid, oldPos, ent->pos, ent->size);

            // Water slows movement: half speed while standing on a Water entity tile.
            if (ent->speed > 0.0f) {
                float baseSpeed = defaultConfig(ent->type).speed;
                bool  onWater   = false;
                for (EntityID at : grid.spatial.at(ent->pos)) {
                    const Entity* ae = registry_.get(at);
                    if (ae && ae->type == EntityType::Water) { onWater = true; break; }
                }
                ent->speed = onWater ? baseSpeed * 0.5f : baseSpeed;
            }
            grid.events.emit({ EventType::Arrived, eid });

            if (eid == playerID_) {
                audioEvents_.push_back(AudioEvent::PlayerStep);
                if (ent->pos.z != playerPrevZ_) {
                    visualEvents_.push_back({ VisualEventType::PlayerLand,
                                             toVec(ent->pos),
                                             static_cast<float>(ent->pos.z) });
                }
                playerPrevZ_ = ent->pos.z;
            }

            // Portal check: queue a transfer for any entity that lands on a portal.
            // Rabbits manage their own warren transfers in tickRabbitAI; skip them here.
            if (ent->type != EntityType::Rabbit) {
                auto pit = grid.portals.find(ent->pos);
                if (pit != grid.portals.end())
                    pendingTransfers_.push_back({ eid, grid.id,
                                                  pit->second.targetField, pit->second.targetPos });
            }
        }
    }

    // Sync carried entities to their carrier's position each tick.
    for (EntityID eid : snapshot) {
        Entity* ent = registry_.get(eid);
        if (!ent || ent->carriedBy == INVALID_ENTITY) continue;
        const Entity* carrier = registry_.get(ent->carriedBy);
        if (!carrier) { ent->carriedBy = INVALID_ENTITY; continue; }
        ent->pos          = carrier->pos;
        ent->destination  = carrier->destination;
        ent->moveProgress = carrier->moveProgress;
    }
}

// ─── Response-profile movement ───────────────────────────────────────────────
//
// Field-gradient movement driven by ResponseProfile constants.
// Replaces the hardcoded scoring equations formerly in tickGoblinAI and
// tickRabbitAI.  Runs after those functions (which still handle side-effects
// like eating, hitting, and warren entry) but before tickVM and tickMovement.
//
// Algorithm (per idle entity with a non-zero ResponseProfile):
//   1. For each of the 4 cardinal directions, accumulate a score by summing
//      resp_p * src_pp_p / (dist² + 1) across all field-source entities and
//      all 10 principles.
//   2. urgency = |bestScore| > urgencyThreshold  (handles both flee and approach)
//   3. Move if urgent, otherwise probabilistically based on wanderRate.

void Game::tickResponseMovement(Field& field, Tick currentTick) {
    static const TilePos kDirs4[] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0}};

    // ── Lazy-initialise PrincipleProfiles for field entities ──────────────────
    for (EntityID eid : field.entities) {
        if (!principleComponents_.has(eid)) {
            const Entity* e = registry_.get(eid);
            if (!e) continue;
            principleComponents_.add(eid, principleProfile(e->type));
        }
    }

    // ── Collect field sources ─────────────────────────────────────────────────
    struct Source { TilePos pos; PrincipleProfile pp; };
    std::vector<Source> sources;
    sources.reserve(field.entities.size());
    for (EntityID eid : field.entities) {
        const Entity* e = registry_.get(eid);
        if (!e || e->carriedBy != INVALID_ENTITY) continue;
        const PrincipleProfile* pp = principleComponents_.get(eid);
        if (pp) sources.push_back({ e->pos, *pp });
    }

    // ── Per-entity movement ────────────────────────────────────────────────────
    std::vector<EntityID> snapshot(field.entities.begin(), field.entities.end());

    for (EntityID eid : snapshot) {
        Entity* ent = registry_.get(eid);
        if (!ent || !ent->isIdle()) continue;
        if (ent->carriedBy != INVALID_ENTITY) continue;

        bool loaded = (ent->carrying != INVALID_ENTITY);
        ResponseProfile resp = responseProfile(ent->type, loaded);

        // Skip entities with no response (all zeros except defaults).
        if (resp.heat == 0 && resp.cold == 0 &&
            resp.wet  == 0 && resp.dry  == 0 &&
            resp.life == 0 && resp.death == 0 &&
            resp.pos  == 0 && resp.neg   == 0 &&
            resp.adhesive == 0 && resp.repellent == 0) continue;

        // Scale life response by hunger when entity has a mana pool.
        float lifeScale = 1.f;
        if (resp.manaMax > 0) {
            lifeScale = std::clamp(float(resp.manaMax - ent->mana) / resp.manaMax,
                                   0.f, 1.f);
        }

        // ── Score each direction ──────────────────────────────────────────────
        float   scores[4] = {};
        float   bestScore  = 0.f;
        float   worstScore = 0.f;
        int     bestDir    = -1;

        for (int di = 0; di < 4; ++di) {
            const TilePos& d = kDirs4[di];
            TilePos probe = ent->pos + d;
            float s = 0.f;

            for (const Source& src : sources) {
                if (src.pos == ent->pos) continue; // skip self
                float dx = float(src.pos.x - probe.x);
                float dy = float(src.pos.y - probe.y);
                float dz = float(src.pos.z - probe.z);
                float dist2 = dx*dx + dy*dy + dz*dz;
                float inv = 1.f / (dist2 + 1.f);

                s += float(resp.heat)      * float(src.pp.heat)      * inv;
                s += float(resp.cold)      * float(src.pp.cold)      * inv;
                s += float(resp.wet)       * float(src.pp.wet)       * inv;
                s += float(resp.dry)       * float(src.pp.dry)       * inv;
                s += float(resp.life)      * lifeScale
                                           * float(src.pp.life)      * inv;
                s += float(resp.death)     * float(src.pp.death)     * inv;
                s += float(resp.pos)       * float(src.pp.pos)       * inv;
                s += float(resp.neg)       * float(src.pp.neg)       * inv;
                s += float(resp.adhesive)  * float(src.pp.adhesive)  * inv;
                s += float(resp.repellent) * float(src.pp.repellent) * inv;
            }

            scores[di] = s;
            if (s > bestScore || bestDir < 0) { bestScore = s; bestDir = di; }
            if (s < worstScore) worstScore = s;
        }

        // ── Urgency gate ──────────────────────────────────────────────────────
        // Use a deterministic hash of (entity ID, tick) so the wander gate does
        // not consume from worldRng_ and does not disturb other systems.
        bool urgent = bestScore  >  resp.urgencyThreshold
                   || worstScore < -resp.urgencyThreshold;
        if (!urgent) {
            uint32_t h = (static_cast<uint32_t>(eid)         * 2654435761u)
                       ^ (static_cast<uint32_t>(currentTick) * 2246822519u);
            if (h % static_cast<uint32_t>(resp.wanderRate) != 0) continue;
        }

        // If wander (non-urgent), pick a direction from the gradient (or any
        // direction when the gradient is flat).
        if (!urgent && bestDir < 0) {
            uint32_t h2 = (static_cast<uint32_t>(eid)         * 1234567891u)
                        ^ (static_cast<uint32_t>(currentTick) * 987654321u);
            bestDir = static_cast<int>(h2 % 4);
        }

        TilePos bestDirPos = kDirs4[bestDir];
        TilePos newDest    = ent->pos + bestDirPos;
        if (!resolveDestHeight(newDest, ent->pos, field)) continue;

        std::vector<MoveIntention> intentions = {{
            eid, ent->pos, newDest, ent->type, ent->size
        }};
        auto allowed = resolveMoves(intentions, field.spatial, registry_);
        if (allowed.count(eid)) {
            ent->destination = newDest;
            ent->facing      = toDirection(bestDirPos);
        }
    }
}
