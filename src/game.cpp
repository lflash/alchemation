#include "game.hpp"
#include "routine.hpp"
#include "terrain.hpp"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <queue>
#include <unordered_set>

// ─── Summon helpers ───────────────────────────────────────────────────────────

struct GolemInfo {
    EntityType  type;
    const char* name;
    int         manaCost;
};

static const GolemInfo GOLEM_TABLE[] = {
    { EntityType::MudGolem,    "Mud Golem",    3 },
    { EntityType::StoneGolem,  "Stone Golem",  5 },
    { EntityType::ClayGolem,   "Clay Golem",   3 },
    { EntityType::BushGolem,   "Bush Golem",   2 },
    { EntityType::WoodGolem,   "Wood Golem",   4 },
    { EntityType::IronGolem,   "Iron Golem",   6 },
    { EntityType::CopperGolem, "Copper Golem", 4 },
    // WaterGolem deferred to Phase 14 (requires Water tile type)
};

static const GolemInfo* golemForMedium(TileType t) {
    switch (t) {
        case TileType::Mud:    return &GOLEM_TABLE[0];
        case TileType::Stone:  return &GOLEM_TABLE[1];
        case TileType::Clay:   return &GOLEM_TABLE[2];
        case TileType::Bush:   return &GOLEM_TABLE[3];
        case TileType::Wood:   return &GOLEM_TABLE[4];
        case TileType::Iron:   return &GOLEM_TABLE[5];
        case TileType::Copper: return &GOLEM_TABLE[6];
        default: return nullptr;
    }
}

// ─── transferEntity ───────────────────────────────────────────────────────────

void transferEntity(EntityID eid, Grid& from, Grid& to,
                    EntityRegistry& registry, TilePos dest) {
    Entity* e = registry.get(eid);
    if (!e) return;
    from.remove(eid, *e);
    e->pos = e->destination = dest;
    e->moveT = 0.0f;
    to.add(eid, *e);
}

// ─── Event subscriptions ─────────────────────────────────────────────────────
//
// Called for every grid (world, studio, and each dynamic room). Subscribes the
// mushroom-collection handler. Portal detection happens in tickMovement instead
// of here to avoid modifying grids from inside an event callback.

void Game::subscribeEvents(Grid& grid) {
    grid.events.subscribe(EventType::Arrived, [this](const Event& ev) {
        if (ev.subject != playerID_) return;
        Entity* player = registry_.get(playerID_);
        if (!player) return;
        Grid& g = activeGrid();
        for (EntityID cid : g.spatial.at(player->pos)) {
            if (cid == playerID_) continue;
            Entity* cand = registry_.get(cid);
            if (!cand) continue;
            if (cand->type == EntityType::Mushroom) {
                player->mana += 3;
                visualEvents_.push_back({ VisualEventType::CollectMushroom,
                                          toVec(cand->pos), static_cast<float>(cand->pos.z) });
                g.remove(cid, *cand);
                registry_.destroy(cid);
                audioEvents_.push_back(AudioEvent::CollectMushroom);
                break;
            }
            if (cand->type == EntityType::Chest) {
                player->mana += 5;
                visualEvents_.push_back({ VisualEventType::CollectMushroom,
                                          toVec(cand->pos), static_cast<float>(cand->pos.z) });
                g.remove(cid, *cand);
                registry_.destroy(cid);
                audioEvents_.push_back(AudioEvent::CollectMushroom);
                break;
            }
        }
    });
}

// ─── Construction ─────────────────────────────────────────────────────────────

Game::Game() {
    grids_.try_emplace(GRID_WORLD,  GRID_WORLD);
    grids_.try_emplace(GRID_STUDIO, GRID_STUDIO);

    subscribeEvents(grids_.at(GRID_WORLD));
    subscribeEvents(grids_.at(GRID_STUDIO));

    Grid& world = grids_.at(GRID_WORLD);

    TilePos playerStart = {0, 0, 0};
    playerStart.z = world.terrain.levelAt(playerStart);
    playerID_ = registry_.spawn(EntityType::Player, playerStart);
    world.add(playerID_, *registry_.get(playerID_));

    TilePos goblinStart = {5, 5, 0};
    goblinStart.z = world.terrain.levelAt(goblinStart);
    EntityID goblinID = registry_.spawn(EntityType::Goblin, goblinStart);
    world.add(goblinID, *registry_.get(goblinID));

    // Helper: spawn a static entity at the correct terrain height.
    auto spawnStatic = [&](EntityType type, int x, int y) {
        TilePos p = {x, y, 0};
        p.z = world.terrain.levelAt(p);
        EntityID id = registry_.spawn(type, p);
        world.add(id, *registry_.get(id));
        return id;
    };

    // ── Fire demo ─────────────────────────────────────────────────────────────
    // Campfire at {3,2} with a TreeStump and Log adjacent — they will ignite
    // after ~5 seconds of continuous heat.
    spawnStatic(EntityType::Campfire,  3,  2);
    spawnStatic(EntityType::TreeStump, 4,  2);   // east of campfire
    spawnStatic(EntityType::Log,       3,  3);   // south of campfire

    // ── Phase 12 demo: medium tiles + terrain objects ─────────────────────────
    // A row of summoning mediums south of spawn so the player can test golems.
    world.terrain.setType({-3,  4}, TileType::Mud);
    world.terrain.setType({-2,  4}, TileType::Stone);
    world.terrain.setType({-1,  4}, TileType::Clay);
    world.terrain.setType({ 0,  4}, TileType::Bush);
    world.terrain.setType({ 1,  4}, TileType::Wood);
    world.terrain.setType({ 2,  4}, TileType::Iron);
    world.terrain.setType({ 3,  4}, TileType::Copper);

    // A Tree, Rock and Chest near spawn (west side, off the north navigation corridor).
    spawnStatic(EntityType::Tree,  -5, -6);
    spawnStatic(EntityType::Rock,  -4, -6);
    spawnStatic(EntityType::Chest, -3, -6);

    // ── Voltage demo ──────────────────────────────────────────────────────────
    // Battery at {-4,0} powers a puddle chain running east.
    // Lightbulb at {-1,0} sits on the third puddle (2V) and will be lit.
    // Second lightbulb at {3,0} is on plain grass — unlit.
    spawnStatic(EntityType::Battery,   -4,  0);
    world.terrain.setType({-3, 0}, TileType::Puddle);   // 4V
    world.terrain.setType({-2, 0}, TileType::Puddle);   // 3V
    world.terrain.setType({-1, 0}, TileType::Puddle);   // 2V
    spawnStatic(EntityType::Lightbulb, -1,  0);   // on puddle → lit
    spawnStatic(EntityType::Lightbulb,  3,  0);   // on grass  → unlit

    // ── Phase 14 demo: water pool south-east of spawn ────────────────────────
    // Placed at (20,20) — far enough away that water spreading won't reach the
    // spawn area during tests (~50 ticks).
    world.terrain.setType({20, 20}, TileType::Water);
    world.terrain.setType({21, 20}, TileType::Water);
    world.terrain.setType({20, 21}, TileType::Water);
}

