# Grid Game — Design Document

## Vision

An **automation game** in the Pokémon Gen 4 visual style — and a simulation of thousands to
millions of autonomous agents, each following routines and reacting to stimuli from their
environment. The player is an alchemist-summoner: they record routines, summon golems from raw
materials in the world, and direct those golems to automate resource collection, terrain
modification, and production chains. The long-term goal is a rich, living world that feels
populated and alive.

Agents perceive and react to environmental stimuli mediated by an **alchemy engine** — eight base
elements (Earth, Air, Water, Fire, Curse, Goo, Holy, Acid) that combine Doodle-God-style to
produce new materials, effects, and entities. Stimuli are per-tile float fields (fire intensity,
cold, wetness, voltage, wind) that propagate across the tile grid each tick. Agents sample the
tile they occupy to determine their reaction. Stimuli are not entities — they are properties of
space, updated in parallel by the compute backend.

The simulation is designed to eventually run in parallel on the GPU — the agent count
target (millions) makes CPU-serial execution infeasible at scale. The current C++/SDL2
implementation is a **feature prototype** on CPU. Once the feature set is fleshed out
(post-Phase 20), a full redesign targeting GPU compute (Vulkan or similar) will replace
it. The CPU prototype does not need to accommodate GPU constraints — just build features
cleanly and iterate fast.

Development proceeds in two initial phases:

- **2D graphical version** — tile-based SDL2 renderer, the primary development target.
- **Terminal version** — a lightweight ASCII/ANSI renderer for the same simulation,
  useful for rapid iteration and running on headless machines.

The core simulation (agents, scheduling, spatial queries, collision, events) is
renderer-agnostic and shared between both frontends.

Agent behaviour is driven by **routines** — authored programs that agents execute
instruction by instruction. A routine is not a high-level goal ("find food") but a
precise sequence of actions ("go right, go forward, if fire > 0.5 go back, loop").
**Golems** execute routines recorded directly by the player. Other agent types (NPCs,
enemies) run authored routines of greater complexity, but all use the same bytecode
format and VM. Thousands of agents run the same routine in parallel on the GPU, each
with their own program counter and local state. Routines can call subroutines, enabling
complex behaviour to be built from reusable components.

## Overview

A 2.5D tile-based game with smooth visual movement, a multi-world system, a movement
recording/playback mechanic, and a vertical terrain system. Written in C++ with SDL2.

Rendering uses a **one-point perspective projection** (Pokémon Gen 4 style): the
camera looks mostly downward with a slight southward tilt. Vertical world lines
(cliff and building edges) converge toward a single vanishing point well below the
screen. This gives correct parallax scrolling, natural east/west cliff face
visibility, and accurate perspective scaling without a full 3D engine.

---

## Core Principles

- **Game logic runs on integer tile positions.** `TilePos` is the source of truth for
  all simulation: collision, spatial queries, scheduling, recordings.
- **Rendering interpolates.** `Vec2f` exists only to smooth visual movement between
  ticks. It has no effect on game logic.
- **Fixed timestep.** The simulation advances in discrete ticks at a fixed rate.
  Rendering runs as fast as possible and interpolates between ticks.
- **Multiple grids, all ticking.** Entities exist in exactly one `Grid` at a time.
  All non-paused grids tick every frame. Only the active grid is rendered. Grids are
  independent simulations (main world, studio, rooms, parallel universes).
- **Tile hard cap.** A maximum of 8 entities may occupy the same tile simultaneously.
  A move that would exceed this cap is blocked.
- **Three occupancy layers, independently managed.** Terrain type and stimulus fields
  are properties of tiles. Entities are a separate system. All three coexist freely.
- **No inventory.** The player carries only mana. All resources exist as world tiles or entities.
- **No weapons.** The player never deals damage directly. Golems, terrain, and environmental
  effects are the only sources of damage.
- **Player is the programmer.** Skill expression is in routine design and golem deployment, not
  real-time combat.

---

## Game Design

### Gameplay Loop

```
Collect resources → automate collection → expand territory →
encounter new materials → summon new golem types →
discover unconquerable areas → research alchemy combinations → repeat
```

Progress is gated by **discovery**, not grind. Each new material unlocks a new golem type with
new capabilities. Unconquerable areas require specific golem types or alchemy combinations to
access — creating a natural exploration-and-unlock structure.

### Endgame

The **Philosopher's Stone** is the terminal alchemy combination. Crafting it requires mastering
the full combination tree. Once crafted, it enables the final act: entering the King's fortress
and defeating the King via golems.

### Entity Placeholders

All current `EntityType` names (`Goblin`, `Mushroom`, `Poop`, `Campfire`, `TreeStump`, `Log`,
`Battery`, `Lightbulb`) are temporary placeholders used during development. They will be renamed
and replaced as the golem system and alchemy engine take shape. See `ENTITIES.md` for the master
list.

---

## Data Types

