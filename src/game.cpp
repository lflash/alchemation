#include "game.hpp"
#include "routine.hpp"
#include "terrain.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <random>
#include <sstream>

// (Summon helpers replaced by alchemyReact in alchemy.cpp)

static const char* entityTypeName(EntityType t);  // defined in gameStateText section

// ─── transferEntity ───────────────────────────────────────────────────────────

void transferEntity(EntityID eid, Field& from, Field& to,
                    EntityRegistry& registry, TilePos dest) {
    Entity* e = registry.get(eid);
    if (!e) return;
    from.remove(eid, *e);
    e->pos = e->destination = dest;
    e->moveProgress = 0.0f;
    to.add(eid, *e);
}

// ─── Event subscriptions ─────────────────────────────────────────────────────
//
// Called for every field (world, studio, and each dynamic room). Subscribes the
// mushroom-collection handler. Portal detection happens in tickMovement instead
// of here to avoid modifying fields from inside an event callback.

void Game::subscribeEvents(Field& field) {
    field.events.subscribe(EventType::Arrived, [this](const Event& ev) {
        Entity* ent = registry_.get(ev.subject);
        if (!ent) return;
        bool isPlayer = (ev.subject == playerID_);
        bool isGol    = isGolem(ent->type);
        if (!isPlayer && !isGol) return;

        // Find the field the entity is in.
        Field* gp = nullptr;
        for (auto& [id, f] : fields_)
            if (f.hasEntity(ev.subject)) { gp = &f; break; }
        if (!gp) return;
        Field& g = *gp;

        for (EntityID cid : std::vector<EntityID>(g.spatial.at(ent->pos))) {
            if (cid == ev.subject) continue;
            Entity* cand = registry_.get(cid);
            if (!cand) continue;
            if (cand->type == EntityType::Mushroom ||
                (isPlayer && cand->type == EntityType::Chest)) {
                ent->mana += cand->mana;
                if (isPlayer) {
                    visualEvents_.push_back({ VisualEventType::CollectMushroom,
                                              toVec(cand->pos), static_cast<float>(cand->pos.z) });
                    audioEvents_.push_back(AudioEvent::CollectMushroom);
                }
                g.remove(cid, *cand);
                registry_.destroy(cid);
                break;
            }
        }
    });
}

// ─── Construction ─────────────────────────────────────────────────────────────

Game::Game() : worldRng_(std::random_device{}()) {
    fields_.try_emplace(FIELD_WORLD,  FIELD_WORLD);
    fields_.try_emplace(FIELD_STUDIO, FIELD_STUDIO);

    subscribeEvents(fields_.at(FIELD_WORLD));
    subscribeEvents(fields_.at(FIELD_STUDIO));

    Field& world = fields_.at(FIELD_WORLD);

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

    // ── Phase 12 demo: medium entities + terrain objects ─────────────────────
    // A row of summoning mediums south of spawn so the player can test golems.
    spawnStatic(EntityType::Mud,    -3,  4);
    spawnStatic(EntityType::Stone,  -2,  4);
    spawnStatic(EntityType::Clay,   -1,  4);
    spawnStatic(EntityType::Bush,    0,  4);
    spawnStatic(EntityType::Wood,    1,  4);
    spawnStatic(EntityType::Iron,    2,  4);
    spawnStatic(EntityType::Copper,  3,  4);

    // A Tree, Rock and Chest near spawn (west side, off the north navigation corridor).
    spawnStatic(EntityType::Tree,  -5, -6);
    spawnStatic(EntityType::Rock,  -4, -6);
    spawnStatic(EntityType::Chest, -3, -6);

    // ── Voltage demo ──────────────────────────────────────────────────────────
    // Battery at {-4,0} powers a puddle chain running east.
    // Lightbulb at {-1,0} sits on the third puddle (2V) and will be lit.
    // Second lightbulb at {3,0} is on plain grass — unlit.
    spawnStatic(EntityType::Rock,      -5,  0);   // carriable stone beside battery
    spawnStatic(EntityType::Battery,   -4,  0);
    // Puddle chain — Puddle entities replace the old TileType::Puddle overrides.
    for (int x : {-3, -2, -1}) {
        EntityID peid = registry_.spawn(EntityType::Puddle, {x, 0, 0});
        world.add(peid, *registry_.get(peid));
    }
    spawnStatic(EntityType::Lightbulb, -1,  0);   // on puddle → lit
    spawnStatic(EntityType::Lightbulb,  3,  0);   // on grass  → unlit

    // ── Studio: medium entities for summon testing ────────────────────────────
    // Player enters studio at {0,0} facing S; place mediums one step south.
    Field& studio = fields_.at(FIELD_STUDIO);
    auto spawnStudio = [&](EntityType et, int x, int y) {
        TilePos p{x, y, 0};
        EntityID eid = registry_.spawn(et, p);
        studio.add(eid, *registry_.get(eid));
    };
    spawnStudio(EntityType::Mud,    0,  1);
    spawnStudio(EntityType::Stone,  1,  1);
    spawnStudio(EntityType::Clay,  -1,  1);

    // ── Phase 17 demo: water pool south-east of spawn ────────────────────────
    const TilePos waterDemo[] = {{20, 20}, {21, 20}, {20, 21}};
    for (const TilePos& p : waterDemo) {
        int lz = world.terrain.levelAt(p);
        TilePos wp = {p.x, p.y, lz};
        EntityID weid = registry_.spawn(EntityType::Water, wp);
        world.add(weid, *registry_.get(weid));
        fluidComponents_.add(weid, {1.0f, 0.f, 0.f});
    }

    // ── Phase 18: pre-mark manually-populated chunks as generated ────────────
    // Prevents world gen from spawning on top of the hand-placed demo content.
    // Covers chunks (-1,-1) through (1,1) — a 3×3 block around the origin.
    for (int cy = -1; cy <= 1; ++cy)
        for (int cx = -1; cx <= 1; ++cx)
            world.generatedChunks.insert({cx, cy, 0});
}

// ─── Warren management ───────────────────────────────────────────────────────

FieldID Game::createWarrenInterior(EntityID warrenEid, TilePos worldPos) {
    FieldID fid = nextFieldID_++;
    fields_.try_emplace(fid, fid, WARREN_W, WARREN_H);
    Field& interior = fields_.at(fid);
    subscribeEvents(interior);

    // Exit portal at centre of interior → world warren tile.
    TilePos centre = { WARREN_W / 2, WARREN_H / 2, 0 };
    {
        EntityID peid = registry_.spawn(EntityType::Portal, centre);
        interior.add(peid, *registry_.get(peid));
    }
    interior.portals[centre] = { FIELD_WORLD, worldPos };

    // Entry portal on the world side → interior (one step north of centre).
    TilePos entryInside = { WARREN_W / 2, WARREN_H / 2 - 1, 0 };
    Field& world = fields_.at(FIELD_WORLD);
    {
        EntityID peid = registry_.spawn(EntityType::Portal, worldPos);
        world.add(peid, *registry_.get(peid));
    }
    world.portals[worldPos] = { fid, entryInside };

    warrenData_[warrenEid] = { fid, worldPos };
    return fid;
}

