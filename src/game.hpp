#pragma once

#include "types.hpp"
#include "entity.hpp"
#include "field.hpp"
#include "input.hpp"
#include "recorder.hpp"
#include "routine_vm.hpp"
#include "effectSpread.hpp"
#include "fluid.hpp"
#include "alchemy.hpp"
#include "component_store.hpp"
#include <unordered_map>
#include <string>
#include <utility>
#include <vector>
#include <random>

// ─── PlayerAction ────────────────────────────────────────────────────────────
//
// The set of actions the player can cycle through with Z and execute with E.
// Individual shortcut keys still work alongside the cycle system.

enum class PlayerAction {
    Dig, Plant, Scythe, Mine, Summon, PlacePortal, PickUp, Drop, Hit
};

inline constexpr int PLAYER_ACTION_COUNT = 9;

inline const char* playerActionName(PlayerAction a) {
    switch (a) {
        case PlayerAction::Dig:        return "Dig";
        case PlayerAction::Plant:      return "Plant";
        case PlayerAction::Scythe:     return "Scythe";
        case PlayerAction::Mine:       return "Mine";
        case PlayerAction::Summon:     return "Summon";
        case PlayerAction::PlacePortal:return "Portal";
        case PlayerAction::PickUp:     return "PickUp";
        case PlayerAction::Drop:       return "Drop";
        case PlayerAction::Hit:        return "Hit";
    }
    return "?";
}

// ─── AudioEvent ───────────────────────────────────────────────────────────────
//
// Game pushes these during tick(); main.cpp drains them each frame and plays
// the corresponding SFX via AudioSystem.

enum class AudioEvent {
    PlayerStep, Dig, Plant, CollectMushroom,
    RecordStart, RecordStop, Summon,
    PortalCreate, PortalEnter, FieldSwitch,
    GoblinHit, AgentStep,
};

// ─── VisualEvent ──────────────────────────────────────────────────────────────
//
// Parallel to AudioEvent: Game emits these; the renderer drains them each
// frame and spawns particles / triggers screen-level effects accordingly.

enum class VisualEventType {
    Dig, CollectMushroom, Summon,
    GoblinHit, GoblinDie, PlayerLand,
    PortalEnter, FieldSwitch,
};

struct VisualEvent {
    VisualEventType type;
    Vec2f           pos;                              // world-space origin (tile units)
    float           z          = 0.0f;               // world z at that position
    EntityID        entityID   = INVALID_ENTITY;     // for flash effects (GoblinHit)
    EntityType      entityType = EntityType::Player; // for dying-entity (GoblinDie)
};

// ─── SummonPreview ───────────────────────────────────────────────────────────
//
// Describes what the player would summon if they pressed the Summon key.
// active = false when the player is not facing a medium tile.

struct SummonPreview {
    bool        active    = false;
    EntityType  golemType = EntityType::MudGolem;
    std::string golemName;
    int         manaCost  = 0;
    bool        canAfford = false;
};

// ─── RoutineInfo ─────────────────────────────────────────────────────────────

struct RoutineInfo {
    size_t      index;
    std::string name;
    int         steps;
    int         manaCost;
    bool        selected;
};

// ─── RabbitSlot / WarrenData ──────────────────────────────────────────────────

struct RabbitSlot {
    EntityID warrenEid;     // warren entity in FIELD_WORLD
    FieldID  warrenFieldID; // warren interior field
    Tick     emergedAt = 0; // tick when rabbit last emerged to world (for cooldown)
};

struct WarrenData {
    FieldID  fieldID;   // interior field
    TilePos  worldPos;  // warren entity's world tile (portal entry point)
};

// Rabbit satiety constants (using entity.mana as the satiety value).
inline constexpr int RABBIT_MANA_MAX    = 20;
inline constexpr int RABBIT_MANA_FULL   = 16;  // return to warren above this
inline constexpr int RABBIT_MANA_HUNGRY =  8;  // emerge from warren below this
inline constexpr int RABBIT_EAT_GAIN    =  6;  // mana gained per LongGrass eaten
inline constexpr int RABBIT_DECAY_RATE  = 500; // ticks between mana decay steps (~10 s/mana)

// Warren interior size.
inline constexpr int WARREN_W = 8;
inline constexpr int WARREN_H = 8;

// ─── AgentSlot ────────────────────────────────────────────────────────────────
//
// Bundles per-agent VM execution state with its assigned routine.
// Stored in Game::agentSlots_, keyed by EntityID.

struct AgentSlot {
    AgentExecState state;
    Routine        routine;
};

// ─── Field ID constants ───────────────────────────────────────────────────────

constexpr FieldID FIELD_WORLD     = 1;
constexpr FieldID FIELD_STUDIO    = 2;
constexpr FieldID FIELD_DYN_START = 3;   // dynamic fields start here

// ─── Room dimensions ─────────────────────────────────────────────────────────

constexpr int ROOM_W = 20;
constexpr int ROOM_H = 20;

// ─── transferEntity ───────────────────────────────────────────────────────────

void transferEntity(EntityID eid, Field& from, Field& to,
                    EntityRegistry& registry, TilePos dest);

// ─── Game ─────────────────────────────────────────────────────────────────────

class Game {
public:
    Game();

    void tick(const Input& input, Tick currentTick);

    // Renderer accessors.
    const Terrain& terrain()     const { return activeField().terrain; }
    int            playerMana()  const;
    bool           isRecording() const { return recorder_.isRecording(); }
    bool           inStudio()    const { return activeFieldID_ == FIELD_STUDIO; }

    // Active field bounds for renderer (0 = unbounded).
    std::pair<int,int> activeFieldBounds() const {
        const Field& f = activeField();
        return { f.width, f.height };
    }