// ─── Top-level tick ──────────────────────────────────────────────────────────

void Game::tick(const Input& input, Tick currentTick) {
    applyPendingTransfer();

    // Capture before the loop: tickPlayerInput() may change activeGridID_ mid-loop,
    // and we must not call it twice (once for the old grid, once for the new one).
    GridID activeAtStart = activeGridID_;

    // All non-paused grids tick every frame.
    // Player input only applies to the active grid.
    for (auto& [id, grid] : grids_) {
        if (grid.paused) continue;
        tickScheduler(grid, currentTick);
        if (id == activeAtStart) tickPlayerInput(input);
        tickGoblinWander(grid);
        tickVM(grid);
        tickMovement(grid);
        tickFire(grid, registry_, currentTick);
        tickVoltage(grid, registry_);
        tickWater(grid);
        grid.events.flush();
    }
}

// ─── Pending portal transfer ─────────────────────────────────────────────────

void Game::applyPendingTransfer() {
    for (auto& t : pendingTransfers_) {
        if (grids_.count(t.fromGrid) && grids_.count(t.toGrid)) {
            transferEntity(t.eid, grids_.at(t.fromGrid), grids_.at(t.toGrid),
                           registry_, t.toPos);
            if (t.eid == playerID_) {
                activeGridID_     = t.toGrid;
                gridJustSwitched_ = true;
                audioEvents_.push_back(AudioEvent::PortalEnter);
                visualEvents_.push_back({ VisualEventType::PortalEnter, {0,0}, 0 });
            }
        }
    }
    pendingTransfers_.clear();
}

// ─── Accessors ────────────────────────────────────────────────────────────────

int Game::playerMana() const {
    const Entity* p = registry_.get(playerID_);
    return p ? p->mana : 0;
}

TilePos Game::playerPos() const {
    const Entity* p = registry_.get(playerID_);
    return p ? p->pos : TilePos{0, 0};
}

TilePos Game::playerDestination() const {
    const Entity* p = registry_.get(playerID_);
    return p ? p->destination : TilePos{0, 0};
}

float Game::playerMoveT() const {
    const Entity* p = registry_.get(playerID_);
    return p ? p->moveT : 0.0f;
}

std::vector<RecordingInfo> Game::recordingList() const {
    std::vector<RecordingInfo> result;
    for (size_t i = 0; i < recorder_.recordings.size(); ++i) {
        const Recording& rec = recorder_.recordings[i];
        int steps = 0;
        for (const auto& instr : rec.instructions)
            if (instr.op != OpCode::HALT) ++steps;
        result.push_back({ i, rec.name, steps, rec.manaCost(), i == selectedRecording_ });
    }
    return result;
}

SummonPreview Game::playerSummonPreview() const {
    const Entity* player = registry_.get(playerID_);
    if (!player || !player->isIdle()) return {};
    const Grid& grid = activeGrid();
    TilePos ahead = player->pos + dirToDelta(player->facing);
    const GolemInfo* gi = golemForMedium(grid.terrain.typeAt(ahead));
    if (!gi) return {};
    return { true, gi->type, gi->name, gi->manaCost, player->mana >= gi->manaCost };
}

void Game::renameRecording(size_t index, const std::string& name) {
    if (index < recorder_.recordings.size())
        recorder_.recordings[index].name = name;
}

// ─── Draw order ──────────────────────────────────────────────────────────────

std::vector<const Entity*> Game::drawOrder() const {
    const Grid& grid = activeGrid();
    std::vector<const Entity*> result;
    result.reserve(grid.entities.size());
    for (EntityID eid : grid.entities) {
        const Entity* e = registry_.get(eid);
        if (e) result.push_back(e);
    }
    std::sort(result.begin(), result.end(),
              [](const Entity* a, const Entity* b) {
                  if (a->pos.y != b->pos.y) return a->pos.y < b->pos.y;
                  if (a->pos.z != b->pos.z) return a->pos.z < b->pos.z;
                  return a->layer < b->layer;
              });
    return result;
}

// ─── Scheduler ───────────────────────────────────────────────────────────────

void Game::tickScheduler(Grid& grid, Tick currentTick) {
    for (auto& action : grid.scheduler.popDue(currentTick)) {
        if (action.type == ActionType::Despawn) {
            Entity* target = registry_.get(action.entity);
            if (target) grid.remove(action.entity, *target);
            registry_.destroy(action.entity);
        } else if (action.type == ActionType::ChangeMana) {
            Entity* target = registry_.get(action.entity);
            if (target)
                target->mana += std::get<ChangeManaPayload>(action.payload).delta;
        }
    }
}