```cpp
struct TilePos { int x, y, z; };        // integer grid coordinate — game logic only
                                         // z = integer height level; 0 ≈ sea level
struct Vec2f   { float x, y; };         // float render position — rendering only
struct Bounds  { Vec2f min, max; };     // AABB hitbox in world space (XY only)
struct Camera  { Vec2f pos; Vec2f target; float zoom; };  // smooth camera state

using EntityID    = uint32_t;
using GridID      = uint32_t;
using RecordingID = uint32_t;
using Tick        = uint64_t;

enum class EntityType {
    Player, Goblin, Mushroom, Poop,
    Campfire,    // static; spreads fire stimulus to adjacent tiles
    TreeStump,   // burnable; ignites after fire exposure, despawns when burned
    Log,         // burnable; same as TreeStump
    Battery,     // static; emits 5V into adjacent Puddle tiles (BFS)
    Lightbulb,   // static; glows when its tile has ≥1V
};
enum class Direction  { N, NE, E, SE, S, SW, W, NW };
enum class ActionType { Move, Spawn, Despawn, ChangeMana, Dig, Plant, Summon };
enum class EventType  { Arrived, Collided, Despawned };

enum class TileType  { Grass, BareEarth, Portal, Fire, Puddle };

// AudioSystem-internal playback enum (audio.hpp):
enum class SFX {
    Step, Dig, Plant, CollectMushroom,
    RecordStart, RecordStop, DeployAgent,
    PortalCreate, PortalEnter, GridSwitch,
    GoblinHit, AgentStep,
};

enum class MusicLayer {
    WorldCalm,      // open world base ambience
    GoblinTension,  // rises with goblin proximity on screen
    Studio,         // inside the studio grid
    RoomInterior,   // inside any bounded room grid
};

// Game-side event enum (game.hpp) — Game emits these; main.cpp maps them to SFX:
enum class AudioEvent {
    PlayerStep, Dig, Plant, CollectMushroom,
    RecordStart, RecordStop, DeployAgent,
    PortalCreate, PortalEnter, GridSwitch,
    GoblinHit, AgentStep,
};

enum class VisualEventType {
    Dig, CollectMushroom, DeployAgent,
    GoblinHit, GoblinDie, PlayerLand,
    PortalEnter, GridSwitch,
};

struct VisualEvent {
    VisualEventType type;
    Vec2f           pos;
    float           z;
    EntityID        entityID   = 0;
    EntityType      entityType = EntityType::Player;
};

using RoutineID = uint32_t;

struct AgentExecState {
    uint32_t pc;
    uint32_t waitTicks;
    // Future: call stack for CALL/RET (depth 8, GPU-safe fixed size)
    // uint32_t callStack[8]; uint8_t callDepth;
};

// Stimulus conditions testable by JUMP_IF / JUMP_IF_NOT (defined in routine.hpp):
enum class Condition : uint8_t { None, Fire, Wet, EntityAhead, AtEdge };
// Note: Wet = stimulus (tile property); Water = fluid (separate dynamics system).

constexpr int TILE_ENTITY_CAP = 8;

// Full opcode set (MOVE_REL/WAIT/HALT implemented; rest planned for Phase 13):
enum class OpCode : uint8_t {
    MOVE_REL, WAIT, HALT,
    DIG, PLANT,
    JUMP, JUMP_IF, JUMP_IF_NOT,
    CALL, RET,
};

// Flat instruction struct — all fields always present, unused fields zero.
// Mapping: MOVE_REL→dir | WAIT→ticks | DIG/PLANT→(none) |
//          JUMP/CALL→addr | JUMP_IF/JUMP_IF_NOT→addr+cond+threshold | RET/HALT→(none)
struct Instruction {
    OpCode    op        = OpCode::HALT;
    RelDir    dir       = RelDir::Forward;
    uint16_t  ticks     = 0;
    uint16_t  addr      = 0;
    Condition cond      = Condition::None;
    uint8_t   threshold = 0;
};
```

---

## Game Loop

Fixed timestep with uncapped render rate and sub-tick interpolation.

```
TICK_RATE = 50 Hz  (20ms per tick)

each frame:
  dt = time since last frame (capped at 100ms)
  accumulator += dt

  while accumulator >= TICK_DT:
    input.snapshot()
    game.tick(currentTick++)
    accumulator -= TICK_DT

  alpha = accumulator / TICK_DT        // 0.0 → 1.0, sub-tick position
  renderer.draw(game, alpha)
  audio.update(dt)
```

---

## Systems

### Input

`Input` snapshots both keyboard and gamepad state each tick. Provides:
- `bool held(Action)`     — action is currently active (keyboard or gamepad)
- `bool pressed(Action)`  — action went active this tick (not on repeat)
- `bool released(Action)` — action went inactive this tick
- `int  scroll()`         — mouse wheel delta this frame (positive = up)
- `bool hasGamepad()`     — true when a controller is connected

**Action enum** — logical intent, not physical key:
`MoveUp MoveDown MoveLeft MoveRight Strafe Dig Plant PlacePortal Record CycleRecording Deploy SwitchGrid PanUp PanDown PanLeft PanRight ResetCamera ZoomModifier Quit Confirm ToggleControls ToggleRecordings ToggleRebind`

**InputMap** — `unordered_map<Action, SDL_Keycode>`. `InputMap::defaults()` returns the
standard WASD layout. `save(path)` / `load(path)` persist bindings as plain text
(`ActionName=KeyName` per line, `SDL_GetKeyName` format). Loaded from `settings.dat`
on startup; saved on quit alongside `save.dat`.

