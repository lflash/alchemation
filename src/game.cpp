#include "game.hpp"
#include <algorithm>
#include <cstdlib>

// ─── transferEntity ───────────────────────────────────────────────────────────

void transferEntity(EntityID eid, Grid& from, Grid& to,
                    EntityRegistry& registry, TilePos dest) {
    Entity* e = registry.get(eid);
    if (!e) return;

    from.remove(eid, *e);

    e->pos         = dest;
    e->destination = dest;
    e->moveT       = 0.0f;

    to.add(eid, *e);
}

// ─── Construction ─────────────────────────────────────────────────────────────

Game::Game() {
    grids_.try_emplace(GRID_WORLD,  GRID_WORLD);
    grids_.try_emplace(GRID_STUDIO, GRID_STUDIO);

    playerID_ = registry_.spawn(EntityType::Player, {0, 0});
    activeGrid().add(playerID_, *registry_.get(playerID_));

    EntityID goblinID = registry_.spawn(EntityType::Goblin, {5, 5});
    activeGrid().add(goblinID, *registry_.get(goblinID));

    // Mushroom collection on player arrival.
    activeGrid().events.subscribe(EventType::Arrived, [this](const Event& ev) {
        if (ev.subject != playerID_) return;
        Entity* player = registry_.get(playerID_);
        if (!player) return;

        Grid& grid = activeGrid();
        for (EntityID cid : grid.spatial.at(player->pos)) {
            if (cid == playerID_) continue;
            Entity* cand = registry_.get(cid);
            if (!cand || cand->type != EntityType::Mushroom) continue;

            player->mana += 3;
            grid.remove(cid, *cand);
            registry_.destroy(cid);
            break;
        }
    });
}

// ─── Top-level tick ──────────────────────────────────────────────────────────

void Game::tick(const Input& input, Tick currentTick) {
    tickScheduler(currentTick);
    tickPlayerInput(input);
    tickGoblinWander();
    tickVM();
    tickMovement();
    activeGrid().events.flush();
}

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
        result.push_back({ i, rec.name, steps, i == selectedRecording_ });
    }
    return result;
}

void Game::renameRecording(size_t index, const std::string& name) {
    if (index < recorder_.recordings.size())
        recorder_.recordings[index].name = name;
}

// ─── Scheduler ───────────────────────────────────────────────────────────────