// ─── Height helper ────────────────────────────────────────────────────────────
//
// Set dest.z from terrain for unbounded grids, then return whether the height
// step is legal (≤ 1 level). Bounded grids have flat floors, so dest.z is
// unchanged and this always returns true for them.

static bool resolveDestHeight(TilePos& dest, const TilePos& from, const Grid& grid) {
    if (!grid.isBounded())
        dest.z = grid.terrain.levelAt(dest);
    return std::abs(dest.z - from.z) <= 1;
}

// ─── Player input ─────────────────────────────────────────────────────────────

void Game::tickPlayerInput(const Input& input) {
    Grid&   grid   = activeGrid();
    Entity* player = registry_.get(playerID_);

    // r: toggle recording
    if (input.pressed(Action::Record)) {
        if (recorder_.isRecording()) {
            recorder_.stop();
            selectedRecording_ = recorder_.recordings.size() - 1;
            audioEvents_.push_back(AudioEvent::RecordStop);
        } else {
            recorder_.start();
            audioEvents_.push_back(AudioEvent::RecordStart);
        }
    }

    // q: cycle selected recording
    if (input.pressed(Action::CycleRecording) && !recorder_.recordings.empty())
        selectedRecording_ = (selectedRecording_ + 1) % recorder_.recordings.size();

    // e: deploy selected recording as Poop agent in front of player
    if (input.pressed(Action::Deploy) && player &&
        !recorder_.recordings.empty() &&
        selectedRecording_ < recorder_.recordings.size()) {

        const Recording& rec  = recorder_.recordings[selectedRecording_];
        int              cost = rec.manaCost();

        if (player->mana >= cost) {
            player->mana -= cost;
            if (player->mana < 1) player->mana = 1;
            TilePos  spawnPos = player->pos + dirToDelta(player->facing);
            EntityID pid      = registry_.spawn(EntityType::Poop, spawnPos);
            Entity*  pe       = registry_.get(pid);
            pe->facing        = player->facing;
            grid.add(pid, *pe);
            agentStates_[pid]     = AgentExecState{};
            agentRecordings_[pid] = rec;
            audioEvents_.push_back(AudioEvent::DeployAgent);
            visualEvents_.push_back({ VisualEventType::DeployAgent,
                                      toVec(spawnPos), static_cast<float>(spawnPos.z) });
        }
    }

    // Tab: toggle between world and studio
    if (input.pressed(Action::SwitchGrid) && player && player->isIdle()) {
        if (activeGridID_ == GRID_WORLD) {
            playerWorldPos_ = player->pos;
            transferEntity(playerID_, grids_.at(GRID_WORLD), grids_.at(GRID_STUDIO),
                           registry_, {0, 0});
            activeGridID_ = GRID_STUDIO;
        } else if (activeGridID_ == GRID_STUDIO) {
            transferEntity(playerID_, grids_.at(GRID_STUDIO), grids_.at(GRID_WORLD),
                           registry_, playerWorldPos_);
            activeGridID_ = GRID_WORLD;
        }
        gridJustSwitched_ = true;
        audioEvents_.push_back(AudioEvent::GridSwitch);
        visualEvents_.push_back({ VisualEventType::GridSwitch, {0,0}, 0 });
        player = registry_.get(playerID_);
    }

    if (!player || !player->isIdle()) return;

    if (recorder_.isRecording()) recorder_.tick();

    // Movement
    TilePos delta = {0, 0};
    if (input.held(Action::MoveUp)) delta.y -= 1;
    if (input.held(Action::MoveDown)) delta.y += 1;
    if (input.held(Action::MoveLeft)) delta.x -= 1;
    if (input.held(Action::MoveRight)) delta.x += 1;

    if (delta != TilePos{0, 0}) {
        TilePos   newDest      = player->pos + delta;
        Direction facingBefore = player->facing;
        if (!input.held(Action::Strafe))
            player->facing = toDirection(delta);

        // Clamp to grid bounds if bounded (XY only — z free)
        if (grid.isBounded()) {
            newDest.x = std::clamp(newDest.x, 0, grid.width  - 1);
            newDest.y = std::clamp(newDest.y, 0, grid.height - 1);
        }

        if (resolveDestHeight(newDest, player->pos, grid)) {

        std::vector<MoveIntention> intentions = {{
            playerID_, player->pos, newDest, player->type, player->size
        }};
        auto allowed = resolveMoves(intentions, grid.spatial, registry_);

        if (allowed.count(playerID_)) {
            player->destination = newDest;
            if (recorder_.isRecording())
                recorder_.recordMove(delta, facingBefore);
        } else {
            // Bump interactions
            Bounds destBounds = boundsAt(newDest, player->size);
            for (EntityID cid : grid.spatial.query(destBounds, newDest.z)) {
                Entity* cand = registry_.get(cid);
                if (!cand) continue;
                if (!overlaps(destBounds, boundsAt(cand->pos, cand->size))) continue;

                if (cand->type == EntityType::Goblin) {
                    // Combat: goblin takes damage, gets pushed
                    cand->health -= player->mana;
                    Vec2f goblinVpos = toVec(cand->pos);
                    float goblinVz   = static_cast<float>(cand->pos.z);
                    audioEvents_.push_back(AudioEvent::GoblinHit);
                    if (cand->health <= 0) {
                        visualEvents_.push_back({ VisualEventType::GoblinDie,
                                                 goblinVpos, goblinVz, cid, cand->type });
                        grid.remove(cid, *cand);
                        registry_.destroy(cid);
                    } else {
                        visualEvents_.push_back({ VisualEventType::GoblinHit,
                                                 goblinVpos, goblinVz, cid });
                        TilePos pushBase = cand->isMoving() ? cand->destination : cand->pos;
                        TilePos pushDest = pushBase + delta;
                        if (cand->isMoving()) {
                            Bounds pushBounds  = boundsAt(pushDest, cand->size);
                            bool   pushBlocked = false;
                            for (EntityID oid : grid.spatial.query(pushBounds, pushDest.z)) {
                                if (oid == cid) continue;
                                const Entity* occ = registry_.get(oid);
                                if (!occ || !overlaps(pushBounds, boundsAt(occ->pos, occ->size))) continue;
                                if (resolveCollision(cand->type, occ->type) == CollisionResult::Block) {
                                    pushBlocked = true; break;
                                }
                            }
                            if (!pushBlocked) {
                                grid.spatial.remove(cid, cand->destination, cand->size);
                                grid.spatial.add(cid, pushDest, cand->size);
                                cand->destination = pushDest;
                            }
                        } else {
                            std::vector<MoveIntention> push = {{
                                cid, cand->pos, pushDest, cand->type, cand->size
                            }};
                            auto pushAllowed = resolveMoves(push, grid.spatial, registry_);
                            if (pushAllowed.count(cid)) cand->destination = pushDest;
                        }
                    }
                    break;

                } else if (cand->isIdle() && cand->hasCapability(Capability::Pushable)) {
                    // Push: shove the entity one tile in the movement direction
                    TilePos pushDest = cand->pos + delta;
                    if (grid.isBounded()) {
                        pushDest.x = std::clamp(pushDest.x, 0, grid.width  - 1);
                        pushDest.y = std::clamp(pushDest.y, 0, grid.height - 1);
                    }
                    std::vector<MoveIntention> push = {{
                        cid, cand->pos, pushDest, cand->type, cand->size
                    }};
                    auto pushAllowed = resolveMoves(push, grid.spatial, registry_);
                    if (pushAllowed.count(cid)) cand->destination = pushDest;
                    break;
                }
            }
        }
        } // height check

    }

    // Terrain interaction
    TilePos ahead = player->pos + dirToDelta(player->facing);
    if (input.pressed(Action::Dig)) {
        grid.terrain.dig(ahead);
        recorder_.recordDig();
        audioEvents_.push_back(AudioEvent::Dig);
        visualEvents_.push_back({ VisualEventType::Dig,
                                  toVec(ahead), static_cast<float>(ahead.z) });
    }

    if (input.pressed(Action::Plant)) {
        if (grid.terrain.typeAt(ahead) == TileType::BareEarth && player->mana >= 1) {
            EntityID mid = registry_.spawn(EntityType::Mushroom, ahead);
            grid.add(mid, *registry_.get(mid));
            grid.terrain.restore(ahead);
            player->mana--;
            if (player->mana < 1) player->mana = 1;
            recorder_.recordPlant();
            audioEvents_.push_back(AudioEvent::Plant);
        }
    }


    // G: summon golem from medium tile ahead
    if (input.pressed(Action::Summon) && player) {
        TilePos ahead = player->pos + dirToDelta(player->facing);
        const GolemInfo* gi = golemForMedium(grid.terrain.typeAt(ahead));
        if (gi && player->mana >= gi->manaCost) {
            player->mana -= gi->manaCost;
            if (player->mana < 1) player->mana = 1;
            grid.terrain.dig(ahead);   // consume medium tile → BareEarth

            EntityID gid = registry_.spawn(gi->type, ahead);
            Entity*  ge  = registry_.get(gid);
            ge->facing   = player->facing;
            grid.add(gid, *ge);

            // Assign the selected recording if one exists
            if (!recorder_.recordings.empty() &&
                selectedRecording_ < recorder_.recordings.size()) {
                agentStates_[gid]     = AgentExecState{};
                agentRecordings_[gid] = recorder_.recordings[selectedRecording_];
            }

            audioEvents_.push_back(AudioEvent::Summon);
            visualEvents_.push_back({ VisualEventType::Summon,
                                      toVec(ahead), static_cast<float>(ahead.z) });
        }
    }

    // O: create portal + linked room ahead of player
    if (input.pressed(Action::PlacePortal) && player) {
        TilePos fwd = player->pos + dirToDelta(player->facing);
        // Only create if the tile is empty and not already a portal
        if (!grid.portals.count(fwd) &&
            grid.terrain.typeAt(fwd) != TileType::Portal) {

            GridID  newID     = nextGridID_++;
            TilePos roomEntry = { ROOM_W / 2, ROOM_H / 2 };
            TilePos returnDst = fwd;   // back on the portal tile itself (safe: portal only fires on move-arrival)

            // Create the new bounded grid
            grids_.try_emplace(newID, newID, ROOM_W, ROOM_H);
            Grid& room = grids_.at(newID);
            subscribeEvents(room);

            // Forward portal in current grid
            grid.terrain.setType(fwd, TileType::Portal);
            grid.portals[fwd] = { newID, roomEntry };

            audioEvents_.push_back(AudioEvent::PortalCreate);

            // Return portal in new room
            room.terrain.setType(roomEntry, TileType::Portal);
            room.portals[roomEntry] = { activeGridID_, returnDst };
        }
    }
}