**GamepadMap** — `unordered_map<Action, GamepadBinding>`. Each binding is a button, a
positive-axis threshold, or a negative-axis threshold. Default layout: D-pad for
movement, face buttons for actions, right stick for camera pan. Left stick
unconditionally drives movement regardless of bindings.

**Rebind panel** (`K`): in-game panel listing all 23 actions with their current key.
Navigate with arrow keys; press Enter to capture the next keypress for the selected
action. Changes saved to `settings.dat` when the panel is closed.

No raw events leak into the game logic. Text input (rename mode) is handled
separately in `main.cpp` via `SDL_TEXTINPUT` events, bypassing the Input snapshot.
Rebind capture mode similarly intercepts `SDL_KEYDOWN` before it reaches `Input`.

---

### Entity & EntityRegistry

```cpp
struct Entity {
    EntityID   id;
    EntityType type;

    TilePos    pos;          // current logical tile
    TilePos    destination;  // target tile (== pos when idle)
    float      moveT;        // 0.0→1.0 progress toward destination (render only)

    Vec2f      size;         // hitbox size in tile units
    float      speed;        // moveT progress added per tick
    Direction  facing;
    int        layer;        // draw order (lower = drawn first)
    int        mana;
    int        health;

    // Stimulus-response flags (set each tick by tickFire / tickVoltage):
    bool       lit         = false;  // Lightbulb: true when tile has ≥1V
    bool       burning     = false;  // TreeStump/Log: true while in entityBurnEnd
    bool       electrified = false;  // any entity: true while on a charged Puddle
};
```

`EntityRegistry` owns all entities across all grids, keyed by `EntityID`. Each
`Grid` holds only a list of `EntityID`s that belong to it.

Movement state: while moving, the entity is registered in `SpatialGrid` at **both**
its `pos` and `destination`. On arrival it is removed from `pos`; `destination`
becomes the new `pos`.

---

### Grid & World

```cpp
struct Portal { GridID targetGrid; TilePos targetPos; };

class Grid {
    GridID   id;
    int      width, height;   // 0 = unbounded (infinite world)
    bool     paused = false;

    unordered_map<TilePos, Portal> portals;
    Terrain     terrain;
    SpatialGrid spatial;
    Scheduler   scheduler;
    EventBus    events;
    vector<EntityID> entities;
};

class Game {
    unordered_map<GridID, Grid> grids_;
    EntityRegistry              registry_;
    Recorder                    recorder_;
    GridID                      activeGridID_;
    GridID                      nextGridID_;      // monotonically increasing
    optional<PendingTransfer>   pendingTransfer_; // applied between ticks
};
```

**Grid IDs:**
- `GRID_WORLD  = 1` — the main infinite open world
- `GRID_STUDIO = 2` — blank bounded studio for recording/testing
- `GRID_DYN_START = 3` — dynamic room grids start here

**All non-paused grids tick every frame.** Only the active grid is rendered.
Player input is delivered only to the active grid's tick.

**Passive grid simulation (Phase 18):** Interior grids (buildings, rooms) are small
and their agents run looping routines. Rather than ticking them every frame, inactive
grids operate in *passive mode*.

**Trigger:** no player in grid → dehydrate. Player enters → hydrate. No distance budget,
no manual flag — presence is the only criterion.

**Dehydration (player leaves):** analyse all running routines to determine cycle length and
outputs (`ProduceMana`, `HarvestMushroom`, `SpawnEntity`, `DigTile`); pre-schedule outputs
into the grid's `Scheduler` for the next N cycles; set `passive = true`; skip in tick loop.

**Hydration (player enters):** cancel pending passive output events; snap each agent to its
correct position via `cycle_pos = (now - cycleStartTick) % cycleLength`; set `passive = false`;
resume full simulation.

**Guarantee:** outputs are exact — identical to what full simulation would have produced.
Reuses the existing per-grid `Scheduler`; no new infrastructure needed.

This keeps per-frame cost proportional to active grids (usually 1–2), not total grid count.

**Bounded rooms** (`width > 0 && height > 0`): tiles outside `[0,w)×[0,h)` are
void. Entities and the player are clamped to bounds.

**Portals:** each `Grid` has a map from `TilePos` to `Portal`. When any entity
arrives on a portal tile, a `PendingTransfer` is queued. It is applied at the
**start of the next tick** (never mid-loop) to avoid iterator invalidation.
Tab (world↔studio) transfers happen immediately within `tickPlayerInput`.

**Creating a room (Key::O):** places a `Portal` tile at the tile ahead of the
player in the current grid, creates a new bounded `20×20` grid, places a return
portal at the room's centre. Return destination is the forward portal tile itself
(safe because portal detection fires only on movement arrival, not on placement).

**Active grid ID is captured before the tick loop** so that `tickPlayerInput`
switching grids mid-loop cannot cause it to fire twice.

---

### SpatialGrid

Hash map from `TilePos` to a fixed-size array of up to `TILE_ENTITY_CAP` (8)
`EntityID`s. With verticality, keys include the z-coordinate.

Entities in motion register in **both** `pos` and `destination` simultaneously.
Multi-tile entities register in all cells their bounds overlap.

---

### Collision Detection

Two-phase:

**Broad phase** — `SpatialGrid.query(AABB)` → candidate `EntityID` list.

**Narrow phase** — AABB intersection test between mover's hitbox and each candidate.

**Collision resolution** by `(mover type, occupant type)`:

```
          │ Player   Goblin   Mushroom  Poop
──────────┼──────────────────────────────────
Player    │  —       Block*   Collect   Pass   *bump combat: push + damage
Goblin    │ Combat   Block    Pass      Pass
Poop      │  Pass    Hit      Pass      Pass
```

*Table covers Phase 8 entity types only. Collision rules for golem types added in Phase 12 — TBD.*

**Swap prevention**: all movement intentions collected first, then resolved
together. A→B and B→A in the same tick → both blocked.

---

### Scheduler

Min-heap of `ScheduledAction`s ordered by `tick`. Each tick pops all due actions.
O(log n) insert and pop. Actions are immutable after insertion.

---

### Events

`EventBus` per grid. Events are queued during the tick and flushed at end, so
handlers always run on consistent state.

Mushroom collection is handled via the `Arrived` event: when the player arrives
on a tile occupied by a `Mushroom`, the mushroom is collected and an
`AudioEvent::CollectMushroom` is pushed. Each grid subscribes its own handler on
creation/load.

---

### Terrain

```cpp
class Terrain {
    FastNoiseLite noise;
    unordered_map<TilePos, float>    heightCache;   // Perlin, lazy-computed; z ignored (keyed at z=0)
    unordered_map<TilePos, TileType> typeOverrides;  // sparse; default = Grass
};
```

`heightAt(TilePos)` returns Perlin float in `[-1, 1]`, cached per (x, y) tile. Used by the renderer for visual shading only.

`levelAt(TilePos)` returns `round(heightAt * 4)` as an integer. This is the authoritative height for movement: a move is blocked if `|levelAt(dest) - levelAt(src)| > 1`. Bounded rooms have flat floors (no Perlin noise driving their height), so the check never fires there.

`typeAt(TilePos)` checks overrides, defaults to `Grass`.

---

### Stimuli

**Design decision:** stimuli are generic `StimulusField { type, intensity, decay }` stored in a
sparse `unordered_map<TilePos, StimulusField>` per `Grid`. Spread and decay are one generic pass.
Adding a new stimulus type = new enum value; no new tick code.

**Wet vs Water:** `Wet` is a stimulus (a tile property set by any fluid source — water, rain, etc.)
that agents can sense via `JUMP_IF Wet`. `Water` is a separate fluid entity with its own dynamics
(full fluid simulation deferred; design TBD). A water tile sets the `Wet` stimulus on itself and
adjacent tiles.

### Alchemy Engine

The long-term replacement and expansion of the current fire/voltage stimulus system. All
environmental interactions are elements of the alchemy system. See `ALCHEMY.md` for the master
element list, combination table, and stimulus field definitions.

**Spreading effects** (Fire, Electricity, Cold, Wet, Wind, …) propagate across tiles each tick
via a **spread equation** — a per-tile computation that takes the effect's underlying quantities
as inputs (e.g. Fire depends on heat and wetness; Electricity depends on voltage and conductivity).
Agents sense effects via `JUMP_IF` / `JUMP_IF_NOT`. At scale, spread equations are the GPU
compute kernel. See `ALCHEMY.md` for the full effect and quantity list.

**Combinations** follow a Doodle-God-style discovery model: combining two elements or materials
produces a new one. The **Philosopher's Stone** is the terminal combination.

*Current implementation (pre-alchemy-engine):* fire and voltage are two hardcoded stimulus
systems. These will be subsumed by the alchemy engine in Phase 14+. See Fire & Voltage Stimuli
below.

---

**Current implementation** (pre-StimulusField refactor — two hardcoded systems):

### Fire & Voltage Stimuli

Two stimulus systems are implemented as sparse maps on `Grid`. Both run each tick
before entity AI and set flags on affected entities, which the renderer reads to
apply visual effects.

**Fire (`tickFire`):**
- `tileFireExp`: maps `TilePos → Tick` — the first tick a tile was exposed to fire.
  `Campfire` entities write to this for all 8 surrounding tiles each tick.
- `fireTileExpiry`: maps `TilePos → Tick` — when a `Fire` tile reverts to `BareEarth`.
  A `Grass` tile with ≥50 ticks exposure is converted to `Fire`; `Fire` expires
  after a further 500 ticks.
- `entityFireExp`: maps `EntityID → Tick` — first tick a burnable entity was exposed.
  `TreeStump` and `Log` ignite (`burning = true`) after 250 ticks; `entityBurnEnd`
  records when they will despawn (ignition + 500 ticks).

**Voltage (`tickVoltage`):**
- `voltage`: BFS-computed map from `TilePos → int`. `Battery` entities seed the BFS
  with 5V; each hop through a `Puddle` tile decrements by 1. Non-Puddle tiles block
  propagation. Recomputed from scratch each tick.
- `Lightbulb` entities set `lit = true` when their tile has ≥1V.
- All other entities set `electrified = true` when on a Puddle tile with ≥1V.

---

### Routine VM

Routines are programs. Agents are threads. The GPU kernel is the interpreter.

