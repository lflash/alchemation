# Grid Game — Design Document

## Vision

A simulation of thousands to millions of autonomous agents, each following routines
and reacting to stimulus from their environment and from other agents. The long-term
goal is a 2.5D game in the style of Pokémon — a rich, living world that feels
populated and alive.

Agents perceive and react to environmental stimuli — fire, water, freezing, being
pushed or pulled, and others to be defined. Stimuli are per-tile float fields: fire
intensity, cold, water level, force vector, and so on. They propagate across the
tile grid each tick (fire spreads, cold radiates, water flows, force transfers) and
agents sample the tile they occupy to determine their reaction. Stimuli are not
entities — they are properties of space, updated in parallel by the compute backend.

The simulation is designed to run in parallel on the GPU or integrated graphics —
the agent count target (millions) makes CPU-serial execution infeasible. The
architecture should be as system-agnostic as possible: no vendor-specific GPU APIs,
no assumptions about dedicated hardware. The compute backend (GPU parallelism,
spatial queries, scheduling) must remain decoupled from both the simulation logic
and the renderer so it can be swapped or extended without rewriting either.

Development proceeds in two initial phases:

- **2D graphical version** — tile-based SDL2 renderer, the primary development target.
- **Terminal version** — a lightweight ASCII/ANSI renderer for the same simulation,
  useful for rapid iteration and running on headless machines.

The core simulation (agents, scheduling, spatial queries, collision, events) is
renderer-agnostic and shared between both frontends.

Agent behaviour is driven by **routines** — authored programs that agents execute
instruction by instruction. A routine is not a high-level goal ("find food") but a
precise sequence of actions ("go right, go forward, if fire > 0.5 go back, loop").
The player records routines by playing, then assigns them to agents. Thousands of
agents run the same routine in parallel on the GPU, each with their own program
counter and local state. Routines can call subroutines, enabling complex behaviour
to be built from reusable components.

## Overview

A 2.5D tile-based game with smooth visual movement, a multi-world system, a movement
recording/playback mechanic, and a vertical terrain system. Written in C++ with SDL2.

Rendering uses an **oblique/dimetric projection** (Pokémon Gen 4 style): tiles are
wider than tall on screen, and Z-elevation shifts tiles straight up, giving depth
without a true 3D engine.

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

---

## Data Types