// ─── Goblin wander ────────────────────────────────────────────────────────────

void Game::tickGoblinWander(Grid& grid) {
    static const TilePos wanderDirs[] = {{1,0},{-1,0},{0,1},{0,-1}};

    for (EntityID eid : grid.entities) {
        Entity* ent = registry_.get(eid);
        if (!ent || ent->type != EntityType::Goblin || !ent->isIdle()) continue;
        if (std::rand() % 80 != 0) continue;

        TilePos delta   = wanderDirs[std::rand() % 4];
        TilePos newDest = ent->pos + delta;
        if (grid.isBounded()) {
            newDest.x = std::clamp(newDest.x, 0, grid.width  - 1);
            newDest.y = std::clamp(newDest.y, 0, grid.height - 1);
        }

        if (!resolveDestHeight(newDest, ent->pos, grid)) continue;

        std::vector<MoveIntention> intentions = {{
            eid, ent->pos, newDest, ent->type, ent->size
        }};
        auto allowed = resolveMoves(intentions, grid.spatial, registry_);
        if (allowed.count(eid)) {
            ent->destination = newDest;
            ent->facing      = toDirection(delta);
        }
    }
}

// ─── Routine VM ──────────────────────────────────────────────────────────────

void Game::tickVM(Grid& grid) {
    std::vector<EntityID> toRemove;

    for (auto& [id, state] : agentStates_) {
        if (!grid.hasEntity(id)) continue;
        Entity* ent = registry_.get(id);
        if (!ent || !ent->isIdle()) continue;

        // Build stimulus vector for conditional jumps.
        uint8_t stimuli[8] = {};
        {
            TileType tileHere = grid.terrain.typeAt(ent->pos);
            if (tileHere == TileType::Fire)
                stimuli[static_cast<int>(Condition::Fire)] = 1;
            if (tileHere == TileType::Puddle || tileHere == TileType::Water)
                stimuli[static_cast<int>(Condition::Wet)] = 1;

            TilePos ahead = ent->pos + dirToDelta(ent->facing);
            auto candidates = grid.spatial.query(boundsAt(ahead, {1.0f, 1.0f}), ahead.z);
            if (!candidates.empty())
                stimuli[static_cast<int>(Condition::EntityAhead)] = 1;

            if (grid.isBounded() &&
                (ent->pos.x == 0 || ent->pos.x == grid.width  - 1 ||
                 ent->pos.y == 0 || ent->pos.y == grid.height - 1))
                stimuli[static_cast<int>(Condition::AtEdge)] = 1;
        }

        VMResult res = vm_.step(state, agentRecordings_[id], ent->facing, stimuli);

        if (res.halt) {
            if (ent->type == EntityType::Poop) {
                // Poop despawns when its routine ends
                grid.remove(id, *ent);
                registry_.destroy(id);
            }
            // Golems and other routine agents stop executing but stay alive
            toRemove.push_back(id);
        } else if (res.wantMove) {
            TilePos newDest = ent->pos + res.moveDelta;

            if (resolveDestHeight(newDest, ent->pos, grid)) {
                std::vector<MoveIntention> intentions = {{
                    id, ent->pos, newDest, ent->type, ent->size
                }};
                auto allowed = resolveMoves(intentions, grid.spatial, registry_);
                if (allowed.count(id)) {
                    ent->destination = newDest;
                    ent->facing      = toDirection(res.moveDelta);
                    audioEvents_.push_back(AudioEvent::AgentStep);
                }
            }
        } else if (res.wantDig) {
            TilePos target = ent->pos + dirToDelta(ent->facing);
            grid.terrain.dig(target);
            audioEvents_.push_back(AudioEvent::Dig);
            visualEvents_.push_back({ VisualEventType::Dig,
                                      toVec(target), static_cast<float>(target.z) });
        } else if (res.wantPlant) {
            TilePos target = ent->pos + dirToDelta(ent->facing);
            if (grid.terrain.typeAt(target) == TileType::BareEarth) {
                EntityID mid = registry_.spawn(EntityType::Mushroom, target);
                grid.add(mid, *registry_.get(mid));
                grid.terrain.restore(target);
                audioEvents_.push_back(AudioEvent::Plant);
            }
        }
    }

    for (EntityID id : toRemove) {
        agentStates_.erase(id);
        agentRecordings_.erase(id);
    }
}