Each agent holds an `AgentExecState` — a program counter and wait counter.
One instruction executes per tick.

**Implemented opcodes:**
```
MOVE_REL  → move one tile relative to agent facing (dir = RelDir: Forward/Back/Left/Right)
WAIT      → pause for ticks ticks
HALT      → agent despawns
```

**Planned opcodes (Phase 13):**
```
DIG             → dig tile in facing direction
PLANT           → plant mushroom ahead if BareEarth
JUMP addr       → unconditional jump to PC addr
JUMP_IF    cond threshold addr → jump if stimulus[cond] > threshold
JUMP_IF_NOT cond threshold addr → jump if stimulus[cond] <= threshold
CALL addr       → push return addr, jump (call stack depth 8)
RET             → pop call stack, return to caller
```

**Instruction format:** flat struct — all fields always present, unused fields default to zero.
Chosen over a tagged union for ease of serialisation and studio editor access. See `routine.hpp`.

---

### Golem System

Golems are summoned by facing a tile containing a summoning medium and pressing the summon key.
Summoning is a world interaction verb — like hoeing or planting, it can be performed by the
player or by other entities with the right capability.

Eight mediums yield eight golem types. See `ENTITIES.md` for the full capability table.

All golems execute player-recorded routines via the VM. Golems are the only agents that deal
damage (Iron Golem is the primary combat unit). Summoning costs mana; the cost scales with
golem tier.

**Vocalizations:** each golem type emits a Simlish-style vocalization on summon (short phoneme
sequence, unique per type).

---

### Camera

Owned by `main.cpp`. Updated every render frame (not every game tick).

```cpp
struct Camera { Vec2f pos; Vec2f target; float zoom; float z; float targetZ; };
Vec2f camOffset;  // manual pan offset added on top of player position
```

**Tracking:** `camera.target = playerRenderPos + camOffset`. Exponential lerp:
`camera.pos += (target - pos) * (1 - exp(-CAM_LERP * dt))`.

**Grid switch snap:** `game.consumeGridSwitch()` returns true on the frame a grid
switch happens. Camera pos and target snap immediately to the new player position
to avoid lerp artefacts.

**Controls:**
- Arrow keys: pan `camOffset` (tile units/sec)
- Backspace: reset `camOffset` to zero
- Ctrl + scroll: zoom `camera.zoom` (clamped `[0.25, 4.0]`)

---

### Renderer

Abstracted behind `IRenderer`. Two implementations:

- **`Renderer`** (SDL2) — primary target; sprite blitting, tile shading, UI overlay
- **`TerminalRenderer`** — ANSI/ASCII; no SDL dependency

**Projection (one-point perspective, Pokémon Gen 4 style):**

Vertical world lines converge to a vanishing point at `(screen_cx, screen_cy + Z_PERSP*Z_STEP)`,
i.e. `Z_PERSP` z-levels below the camera's ground reference — well below the visible screen.

The perspective scale factor for a tile at height `tileZ`:
```
f = 1 + (tileZ - cam.z) / Z_PERSP

screen_x = VIEWPORT_W/2 + (tile_x - cam.x) * TILE_SIZE * zoom * f
screen_y = VIEWPORT_H/2 + (tile_y - cam.y) * TILE_H * zoom
                         - (tile_z - cam.z) * Z_STEP  * zoom * f
```
Constants: `TILE_SIZE=32`, `TILE_H=20`, `Z_STEP=12`, `Z_PERSP=30` (unzoomed).

Key properties:
- **Parallax**: tiles at higher z scroll faster when the camera moves (by factor `f`)
- **East/west faces**: at any cliff edge, the higher tile's apparent width (`TILE_SIZE * f`)
  is wider than at the lower level, creating a natural gap — east face visible left of
  screen centre, west face right of centre
- **Ground plane**: rendered orthographically (`f ≈ 1` for tiles near `cam.z`)

Entity render z interpolates `pos.z → destination.z` via `moveT`. Draw order: sort by
`world_y` ascending (back-to-front), then by `world_z` ascending within the same row.

**Bounded grid void:** tiles outside `[0,w)×[0,h)` render as dark void `(18,18,18)`.

**Terrain colour:**
- Grass: flat checkerboard `(0,134,0)` / `(0,120,0)` — no Perlin shading
- BareEarth: flat brown `(139, 90, 43)`
- Portal: purple `(160, 60, 220)` with pulsing shimmer each frame
- Fire: orange-red `(220, 80, 20)` with per-frame flicker
- Puddle: blue-grey with ripple animation driven by `rendererTick_`
- Studio mode: muted blue-grey palette, height-varied (uses raw Perlin float)

**UI panels:**
- Controls (H): static keybinding reference
- Recordings (I): recording list with rename; mutually exclusive with H
- Rebind (K): all 23 actions with current key; navigate and capture to remap

**Facing indicator:** small filled triangle (SDL_RenderGeometry) at the edge of
each non-Mushroom entity's tile, pointing in `facing` direction.

**UI (SDL_ttf, font: DejaVuSansMono 13pt):**
- HUD (always visible, top-left): mana counter `♦ N`, recording indicator `● REC`
- Controls panel (H): keybinding reference, top-right
- Recordings panel (I): recording list with rename; mutually exclusive with H panel
  - Enter: start rename (SDL_TEXTINPUT mode); text input bypasses Input snapshot
  - While renaming, empty Input passed to game.tick() to suppress game actions

