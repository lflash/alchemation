#pragma once

#include "types.hpp"
#include "entity.hpp"
#include "grid.hpp"
#include "input.hpp"
#include "recorder.hpp"
#include "routine_vm.hpp"
#include <unordered_map>

constexpr GridID GRID_WORLD  = 1;
constexpr GridID GRID_STUDIO = 2;

// ─── transferEntity ───────────────────────────────────────────────────────────
//
// Move an entity from one grid to another. Removes it from `from`'s spatial
// and entity list, snaps its position to `dest`, then registers it in `to`.
// Resets moveT so no interpolation artefact appears on arrival.

void transferEntity(EntityID eid, Grid& from, Grid& to,
                    EntityRegistry& registry, TilePos dest);

// ─── Game ─────────────────────────────────────────────────────────────────────
//
// Owns all simulation state: grids, entities, recorder, and the per-agent VM
// state. Rendering and input are caller concerns; tick() accepts a snapshot of
// input state and advances the simulation by one fixed timestep.

class Game {
public:
    Game();

    void tick(const Input& input, Tick currentTick);

    // Accessors for the renderer.
    const Terrain&        terrain()    const { return activeGrid().terrain; }
    const EntityRegistry& registry()   const { return registry_; }
    int                   playerMana() const;
    bool                  isRecording() const { return recorder_.isRecording(); }

private:
    EntityRegistry registry_;
    std::unordered_map<GridID, Grid> grids_;
    GridID   activeGridID_ = GRID_WORLD;
    EntityID playerID_     = INVALID_ENTITY;

    Recorder  recorder_;
    RoutineVM vm_;
    std::unordered_map<EntityID, AgentExecState> agentStates_;
    std::unordered_map<EntityID, Recording>      agentRecordings_;
    size_t selectedRecording_ = 0;

    Grid&       activeGrid()       { return grids_.at(activeGridID_); }
    const Grid& activeGrid() const { return grids_.at(activeGridID_); }

    void tickScheduler(Tick currentTick);
    void tickPlayerInput(const Input& input);
    void tickGoblinWander();
    void tickVM();
    void tickMovement();
};