// ─── Movement ────────────────────────────────────────────────────────────────

void Game::tickMovement(Grid& grid) {
    std::vector<EntityID> snapshot = grid.entities;

    for (EntityID eid : snapshot) {
        Entity* ent = registry_.get(eid);
        if (!ent) continue;
        TilePos oldPos  = ent->pos;
        bool    arrived = stepMovement(*ent);
        if (arrived) {
            grid.spatial.move(eid, oldPos, ent->pos, ent->size);

            // Water slows movement: half speed while standing on a Water tile.
            if (ent->speed > 0.0f) {
                TileType tileHere = grid.terrain.typeAt(ent->pos);
                float    baseSpeed = defaultConfig(ent->type).speed;
                ent->speed = (tileHere == TileType::Water) ? baseSpeed * 0.5f : baseSpeed;
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
            auto pit = grid.portals.find(ent->pos);
            if (pit != grid.portals.end())
                pendingTransfers_.push_back({ eid, grid.id,
                                              pit->second.targetGrid, pit->second.targetPos });
        }
    }
}

// ─── Fire simulation ─────────────────────────────────────────────────────────
//
// 50 Hz timestep: 1s = 50 ticks, 3s = 150 ticks, 5s = 250 ticks, 10s = 500 ticks.
// Grass adjacent to fire catches after 50 ticks and burns for 150 ticks → BareEarth.
// TreeStump/Log adjacent to fire ignites after 250 ticks and despawns 500 ticks later.

void tickFire(Grid& grid, EntityRegistry& registry, Tick currentTick) {
    static const TilePos kDirs4[] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0}};

    // 1. Expire fire tiles whose timer has elapsed → BareEarth.
    //    Also extinguish any Fire tile adjacent to a Water tile.
    {
        std::vector<TilePos> done;
        for (const auto& [pos, expiry] : grid.fireTileExpiry) {
            if (currentTick >= expiry) { done.push_back(pos); continue; }
            for (const auto& d : kDirs4) {
                if (grid.terrain.typeAt(pos + d) == TileType::Water)
                    { done.push_back(pos); break; }
            }
        }
        for (const TilePos& pos : done) {
            grid.terrain.setType(pos, TileType::BareEarth);
            grid.fireTileExpiry.erase(pos);
            grid.tileFireExp.erase(pos);
        }
    }

    // 2. Despawn burning entities whose timer has elapsed.
    {
        std::vector<EntityID> done;
        for (const auto& [eid, burnEnd] : grid.entityBurnEnd)
            if (currentTick >= burnEnd) done.push_back(eid);
        for (EntityID eid : done) {
            Entity* e = registry.get(eid);
            if (e) grid.remove(eid, *e);
            registry.destroy(eid);
            grid.entityBurnEnd.erase(eid);
            grid.entityFireExp.erase(eid);
        }
    }

    // 3. Build heated set: all tiles adjacent to a Fire tile or Campfire entity.
    std::unordered_set<TilePos, TilePosHash> heated;
    for (const auto& [pos, _] : grid.fireTileExpiry)
        for (const auto& d : kDirs4)
            heated.insert(pos + d);
    for (EntityID eid : grid.entities) {
        const Entity* e = registry.get(eid);
        if (!e || e->type != EntityType::Campfire) continue;
        for (const auto& d : kDirs4)
            heated.insert(e->pos + d);
    }

    // 4. Decay (remove) exposure for tiles/entities no longer in heat.
    {
        std::vector<TilePos> cold;
        for (const auto& [pos, _] : grid.tileFireExp)
            if (!heated.count(pos)) cold.push_back(pos);
        for (const TilePos& pos : cold) grid.tileFireExp.erase(pos);
    }
    {
        std::vector<EntityID> cold;
        for (const auto& [eid, _] : grid.entityFireExp) {
            const Entity* e = registry.get(eid);
            if (!e || !heated.count(e->pos)) cold.push_back(eid);
        }
        for (EntityID eid : cold) grid.entityFireExp.erase(eid);
    }

    // 5. Heat Grass tiles: accumulate exposure; ignite at 50 ticks.
    for (const TilePos& pos : heated) {
        if (grid.terrain.typeAt(pos) != TileType::Grass) continue;
        if (++grid.tileFireExp[pos] >= 50) {
            grid.terrain.setType(pos, TileType::Fire);
            grid.fireTileExpiry[pos] = currentTick + 150;
            grid.tileFireExp.erase(pos);
        }
    }

    // 6. Heat TreeStump/Log entities: accumulate exposure; start burning at 250 ticks.
    for (EntityID eid : grid.entities) {
        if (grid.entityBurnEnd.count(eid)) continue;   // already burning
        Entity* e = registry.get(eid);
        if (!e) continue;
        if (e->type != EntityType::TreeStump && e->type != EntityType::Log) continue;
        if (!heated.count(e->pos)) continue;
        if (++grid.entityFireExp[eid] >= 250) {
            grid.entityBurnEnd[eid] = currentTick + 500;
            grid.entityFireExp.erase(eid);
            e->burning = true;
        }
    }
}

