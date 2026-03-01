#pragma once

#include "types.hpp"
#include "entity.hpp"
#include "grid.hpp"
#include "input.hpp"
#include "recorder.hpp"
#include "routine_vm.hpp"
#include <optional>
#include <unordered_map>
#include <string>
#include <utility>

// ─── RecordingInfo ────────────────────────────────────────────────────────────

struct RecordingInfo {
    size_t      index;
    std::string name;
    int         steps;
    bool        selected;
};

// ─── Grid ID constants ────────────────────────────────────────────────────────

constexpr GridID GRID_WORLD  = 1;
constexpr GridID GRID_STUDIO = 2;
constexpr GridID GRID_DYN_START = 3;   // dynamic grids start here

// ─── Room dimensions ─────────────────────────────────────────────────────────

constexpr int ROOM_W = 20;
constexpr int ROOM_H = 20;

// ─── transferEntity ───────────────────────────────────────────────────────────

void transferEntity(EntityID eid, Grid& from, Grid& to,
                    EntityRegistry& registry, TilePos dest);

// ─── Game ─────────────────────────────────────────────────────────────────────

class Game {
public:
    Game();

    void tick(const Input& input, Tick currentTick);

    // Renderer accessors.
    const Terrain& terrain()     const { return activeGrid().terrain; }
    int            playerMana()  const;
    bool           isRecording() const { return recorder_.isRecording(); }
    bool           inStudio()    const { return activeGridID_ == GRID_STUDIO; }

    // Active grid bounds for renderer (0 = unbounded).
    std::pair<int,int> activeGridBounds() const {
        const Grid& g = activeGrid();
        return { g.width, g.height };
    }

    // Player position for camera tracking.
    TilePos playerPos()         const;
    TilePos playerDestination() const;
    float   playerMoveT()       const;

    // Recordings panel.
    std::vector<RecordingInfo> recordingList() const;
    void renameRecording(size_t index, const std::string& name);

    // Persistence.
    void save(const std::string& path) const;
    bool load(const std::string& path);

    // Clears and returns the grid-switched flag (used for camera snap).
    bool consumeGridSwitch() { bool v = gridJustSwitched_; gridJustSwitched_ = false; return v; }

    // Entities in the active grid, sorted by layer.
    std::vector<const Entity*> drawOrder() const;

private:
    // ── Pending cross-grid transfer ───────────────────────────────────────────
    struct PendingTransfer {
        EntityID eid;
        GridID   fromGrid;
        GridID   toGrid;
        TilePos  toPos;
    };
    std::optional<PendingTransfer> pendingTransfer_;

    // ── State ─────────────────────────────────────────────────────────────────
    EntityRegistry                   registry_;
    std::unordered_map<GridID, Grid> grids_;
    GridID   activeGridID_   = GRID_WORLD;
    GridID   nextGridID_     = GRID_DYN_START;
    EntityID playerID_       = INVALID_ENTITY;
    TilePos  playerWorldPos_ = {0, 0};
    bool     gridJustSwitched_ = false;

    Recorder  recorder_;
    RoutineVM vm_;
    std::unordered_map<EntityID, AgentExecState> agentStates_;
    std::unordered_map<EntityID, Recording>      agentRecordings_;
    size_t selectedRecording_ = 0;

    // ── Helpers ───────────────────────────────────────────────────────────────
    Grid&       activeGrid()       { return grids_.at(activeGridID_); }
    const Grid& activeGrid() const { return grids_.at(activeGridID_); }

    void subscribeEvents(Grid& grid);
    void applyPendingTransfer();

    // Per-grid tick sub-systems (all grids run these each tick if not paused).
    void tickScheduler(Grid& grid, Tick currentTick);
    void tickGoblinWander(Grid& grid);
    void tickVM(Grid& grid);
    void tickMovement(Grid& grid);

    // Player input — only called for the active grid.
    void tickPlayerInput(const Input& input);
};