void Game::onRabbitDied(EntityID rabbitEid) {
    auto it = rabbitSlots_.find(rabbitEid);
    if (it == rabbitSlots_.end()) return;

    EntityID warrenEid     = it->second.warrenEid;
    FieldID  warrenFieldID = it->second.warrenFieldID;
    rabbitSlots_.erase(it);

    // Check if any remaining rabbit still belongs to this warren.
    for (const auto& [rid, slot] : rabbitSlots_)
        if (slot.warrenEid == warrenEid) return;  // warren still inhabited

    // Last rabbit gone — destroy the warren entity and its interior field.
    auto wd = warrenData_.find(warrenEid);
    if (wd != warrenData_.end()) {
        // Remove all entities in the interior field from the registry.
        auto fit = fields_.find(warrenFieldID);
        if (fit != fields_.end()) {
            for (EntityID eid : fit->second.entities)
                registry_.destroy(eid);
            fields_.erase(fit);
        }
        warrenData_.erase(wd);
    }
    // Remove the warren entity itself from the world.
    Entity* warren = registry_.get(warrenEid);
    if (warren) {
        Field& world = fields_.at(FIELD_WORLD);
        world.remove(warrenEid, *warren);
        registry_.destroy(warrenEid);
    }
}

// ─── Top-level tick ──────────────────────────────────────────────────────────

void Game::tick(const Input& input, Tick currentTick) {
    applyPendingTransfer();

    // Capture before the loop: tickPlayerInput() may change activeFieldID_ mid-loop,
    // and we must not call it twice (once for the old field, once for the new one).
    FieldID activeAtStart = activeFieldID_;

    // All non-paused fields tick every frame.
    // Player input only applies to the active field.
    for (auto& [id, field] : fields_) {
        if (field.paused) continue;
        tickScheduler(field, currentTick);
        if (id == activeAtStart) tickPlayerInput(input);
        tickGoblinAI(field, currentTick);
        tickRabbitAI(field, currentTick);
        tickRabbitBreeding(field);
        tickResponseMovement(field, currentTick);
        tickVM(field);
        tickMovement(field);
        tickFire(field, registry_, currentTick);
        tickCooking(field, currentTick);
        tickVoltage(field, registry_);
        tickFluid(field, fluidComponents_, registry_);
        if (id == FIELD_WORLD) tickLongGrass(field, registry_, worldRng_);
        field.events.flush();
        // Lazy world generation: expand the world around the active player.
        if (!field.isBounded() && id == FIELD_WORLD) {
            const Entity* player = registry_.get(playerID_);
            if (player) maybeGenerateChunks(field, player->pos);
        }
    }
}

// ─── Pending portal transfer ─────────────────────────────────────────────────