// ─── Voltage simulation ───────────────────────────────────────────────────────
//
// Battery entities emit 5V. BFS propagates through adjacent Puddle tiles,
// decrementing by 1 per hop. Lightbulb entities lit if their tile has ≥1V.

void tickVoltage(Grid& grid, EntityRegistry& registry) {
    grid.voltage.clear();

    static const TilePos kDirs4[] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0}};

    std::queue<std::pair<TilePos, int>> q;

    // Seed from Battery entities: adjacent puddle tiles get 4V (5-1).
    for (EntityID eid : grid.entities) {
        const Entity* e = registry.get(eid);
        if (!e || e->type != EntityType::Battery) continue;
        for (const auto& d : kDirs4) {
            TilePos adj = e->pos + d;
            if (grid.terrain.typeAt(adj) != TileType::Puddle) continue;
            if (!grid.voltage.count(adj) || grid.voltage[adj] < 4) {
                grid.voltage[adj] = 4;
                q.push({adj, 4});
            }
        }
    }

    // BFS through puddle network, voltage decrements by 1 per hop.
    while (!q.empty()) {
        auto [pos, v] = q.front();
        q.pop();
        if (v <= 1) continue;
        for (const auto& d : kDirs4) {
            TilePos adj = pos + d;
            if (grid.terrain.typeAt(adj) != TileType::Puddle) continue;
            if (!grid.voltage.count(adj) || grid.voltage[adj] < v - 1) {
                grid.voltage[adj] = v - 1;
                q.push({adj, v - 1});
            }
        }
    }

    // Update Lightbulb lit state and electrified flag for all other entities.
    for (EntityID eid : grid.entities) {
        Entity* e = registry.get(eid);
        if (!e) continue;
        auto it = grid.voltage.find(e->pos);
        bool charged = (it != grid.voltage.end() && it->second >= 1);
        if (e->type == EntityType::Lightbulb)
            e->lit = charged;
        else
            e->electrified = charged;
    }
}

// ─── Water simulation ─────────────────────────────────────────────────────────
//
// Each tick, every Water tile tries to expand to adjacent non-water tiles that are
// at the same height or lower (and no more than 1 level lower — no waterfall leaps).
// New tiles are batched so water expands by exactly one step per tick.