    // Player position for camera tracking.
    TilePos playerPos()            const;
    TilePos playerDestination()    const;
    float   playerMoveProgress()   const;

    // Routines panel.
    std::vector<RoutineInfo> routineList() const;
    void renameRoutine(size_t index, const std::string& name);
    void deleteRoutine(size_t index);

    // Routine access (Phase 15).
    size_t         routineCount()      const { return recorder_.routines.size(); }
    const Routine& routine(size_t idx) const { return recorder_.routines.at(idx); }
    size_t         selectedRoutineIdx()const { return selectedRoutine_; }

    // Instruction editing (Phase 15).
    void deleteInstruction(size_t recIdx, size_t instrIdx);
    void insertWait(size_t recIdx, size_t pos, uint16_t ticks);
    void insertMoveRel(size_t recIdx, size_t pos, RelDir dir);
    void reorderInstruction(size_t recIdx, size_t from, size_t to);

    // Active player action (cycle system — Z cycles, E executes).
    PlayerAction activePlayerAction() const { return activeAction_; }

    // HUD summon preview: shown only when active action is Summon.
    SummonPreview playerSummonPreview() const;

    // Mouse interaction (Phase 16).
    // Returns the first entity whose pos matches tile in the active grid.
    const Entity* entityAtTile(TilePos tile) const;
    // Queue a one-step move toward target for the next tick.
    void queueClickMove(TilePos target);

    // Fluid overlay (Phase 17).
    // Returns {pos, h} for every Water entity in the active grid.
    // Passed to Renderer::drawFluidOverlay() each frame.
    std::vector<FluidOverlay> fluidOverlay() const;

    // Persistence.
    void save(const std::string& path) const;
    bool load(const std::string& path);

    // Clears and returns the field-switched flag (used for camera snap).
    bool consumeFieldSwitch() { bool v = fieldJustSwitched_; fieldJustSwitched_ = false; return v; }

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

    // Entities in the active field, sorted by drawOrder.
    std::vector<const Entity*> drawOrder() const;

private:
    // ── Pending cross-field transfer ──────────────────────────────────────────
    struct PendingTransfer {
        EntityID eid;
        FieldID  fromField;
        FieldID  toField;
        TilePos  toPos;
    };
    std::vector<PendingTransfer> pendingTransfers_;

    // ── State ─────────────────────────────────────────────────────────────────
    EntityRegistry                    registry_;
    std::unordered_map<FieldID, Field> fields_;
    FieldID  activeFieldID_  = FIELD_WORLD;
    FieldID  nextFieldID_    = FIELD_DYN_START;
    EntityID playerID_       = INVALID_ENTITY;
    TilePos  playerWorldPos_ = {0, 0};
    bool     fieldJustSwitched_ = false;

    Recorder  recorder_;
    RoutineVM vm_;
    std::vector<AudioEvent>  audioEvents_;
    std::vector<VisualEvent> visualEvents_;
    int                      playerPrevZ_ = 0;
    std::unordered_map<EntityID, AgentSlot> agentSlots_;
    size_t selectedRoutine_ = 0;

    // ECS component stores (Phase 17+)
    ComponentStore<FluidComponent>    fluidComponents_;
    ComponentStore<PrincipleProfile>  principleComponents_;

    // Shared RNG for world simulation (LongGrass spread, etc.)
    std::mt19937 worldRng_;

    // Per-rabbit slot (keyed by EntityID).
    std::unordered_map<EntityID, RabbitSlot> rabbitSlots_;

    // Cooking: tick when each Meat entity first came adjacent to fire.
    // Removed when meat moves away from fire or is converted.
    std::unordered_map<EntityID, Tick> cookingStart_;
    // Per-warren data (keyed by warren EntityID).
    std::unordered_map<EntityID, WarrenData> warrenData_;

    // Action cycle state (Phase 18 extension)
    PlayerAction activeAction_ = PlayerAction::Summon;

    // Pending click-move (set by queueClickMove, consumed in tickPlayerInput).
    TilePos pendingClickDelta_ = {0, 0, 0};
    bool    hasPendingClick_   = false;

    // ── Helpers ───────────────────────────────────────────────────────────────
    Field&       activeField()       { return fields_.at(activeFieldID_); }
    const Field& activeField() const { return fields_.at(activeFieldID_); }

    void subscribeEvents(Field& field);
    void applyPendingTransfer();

    // Per-field tick sub-systems (all fields run these each tick if not paused).
    void tickScheduler(Field& field, Tick currentTick);
    void tickGoblinAI(Field& field, Tick currentTick);
    void tickRabbitAI(Field& field, Tick currentTick);
    void tickRabbitBreeding(Field& field);
    // Creates a warren interior field for the given warren entity.
    // Called from generateChunk immediately after the warren is spawned.
    FieldID createWarrenInterior(EntityID warrenEid, TilePos worldPos);
    // Called when a rabbit entity is destroyed; handles warren cleanup.
    void onRabbitDied(EntityID rabbitEid);
    void tickVM(Field& field);
    void tickMovement(Field& field);
    void tickResponseMovement(Field& field, Tick currentTick);
    void tickCooking(Field& field, Tick currentTick);

    // Player input — only called for the active field.
    void tickPlayerInput(const Input& input);

    // Lazy world generation (Phase 18).
    // Generates any unvisited chunks within 2 chunks of playerPos.
    void maybeGenerateChunks(Field& field, TilePos playerPos);
    // Generates one chunk at chunk coordinates (cx, cy).
    void generateChunk(Field& field, int cx, int cy);
};

// ─── gameStateText ────────────────────────────────────────────────────────────
//
// Returns a structured text description of the current game state: player
// stats, what's ahead, and nearby entities sorted by distance.
// Intended for headless / AI play sessions.

std::string gameStateText(const Game& g);