---

### UI Layer (planned — Phase 16)

**Problem with current approach:** each panel (`drawControlsMenu`, `drawRecordingsPanel`,
`drawRebindPanel`) is a flat function in `renderer.cpp` with hardcoded pixel constants, no text
caching, and no shared layout logic. Panel visibility is loose booleans in `main.cpp`. Phase 15
needs ~5 interactive studio panels — this pattern won't scale.

**Design:**

```
TextCache
  unordered_map<(string, color), SDL_Texture*>
  get(text, color) → texture (created on first use)
  clear()          → invalidate all (e.g. on font change)

Rect { int x, y, w, h }
  contains(px, py) → bool   // hit testing

Panel { Rect bounds; RGBA bg; RGBA border }
  draw(sdl)                  // filled rect + border stroke

Label { Rect bounds; string text; SDL_Color color; Align align }
  draw(sdl, cache)

ListWidget { Rect bounds; vector<string> items; int selected; int scrollOffset }
  draw(sdl, cache)            // renders visible rows, highlights selected
  itemAt(screenY) → int       // which item index is under a y coordinate (-1 = none)
  scrollTo(index)             // ensure item is visible

Button { Rect bounds; string label; bool hovered; bool pressed }
  draw(sdl, cache)
```

**Panel state** — `UIState` owned by `main.cpp`:
```cpp
enum class ActivePanel { None, Controls, Recordings, Rebind, Studio };
struct UIState { ActivePanel active = ActivePanel::None; };
```
Input routing: keyboard/mouse events go to `active` panel first; fall through to game if unconsumed.

**Mouse:** panels absorb clicks via `Rect::contains`. World click only fires if no panel hit.

### Audio

```cpp
class AudioSystem {
    // Channel layout (SDL2_mixer):
    //   0–7  : SFX (group 0, 8 concurrent)
    //   8–11 : music layers (looping, volume-faded)
    //   12   : idle ambient (one-shot)
};
```

**SFX:** one-shot playback on the next free SFX channel. If all 8 are busy, oldest
is stolen. `Game` accumulates `AudioEvent`s during `tick()`; `main.cpp` drains them
each render frame and calls `audio.playSFX()`.

**Music layers:** four looping tracks started at volume 0 at startup. Each frame,
`main.cpp` computes layer targets from game state and calls `setLayerTarget()`.
`update(dt)` smoothly fades each layer's actual volume toward its target (1 vol
unit/sec). Targets:
- `WorldCalm` → 1.0 when in the open world, 0.0 in studio/rooms
- `GoblinTension` → `min(1.0, goblin_count_on_screen / 3.0)`
- `Studio` → 1.0 when in studio grid
- `RoomInterior` → 1.0 when in any bounded room grid

**Idle ambience (Minecraft-style):** when all layer volumes are below 0.05 for 30
seconds, a random ambient track is selected and faded in (0.25 vol/sec). Fades out
when music activity resumes. Plays once, not looping.

**Asset format:** WAV (placeholder); any format supported by `Mix_LoadWAV` can be
dropped in as a replacement (OGG with SDL2_mixer OGG support).

---

### Visual Events & Effects

Parallel to `AudioEvent`, `Game::tick()` accumulates `VisualEvent`s that `main.cpp`
drains each render frame and routes to the `Renderer`.

**Events and their renderer effects:**
| Event | Effect |
|---|---|
| `Dig` | Brown dirt particle burst at tile |
| `CollectMushroom` | Yellow sparkle burst |
| `DeployAgent` | Purple/blue puff burst |
| `GoblinHit` | Red entity flash + camera shake |
| `GoblinDie` | Death-fade sprite + red burst + shake |
| `PlayerLand` | Small dust burst |
| `PortalEnter` | Black fade-to in/out |
| `GridSwitch` | Camera shake |

**Renderer effect state** (all renderer-side, game logic unaffected):
- `Particle` pool — `{pos, vel, z, life, maxLife, size, RGBA}`; ticked each frame
- `EntityFlash` map — per-entity tint for N renderer ticks (hit flash)
- `DyingEntity` list — fading sprite rendered after entity is destroyed
- `shakeAmt_` — decays exponentially each frame; added to all `toPixelX/Y` calls
- `fadeAlpha_` / `fadeDelta_` — full-screen black overlay for portal/grid transitions
- `dayNightT_` — slow sinusoidal counter driving ambient terrain colour shift
- Per-entity persistent effects: `burning` → pulsing orange overlay + rising sparks;
  `electrified` → flickering cyan overlay + random electric sparks

**`effects.hpp`** — SDL-free header containing `AnimFrame`, `Animation` (with
`frameAt()`), `AnimState`, `RGBA`, `Particle` (with `tick()`), and `shakeDecay()`.
No SDL dependency so these types are usable in unit tests without a display.

---

### Persistence

Binary save format (version 7). Auto-loaded on startup, auto-saved on quit (Esc).

**Versioning policy:** bump version on every layout change; version mismatch = fresh world (no
migration). The CPU save format is a prototype — a full GPU redesign post-Phase 20 will replace
it entirely, so migration code is not worth writing.

