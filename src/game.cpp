#include "game.hpp"
#include "routine.hpp"
#include "terrain.hpp"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>

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
            if (!cand || cand->type != EntityType::Mushroom) continue;
            player->mana += 3;
            g.remove(cid, *cand);
            registry_.destroy(cid);
            audioEvents_.push_back(AudioEvent::CollectMushroom);
            break;
        }
    });
}

// ─── Construction ─────────────────────────────────────────────────────────────

Game::Game() {
    grids_.try_emplace(GRID_WORLD,  GRID_WORLD);
    grids_.try_emplace(GRID_STUDIO, GRID_STUDIO);

    subscribeEvents(grids_.at(GRID_WORLD));
    subscribeEvents(grids_.at(GRID_STUDIO));

    playerID_ = registry_.spawn(EntityType::Player, {0, 0});
    activeGrid().add(playerID_, *registry_.get(playerID_));

    EntityID goblinID = registry_.spawn(EntityType::Goblin, {5, 5});
    activeGrid().add(goblinID, *registry_.get(goblinID));
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
        grid.events.flush();
    }
}

// ─── Pending portal transfer ─────────────────────────────────────────────────

void Game::applyPendingTransfer() {
    if (!pendingTransfer_) return;
    auto& t = *pendingTransfer_;
    if (grids_.count(t.fromGrid) && grids_.count(t.toGrid)) {
        transferEntity(t.eid, grids_.at(t.fromGrid), grids_.at(t.toGrid),
                       registry_, t.toPos);
        if (t.eid == playerID_) {
            activeGridID_     = t.toGrid;
            gridJustSwitched_ = true;
            audioEvents_.push_back(AudioEvent::PortalEnter);
        }
    }
    pendingTransfer_.reset();
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
        result.push_back({ i, rec.name, steps, i == selectedRecording_ });
    }
    return result;
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

// ─── Player input ─────────────────────────────────────────────────────────────