```cpp
struct TilePos { int x, y, z; };        // integer grid coordinate — game logic only
                                         // z = vertical level (0 = ground)
struct Vec2f   { float x, y; };         // float render position — rendering only
struct Bounds  { Vec2f min, max; };     // AABB hitbox in world space (XY only)
struct Camera  { Vec2f pos; Vec2f target; float zoom; };  // smooth camera state

using EntityID    = uint32_t;
using GridID      = uint32_t;
using RecordingID = uint32_t;
using Tick        = uint64_t;

enum class EntityType { Player, Goblin, Mushroom, Poop };
enum class Direction  { N, NE, E, SE, S, SW, W, NW };
enum class ActionType { Move, Spawn, Despawn, ChangeMana, Dig, Plant, Summon };
enum class EventType  { Arrived, Collided, Despawned };

enum class TileType  { Grass, BareEarth, Portal };

// Geometric shape of a tile — determines how entities traverse it vertically.
// Cardinal slopes: SlopeN means walking North ascends by one z-level.
// Corner slopes: SlopeNE has the NE corner raised; passable from all directions.
enum class TileShape { Flat,
                       SlopeN, SlopeS, SlopeE, SlopeW,
                       SlopeNE, SlopeNW, SlopeSE, SlopeSW };

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

enum class AudioEvent {
    PlayerStep, Dig, Plant, CollectMushroom,
    RecordStart, RecordStop, DeployAgent,
    PortalCreate, PortalEnter, GridSwitch,
    GoblinHit, AgentStep,
};

using RoutineID = uint32_t;

enum class OpCode : uint8_t {
    MOVE_REL,      // move one tile relative to facing (forward/back/left/right)
    MOVE_ABS,      // move one tile in absolute direction (N/S/E/W)
    FACE,          // set facing without moving
    WAIT,          // pause for arg0 ticks
    DIG,           // dig tile in facing direction
    PLANT,         // plant mushroom in facing direction (costs 1 mana)
    JUMP,          // unconditional jump to addr
    JUMP_IF,       // jump to addr if stimulus[condition] > threshold
    JUMP_IF_NOT,   // jump to addr if stimulus[condition] <= threshold
    CALL,          // push return address, jump to subroutine
    RET,           // pop call stack, return to caller
    HALT,          // stop executing (agent becomes idle)
};

enum class Condition : uint8_t {
    FIRE, COLD, WATER, FORCE_X, FORCE_Y, ENTITY_AHEAD, AT_EDGE
};

struct Instruction {
    OpCode    op;
    uint8_t   dir;        // Direction (for MOVE_REL, MOVE_ABS, FACE)
    Condition condition;  // stimulus to test (for JUMP_IF / JUMP_IF_NOT)
    float     threshold;  // stimulus threshold
    uint32_t  addr;       // jump target or subroutine RoutineID (for CALL)
};

constexpr int CALL_STACK_DEPTH = 8;

struct AgentExecState {
    RoutineID routineID;
    uint32_t  pc;
    uint32_t  waitTicks;
    uint32_t  callStack[CALL_STACK_DEPTH];
    uint8_t   callDepth;
};

constexpr int TILE_ENTITY_CAP = 8;

struct TileFields {
    TileType  type;
    TileShape shape;     // geometric shape (flat or slope direction)
    float     height;    // Perlin height, render shading only
    float     fire;
    float     cold;
    float     water;
    float     forceX;
    float     forceY;
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

Snapshots key state once per tick. Provides:
- `bool held(Key)`     — key currently down
- `bool pressed(Key)`  — key went down this tick (not on repeat)
- `bool released(Key)` — key went up this tick
- `int  scroll()`      — mouse wheel delta this frame (positive = up)

Keys: `W A S D E F C R Q O H I Tab Shift Escape Enter Backspace`
      `ArrowUp ArrowDown ArrowLeft ArrowRight Ctrl`

No raw events leak into the game logic. Text input (rename mode) is handled
separately in main.cpp via SDL_TEXTINPUT events, bypassing the Input snapshot.

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
    unordered_map<TilePos, float>    heightCache;   // Perlin, lazy-computed
    unordered_map<TilePos, TileType> typeOverrides;  // sparse; default = Grass
    // (TileShape stored separately when verticality is added)
};
```

`heightAt(TilePos)` returns Perlin float in `[-1, 1]`, cached per tile.
`typeAt(TilePos)` checks overrides, defaults to `Grass`.

With verticality, `TilePos` includes `z` and the Terrain becomes a sparse 3D
volume: a tile only exists at `(x, y, z)` if explicitly placed. Most positions
in a column are air.

---

### Routine VM

Routines are programs. Agents are threads. The GPU kernel is the interpreter.

Each agent holds an `AgentExecState` — a program counter, wait counter, and fixed-
depth call stack. One instruction executes per tick.

```
MOVE_REL / MOVE_ABS → sets intended move for the move-resolution pass
FACE                → updates agent facing
WAIT                → decrements waitTicks, skips until zero
DIG / PLANT         → terrain interaction (wired to Terrain::dig/restore)
JUMP                → unconditional jump
JUMP_IF / JUMP_IF_NOT → conditional jump on stimulus threshold
CALL / RET          → subroutine call stack (depth 8, GPU-safe fixed size)
HALT                → agent despawns
```

---

### Camera

Owned by `main.cpp`. Updated every render frame (not every game tick).