**Format:**
```
magic: "GRID" (4 bytes)
version: uint8 = 7
activeGridID: uint32
nextGridID: uint32

grid_count: uint32
for each grid (excluding studio):
    gridID: uint32
    width, height: int32
    terrain override count: uint32
    for each override: TilePos (x,y), TileType
    portal count: uint32
    for each portal: TilePos (x,y), targetGrid, targetPos (x,y)
    entity count: uint32
    for each entity: type, pos(x,y,z), facing

player: pos(x,y,z), facing, mana
playerWorldPos: x, y, z
selectedRecording: uint32
recording count: uint32
for each recording: name (length-prefixed string), instruction count, instructions
```

Version mismatch → load fails silently (no save file = fresh world).

**`settings.dat`** — plain text, `ActionName=KeyName` per line (SDL_GetKeyName format). Not
versioned; malformed lines are skipped. Independent of `save.dat`. Locale preference will also
be stored here (Phase 20).

---

### Height-based movement (Phase 9 — implemented)

`TilePos` carries an integer `z` coordinate representing the entity's height level.
Terrain height is computed from Perlin noise and quantised: `levelAt(x, y) = round(heightAt * 4)`,
giving levels roughly in the range `[-4, 4]`.

**Movement rule:** a move from `src` to `dest` is blocked if
`|terrain.levelAt(dest) - terrain.levelAt(src)| > 1`. Steps of ±1 level are free.
Bounded rooms (studio, interiors) are always flat — the height check does not apply there.

Entities track their current z in `pos.z` and their destination z in `destination.z`.
On spawn (or first load), `pos.z` is set from `terrain.levelAt(pos)`. On each
permitted step, `destination.z` is set to `terrain.levelAt(destination)` before
`resolveMoves` is called, so the entity's z is always consistent with the terrain.

`SpatialGrid` keys on `TilePos {x, y, z}`, so entities on different z levels at the
same (x, y) tile do not collide.

**Rendering:** entity render height interpolates `pos.z → destination.z` via `moveT`,
producing smooth visual vertical movement. `Camera.z` / `Camera.targetZ` track the
player's interpolated z with the same exponential lerp used for XY, and snap
instantly on grid switch.

Terrain tiles are drawn at their integer z level using the one-point perspective
projection. South cliff faces fill the vertical gap below elevated tiles; east and
west cliff faces emerge naturally from the perspective-scaled width difference between
adjacent tiles at different heights.

---

## Combat

Design decision pending. Three options under consideration:

- **VATS-style slow-time** — player enters a slow-motion targeting mode and queues golem actions
- **Stop-time** — game pauses; player issues orders to each golem; then resumes
- **Turn-based** — full turn structure when enemies are in range

Settled constraints:
- The player never deals damage — golems, terrain, and environmental stimuli are the only damage sources
- The player's role in combat is positioning and routine design, not real-time input

---

## World Interactions

The player and entities interact with the world through a set of action verbs. Capability flags
on each entity type determine which verbs it can perform. See `VERBS.md` for the master verb
list and actor assignments.

---

## Game Mechanics — Controls

Default keyboard bindings (all remappable via K panel or `settings.dat`):

| Key | Action |
|---|---|
| `WASD` | Move player one tile. Updates `facing`. |
| `Shift + WASD` | Strafe (move without updating `facing`). |
| `F` | Dig tile ahead (`terrain.dig()`). |
| `C` | If tile ahead is `BareEarth` and `mana >= 1`: plant mushroom, restore terrain, deduct 1 mana. |
| `R` | Toggle recording. On stop, saves `Recording` with default name "Script N". |
| `Q` | Cycle `selectedRecording`. |
| `E` | Deploy `selectedRecording` as a routine agent spawned ahead of player (currently `Poop` — placeholder, renamed Phase 12). |
| `O` | Create portal tile ahead; spawn linked 20×20 room grid. |
| `Tab` | Toggle between world and studio (direct transfer, no portal). |
| `H` | Toggle controls reference panel. |
| `I` | Toggle recordings panel (replaces H/K panel). |
| `K` | Toggle key rebind panel. |
| `Enter` | (Recordings panel open) Begin rename of selected recording. |
| `Arrow keys` | Pan camera offset. |
| `Backspace` | Re-centre camera (reset pan offset). |
| `Ctrl + Scroll` | Zoom in/out (`[0.25×, 4.0×]`). |
| `Esc` | Save and quit. |

| Collision | Result |
|---|---|
| Player + Mushroom | Player gains 3 mana. Mushroom despawns. |
| Player + Goblin | Bump combat: goblin takes `player.mana` damage, is pushed back. |
| Poop + Goblin | Goblin takes hit. |

---

## File Structure