void Game::tickPlayerInput(const Input& input) {
    Grid&   grid   = activeGrid();
    Entity* player = registry_.get(playerID_);

    // r: toggle recording
    if (input.pressed(Key::R)) {
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
        audioEvents_.push_back(AudioEvent::DeployAgent);
    }

    // Tab: toggle between world and studio
    if (input.pressed(Key::Tab) && player && player->isIdle()) {
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

        // Clamp to grid bounds if bounded (XY only — z free)
        if (grid.isBounded()) {
            newDest.x = std::clamp(newDest.x, 0, grid.width  - 1);
            newDest.y = std::clamp(newDest.y, 0, grid.height - 1);
        }

        // Resolve z via slope rules; nullopt means slope blocks this move.
        auto slopeDest = resolveZ(player->pos, newDest, grid.terrain);
        if (slopeDest) {
            newDest = *slopeDest;

        std::vector<MoveIntention> intentions = {{
            playerID_, player->pos, newDest, player->type, player->size
        }};
        auto allowed = resolveMoves(intentions, grid.spatial, registry_);

        if (allowed.count(playerID_)) {
            player->destination = newDest;
            if (recorder_.isRecording())
                recorder_.recordMove(delta, facingBefore);
        } else {
            // Bump combat
            Bounds destBounds = boundsAt(newDest, player->size);
            for (EntityID cid : grid.spatial.query(destBounds, newDest.z)) {
                Entity* cand = registry_.get(cid);
                if (!cand || cand->type != EntityType::Goblin) continue;
                if (!overlaps(destBounds, boundsAt(cand->pos, cand->size))) continue;

                cand->health -= player->mana;
                audioEvents_.push_back(AudioEvent::GoblinHit);
                if (cand->health <= 0) {
                    grid.remove(cid, *cand);
                    registry_.destroy(cid);
                } else {
                    TilePos pushBase = cand->isMoving() ? cand->destination : cand->pos;
                    TilePos pushDest = pushBase + delta;
                    if (cand->isMoving()) {
                        Bounds pushBounds   = boundsAt(pushDest, cand->size);
                        bool   pushBlocked  = false;
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
            }
        }
        } // if (slopeDest)
    }

    // Terrain interaction
    TilePos ahead = player->pos + dirToDelta(player->facing);
    if (input.pressed(Key::F)) {
        grid.terrain.dig(ahead);
        audioEvents_.push_back(AudioEvent::Dig);
    }

    if (input.pressed(Key::C)) {
        if (grid.terrain.typeAt(ahead) == TileType::BareEarth && player->mana >= 1) {
            EntityID mid = registry_.spawn(EntityType::Mushroom, ahead);
            grid.add(mid, *registry_.get(mid));
            grid.terrain.restore(ahead);
            player->mana--;
            audioEvents_.push_back(AudioEvent::Plant);
        }
    }

    // Z: cycle slope shape on tile ahead
    if (input.pressed(Key::Z)) {
        TilePos ahead2 = player->pos + dirToDelta(player->facing);
        TileShape cur  = grid.terrain.shapeAt(ahead2);
        TileShape next = TileShape::Flat;
        switch (cur) {
            case TileShape::Flat:   next = TileShape::SlopeN; break;
            case TileShape::SlopeN: next = TileShape::SlopeE; break;
            case TileShape::SlopeE: next = TileShape::SlopeS; break;
            case TileShape::SlopeS: next = TileShape::SlopeW; break;
            case TileShape::SlopeW: next = TileShape::Flat;   break;
        }
        grid.terrain.setShape(ahead2, next);
    }

    // O: create portal + linked room ahead of player
    if (input.pressed(Key::O) && player) {
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
        auto slopeDest = resolveZ(ent->pos, newDest, grid.terrain);
        if (!slopeDest) continue;
        newDest = *slopeDest;
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

        VMResult res = vm_.step(state, agentRecordings_[id], ent->facing);

        if (res.halt) {
            grid.remove(id, *ent);
            registry_.destroy(id);
            toRemove.push_back(id);
        } else if (res.wantMove) {
            TilePos newDest  = ent->pos + res.moveDelta;
            auto slopeDest   = resolveZ(ent->pos, newDest, grid.terrain);
            if (slopeDest) {
                newDest = *slopeDest;
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
            grid.events.emit({ EventType::Arrived, eid });

            if (eid == playerID_)
                audioEvents_.push_back(AudioEvent::PlayerStep);

            // Portal check: any entity, only if no transfer already pending
            if (!pendingTransfer_) {
                auto pit = grid.portals.find(ent->pos);
                if (pit != grid.portals.end())
                    pendingTransfer_ = { eid, grid.id,
                                         pit->second.targetGrid, pit->second.targetPos };
            }
        }
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

        // Shape overrides
        const auto& shp = grid.terrain.shapes();
        wr<uint32_t>(f, static_cast<uint32_t>(shp.size()));
        for (const auto& [pos, shape] : shp) {
            wr<int32_t>(f, pos.x);
            wr<int32_t>(f, pos.y);
            wr<int32_t>(f, pos.z);
            wr<uint8_t>(f, static_cast<uint8_t>(shape));
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
    wr<uint8_t>(f, 3);   // version 3

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
            wr<uint8_t>(f, ins.dir);
            wr<uint32_t>(f, ins.arg);
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
    if (version != 3) return false;

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
    pendingTransfer_.reset();

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
            grid.terrain.setType(pos, type);
        }

        // Shape overrides
        uint32_t shpCount = rd<uint32_t>(f);
        for (uint32_t i = 0; i < shpCount; ++i) {
            TilePos   pos   = { rd<int32_t>(f), rd<int32_t>(f), rd<int32_t>(f) };
            TileShape shape = static_cast<TileShape>(rd<uint8_t>(f));
            grid.terrain.setShape(pos, shape);
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
            OpCode   op  = static_cast<OpCode>(rd<uint8_t>(f));
            uint8_t  dir = rd<uint8_t>(f);
            uint32_t arg = rd<uint32_t>(f);
            rec.instructions.push_back({ op, dir, arg });
        }
        recorder_.recordings.push_back(std::move(rec));
    }
    selectedRecording_ = static_cast<size_t>(rd<uint64_t>(f));
    if (selectedRecording_ >= recorder_.recordings.size())
        selectedRecording_ = 0;

    return true;
}