void tickWater(Grid& grid) {
    static const TilePos kDirs4[] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0}};

    // Collect all current Water tiles.
    std::vector<TilePos> waterTiles;
    for (const auto& [pos, type] : grid.terrain.overrides())
        if (type == TileType::Water) waterTiles.push_back(pos);

    // Expand to eligible adjacent tiles.
    std::vector<TilePos> toFlood;
    for (const TilePos& pos : waterTiles) {
        int srcLevel = grid.terrain.levelAt(pos);
        for (const auto& d : kDirs4) {
            TilePos adj = pos + d;
            // Don't overwrite portals, fire, or other significant tile types.
            TileType adjType = grid.terrain.typeAt(adj);
            if (adjType == TileType::Water || adjType == TileType::Fire ||
                adjType == TileType::Portal) continue;
            int dstLevel = grid.terrain.levelAt(adj);
            // Water flows downhill or level; won't leap off cliffs.
            if (dstLevel > srcLevel) continue;
            if (srcLevel - dstLevel > 1) continue;
            toFlood.push_back(adj);
        }
    }

    for (const TilePos& pos : toFlood)
        grid.terrain.setType(pos, TileType::Water);
}

// ─── Persistence ─────────────────────────────────────────────────────────────

namespace {
    template<typename T>
    void wr(std::ostream& f, T v) { f.write(reinterpret_cast<const char*>(&v), sizeof(T)); }

    template<typename T>
    T rd(std::istream& f) { T v{}; f.read(reinterpret_cast<char*>(&v), sizeof(T)); return v; }

    void wrStr(std::ostream& f, const std::string& s) {
        auto len = static_cast<uint8_t>(std::min(s.size(), size_t(255)));
        wr<uint8_t>(f, len);
        f.write(s.data(), len);
    }
    std::string rdStr(std::istream& f) {
        uint8_t len = rd<uint8_t>(f);
        std::string s(len, '\0');
        f.read(s.data(), len);
        return s;
    }

    // Write one grid's terrain + portals + non-player, non-Poop entities.
    void wrGrid(std::ostream& f, const Grid& grid,
                EntityID playerID, const EntityRegistry& reg) {
        wr<uint32_t>(f, grid.id);
        wr<int32_t>(f,  grid.width);
        wr<int32_t>(f,  grid.height);
        wr<uint8_t>(f,  grid.paused ? 1 : 0);

        // Portals
        wr<uint32_t>(f, static_cast<uint32_t>(grid.portals.size()));
        for (const auto& [pos, portal] : grid.portals) {
            wr<int32_t>(f, pos.x);
            wr<int32_t>(f, pos.y);
            wr<int32_t>(f, pos.z);
            wr<uint32_t>(f, portal.targetGrid);
            wr<int32_t>(f, portal.targetPos.x);
            wr<int32_t>(f, portal.targetPos.y);
            wr<int32_t>(f, portal.targetPos.z);
        }

        // Terrain overrides
        const auto& ovr = grid.terrain.overrides();
        wr<uint32_t>(f, static_cast<uint32_t>(ovr.size()));
        for (const auto& [pos, type] : ovr) {
            wr<int32_t>(f, pos.x);
            wr<int32_t>(f, pos.y);
            wr<int32_t>(f, pos.z);
            wr<uint8_t>(f, static_cast<uint8_t>(type));
        }


        // Entities (skip player and Poop)
        std::vector<EntityID> toSave;
        for (EntityID eid : grid.entities)
            if (eid != playerID) {
                const Entity* e = reg.get(eid);
                if (e && e->type != EntityType::Poop) toSave.push_back(eid);
            }
        wr<uint32_t>(f, static_cast<uint32_t>(toSave.size()));
        for (EntityID eid : toSave) {
            const Entity* e = reg.get(eid);
            wr<uint8_t>(f, static_cast<uint8_t>(e->type));
            wr<int32_t>(f, e->pos.x);
            wr<int32_t>(f, e->pos.y);
            wr<int32_t>(f, e->pos.z);
            wr<uint8_t>(f, static_cast<uint8_t>(e->facing));
            wr<int32_t>(f, e->mana);
            wr<int32_t>(f, e->health);
        }
    }
}

void Game::save(const std::string& path) const {
    std::ofstream f(path, std::ios::binary);
    if (!f) return;

    f.write("GRID", 4);
    wr<uint8_t>(f, 9);   // version 9: Water tile type, mana floor

    // Player
    const Entity* player = registry_.get(playerID_);
    GridID  playerGrid = activeGridID_;
    TilePos ppos = player ? player->pos : TilePos{0, 0};
    // If in studio, save player back in world at the stored world position
    if (activeGridID_ == GRID_STUDIO) { playerGrid = GRID_WORLD; ppos = playerWorldPos_; }
    wr<uint32_t>(f, playerGrid);
    wr<int32_t>(f,  ppos.x);
    wr<int32_t>(f,  ppos.y);
    wr<int32_t>(f,  ppos.z);
    wr<uint8_t>(f,  player ? static_cast<uint8_t>(player->facing) : 0);
    wr<int32_t>(f,  player ? player->mana : 0);

    // Grids (all except studio)
    uint32_t gridCount = 0;
    for (const auto& [id, _] : grids_)
        if (id != GRID_STUDIO) ++gridCount;
    wr<uint32_t>(f, gridCount);
    wr<uint32_t>(f, nextGridID_);

    for (const auto& [id, grid] : grids_) {
        if (id == GRID_STUDIO) continue;
        wrGrid(f, grid, playerID_, registry_);
    }

    // Recordings
    wr<uint32_t>(f, static_cast<uint32_t>(recorder_.recordings.size()));
    for (const Recording& rec : recorder_.recordings) {
        wrStr(f, rec.name);
        wr<uint32_t>(f, static_cast<uint32_t>(rec.instructions.size()));
        for (const Instruction& ins : rec.instructions) {
            wr<uint8_t>(f, static_cast<uint8_t>(ins.op));
            wr<uint8_t>(f, static_cast<uint8_t>(ins.dir));
            wr<uint16_t>(f, ins.ticks);
            wr<uint16_t>(f, ins.addr);
            wr<uint8_t>(f, static_cast<uint8_t>(ins.cond));
            wr<uint8_t>(f, ins.threshold);
        }
    }
    wr<uint64_t>(f, static_cast<uint64_t>(selectedRecording_));
}