void Game::applyPendingTransfer() {
    for (auto& t : pendingTransfers_) {
        if (fields_.count(t.fromField) && fields_.count(t.toField)) {
            transferEntity(t.eid, fields_.at(t.fromField), fields_.at(t.toField),
                           registry_, t.toPos);
            if (t.eid == playerID_) {
                activeFieldID_     = t.toField;
                fieldJustSwitched_ = true;
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

float Game::playerMoveProgress() const {
    const Entity* p = registry_.get(playerID_);
    return p ? p->moveProgress : 0.0f;
}

std::vector<RoutineInfo> Game::routineList() const {
    std::vector<RoutineInfo> result;
    for (size_t i = 0; i < recorder_.routines.size(); ++i) {
        const Routine& rec = recorder_.routines[i];
        int steps = 0;
        for (const auto& instr : rec.instructions)
            if (instr.op != OpCode::HALT) ++steps;
        result.push_back({ i, rec.name, steps, rec.manaCost(), i == selectedRoutine_ });
    }
    return result;
}

SummonPreview Game::playerSummonPreview() const {
    if (activeAction_ != PlayerAction::Summon) return {};
    const Entity* player = registry_.get(playerID_);
    if (!player || !player->isIdle()) return {};
    const Field& grid = activeField();
    TilePos ahead = player->pos + dirToDelta(player->facing);

    // Find medium entity (if any) at the facing tile.
    EntityType golemType = EntityType::MudGolem;
    for (EntityID cid : grid.spatial.at(ahead)) {
        const Entity* cand = registry_.get(cid);
        if (!cand) continue;
        auto result = alchemyReact(cand->type);
        if (result) { golemType = *result; break; }
    }

    bool hasRec = !recorder_.routines.empty() && selectedRoutine_ < recorder_.routines.size();
    int cost = hasRec ? recorder_.routines[selectedRoutine_].manaCost() : 0;
    const char* name = entityTypeName(golemType);
    return { true, golemType, name, cost, player->mana >= cost };
}

void Game::renameRoutine(size_t index, const std::string& name) {
    if (index < recorder_.routines.size())
        recorder_.routines[index].name = name;
}

void Game::deleteRoutine(size_t index) {
    if (index >= recorder_.routines.size()) return;
    recorder_.routines.erase(recorder_.routines.begin() + (ptrdiff_t)index);
    // Keep selectedRoutine_ valid.
    if (recorder_.routines.empty())
        selectedRoutine_ = 0;
    else if (selectedRoutine_ >= recorder_.routines.size())
        selectedRoutine_ = recorder_.routines.size() - 1;
}

// ─── Draw order ──────────────────────────────────────────────────────────────

std::vector<const Entity*> Game::drawOrder() const {
    const Field& field = activeField();
    std::vector<const Entity*> result;
    result.reserve(field.entities.size());
    for (EntityID eid : field.entities) {
        const Entity* e = registry_.get(eid);
        if (e) result.push_back(e);
    }
    std::sort(result.begin(), result.end(),
              [](const Entity* a, const Entity* b) {
                  if (a->pos.y != b->pos.y) return a->pos.y < b->pos.y;
                  if (a->pos.z != b->pos.z) return a->pos.z < b->pos.z;
                  return a->drawOrder < b->drawOrder;
              });
    return result;
}

// ─── Test helpers ─────────────────────────────────────────────────────────────

EntityID Game::injectEntity(EntityType type, int x, int y, int mana) {
    Field& field = fields_.at(FIELD_WORLD);
    int z = field.terrain.levelAt({x, y, 0});
    EntityID eid = registry_.spawn(type, {x, y, z});
    Entity* e = registry_.get(eid);
    if (!e) return INVALID_ENTITY;
    if (mana > 0) e->mana = mana;
    field.add(eid, *e);
    return eid;
}

// ─── Mouse interaction (Phase 16) ─────────────────────────────────────────────

const Entity* Game::entityAtTile(TilePos tile) const {
    const Field& g = activeField();
    for (EntityID eid : g.entities) {
        const Entity* e = registry_.get(eid);
        if (e && e->pos == tile) return e;
    }
    return nullptr;
}

std::vector<FluidOverlay> Game::fluidOverlay() const {
    std::vector<FluidOverlay> out;
    const Field& g = activeField();
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
    if (recIdx >= recorder_.routines.size()) return;
    Routine& rec = recorder_.routines[recIdx];
    if (instrIdx >= rec.instructions.size()) return;
    fixAddrsDelete(rec.instructions, instrIdx);
    rec.instructions.erase(rec.instructions.begin() + (ptrdiff_t)instrIdx);
}

void Game::insertWait(size_t recIdx, size_t pos, uint16_t ticks) {
    if (recIdx >= recorder_.routines.size()) return;
    Routine& rec = recorder_.routines[recIdx];
    size_t insertAt = std::min(pos, rec.instructions.size());
    Instruction instr;
    instr.op    = OpCode::WAIT;
    instr.ticks = (ticks > 0) ? ticks : 1;
    fixAddrsInsert(rec.instructions, insertAt);
    rec.instructions.insert(rec.instructions.begin() + (ptrdiff_t)insertAt, instr);
}

void Game::insertMoveRel(size_t recIdx, size_t pos, RelDir dir) {
    if (recIdx >= recorder_.routines.size()) return;
    Routine& rec = recorder_.routines[recIdx];
    size_t insertAt = std::min(pos, rec.instructions.size());
    Instruction instr;
    instr.op  = OpCode::MOVE_REL;
    instr.dir = dir;
    fixAddrsInsert(rec.instructions, insertAt);
    rec.instructions.insert(rec.instructions.begin() + (ptrdiff_t)insertAt, instr);
}

void Game::reorderInstruction(size_t recIdx, size_t from, size_t to) {
    if (recIdx >= recorder_.routines.size()) return;
    Routine& rec = recorder_.routines[recIdx];
    size_t n = rec.instructions.size();
    if (from >= n || to >= n || from == to) return;
    Instruction moved = rec.instructions[from];
    rec.instructions.erase(rec.instructions.begin() + (ptrdiff_t)from);
    size_t insertAt = (to > from) ? to - 1 : to;
    rec.instructions.insert(rec.instructions.begin() + (ptrdiff_t)insertAt, moved);
}

// ─── Scheduler ───────────────────────────────────────────────────────────────

void Game::tickScheduler(Field& grid, Tick currentTick) {
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

static bool resolveDestHeight(TilePos& dest, const TilePos& from, const Field& field) {
    if (!field.isBounded())
        dest.z = field.terrain.levelAt(dest);
    return std::abs(dest.z - from.z) <= 1;
}

// ─── Player input ─────────────────────────────────────────────────────────────

void Game::tickPlayerInput(const Input& input) {
    Field&  grid     = activeField();
    Entity* player   = registry_.get(playerID_);
    bool    inStudio = (activeFieldID_ == FIELD_STUDIO);

    // r: toggle recording
    if (input.pressed(Action::Record)) {
        if (recorder_.isRecording()) {
            recorder_.stop();
            selectedRoutine_ = recorder_.routines.size() - 1;
            audioEvents_.push_back(AudioEvent::RecordStop);
        } else {
            recorder_.start();
            audioEvents_.push_back(AudioEvent::RecordStart);
        }
    }

    // q: cycle selected routine
    if (input.pressed(Action::CycleRecording) && !recorder_.routines.empty())
        selectedRoutine_ = (selectedRoutine_ + 1) % recorder_.routines.size();

    // z: cycle active player action
    if (input.pressed(Action::CycleAction))
        activeAction_ = static_cast<PlayerAction>(
            (static_cast<int>(activeAction_) + 1) % PLAYER_ACTION_COUNT);


    // Tab: toggle between world and studio
    if (input.pressed(Action::SwitchGrid) && player && player->isIdle()) {
        if (activeFieldID_ == FIELD_WORLD) {
            playerWorldPos_ = player->pos;
            transferEntity(playerID_, fields_.at(FIELD_WORLD), fields_.at(FIELD_STUDIO),
                           registry_, {0, 0});
            activeFieldID_ = FIELD_STUDIO;
        } else if (activeFieldID_ == FIELD_STUDIO) {
            transferEntity(playerID_, fields_.at(FIELD_STUDIO), fields_.at(FIELD_WORLD),
                           registry_, playerWorldPos_);
            activeFieldID_ = FIELD_WORLD;
        }
        fieldJustSwitched_ = true;
        audioEvents_.push_back(AudioEvent::FieldSwitch);
        visualEvents_.push_back({ VisualEventType::FieldSwitch, {0,0}, 0 });
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

                    // For multi-tile entities, also verify extra tile destinations are clear.
                    bool extraBlocked = false;
                    if (cand->tileCount > 1) {
                        for (int i = 1; i < cand->tileCount && !extraBlocked; ++i) {
                            TilePos extraDest = { cand->extraTiles[i-1].x + delta.x,
                                                  cand->extraTiles[i-1].y + delta.y,
                                                  cand->extraTiles[i-1].z };
                            if (!grid.isBounded())
                                extraDest.z = grid.terrain.levelAt(extraDest);
                            for (EntityID oid : std::vector<EntityID>(grid.spatial.at(extraDest))) {
                                if (oid == cid) continue;
                                const Entity* occ = registry_.get(oid);
                                if (!occ) continue;
                                if (resolveCollision(cand->type, occ->type) == CollisionResult::Block)
                                    extraBlocked = true;
                            }
                        }
                    }

                    if (!extraBlocked) {
                        std::vector<MoveIntention> push = {{
                            cid, cand->pos, pushDest, cand->type, cand->size
                        }};
                        auto pushAllowed = resolveMoves(push, grid.spatial, registry_);
                        if (pushAllowed.count(cid)) cand->destination = pushDest;
                    }
                    break;
                }
            }
        }
        } // height check

    }

    // ── Terrain / action verbs ────────────────────────────────────────────────
    TilePos ahead = player->pos + dirToDelta(player->facing);
    if (!grid.isBounded()) ahead.z = grid.terrain.levelAt(ahead);

    // Action lambdas — shared between individual hotkeys and the E cycle system.
    auto doDig = [&] {
        // Spawn BareEarth entity only if none already present.
        bool alreadyDug = false;
        for (EntityID cid : grid.spatial.at(ahead)) {
            const Entity* ce = registry_.get(cid);
            if (ce && ce->type == EntityType::BareEarth) { alreadyDug = true; break; }
        }
        if (!alreadyDug) {
            EntityID beid = registry_.spawn(EntityType::BareEarth, ahead);
            grid.add(beid, *registry_.get(beid));
        }
        recorder_.recordDig();
        audioEvents_.push_back(AudioEvent::Dig);
        visualEvents_.push_back({ VisualEventType::Dig,
                                  toVec(ahead), static_cast<float>(ahead.z) });
    };

    auto doPlant = [&] {
        EntityID bareID = INVALID_ENTITY;
        for (EntityID cid : grid.spatial.at(ahead)) {
            const Entity* ce = registry_.get(cid);
            if (ce && ce->type == EntityType::BareEarth) { bareID = cid; break; }
        }
        if (bareID != INVALID_ENTITY && (inStudio || player->mana >= 1)) {
            EntityID mid = registry_.spawn(EntityType::Mushroom, ahead);
            grid.add(mid, *registry_.get(mid));
            Entity* be = registry_.get(bareID);
            if (be) { grid.remove(bareID, *be); registry_.destroy(bareID); }
            if (!inStudio) { player->mana--; if (player->mana < 1) player->mana = 1; }
            recorder_.recordPlant();
            audioEvents_.push_back(AudioEvent::Plant);
        }
    };

    auto doScythe = [&] {
        // Only scythe plain grass: no tile-state entities at the tile.
        bool hasTileState = false;
        for (EntityID cid : grid.spatial.at(ahead)) {
            const Entity* ce = registry_.get(cid);
            if (ce && (ce->type == EntityType::BareEarth || ce->type == EntityType::Fire ||
                       ce->type == EntityType::Puddle    || ce->type == EntityType::Straw ||
                       ce->type == EntityType::Portal)) { hasTileState = true; break; }
        }
        if (!hasTileState) {
            EntityID seid = registry_.spawn(EntityType::Straw, ahead);
            grid.add(seid, *registry_.get(seid));
            recorder_.recordScythe();
        }
    };

    auto doMine = [&] {
        for (EntityID cid : grid.spatial.at(ahead)) {
            Entity* cand = registry_.get(cid);
            if (!cand) continue;
            bool isOre = (cand->type == EntityType::IronOre  ||
                          cand->type == EntityType::CopperOre ||
                          cand->type == EntityType::CoalOre   ||
                          cand->type == EntityType::SulphurOre);
            if (isOre) { cand->capabilities |= Capability::Pushable; recorder_.recordMine(); break; }
        }
    };

    auto doSummon = [&] {
        recorder_.recordSummon(selectedRoutine_);

        // Find medium entity (if any) at the facing tile; default to MudGolem.
        EntityType golemType = EntityType::MudGolem;
        EntityID   mediumID  = INVALID_ENTITY;
        for (EntityID cid : grid.spatial.at(ahead)) {
            const Entity* cand = registry_.get(cid);
            if (!cand) continue;
            auto result = alchemyReact(cand->type);
            if (result) { golemType = *result; mediumID = cid; break; }
        }

        bool hasRec     = !recorder_.routines.empty() && selectedRoutine_ < recorder_.routines.size();
        int  deployCost = hasRec ? recorder_.routines[selectedRoutine_].manaCost() : 0;
        if (inStudio || player->mana >= deployCost) {
            if (!inStudio) { player->mana -= deployCost; if (player->mana < 1) player->mana = 1; }

            // Consume the medium entity (the Spark is implicit — spawned and
            // reacted in the same instant, so it never persists in the world).
            if (mediumID != INVALID_ENTITY) {
                Entity* me = registry_.get(mediumID);
                if (me) { grid.remove(mediumID, *me); registry_.destroy(mediumID); }
            }

            EntityID gid = registry_.spawn(golemType, ahead);
            Entity*  ge  = registry_.get(gid);
            ge->facing   = player->facing;
            grid.add(gid, *ge);
            if (hasRec)
                agentSlots_[gid] = { AgentExecState{}, recorder_.routines[selectedRoutine_] };
            audioEvents_.push_back(AudioEvent::Summon);
            visualEvents_.push_back({ VisualEventType::Summon,
                                      toVec(ahead), static_cast<float>(ahead.z) });
        }
    };

    auto doPortal = [&] {
        TilePos fwd = player->pos + dirToDelta(player->facing);
        if (!grid.portals.count(fwd)) {
            FieldID newID     = nextFieldID_++;
            TilePos roomEntry = { ROOM_W / 2, ROOM_H / 2 };
            fields_.try_emplace(newID, newID, ROOM_W, ROOM_H);
            Field& room = fields_.at(newID);
            subscribeEvents(room);
            {
                EntityID peid = registry_.spawn(EntityType::Portal, fwd);
                grid.add(peid, *registry_.get(peid));
            }
            grid.portals[fwd] = { newID, roomEntry };
            audioEvents_.push_back(AudioEvent::PortalCreate);
            {
                EntityID peid = registry_.spawn(EntityType::Portal, roomEntry);
                room.add(peid, *registry_.get(peid));
            }
            room.portals[roomEntry] = { activeFieldID_, fwd };
        }
    };

    // For pick-up and drop, compute the actual terrain height at the ahead tile
    // so we search the right z-level on the unbounded world grid.
    auto aheadAt = [&](int dz = 0) -> TilePos {
        TilePos p = ahead;
        p.z = grid.isBounded() ? 0 : grid.terrain.levelAt(ahead);
        p.z += dz;
        return p;
    };

    auto doPickUp = [&] {
        if (player->carrying != INVALID_ENTITY) return;  // already holding something
        // Search ±1 z-levels to handle height variation on the world grid.
        for (int dz : {0, 1, -1}) {
            TilePos checkPos = aheadAt(dz);
            for (EntityID cid : grid.spatial.at(checkPos)) {
                Entity* cand = registry_.get(cid);
                if (!cand || cid == playerID_) continue;
                if (!cand->hasCapability(Capability::Carriable)) continue;
                if (cand->mass > player->maxCarryMass) continue;   // too heavy
                // Remove from spatial at all occupied tiles.
                grid.spatial.remove(cid, cand->pos, cand->size);
                for (int i = 1; i < cand->tileCount; ++i)
                    grid.spatial.remove(cid, cand->extraTiles[i - 1], cand->size);
                cand->carriedBy = playerID_;
                player->carrying = cid;
                return;
            }
        }
    };

    auto doDrop = [&] {
        if (player->carrying == INVALID_ENTITY) return;
        Entity* carried = registry_.get(player->carrying);
        if (!carried) { player->carrying = INVALID_ENTITY; return; }
        TilePos dropPos = aheadAt();

        // Compute all tile positions the dropped entity will occupy.
        std::vector<TilePos> dropTiles;
        dropTiles.reserve(carried->tileCount);
        dropTiles.push_back(dropPos);
        if (carried->tileCount > 1) {
            TilePos d = dirToDelta(carried->facing);
            for (int i = 1; i < carried->tileCount; ++i) {
                TilePos extra = { dropPos.x + d.x * i,
                                  dropPos.y + d.y * i,
                                  dropPos.z };
                if (!grid.isBounded())
                    extra.z = grid.terrain.levelAt(extra);
                dropTiles.push_back(extra);
            }
        }

        // Check every drop tile for blocking occupants.
        bool blocked = false;
        for (const TilePos& tile : dropTiles) {
            Bounds dropBounds = boundsAt(tile, carried->size);
            for (EntityID oid : grid.spatial.at(tile)) {
                const Entity* occ = registry_.get(oid);
                if (!occ || oid == playerID_) continue;
                if (overlaps(dropBounds, boundsAt(occ->pos, occ->size))) {
                    if (resolveCollision(carried->type, occ->type) == CollisionResult::Block) {
                        blocked = true; break;
                    }
                }
            }
            if (blocked) break;
        }

        if (!blocked) {
            carried->pos          = dropTiles[0];
            carried->destination  = dropTiles[0];
            carried->moveProgress = 0.0f;
            carried->carriedBy    = INVALID_ENTITY;
            grid.spatial.add(player->carrying, dropTiles[0], carried->size);
            for (int i = 1; i < carried->tileCount; ++i) {
                carried->extraTiles[i - 1] = dropTiles[i];
                grid.spatial.add(player->carrying, dropTiles[i], carried->size);
            }
            player->carrying = INVALID_ENTITY;
        }
    };

    auto doHit = [&] {
        for (int dz : {0, 1, -1}) {
            for (EntityID cid : grid.spatial.at(aheadAt(dz))) {
                Entity* cand = registry_.get(cid);
                if (!cand || cid == playerID_) continue;
                if (cand->type == EntityType::Tree) {
                    if (cand->health > 0) --cand->health;
                    if (cand->health == 0) {
                        chopTree(grid, cid, cand, player->facing, 0);
                    }
                } else {
                    if (cand->mana > 0) --cand->mana;
                }
                return;
            }
        }
    };

    // Individual shortcut keys.
    if (input.pressed(Action::Dig))         doDig();
    if (input.pressed(Action::Plant))       doPlant();
    if (input.pressed(Action::Scythe))      doScythe();
    if (input.pressed(Action::Mine))        doMine();
    if (input.pressed(Action::PlacePortal)) doPortal();
    if (input.pressed(Action::PickUp))      doPickUp();
    if (input.pressed(Action::Drop))        doDrop();
    if (input.pressed(Action::Hit))         doHit();

    // E: execute whichever action is currently selected.
    if (input.pressed(Action::Summon)) {
        switch (activeAction_) {
            case PlayerAction::Dig:         doDig();      break;
            case PlayerAction::Plant:       doPlant();    break;
            case PlayerAction::Scythe:      doScythe();   break;
            case PlayerAction::Mine:        doMine();     break;
            case PlayerAction::Summon:      doSummon();   break;
            case PlayerAction::PlacePortal: doPortal();   break;
            // Context-aware: PickUp drops if already carrying, Drop picks up if not.
            case PlayerAction::PickUp:
                if (player->carrying != INVALID_ENTITY) doDrop(); else doPickUp();
                break;
            case PlayerAction::Drop:
                if (player->carrying == INVALID_ENTITY) doPickUp(); else doDrop();
                break;
            case PlayerAction::Hit:         doHit();      break;
        }
    }
}

// ─── chopTree ────────────────────────────────────────────────────────────────

void Game::chopTree(Field& field, EntityID treeID, Entity* tree, Direction facing, Tick) {
    TilePos rootPos = tree->pos;
    int     logLen  = tree->mass;   // tree mass = tree height = log tile count (1–3)
    TilePos delta   = dirToDelta(facing);

    // Despawn tree.
    field.remove(treeID, *tree);
    registry_.destroy(treeID);

    // Spawn log.
    EntityID lid = registry_.spawn(EntityType::Log, rootPos);
    Entity*  log = registry_.get(lid);
    log->mass      = logLen;
    log->tileCount = logLen;
    log->facing    = facing;   // store fall direction for drop orientation
    // Extra tile positions (fall in facing direction from root).
    for (int i = 1; i < logLen; ++i) {
        TilePos extra = { rootPos.x + delta.x * i,
                          rootPos.y + delta.y * i,
                          rootPos.z };
        if (!field.isBounded())
            extra.z = field.terrain.levelAt(extra);
        log->extraTiles[i - 1] = extra;
    }
    field.add(lid, *log);

    audioEvents_.push_back(AudioEvent::Dig);
    visualEvents_.push_back({ VisualEventType::Dig,
                               toVec(rootPos), static_cast<float>(rootPos.z) });
}

// ─── Routine VM ──────────────────────────────────────────────────────────────

void Game::tickVM(Field& grid) {
    std::vector<EntityID>   toRemove;
    std::vector<std::pair<EntityID, AgentSlot>> toAdd;  // defer insertion to avoid iterator invalidation

    for (auto& [id, slot] : agentSlots_) {
        if (!grid.hasEntity(id)) continue;
        Entity* ent = registry_.get(id);
        if (!ent || !ent->isIdle()) continue;

        // Build stimulus vector for conditional jumps.
        uint8_t stimuli[8] = {};
        {
            bool hasFire = false, hasPuddle = false, hasFluid = false;
            for (EntityID at : grid.spatial.at(ent->pos)) {
                const Entity* ae = registry_.get(at);
                if (!ae) continue;
                if (ae->type == EntityType::Fire)   hasFire   = true;
                if (ae->type == EntityType::Puddle) hasPuddle = true;
                if (ae->type == EntityType::Water)  hasFluid  = true;
            }
            if (hasFire)
                stimuli[static_cast<int>(Condition::Fire)] = 1;
            if (hasPuddle || hasFluid)
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

        VMResult res = vm_.step(slot.state, slot.routine, ent->facing, stimuli);

        if (res.halt) {
            // Drop a mushroom with leftover mana before despawning.
            if (ent->mana > 0) {
                TilePos dropPos = ent->pos;
                EntityID mid = registry_.spawn(EntityType::Mushroom, dropPos);
                Entity* me = registry_.get(mid);
                if (me) me->mana = ent->mana;
                grid.add(mid, *registry_.get(mid));
            }
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
            bool alreadyDug = false;
            for (EntityID cid : grid.spatial.at(target)) {
                const Entity* ce = registry_.get(cid);
                if (ce && ce->type == EntityType::BareEarth) { alreadyDug = true; break; }
            }
            if (!alreadyDug) {
                EntityID beid = registry_.spawn(EntityType::BareEarth, target);
                grid.add(beid, *registry_.get(beid));
            }
            audioEvents_.push_back(AudioEvent::Dig);
            visualEvents_.push_back({ VisualEventType::Dig,
                                      toVec(target), static_cast<float>(target.z) });
        } else if (res.wantPlant) {
            TilePos target = ent->pos + dirToDelta(ent->facing);
            EntityID bareID = INVALID_ENTITY;
            for (EntityID cid : grid.spatial.at(target)) {
                const Entity* ce = registry_.get(cid);
                if (ce && ce->type == EntityType::BareEarth) { bareID = cid; break; }
            }
            if (bareID != INVALID_ENTITY) {
                EntityID mid = registry_.spawn(EntityType::Mushroom, target);
                grid.add(mid, *registry_.get(mid));
                Entity* be = registry_.get(bareID);
                if (be) { grid.remove(bareID, *be); registry_.destroy(bareID); }
                audioEvents_.push_back(AudioEvent::Plant);
            }
        } else if (res.wantScythe) {
            TilePos target = ent->pos + dirToDelta(ent->facing);
            bool hasTileState = false;
            for (EntityID cid : grid.spatial.at(target)) {
                const Entity* ce = registry_.get(cid);
                if (ce && (ce->type == EntityType::BareEarth || ce->type == EntityType::Fire ||
                           ce->type == EntityType::Puddle    || ce->type == EntityType::Straw ||
                           ce->type == EntityType::Portal)) { hasTileState = true; break; }
            }
            if (!hasTileState) {
                EntityID seid = registry_.spawn(EntityType::Straw, target);
                grid.add(seid, *registry_.get(seid));
            }
        } else if (res.wantMine) {
            TilePos target = ent->pos + dirToDelta(ent->facing);
            for (EntityID cid : grid.spatial.at(target)) {
                Entity* cand = registry_.get(cid);
                if (!cand) continue;
                bool isOre = (cand->type == EntityType::IronOre  ||
                              cand->type == EntityType::CopperOre ||
                              cand->type == EntityType::CoalOre   ||
                              cand->type == EntityType::SulphurOre);
                if (isOre) { cand->capabilities |= Capability::Pushable; break; }
            }
        } else if (res.wantSummon) {
            TilePos target = ent->pos + dirToDelta(ent->facing);
            // Alchemy: find medium entity at target (default MudGolem).
            EntityType golemType = EntityType::MudGolem;
            EntityID   mediumID  = INVALID_ENTITY;
            for (EntityID cid : grid.spatial.at(target)) {
                const Entity* cand = registry_.get(cid);
                if (!cand) continue;
                auto result = alchemyReact(cand->type);
                if (result) { golemType = *result; mediumID = cid; break; }
            }
            if (mediumID != INVALID_ENTITY) {
                Entity* me = registry_.get(mediumID);
                if (me) { grid.remove(mediumID, *me); registry_.destroy(mediumID); }
            }
            {
                EntityID gid = registry_.spawn(golemType, target);
                Entity*  ge  = registry_.get(gid);
                ge->facing   = ent->facing;
                grid.add(gid, *ge);
                // Assign the routine encoded in the SUMMON instruction.
                Routine routine = (res.summonRoutineIdx < recorder_.routines.size())
                    ? recorder_.routines[res.summonRoutineIdx]
                    : slot.routine;
                toAdd.emplace_back(gid, AgentSlot{{}, std::move(routine)});
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


// ─── Lazy world generation (Phase 18) ────────────────────────────────────────

// Floor-division that handles negative numerators correctly.
// ─── Cooking ─────────────────────────────────────────────────────────────────

static constexpr Tick COOK_TICKS = 150;  // 3 seconds at 50 Hz

void Game::tickCooking(Field& grid, Tick currentTick) {
    // Check every Meat entity; if adjacent to a fire tile, advance its cook timer.
    for (EntityID eid : std::vector<EntityID>(grid.entities)) {
        Entity* ent = registry_.get(eid);
        if (!ent || ent->type != EntityType::Meat) continue;
        if (ent->carriedBy != INVALID_ENTITY) {
            // Carried meat can't cook; reset any in-progress timer.
            cookingStart_.erase(eid);
            continue;
        }

        // Check 4-directional neighbours for a Fire entity or Campfire.
        bool nearFire = false;
        const TilePos dirs[4] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0}};
        for (const TilePos& d : dirs) {
            for (EntityID at : grid.spatial.at(ent->pos + d)) {
                const Entity* ae = registry_.get(at);
                if (ae && (ae->type == EntityType::Fire || ae->type == EntityType::Campfire)) {
                    nearFire = true; break;
                }
            }
            if (nearFire) break;
        }

        if (!nearFire) {
            cookingStart_.erase(eid);
            continue;
        }

        // Start timer if not already cooking.
        if (!cookingStart_.count(eid))
            cookingStart_[eid] = currentTick;

        if (currentTick - cookingStart_[eid] < COOK_TICKS)
            continue;

        // Convert to CookedMeat with 4× mana.
        int cookedMana = ent->mana * 4;
        TilePos pos    = ent->pos;

        // Remove raw meat.
        grid.remove(eid, *ent);
        registry_.destroy(eid);
        cookingStart_.erase(eid);

        // Spawn cooked meat.
        EntityID cid   = registry_.spawn(EntityType::CookedMeat, pos);
        Entity*  cooked = registry_.get(cid);
        cooked->mana   = cookedMana;
        grid.add(cid, *cooked);
    }
}

static int floorDiv(int a, int b) {
    return a / b - (a % b != 0 && (a ^ b) < 0);
}

void Game::maybeGenerateChunks(Field& grid, TilePos playerPos) {
    int pcx = floorDiv(playerPos.x, CHUNK_SIZE);
    int pcy = floorDiv(playerPos.y, CHUNK_SIZE);

    for (int cy = pcy - 2; cy <= pcy + 2; ++cy) {
        for (int cx = pcx - 2; cx <= pcx + 2; ++cx) {
            TilePos key = {cx, cy, 0};
            if (grid.generatedChunks.count(key)) continue;
            grid.generatedChunks.insert(key);
            generateChunk(grid, cx, cy);
        }
    }
}

void Game::generateChunk(Field& grid, int cx, int cy) {
    // Seed RNG deterministically from chunk coordinates.
    uint32_t seed = static_cast<uint32_t>(cx * 73856093u ^ cy * 19349663u);
    std::mt19937 rng(seed);

    int x0 = cx * CHUNK_SIZE;
    int y0 = cy * CHUNK_SIZE;

    // Biome from chunk centre.
    TilePos centre = {x0 + CHUNK_SIZE / 2, y0 + CHUNK_SIZE / 2, 0};
    Biome biome = grid.terrain.biomeAt(centre);

    std::uniform_real_distribution<float> chance(0.f, 1.f);
    std::uniform_int_distribution<int>    xi(x0, x0 + CHUNK_SIZE - 1);
    std::uniform_int_distribution<int>    yi(y0, y0 + CHUNK_SIZE - 1);

    // Helper: spawn one entity at a random position in the chunk.
    auto trySpawn = [&](EntityType type, float probability) {
        if (chance(rng) >= probability) return;
        int x = xi(rng), y = yi(rng);
        TilePos p = {x, y, 0};
        p.z = grid.terrain.levelAt(p);
        EntityID eid = registry_.spawn(type, p);
        grid.add(eid, *registry_.get(eid));
    };

    switch (biome) {
        case Biome::Grassland: {
            // Spawn a warren first; rabbits are seeded inside it.
            EntityID warrenEid = INVALID_ENTITY;
            if (chance(rng) < 0.30f) {
                int wx = xi(rng), wy = yi(rng);
                TilePos wp = {wx, wy, 0};
                wp.z = grid.terrain.levelAt(wp);
                warrenEid = registry_.spawn(EntityType::Warren, wp);
                grid.add(warrenEid, *registry_.get(warrenEid));
                FieldID wgid = createWarrenInterior(warrenEid, wp);
                Field& interior = fields_.at(wgid);
                // Seed 4 rabbits inside the warren.
                TilePos rp = { WARREN_W / 2 - 2, WARREN_H / 2, 0 };
                for (int i = 0; i < 4; ++i) {
                    TilePos rpos = { rp.x + i, rp.y, 0 };
                    EntityID reid = registry_.spawn(EntityType::Rabbit, rpos);
                    Entity* re = registry_.get(reid);
                    re->mana = RABBIT_MANA_HUNGRY - 1; // start hungry → will emerge
                    interior.add(reid, *re);
                    rabbitSlots_[reid] = { warrenEid, wgid };
                }
            }
            trySpawn(EntityType::LongGrass, 0.60f);
            trySpawn(EntityType::LongGrass, 0.60f);
            trySpawn(EntityType::LongGrass, 0.40f);
            break;
        }

        case Biome::Forest:
            trySpawn(EntityType::Tree,     0.70f);
            trySpawn(EntityType::Tree,     0.50f);
            trySpawn(EntityType::Mushroom, 0.20f);
            break;

        case Biome::Volcanic:
            trySpawn(EntityType::Rock,       0.60f);
            trySpawn(EntityType::SulphurOre, 0.30f);
            trySpawn(EntityType::CoalOre,    0.20f);
            break;

        case Biome::Lake:
            // Pre-seed with water to let the fluid system spread it.
            for (int i = 0; i < 4; ++i) {
                int x = xi(rng), y = yi(rng);
                TilePos p = {x, y, 0};
                p.z = grid.terrain.levelAt(p);
                EntityID weid = registry_.spawn(EntityType::Water, p);
                grid.add(weid, *registry_.get(weid));
                fluidComponents_.add(weid, {1.0f, 0.f, 0.f});
            }
            break;

        case Biome::Mountains:
            trySpawn(EntityType::Rock,      0.50f);
            trySpawn(EntityType::IronOre,   0.40f);
            trySpawn(EntityType::CopperOre, 0.30f);
            break;
    }
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

    // Write one field's terrain + portals + non-player, non-Agent entities.
    void wrGrid(std::ostream& f, const Field& grid,
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
            wr<uint32_t>(f, portal.targetField);
            wr<int32_t>(f, portal.targetPos.x);
            wr<int32_t>(f, portal.targetPos.y);
            wr<int32_t>(f, portal.targetPos.z);
        }

        // Entities (skip player)
        std::vector<EntityID> toSave;
        for (EntityID eid : grid.entities)
            if (eid != playerID) {
                const Entity* e = reg.get(eid);
                if (e) toSave.push_back(eid);
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
            wr<int32_t>(f, e->mass);
            wr<uint8_t>(f, static_cast<uint8_t>(e->tileCount));
            for (int i = 1; i < e->tileCount; ++i) {
                wr<int32_t>(f, e->extraTiles[i-1].x);
                wr<int32_t>(f, e->extraTiles[i-1].y);
                wr<int32_t>(f, e->extraTiles[i-1].z);
            }
            // Water entities carry extra FluidComponent data.
            if (e->type == EntityType::Water) {
                const FluidComponent* fc = fluids.get(eid);
                float h  = fc ? fc->h  : 0.f;
                float vx = fc ? fc->vx : 0.f;
                float vy = fc ? fc->vy : 0.f;
                wr<float>(f, h); wr<float>(f, vx); wr<float>(f, vy);
            }
        }

        // Generated chunks (unbounded grids only; bounded rooms always 0).
        wr<uint32_t>(f, static_cast<uint32_t>(grid.generatedChunks.size()));
        for (const TilePos& p : grid.generatedChunks) {
            wr<int32_t>(f, p.x);
            wr<int32_t>(f, p.y);
        }
    }
}

void Game::save(const std::string& path) const {
    std::ofstream f(path, std::ios::binary);
    if (!f) return;

    f.write("GRID", 4);
    wr<uint8_t>(f, 14);   // version 14: added mass, tileCount, extraTiles per entity

    // Player
    const Entity* player = registry_.get(playerID_);
    FieldID playerGrid = activeFieldID_;
    TilePos ppos = player ? player->pos : TilePos{0, 0};
    // If in studio, save player back in world at the stored world position
    if (activeFieldID_ == FIELD_STUDIO) { playerGrid = FIELD_WORLD; ppos = playerWorldPos_; }
    wr<uint32_t>(f, playerGrid);
    wr<int32_t>(f,  ppos.x);
    wr<int32_t>(f,  ppos.y);
    wr<int32_t>(f,  ppos.z);
    wr<uint8_t>(f,  player ? static_cast<uint8_t>(player->facing) : 0);
    wr<int32_t>(f,  player ? player->mana : 0);

    // Fields (all except studio)
    uint32_t gridCount = 0;
    for (const auto& [id, _] : fields_)
        if (id != FIELD_STUDIO) ++gridCount;
    wr<uint32_t>(f, gridCount);
    wr<uint32_t>(f, nextFieldID_);

    for (const auto& [id, grid] : fields_) {
        if (id == FIELD_STUDIO) continue;
        wrGrid(f, grid, playerID_, registry_, fluidComponents_);
    }

    // Routines
    wr<uint32_t>(f, static_cast<uint32_t>(recorder_.routines.size()));
    for (const Routine& rec : recorder_.routines) {
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
    wr<uint64_t>(f, static_cast<uint64_t>(selectedRoutine_));
}

bool Game::load(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;

    char magic[4];
    f.read(magic, 4);
    if (std::strncmp(magic, "GRID", 4) != 0) return false;
    uint8_t version = rd<uint8_t>(f);
    if (version != 14) return false;

    // Clear all state
    for (auto& [id, field] : fields_) {
        for (EntityID eid : std::vector<EntityID>(field.entities)) {
            Entity* e = registry_.get(eid);
            if (e) field.remove(eid, *e);
            registry_.destroy(eid);
        }
        field.portals.clear();
        field.paused = false;
    }
    // Remove dynamic fields (keep world + studio)
    for (auto it = fields_.begin(); it != fields_.end(); ) {
        if (it->first != FIELD_WORLD && it->first != FIELD_STUDIO)
            it = fields_.erase(it);
        else ++it;
    }
    agentSlots_.clear();
    fluidComponents_.clear();
    pendingTransfers_.clear();

    // Player
    FieldID   playerGrid = rd<uint32_t>(f);
    TilePos   ppos       = { rd<int32_t>(f), rd<int32_t>(f), rd<int32_t>(f) };
    Direction pfacing    = static_cast<Direction>(rd<uint8_t>(f));
    int       pmana    = rd<int32_t>(f);

    // Fields
    uint32_t gridCount = rd<uint32_t>(f);
    nextFieldID_       = rd<uint32_t>(f);

    for (uint32_t g = 0; g < gridCount; ++g) {
        FieldID gid   = rd<uint32_t>(f);
        int    width  = rd<int32_t>(f);
        int    height = rd<int32_t>(f);
        bool   paused = rd<uint8_t>(f) != 0;

        // Ensure field exists
        if (!fields_.count(gid)) {
            fields_.try_emplace(gid, gid, width, height);
            subscribeEvents(fields_.at(gid));
        }
        Field& grid  = fields_.at(gid);
        grid.width   = width;
        grid.height  = height;
        grid.paused  = paused;
        grid.generatedChunks.clear();

        // Portals
        uint32_t portalCount = rd<uint32_t>(f);
        for (uint32_t i = 0; i < portalCount; ++i) {
            TilePos  pos       = { rd<int32_t>(f), rd<int32_t>(f), rd<int32_t>(f) };
            FieldID  tField    = rd<uint32_t>(f);
            TilePos  tPos      = { rd<int32_t>(f), rd<int32_t>(f), rd<int32_t>(f) };
            grid.portals[pos]  = { tField, tPos };
        }

        // Entities
        uint32_t entCount = rd<uint32_t>(f);
        for (uint32_t i = 0; i < entCount; ++i) {
            EntityType et     = static_cast<EntityType>(rd<uint8_t>(f));
            TilePos    pos    = { rd<int32_t>(f), rd<int32_t>(f), rd<int32_t>(f) };
            Direction  facing = static_cast<Direction>(rd<uint8_t>(f));
            int        mana   = rd<int32_t>(f);
            int        health = rd<int32_t>(f);
            int        mass   = rd<int32_t>(f);
            int        tileCount = rd<uint8_t>(f);
            TilePos    extraTiles[2] = {};
            for (int i = 1; i < tileCount && i <= 2; ++i) {
                extraTiles[i-1].x = rd<int32_t>(f);
                extraTiles[i-1].y = rd<int32_t>(f);
                extraTiles[i-1].z = rd<int32_t>(f);
            }

            EntityID eid = registry_.spawn(et, pos);
            Entity*  e   = registry_.get(eid);
            e->facing = facing; e->mana = mana; e->health = health;
            e->mass = mass;
            e->tileCount = tileCount;
            for (int i = 1; i < tileCount && i <= 2; ++i)
                e->extraTiles[i-1] = extraTiles[i-1];
            grid.add(eid, *e);
            if (et == EntityType::Water) {
                float h  = rd<float>(f);
                float vx = rd<float>(f);
                float vy = rd<float>(f);
                fluidComponents_.add(eid, {h, vx, vy});
            }
        }

        // Generated chunks
        uint32_t chunkCount = rd<uint32_t>(f);
        for (uint32_t i = 0; i < chunkCount; ++i) {
            int cx = rd<int32_t>(f);
            int cy = rd<int32_t>(f);
            grid.generatedChunks.insert({cx, cy, 0});
        }
    }

    // Spawn player in saved field
    if (!fields_.count(playerGrid)) playerGrid = FIELD_WORLD;
    playerID_ = registry_.spawn(EntityType::Player, ppos);
    Entity* player = registry_.get(playerID_);
    player->facing = pfacing;
    player->mana   = pmana;
    fields_.at(playerGrid).add(playerID_, *player);
    activeFieldID_  = playerGrid;
    playerWorldPos_ = (playerGrid == FIELD_WORLD) ? ppos : TilePos{0, 0};

    // Routines
    recorder_.routines.clear();
    uint32_t recCount = rd<uint32_t>(f);
    for (uint32_t i = 0; i < recCount; ++i) {
        Routine rec;
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
        recorder_.routines.push_back(std::move(rec));
    }
    selectedRoutine_ = static_cast<size_t>(rd<uint64_t>(f));
    if (selectedRoutine_ >= recorder_.routines.size())
        selectedRoutine_ = 0;

    // Re-populate studio medium entities (studio is never saved).
    Field& studio = fields_.at(FIELD_STUDIO);
    auto spawnStudioLoad = [&](EntityType et, int x, int y) {
        TilePos p{x, y, 0};
        EntityID eid = registry_.spawn(et, p);
        studio.add(eid, *registry_.get(eid));
    };
    spawnStudioLoad(EntityType::Mud,    0,  1);
    spawnStudioLoad(EntityType::Stone,  1,  1);
    spawnStudioLoad(EntityType::Clay,  -1,  1);

    return true;
}

// ─── gameStateText ────────────────────────────────────────────────────────────

static const char* entityTypeName(EntityType t) {
    switch (t) {
        case EntityType::Player:     return "Player";
        case EntityType::Goblin:     return "Goblin";
        case EntityType::Mushroom:   return "Mushroom";
        case EntityType::Campfire:   return "Campfire";
        case EntityType::TreeStump:  return "TreeStump";
        case EntityType::Log:        return "Log";
        case EntityType::Battery:    return "Battery";
        case EntityType::Lightbulb:  return "Lightbulb";
        case EntityType::Tree:       return "Tree";
        case EntityType::Rock:       return "Rock";
        case EntityType::Chest:      return "Chest";
        case EntityType::MudGolem:   return "MudGolem";
        case EntityType::StoneGolem: return "StoneGolem";
        case EntityType::ClayGolem:  return "ClayGolem";
        case EntityType::WaterGolem: return "WaterGolem";
        case EntityType::BushGolem:  return "BushGolem";
        case EntityType::WoodGolem:  return "WoodGolem";
        case EntityType::IronGolem:  return "IronGolem";
        case EntityType::CopperGolem:return "CopperGolem";
        case EntityType::Water:      return "Water";
        case EntityType::Rabbit:     return "Rabbit";
        case EntityType::Warren:     return "Warren";
        case EntityType::IronOre:    return "IronOre";
        case EntityType::CopperOre:  return "CopperOre";
        case EntityType::CoalOre:    return "CoalOre";
        case EntityType::SulphurOre: return "SulphurOre";
        case EntityType::LongGrass:  return "LongGrass";
        case EntityType::Meat:       return "Meat";
        case EntityType::CookedMeat: return "CookedMeat";
        case EntityType::Spark:      return "Spark";
        case EntityType::Mud:        return "Mud";
        case EntityType::Stone:      return "Stone";
        case EntityType::Clay:       return "Clay";
        case EntityType::Bush:       return "Bush";
        case EntityType::Wood:       return "Wood";
        case EntityType::Iron:       return "Iron";
        case EntityType::Copper:     return "Copper";
        case EntityType::BareEarth:  return "BareEarth";
        case EntityType::Fire:       return "Fire";
        case EntityType::Puddle:     return "Puddle";
        case EntityType::Straw:      return "Straw";
        case EntityType::Portal:     return "Portal";
    }
    return "Unknown";
}

static const char* dirName(Direction d) {
    switch (d) {
        case Direction::N:  return "N";
        case Direction::NE: return "NE";
        case Direction::E:  return "E";
        case Direction::SE: return "SE";
        case Direction::S:  return "S";
        case Direction::SW: return "SW";
        case Direction::W:  return "W";
        case Direction::NW: return "NW";
    }
    return "?";
}

// Compass direction label from 'from' toward 'to'.
static const char* compassTo(TilePos from, TilePos to) {
    int dx = to.x - from.x, dy = to.y - from.y;
    // Use the dominant axis for ordinal labelling.
    bool east  = dx > 0, west = dx < 0;
    bool south = dy > 0, north = dy < 0;
    if (north && east)  return "NE";
    if (north && west)  return "NW";
    if (south && east)  return "SE";
    if (south && west)  return "SW";
    if (north)          return "N";
    if (south)          return "S";
    if (east)           return "E";
    if (west)           return "W";
    return "here";
}

std::string gameStateText(const Game& g) {
    auto entities  = g.drawOrder();
    TilePos ppos   = g.playerPos();

    // Locate the player entity for facing / carrying.
    const Entity* player = nullptr;
    for (const Entity* e : entities)
        if (e->type == EntityType::Player) { player = e; break; }

    std::ostringstream out;

    // ── Header ────────────────────────────────────────────────────────────────
    out << "=== " << (g.inStudio() ? "STUDIO" : "WORLD")
        << " | Mana " << g.playerMana() << " ===\n";

    // ── Player line ───────────────────────────────────────────────────────────
    if (player) {
        out << "@ {" << ppos.x << "," << ppos.y << "}"
            << " facing " << dirName(player->facing)
            << " | Action: " << playerActionName(g.activePlayerAction());

        if (player->carrying != INVALID_ENTITY) {
            for (const Entity* e : entities)
                if (e->id == player->carrying) {
                    out << " | Carrying: " << entityTypeName(e->type);
                    break;
                }
        } else {
            out << " | Carrying: nothing";
        }
        out << '\n';

        // ── Ahead tile ────────────────────────────────────────────────────────
        TilePos ahead = ppos + dirToDelta(player->facing);
        out << "Ahead (" << dirName(player->facing)
            << " {" << ahead.x << "," << ahead.y << "})";
        const Entity* aheadEnt = g.entityAtTile(ahead);
        if (aheadEnt) out << ": " << entityTypeName(aheadEnt->type);
        else          out << ": Grass";
        out << '\n';
    }

    // ── Nearby entities ───────────────────────────────────────────────────────
    struct Near { float dist; const char* name; int x, y; const char* note; };
    std::vector<Near> nearby;

    for (const Entity* e : entities) {
        if (e->type == EntityType::Player) continue;
        if (e->carriedBy != INVALID_ENTITY) continue;
        float dx = float(e->pos.x - ppos.x), dy = float(e->pos.y - ppos.y);
        float d  = std::sqrt(dx*dx + dy*dy);
        if (d > 15.f) continue;
        const char* note = "";
        if (e->type == EntityType::Goblin)
            note = (e->carrying != INVALID_ENTITY) ? " [loaded]" : " [hungry]";
        nearby.push_back({ d, entityTypeName(e->type), e->pos.x, e->pos.y, note });
    }
    std::sort(nearby.begin(), nearby.end(),
              [](const Near& a, const Near& b){ return a.dist < b.dist; });

    out << "Nearby:\n";
    if (nearby.empty()) {
        out << "  (none within 15 tiles)\n";
    } else {
        int shown = 0;
        for (const auto& n : nearby) {
            if (shown++ >= 10) break;
            out << "  " << std::fixed << std::setprecision(1) << n.dist
                << " " << compassTo(ppos, {n.x, n.y, ppos.z})
                << " " << n.name
                << " {" << n.x << "," << n.y << "}"
                << n.note << '\n';
        }
    }

    return out.str();
}
