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
    const Terrain& terrain()          const { return activeGrid().terrain; }
    int            playerMana()       const;
    bool           isRecording()      const { return recorder_.isRecording(); }
    bool           inStudio()         const { return activeGridID_ == GRID_STUDIO; }

    // Player position accessors for camera tracking.
    TilePos playerPos()         const;
    TilePos playerDestination() const;
    float   playerMoveT()       const;

    // Returns true (and clears the flag) if the active grid switched this tick.
    // Used by main.cpp to snap the camera without lerping.
    bool consumeGridSwitch() { bool v = gridJustSwitched_; gridJustSwitched_ = false; return v; }

    // Entities in the active grid, sorted by layer — for rendering.
    std::vector<const Entity*> drawOrder() const;

private:
    EntityRegistry registry_;
    std::unordered_map<GridID, Grid> grids_;
    GridID   activeGridID_    = GRID_WORLD;
    EntityID playerID_        = INVALID_ENTITY;
    TilePos  playerWorldPos_  = {0, 0};   // saved on entering studio

    bool      gridJustSwitched_ = false;

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