bool Game::load(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;

    char magic[4];
    f.read(magic, 4);
    if (std::strncmp(magic, "GRID", 4) != 0) return false;
    uint8_t version = rd<uint8_t>(f);
    if (version != 9) return false;

    // Clear all state
    for (auto& [id, grid] : grids_) {
        for (EntityID eid : std::vector<EntityID>(grid.entities)) {
            Entity* e = registry_.get(eid);
            if (e) grid.remove(eid, *e);
            registry_.destroy(eid);
        }
        grid.terrain.clearOverrides();
        grid.portals.clear();
        grid.paused = false;
    }
    // Remove dynamic grids (keep world + studio)
    for (auto it = grids_.begin(); it != grids_.end(); ) {
        if (it->first != GRID_WORLD && it->first != GRID_STUDIO)
            it = grids_.erase(it);
        else ++it;
    }
    agentStates_.clear();
    agentRecordings_.clear();
    pendingTransfers_.clear();

    // Player
    GridID    playerGrid = rd<uint32_t>(f);
    TilePos   ppos       = { rd<int32_t>(f), rd<int32_t>(f), rd<int32_t>(f) };
    Direction pfacing    = static_cast<Direction>(rd<uint8_t>(f));
    int       pmana    = rd<int32_t>(f);

    // Grids
    uint32_t gridCount = rd<uint32_t>(f);
    nextGridID_        = rd<uint32_t>(f);

    for (uint32_t g = 0; g < gridCount; ++g) {
        GridID gid    = rd<uint32_t>(f);
        int    width  = rd<int32_t>(f);
        int    height = rd<int32_t>(f);
        bool   paused = rd<uint8_t>(f) != 0;

        // Ensure grid exists
        if (!grids_.count(gid)) {
            grids_.try_emplace(gid, gid, width, height);
            subscribeEvents(grids_.at(gid));
        }
        Grid& grid   = grids_.at(gid);
        grid.width   = width;
        grid.height  = height;
        grid.paused  = paused;

        // Portals
        uint32_t portalCount = rd<uint32_t>(f);
        for (uint32_t i = 0; i < portalCount; ++i) {
            TilePos  pos       = { rd<int32_t>(f), rd<int32_t>(f), rd<int32_t>(f) };
            GridID   tGrid     = rd<uint32_t>(f);
            TilePos  tPos      = { rd<int32_t>(f), rd<int32_t>(f), rd<int32_t>(f) };
            grid.portals[pos]  = { tGrid, tPos };
        }

        // Terrain overrides
        uint32_t ovrCount = rd<uint32_t>(f);
        for (uint32_t i = 0; i < ovrCount; ++i) {
            TilePos  pos  = { rd<int32_t>(f), rd<int32_t>(f), rd<int32_t>(f) };
            TileType type = static_cast<TileType>(rd<uint8_t>(f));
            // Fire tiles without expiry entries would burn forever; clear on load.
            if (type == TileType::Fire) type = TileType::BareEarth;
            grid.terrain.setType(pos, type);
        }


        // Entities
        uint32_t entCount = rd<uint32_t>(f);
        for (uint32_t i = 0; i < entCount; ++i) {
            EntityType et     = static_cast<EntityType>(rd<uint8_t>(f));
            TilePos    pos    = { rd<int32_t>(f), rd<int32_t>(f), rd<int32_t>(f) };
            Direction  facing = static_cast<Direction>(rd<uint8_t>(f));
            int        mana   = rd<int32_t>(f);
            int        health = rd<int32_t>(f);

            EntityID eid = registry_.spawn(et, pos);
            Entity*  e   = registry_.get(eid);
            e->facing = facing; e->mana = mana; e->health = health;
            grid.add(eid, *e);
        }
    }

    // Spawn player in saved grid
    if (!grids_.count(playerGrid)) playerGrid = GRID_WORLD;
    playerID_ = registry_.spawn(EntityType::Player, ppos);
    Entity* player = registry_.get(playerID_);
    player->facing = pfacing;
    player->mana   = pmana;
    grids_.at(playerGrid).add(playerID_, *player);
    activeGridID_   = playerGrid;
    playerWorldPos_ = (playerGrid == GRID_WORLD) ? ppos : TilePos{0, 0};

    // Recordings
    recorder_.recordings.clear();
    uint32_t recCount = rd<uint32_t>(f);
    for (uint32_t i = 0; i < recCount; ++i) {
        Recording rec;
        rec.name = rdStr(f);
        uint32_t instrCount = rd<uint32_t>(f);
        for (uint32_t j = 0; j < instrCount; ++j) {
            Instruction ins;
            ins.op        = static_cast<OpCode>(rd<uint8_t>(f));
            ins.dir       = static_cast<RelDir>(rd<uint8_t>(f));
            ins.ticks     = rd<uint16_t>(f);
            ins.addr      = rd<uint16_t>(f);
            ins.cond      = static_cast<Condition>(rd<uint8_t>(f));
            ins.threshold = rd<uint8_t>(f);
            rec.instructions.push_back(ins);
        }
        recorder_.recordings.push_back(std::move(rec));
    }
    selectedRecording_ = static_cast<size_t>(rd<uint64_t>(f));
    if (selectedRecording_ >= recorder_.recordings.size())
        selectedRecording_ = 0;

    return true;
}