```cpp
struct Camera { Vec2f pos; Vec2f target; float zoom; };
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

**Projection (oblique/dimetric, Pokémon Gen 4 style):**
```
screen_x = VIEWPORT_W/2 + (tile_x - cam.x) * TILE_SIZE * zoom
screen_y = VIEWPORT_H/2 + (tile_y - cam.y) * TILE_H * zoom - (tile_z - cam.z) * Z_STEP * zoom
```
`TILE_SIZE=32`, `TILE_H=20`, `Z_STEP=12` (unzoomed). Draw order: sort by `world_y`
ascending (back-to-front), then by `world_z` ascending within the same row.
Elevated tiles show a south-facing cliff strip when the tile to their south is at
a lower z.

**Bounded grid void:** tiles outside `[0,w)×[0,h)` render as dark void `(18,18,18)`.

**Terrain colour:**
- Grass: flat checkerboard `(0,134,0)` / `(0,120,0)` — no Perlin noise, for clear z-level visibility
- BareEarth: flat brown `(139, 90, 43)`
- Portal: purple `(160, 60, 220)`
- Studio mode: muted blue-grey palette, height-varied

**Facing indicator:** small filled triangle (SDL_RenderGeometry) at the edge of
each non-Mushroom entity's tile, pointing in `facing` direction.

**UI (SDL_ttf, font: DejaVuSansMono 13pt):**
- HUD (always visible, top-left): mana counter `♦ N`, recording indicator `● REC`
- Controls panel (H): keybinding reference, top-right
- Recordings panel (I): recording list with rename; mutually exclusive with H panel
  - Enter: start rename (SDL_TEXTINPUT mode); text input bypasses Input snapshot
  - While renaming, empty Input passed to game.tick() to suppress game actions

---

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

### Persistence

Binary save format (version 4). Auto-loaded on startup, auto-saved on quit (Esc).

**Format:**
```
magic: "GRID" (4 bytes)
version: uint32 = 2
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
    for each entity: type, pos(x,y), facing