void Game::tickScheduler(Tick currentTick) {
    Grid& grid = activeGrid();
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

// ─── Player input ─────────────────────────────────────────────────────────────

void Game::tickPlayerInput(const Input& input) {
    Grid&   grid   = activeGrid();
    Entity* player = registry_.get(playerID_);

    // r: toggle recording
    if (input.pressed(Key::R)) {
        if (recorder_.isRecording()) {
            recorder_.stop();
            selectedRecording_ = recorder_.recordings.size() - 1;
        } else {
            recorder_.start();
        }
    }

    // q: cycle selected recording
    if (input.pressed(Key::Q) && !recorder_.recordings.empty())
        selectedRecording_ = (selectedRecording_ + 1) % recorder_.recordings.size();

    // e: deploy selected recording as Poop agent in front of player
    if (input.pressed(Key::E) && player &&
        !recorder_.recordings.empty() &&
        selectedRecording_ < recorder_.recordings.size()) {

        TilePos  spawnPos = player->pos + dirToDelta(player->facing);
        EntityID pid      = registry_.spawn(EntityType::Poop, spawnPos);
        Entity*  pe       = registry_.get(pid);
        pe->facing        = player->facing;
        grid.add(pid, *pe);
        agentStates_[pid]     = AgentExecState{};
        agentRecordings_[pid] = recorder_.recordings[selectedRecording_];
    }

    // Tab: toggle between world and studio
    if (input.pressed(Key::Tab) && player && player->isIdle()) {
        if (activeGridID_ == GRID_WORLD) {
            playerWorldPos_ = player->pos;
            transferEntity(playerID_, grids_.at(GRID_WORLD), grids_.at(GRID_STUDIO),
                           registry_, {0, 0});
            activeGridID_ = GRID_STUDIO;
        } else {
            transferEntity(playerID_, grids_.at(GRID_STUDIO), grids_.at(GRID_WORLD),
                           registry_, playerWorldPos_);
            activeGridID_ = GRID_WORLD;
        }
        gridJustSwitched_ = true;
        // Re-fetch player pointer — it's still valid but grid context changed.
        player = registry_.get(playerID_);
    }

    if (!player || !player->isIdle()) return;

    if (recorder_.isRecording()) recorder_.tick();

    // Movement
    TilePos delta = {0, 0};
    if (input.held(Key::W)) delta.y -= 1;
    if (input.held(Key::S)) delta.y += 1;
    if (input.held(Key::A)) delta.x -= 1;
    if (input.held(Key::D)) delta.x += 1;

    if (delta != TilePos{0, 0}) {
        TilePos   newDest      = player->pos + delta;
        Direction facingBefore = player->facing;
        if (!input.held(Key::Shift))
            player->facing = toDirection(delta);

        std::vector<MoveIntention> intentions = {{
            playerID_, player->pos, newDest, player->type, player->size
        }};
        auto allowed = resolveMoves(intentions, grid.spatial, registry_);

        if (allowed.count(playerID_)) {
            player->destination = newDest;
            if (recorder_.isRecording())
                recorder_.recordMove(delta, facingBefore);
        } else {
            // Bump combat: move blocked — find goblin at destination and push it.
            Bounds destBounds = boundsAt(newDest, player->size);
            for (EntityID cid : grid.spatial.query(destBounds)) {
                Entity* cand = registry_.get(cid);
                if (!cand || cand->type != EntityType::Goblin) continue;
                if (!overlaps(destBounds, boundsAt(cand->pos, cand->size))) continue;

                cand->health -= player->mana;
                if (cand->health <= 0) {
                    grid.remove(cid, *cand);
                    registry_.destroy(cid);
                } else {
                    TilePos pushBase = cand->isMoving() ? cand->destination : cand->pos;
                    TilePos pushDest = pushBase + delta;
                    if (cand->isMoving()) {
                        Bounds pushBounds = boundsAt(pushDest, cand->size);
                        bool   pushBlocked = false;
                        for (EntityID oid : grid.spatial.query(pushBounds)) {
                            if (oid == cid) continue;
                            const Entity* occ = registry_.get(oid);
                            if (!occ || !overlaps(pushBounds, boundsAt(occ->pos, occ->size))) continue;
                            if (resolveCollision(cand->type, occ->type) == CollisionResult::Block) {
                                pushBlocked = true;
                                break;
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
                        if (pushAllowed.count(cid))
                            cand->destination = pushDest;
                    }
                }
                break;
            }
        }
    }

    // Terrain interaction
    TilePos ahead = player->pos + dirToDelta(player->facing);
    if (input.pressed(Key::F))
        grid.terrain.dig(ahead);

    if (input.pressed(Key::C)) {
        if (grid.terrain.typeAt(ahead) == TileType::BareEarth && player->mana >= 1) {
            EntityID mid = registry_.spawn(EntityType::Mushroom, ahead);
            Entity*  m   = registry_.get(mid);
            grid.add(mid, *m);
            grid.terrain.restore(ahead);
            player->mana--;
        }
    }
}

// ─── Goblin wander ────────────────────────────────────────────────────────────

void Game::tickGoblinWander() {
    Grid& grid = activeGrid();
    static const TilePos wanderDirs[] = {{1,0},{-1,0},{0,1},{0,-1}};

    for (EntityID eid : grid.entities) {
        Entity* ent = registry_.get(eid);
        if (!ent || ent->type != EntityType::Goblin || !ent->isIdle()) continue;
        if (std::rand() % 80 != 0) continue;

        TilePos delta   = wanderDirs[std::rand() % 4];
        TilePos newDest = ent->pos + delta;
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

void Game::tickVM() {
    Grid& grid = activeGrid();
    std::vector<EntityID> toRemove;

    for (auto& [id, state] : agentStates_) {
        if (!grid.hasEntity(id)) continue;
        Entity* ent = registry_.get(id);
        if (!ent || !ent->isIdle()) continue;

        VMResult res = vm_.step(state, agentRecordings_[id], ent->facing);

        if (res.halt) {
            grid.remove(id, *ent);
            registry_.destroy(id);
            toRemove.push_back(id);
        } else if (res.wantMove) {
            TilePos newDest = ent->pos + res.moveDelta;
            std::vector<MoveIntention> intentions = {{
                id, ent->pos, newDest, ent->type, ent->size
            }};
            auto allowed = resolveMoves(intentions, grid.spatial, registry_);
            if (allowed.count(id)) {
                ent->destination = newDest;
                ent->facing      = toDirection(res.moveDelta);
            }
        }
    }

    for (EntityID id : toRemove) {
        agentStates_.erase(id);
        agentRecordings_.erase(id);
    }
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
              [](const Entity* a, const Entity* b) { return a->layer < b->layer; });
    return result;
}

// ─── Movement ────────────────────────────────────────────────────────────────

void Game::tickMovement() {
    Grid& grid = activeGrid();
    // Snapshot: movement cannot add/remove entities, but events can (flushed later).
    std::vector<EntityID> snapshot = grid.entities;

    for (EntityID eid : snapshot) {
        Entity* ent = registry_.get(eid);
        if (!ent) continue;
        TilePos oldPos  = ent->pos;
        bool    arrived = stepMovement(*ent);
        if (arrived) {
            grid.spatial.move(eid, oldPos, ent->pos, ent->size);
            grid.events.emit({ EventType::Arrived, eid });
        }
    }
}
