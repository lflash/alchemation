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
#include <vector>

// ─── AudioEvent ───────────────────────────────────────────────────────────────
//
// Game pushes these during tick(); main.cpp drains them each frame and plays
// the corresponding SFX via AudioSystem.

enum class AudioEvent {
    PlayerStep, Dig, Plant, CollectMushroom,
    RecordStart, RecordStop, DeployAgent,
    PortalCreate, PortalEnter, GridSwitch,
    GoblinHit, AgentStep,
};

// ─── VisualEvent ──────────────────────────────────────────────────────────────
//
// Parallel to AudioEvent: Game emits these; the renderer drains them each
// frame and spawns particles / triggers screen-level effects accordingly.

enum class VisualEventType {
    Dig, CollectMushroom, DeployAgent,
    GoblinHit, GoblinDie, PlayerLand,
    PortalEnter, GridSwitch,
};

struct VisualEvent {
    VisualEventType type;
    Vec2f           pos;                              // world-space origin (tile units)
    float           z          = 0.0f;               // world z at that position
    EntityID        entityID   = INVALID_ENTITY;     // for flash effects (GoblinHit)
    EntityType      entityType = EntityType::Player; // for dying-entity (GoblinDie)
};

// ─── RecordingInfo ────────────────────────────────────────────────────────────

struct RecordingInfo {
    size_t      index;
    std::string name;
    int         steps;
    bool        selected;
};

// ─── Fire & Voltage simulation ───────────────────────────────────────────────
//
// Free functions so they can be exercised directly in unit tests without a
// full Game object. Game::tick() calls both for every non-paused grid.

void tickFire(Grid& grid, EntityRegistry& registry, Tick currentTick);
void tickVoltage(Grid& grid, EntityRegistry& registry);

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

    // Drains all audio events accumulated since the last call. main.cpp plays
    // the corresponding SFX for each entry.
    std::vector<AudioEvent> drainAudioEvents() {
        auto v = std::move(audioEvents_);
        audioEvents_.clear();
        return v;
    }

    // Drains all visual events accumulated since the last call.  main.cpp
    // translates these into renderer particle / flash / shake / fade calls.
    std::vector<VisualEvent> drainVisualEvents() {
        auto v = std::move(visualEvents_);
        visualEvents_.clear();
        return v;
    }

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
    std::vector<AudioEvent>  audioEvents_;
    std::vector<VisualEvent> visualEvents_;
    int                      playerPrevZ_ = 0;
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
