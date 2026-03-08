#include "game.hpp"
#include "routine.hpp"
#include "terrain.hpp"
#include <algorithm>
#include <cstring>
#include <fstream>

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

    // ── Studio: medium tiles for summon testing ───────────────────────────────
    // Player enters studio at {0,0} facing S; place mediums one step south.
    Grid& studio = grids_.at(GRID_STUDIO);
    studio.terrain.setType({ 0,  1}, TileType::Mud);
    studio.terrain.setType({ 1,  1}, TileType::Stone);
    studio.terrain.setType({-1,  1}, TileType::Clay);

    // ── Phase 17 demo: water pool south-east of spawn ────────────────────────
    const TilePos waterDemo[] = {{20, 20}, {21, 20}, {20, 21}};
    for (const TilePos& p : waterDemo) {
        int lz = world.terrain.levelAt(p);
        TilePos wp = {p.x, p.y, lz};
        EntityID weid = registry_.spawn(EntityType::Water, wp);
        world.add(weid, *registry_.get(weid));
        fluidComponents_.add(weid, {1.0f, 0.f, 0.f});
    }
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
        tickFluid(grid, fluidComponents_, registry_);
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

void Game::deleteRecording(size_t index) {
    if (index >= recorder_.recordings.size()) return;
    recorder_.recordings.erase(recorder_.recordings.begin() + (ptrdiff_t)index);
    // Keep selectedRecording_ valid.
    if (recorder_.recordings.empty())
        selectedRecording_ = 0;
    else if (selectedRecording_ >= recorder_.recordings.size())
        selectedRecording_ = recorder_.recordings.size() - 1;
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

// ─── Mouse interaction (Phase 16) ─────────────────────────────────────────────

const Entity* Game::entityAtTile(TilePos tile) const {
    const Grid& g = activeGrid();
    for (EntityID eid : g.entities) {
        const Entity* e = registry_.get(eid);
        if (e && e->pos == tile) return e;
    }
    return nullptr;
}

std::vector<FluidOverlay> Game::fluidOverlay() const {
    std::vector<FluidOverlay> out;
    const Grid& g = activeGrid();
    for (EntityID eid : g.entities) {
        const Entity* e = registry_.get(eid);
        if (!e || e->type != EntityType::Water) continue;
        const FluidComponent* fc = fluidComponents_.get(eid);
        if (fc && fc->h > 0.f) out.push_back({e->pos, fc->h});
    }
    return out;
}

void Game::queueClickMove(TilePos target) {
    const Entity* p = registry_.get(playerID_);
    if (!p) return;
    int dx = target.x - p->pos.x;
    int dy = target.y - p->pos.y;
    if (dx == 0 && dy == 0) return;
    if (dx > 1) dx = 1; else if (dx < -1) dx = -1;
    if (dy > 1) dy = 1; else if (dy < -1) dy = -1;
    pendingClickDelta_ = {dx, dy, 0};
    hasPendingClick_   = true;
}

// ─── Instruction editing (Phase 15) ──────────────────────────────────────────

// Adjust JUMP/CALL addresses after an instruction is removed at index 'deleted'.
static void fixAddrsDelete(std::vector<Instruction>& instrs, size_t deleted) {
    for (auto& instr : instrs) {
        bool isJump = (instr.op == OpCode::JUMP || instr.op == OpCode::JUMP_IF ||
                       instr.op == OpCode::JUMP_IF_NOT || instr.op == OpCode::CALL);
        if (!isJump) continue;
        if (instr.addr > (uint16_t)deleted)
            instr.addr--;
        else if (instr.addr == (uint16_t)deleted)
            instr.addr = 0;
    }
}

// Adjust JUMP/CALL addresses after an instruction is inserted at 'insertPos'.
static void fixAddrsInsert(std::vector<Instruction>& instrs, size_t insertPos) {
    for (auto& instr : instrs) {
        bool isJump = (instr.op == OpCode::JUMP || instr.op == OpCode::JUMP_IF ||
                       instr.op == OpCode::JUMP_IF_NOT || instr.op == OpCode::CALL);
        if (isJump && instr.addr >= (uint16_t)insertPos)
            instr.addr++;
    }
}

void Game::deleteInstruction(size_t recIdx, size_t instrIdx) {
    if (recIdx >= recorder_.recordings.size()) return;
    Recording& rec = recorder_.recordings[recIdx];
    if (instrIdx >= rec.instructions.size()) return;
    fixAddrsDelete(rec.instructions, instrIdx);
    rec.instructions.erase(rec.instructions.begin() + (ptrdiff_t)instrIdx);
}

void Game::insertWait(size_t recIdx, size_t pos, uint16_t ticks) {
    if (recIdx >= recorder_.recordings.size()) return;
    Recording& rec = recorder_.recordings[recIdx];
    size_t insertAt = std::min(pos, rec.instructions.size());
    Instruction instr;
    instr.op    = OpCode::WAIT;
    instr.ticks = (ticks > 0) ? ticks : 1;
    fixAddrsInsert(rec.instructions, insertAt);
    rec.instructions.insert(rec.instructions.begin() + (ptrdiff_t)insertAt, instr);
}

void Game::insertMoveRel(size_t recIdx, size_t pos, RelDir dir) {
    if (recIdx >= recorder_.recordings.size()) return;
    Recording& rec = recorder_.recordings[recIdx];
    size_t insertAt = std::min(pos, rec.instructions.size());
    Instruction instr;
    instr.op  = OpCode::MOVE_REL;
    instr.dir = dir;
    fixAddrsInsert(rec.instructions, insertAt);
    rec.instructions.insert(rec.instructions.begin() + (ptrdiff_t)insertAt, instr);
}

void Game::reorderInstruction(size_t recIdx, size_t from, size_t to) {
    if (recIdx >= recorder_.recordings.size()) return;
    Recording& rec = recorder_.recordings[recIdx];
    size_t n = rec.instructions.size();
    if (from >= n || to >= n || from == to) return;
    Instruction moved = rec.instructions[from];
    rec.instructions.erase(rec.instructions.begin() + (ptrdiff_t)from);
    size_t insertAt = (to > from) ? to - 1 : to;
    rec.instructions.insert(rec.instructions.begin() + (ptrdiff_t)insertAt, moved);
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
    Grid&   grid     = activeGrid();
    Entity* player   = registry_.get(playerID_);
    bool    inStudio = (activeGridID_ == GRID_STUDIO);

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
    TilePos delta = {0, 0, 0};
    if (input.held(Action::MoveUp))    delta.y -= 1;
    if (input.held(Action::MoveDown))  delta.y += 1;
    if (input.held(Action::MoveLeft))  delta.x -= 1;
    if (input.held(Action::MoveRight)) delta.x += 1;
    // Click-move: fires when no keyboard direction is held.
    if (delta == TilePos{0, 0, 0} && hasPendingClick_)
        delta = pendingClickDelta_;
    hasPendingClick_ = false;

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
                recorder_.recordMove(delta, facingBefore, input.held(Action::Strafe));
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
        if (grid.terrain.typeAt(ahead) == TileType::BareEarth && (inStudio || player->mana >= 1)) {
            EntityID mid = registry_.spawn(EntityType::Mushroom, ahead);
            grid.add(mid, *registry_.get(mid));
            grid.terrain.restore(ahead);
            if (!inStudio) { player->mana--; if (player->mana < 1) player->mana = 1; }
            recorder_.recordPlant();
            audioEvents_.push_back(AudioEvent::Plant);
        }
    }


    // G: summon golem from medium tile ahead
    if (input.pressed(Action::Summon) && player) {
        // Always record the intent when recording, regardless of success.
        recorder_.recordSummon(selectedRecording_);

        TilePos ahead = player->pos + dirToDelta(player->facing);
        const GolemInfo* gi = golemForMedium(grid.terrain.typeAt(ahead));
        if (!gi) gi = &GOLEM_TABLE[0];  // default: Mud Golem
        // Deploy cost = recording complexity (moves); fallback to golem's intrinsic cost.
        bool hasRec = !recorder_.recordings.empty() && selectedRecording_ < recorder_.recordings.size();
        int deployCost = hasRec ? recorder_.recordings[selectedRecording_].manaCost() : gi->manaCost;
        if (inStudio || player->mana >= deployCost) {
            if (!inStudio) { player->mana -= deployCost; if (player->mana < 1) player->mana = 1; }

            EntityID gid = registry_.spawn(gi->type, ahead);
            Entity*  ge  = registry_.get(gid);
            ge->facing   = player->facing;
            grid.add(gid, *ge);

            // Assign the selected recording if one exists
            if (!recorder_.recordings.empty() &&
                selectedRecording_ < recorder_.recordings.size()) {
                agentSlots_[gid] = { AgentExecState{},
                                     recorder_.recordings[selectedRecording_] };
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

// ─── Routine VM ──────────────────────────────────────────────────────────────

void Game::tickVM(Grid& grid) {
    std::vector<EntityID>   toRemove;
    std::vector<std::pair<EntityID, AgentSlot>> toAdd;  // defer insertion to avoid iterator invalidation

    for (auto& [id, slot] : agentSlots_) {
        if (!grid.hasEntity(id)) continue;
        Entity* ent = registry_.get(id);
        if (!ent || !ent->isIdle()) continue;

        // Build stimulus vector for conditional jumps.
        uint8_t stimuli[8] = {};
        {
            TileType tileHere = grid.terrain.typeAt(ent->pos);
            if (tileHere == TileType::Fire)
                stimuli[static_cast<int>(Condition::Fire)] = 1;
            bool hasFluid = false;
            for (EntityID at : grid.spatial.at(ent->pos)) {
                const Entity* ae = registry_.get(at);
                if (ae && ae->type == EntityType::Water) { hasFluid = true; break; }
            }
            if (tileHere == TileType::Puddle || hasFluid)
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

        VMResult res = vm_.step(slot.state, slot.rec, ent->facing, stimuli);

        if (res.halt) {
            // All routine agents despawn when their script ends.
            grid.remove(id, *ent);
            registry_.destroy(id);
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
                    if (!res.isStrafe)
                        ent->facing = toDirection(res.moveDelta);
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
        } else if (res.wantSummon) {
            TilePos target = ent->pos + dirToDelta(ent->facing);
            const GolemInfo* gi = golemForMedium(grid.terrain.typeAt(target));
            if (!gi) gi = &GOLEM_TABLE[0];  // default: Mud Golem
            {
                EntityID gid = registry_.spawn(gi->type, target);
                Entity*  ge  = registry_.get(gid);
                ge->facing   = ent->facing;
                grid.add(gid, *ge);
                // Assign the recording encoded in the SUMMON instruction.
                Recording rec = (res.summonRecIdx < recorder_.recordings.size())
                    ? recorder_.recordings[res.summonRecIdx]
                    : slot.rec;
                toAdd.emplace_back(gid, AgentSlot{{}, std::move(rec)});
                audioEvents_.push_back(AudioEvent::Summon);
                visualEvents_.push_back({ VisualEventType::Summon,
                                          toVec(target), static_cast<float>(target.z) });
            }
        }
    }

    for (EntityID id : toRemove)
        agentSlots_.erase(id);
    for (auto& [gid, slot] : toAdd)
        agentSlots_[gid] = std::move(slot);
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
                EntityID playerID, const EntityRegistry& reg,
                const ComponentStore<FluidComponent>& fluids) {
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
            // Water entities carry extra FluidComponent data.
            if (e->type == EntityType::Water) {
                const FluidComponent* fc = fluids.get(eid);
                float h  = fc ? fc->h  : 0.f;
                float vx = fc ? fc->vx : 0.f;
                float vy = fc ? fc->vy : 0.f;
                wr<float>(f, h); wr<float>(f, vx); wr<float>(f, vy);
            }
        }
    }
}

void Game::save(const std::string& path) const {
    std::ofstream f(path, std::ios::binary);
    if (!f) return;

    f.write("GRID", 4);
    wr<uint8_t>(f, 10);   // version 10: fluid entities (FluidComponent), removed TileType::Water

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
        wrGrid(f, grid, playerID_, registry_, fluidComponents_);
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
    if (version != 10) return false;

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
    agentSlots_.clear();
    fluidComponents_.clear();
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
            if (et == EntityType::Water) {
                float h  = rd<float>(f);
                float vx = rd<float>(f);
                float vy = rd<float>(f);
                fluidComponents_.add(eid, {h, vx, vy});
            }
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

    // Re-apply static studio medium tiles (cleared by clearOverrides above).
    Grid& studio = grids_.at(GRID_STUDIO);
    if (!studio.terrain.overrides().count({ 0,  1})) studio.terrain.setType({ 0,  1}, TileType::Mud);
    if (!studio.terrain.overrides().count({ 1,  1})) studio.terrain.setType({ 1,  1}, TileType::Stone);
    if (!studio.terrain.overrides().count({-1,  1})) studio.terrain.setType({-1,  1}, TileType::Clay);

    return true;
}