player: pos(x,y), facing, mana
playerWorldPos: x, y
selectedRecording: uint32
recording count: uint32
for each recording: name (length-prefixed string), instruction count, instructions
```

Version mismatch → load fails silently (no save file = fresh world).

---

### Verticality (Phase 9 — implemented)

The world is a **sparse 3D volume**. Each `(x, y)` column can have surfaces at
multiple z-levels simultaneously — e.g. a bridge at z=2 over a chasm floor at z=0,
with air at z=1.

**Data model:**
- `TilePos { int x, y, z }` — z=0 is ground level
- `TileShape` — `Flat`, four cardinal slopes, four corner slopes (see enum above)
  - `SlopeN`: walking North ascends by one z-level (z+1); descend by walking South
  - Corner slopes (`SlopeNE` etc.): one corner raised; passable from all directions
- `Terrain::shapeAt(TilePos)` — sparse override map; default Flat
- `Terrain::generateSlopes(radius, safeRadius)` — auto-generates slopes from Perlin
  height: cardinal ramp where exactly one cardinal neighbour is high; corner ramp
  where two perpendicular neighbours are high. Safe zone around origin stays flat.
- `Entity::pos` and `Entity::destination` include z
- `SpatialGrid` keys on `TilePos {x, y, z}`

**Movement with slopes — `resolveZ(from, to, terrain)`:**

Only cardinal moves interact with slopes. Given movement direction D:

1. **Ascend**: cardinal slope at `(to.x, to.y, from.z)` whose ascent == D → `z+1`
2. **Descend via dest**: cardinal slope at `(to.x, to.y, from.z-1)` opposing D → `z-1`
3. **Descend off source**: cardinal slope at `(from.x, from.y, from.z-1)` opposing D → `z-1`
4. **All other cases** (perpendicular, back-face, flat, corner slope) → pass through at z unchanged

There is no jump or free vertical movement; only slopes connect z-levels.

**Visual z:** entity render height uses visual z, not logical z. Cardinal slope
occupants render at z=0.5 (mid-ramp) regardless of their logical z, for smooth
visual transitions. Lerped between source and destination visual z each frame.

**Rendering: oblique/dimetric projection**

```
screen_x = VIEWPORT_W/2 + (tile_x - cam.x) * TILE_SIZE * zoom
screen_y = VIEWPORT_H/2 + (tile_y - cam.y) * TILE_H * zoom - (tile_z - cam.z) * Z_STEP * zoom
```

- `TILE_SIZE` = 32px, `TILE_H` = 20px (squished), `Z_STEP` = 12px (all unzoomed)

Draw order (back-to-front): sort by `world_y` ascending, then `world_z` ascending.

**Cliff faces:** when a tile south of an elevated tile is at z=0, draw a vertical
strip (55% darkened) between them on the south face — gives the Pokémon Gen 4 look.

**Sprite/shadow anchor:** sprite bottom sits at tile centre; shadow ellipse is
centred at tile centre. Facing indicator centred on sprite body.

**Camera z:** `Camera` gains `float z`. Tracks player visual z — exponential lerp
each frame, snaps instantly on grid switch.

---

## Game Mechanics — Controls

| Input | Action |
|---|---|
| `WASD` | Move player one tile. Updates `facing`. |
| `Shift + WASD` | Strafe (move without updating `facing`). |
| `F` | Dig tile ahead (`terrain.dig()`). |
| `C` | If tile ahead is `BareEarth` and `mana >= 1`: plant mushroom, restore terrain, deduct 1 mana. |
| `R` | Toggle recording. On stop, saves `Recording` to deque with default name "Script N". |
| `Q` | Cycle `selectedRecording`. |
| `E` | Deploy `selectedRecording` as a `Poop` agent spawned ahead of player. |
| `O` | Create portal tile ahead; spawn linked 20×20 room grid. |
| `Tab` | Toggle between world and studio (direct transfer, no portal). |
| `H` | Toggle controls reference panel. |
| `I` | Toggle recordings panel (replaces H panel). |
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
│   │   ├── player.png
│   │   ├── goblin.png
│   │   ├── mushroom.png
│   │   └── poop.png
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
│   ├── test_phase1.cpp  …  test_phase8.cpp
│   └── test_terminal_renderer.cpp
└── src/
    ├── main.cpp
    ├── types.hpp          ← TilePos, Vec2f, Bounds, Camera, enums, lerp, toVec, TilePosHash
    ├── game.hpp / .cpp    ← Game, tick, AudioEvent queue, save/load
    ├── entity.hpp / .cpp  ← Entity, EntityRegistry, stepMovement, resolveMoves
    ├── grid.hpp           ← Grid, Portal
    ├── terrain.hpp / .cpp ← Terrain (Perlin + sparse overrides)
    ├── spatial.hpp / .cpp ← SpatialGrid (entity occupancy, hard cap 8)
    ├── scheduler.hpp / .cpp ← ScheduledAction, Scheduler (min-heap)
    ├── events.hpp / .cpp  ← Event, EventBus
    ├── routine.hpp        ← Instruction, OpCode, Condition, AgentExecState, Recording
    ├── routine_vm.hpp / .cpp ← RoutineVM: step(), move resolution
    ├── recorder.hpp / .cpp ← Recorder: player actions → Instruction stream
    ├── input.hpp / .cpp   ← Input snapshot (SDL keycodes → Key enum)
    ├── audio.hpp / .cpp   ← AudioSystem (SDL2_mixer, SFX + music layers + ambient)
    ├── irenderer.hpp      ← IRenderer interface
    ├── renderer.hpp / .cpp ← Renderer (SDL2, camera, UI, SDL_ttf)
    └── terminal_renderer.hpp / .cpp ← TerminalRenderer (ANSI/ASCII)
```

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