```
grid_game/
├── DESIGN.md
├── TODO.md
├── CMakeLists.txt
├── save.dat                           ← auto-saved binary state (gitignored)
├── assets/
│   ├── fonts/
│   │   └── DejaVuSansMono.ttf
│   ├── sprites/
│   │   ├── player.png  goblin.png  mushroom.png  poop.png
│   │   ├── campfire.png  treestump.png  log.png
│   │   └── battery.png  lightbulb.png
│   ├── sfx/                           ← one-shot sound effects (WAV/OGG)
│   │   ├── step.wav  dig.wav  plant.wav  collect.wav
│   │   ├── record_start.wav  record_stop.wav  deploy.wav
│   │   ├── portal_create.wav  portal_enter.wav  grid_switch.wav
│   │   ├── goblin_hit.wav  agent_step.wav
│   └── music/                         ← looping layers + idle ambient (WAV/OGG)
│       ├── world_calm.wav  goblin_tension.wav
│       ├── studio.wav  room_interior.wav
│       └── ambient_1.wav  ambient_2.wav  ambient_3.wav
├── vendor/
│   ├── FastNoiseLite.h
│   └── doctest.h
├── tests/
│   ├── test_phase1.cpp  …  test_phase9.cpp
│   ├── test_terminal_renderer.cpp
│   ├── test_fire_voltage.cpp
│   ├── test_phase10.cpp
│   └── test_input_map.cpp
└── src/
    ├── main.cpp
    ├── types.hpp             ← TilePos, Vec2f, Bounds, Camera, enums, lerp, toVec, TilePosHash
    ├── effects.hpp           ← AnimFrame, Animation, AnimState, RGBA, Particle, shakeDecay (SDL-free)
    ├── game.hpp / .cpp       ← Game, tick, AudioEvent + VisualEvent queues, save/load
    ├── entity.hpp / .cpp     ← Entity, EntityRegistry, stepMovement, resolveMoves
    ├── grid.hpp / .cpp       ← Grid, Portal, tickFire, tickVoltage
    ├── terrain.hpp / .cpp    ← Terrain (Perlin + sparse overrides)
    ├── spatial.hpp / .cpp    ← SpatialGrid (entity occupancy, hard cap 8)
    ├── scheduler.hpp / .cpp  ← ScheduledAction, Scheduler (min-heap)
    ├── events.hpp / .cpp     ← Event, EventBus
    ├── routine.hpp           ← Instruction, OpCode, AgentExecState, Recording
    ├── routine_vm.hpp / .cpp ← RoutineVM: step(), move resolution
    ├── recorder.hpp / .cpp   ← Recorder: player actions → Instruction stream
    ├── input.hpp / .cpp      ← Input, InputMap, GamepadMap, Action enum
    ├── audio.hpp / .cpp      ← AudioSystem (SDL2_mixer, SFX + music layers + ambient)
    ├── irenderer.hpp         ← IRenderer interface
    ├── renderer.hpp / .cpp   ← Renderer (SDL2, camera, particles, UI, SDL_ttf)
    └── terminal_renderer.hpp / .cpp ← TerminalRenderer (ANSI/ASCII)
```

---

## Architectural Decisions

Decisions made through conversation; recorded here to avoid revisiting them unnecessarily.

| # | Topic | Decision |
|---|---|---|
| 1 | Entity model | Keep type-driven dispatch + capabilities bitfield for now. Alternatives (component bag, full ECS) not worth the complexity at current entity-type count. Revisit before Phase 17 when entity variety peaks. |
| 2 | Instruction format | Flat struct — all fields always present, unused fields zero. Chosen over tagged union for ease of serialisation and studio editor field access. Implemented in `routine.hpp`. |
| 3 | Stimulus abstraction | Generic `StimulusField { type, intensity, decay }` per tile. `Wet` is a stimulus (any fluid source sets it). `Water` is a fluid entity with its own dynamics (deferred). VM checks `Wet`, not `Water`. |
| 4 | Multi-grid scaling | Hibernation: no player in grid → analyse routines, pre-schedule outputs, hibernate. Player enters → cancel outputs, snap agents, resume. Exact (no approximation). Reuses existing Scheduler. |
| 5 | GPU goal | CPU simulation is a feature prototype. Full GPU redesign (Vulkan compute or similar) post-Phase 20. Struct-of-arrays layout, fixed-size slots, parallel tick logic. No incremental migration — full rewrite. |
| 6 | Save format versioning | Bump version on every layout change; mismatch = fresh world. No migration code. Format will be replaced in GPU rewrite. |

---

## Platform

- **Input** — keyboard, gamepad, and touchscreen are co-equal input methods. All interactions
  must be accessible via any of them.
- **Target platforms** — Windows, macOS, Linux, iOS, Android, and major consoles (Switch,
  PlayStation, Xbox). Exact porting order TBD.
- **Split-screen multiplayer (near-term)** — two players share one screen, each with their own
  camera viewport and controls; golems are shared. Design TBD.
- **Online multiplayer (far future)** — shared world, separate cameras, state sync via
  authoritative server. Not planned until after the GPU rewrite.

---

## Libraries

| Library | Purpose | Integration |
|---|---|---|
| SDL2 | Window, input, 2D rendering | `find_package(SDL2)` |
| SDL2_image | PNG sprite loading | `find_package(SDL2_image)` |
| SDL2_ttf | Font rendering for UI overlay | `pkg_check_modules(SDL2_TTF ...)` |
| SDL2_mixer | SFX and music playback | `pkg_check_modules(SDL2_MIXER ...)` |
| FastNoiseLite | Perlin noise (single header) | `vendor/FastNoiseLite.h` |
| doctest | Unit test framework (single header) | `vendor/doctest.h` |

Build system: CMake. C++23.
