# Grid Game — Design Document

## Vision

A simulation of thousands to millions of autonomous agents, each following routines
and reacting to stimulus from their environment and from other agents. The long-term
goal is a 2.5D game in the style of Pokémon — a rich, living world that feels
populated and alive.

Agents perceive and react to environmental stimuli — fire, water, freezing, being
pushed or pulled, and others to be defined. Stimuli are per-tile float fields: fire
intensity, cold, water level, force vector, and so on. They propagate across the
tile grid each tick (fire spreads, cold radiates, force transfers) and agents sample
the tile they occupy to determine their reaction. Stimuli are not entities — they
are properties of space, updated in parallel by the compute backend.

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

A 2D tile-based game with smooth visual movement, a multi-world system, and a
movement recording/playback mechanic. Written in C++ with SDL2.

---

## Core Principles

- **Game logic runs on integer tile positions.** `TilePos` is the source of truth for
  all simulation: collision, spatial queries, scheduling, recordings.
- **Rendering interpolates.** `Vec2f` exists only to smooth visual movement between
  ticks. It has no effect on game logic.
- **Fixed timestep.** The simulation advances in discrete ticks at a fixed rate.
  Rendering runs as fast as possible and interpolates between ticks.
- **One grid per world.** Entities exist in exactly one `Grid` at a time. Grids are
  independent simulations (main world, studio, interiors, parallel universes).
- **Tile hard cap.** A maximum of 8 entities may occupy the same tile simultaneously.
  A move that would exceed this cap is blocked.
- **Three occupancy layers, independently managed.** Terrain type and stimulus fields
  are properties of tiles (flat arrays). Entities are a separate system. All three
  coexist freely — an entity standing on a fire tile does not displace the fire.

---

## Data Types

```cpp
struct TilePos { int x, y; };           // integer grid coordinate — game logic only
struct Vec2f   { float x, y; };         // float world position  — rendering only
struct Bounds  { Vec2f min, max; };     // AABB hitbox in world space

using EntityID   = uint32_t;
using GridID     = uint32_t;
using RecordingID = uint32_t;
using Tick       = uint64_t;

enum class EntityType { Player, Goblin, Mushroom, Poop };
enum class Direction  { N, NE, E, SE, S, SW, W, NW };
enum class ActionType { Move, Spawn, Despawn, ChangeMana, Dig, Plant, Summon };
enum class EventType  { Arrived, Collided, Despawned };
enum class TileType   { Grass, BareEarth };

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

constexpr int CALL_STACK_DEPTH = 8;   // max subroutine nesting — fixed for GPU

struct AgentExecState {
    RoutineID routineID;                    // which routine to execute
    uint32_t  pc;                           // program counter
    uint32_t  waitTicks;                    // remaining ticks on a WAIT
    uint32_t  callStack[CALL_STACK_DEPTH];  // return addresses for CALL/RET
    uint8_t   callDepth;                    // current call stack depth
};

constexpr int TILE_ENTITY_CAP = 8;   // max entities per tile

// Per-tile field data (flat arrays in TileGrid, indexed by TilePos)
struct TileFields {
    TileType type;       // terrain type
    float    height;     // Perlin height, render only
    float    fire;       // stimulus: fire intensity  (0.0 = none)
    float    cold;       // stimulus: cold intensity  (0.0 = none)
    float    water;      // stimulus: water level     (0.0 = none)
    float    forceX;     // stimulus: push/pull force (tile units/tick)
    float    forceY;
};
```

---

## Game Loop

Fixed timestep with uncapped render rate and sub-tick interpolation.

```
TICK_RATE = 50 Hz  (20ms per tick)

each frame:
  dt = time since last frame (capped at 100ms to prevent spiral of death)
  accumulator += dt

  while accumulator >= TICK_DT:
    input.snapshot()
    game.tick(currentTick++)
    accumulator -= TICK_DT

  alpha = accumulator / TICK_DT        // 0.0 → 1.0, sub-tick position
  renderer.draw(game, alpha)
```

`alpha` is passed to the renderer to interpolate entity positions between their last
tick state and their current state, producing smooth visuals at any framerate.

---

## Systems

### Input

Snapshots key state once per tick. Provides:
- `bool held(Key)`   — key currently down
- `bool pressed(Key)` — key went down this tick
- `bool released(Key)` — key went up this tick

No raw events leak into the game logic. Input is polled, not pushed.

---

### Entity & EntityRegistry

```cpp
struct Entity {
    EntityID  id;
    EntityType type;
    GridID    grid;

    TilePos   pos;           // current logical tile (game logic)
    TilePos   destination;   // target tile (== pos when idle)
    float     moveT;         // 0.0→1.0 progress toward destination (render only)

    Vec2f     bounds;        // hitbox size in tile units (can be < 1.0 or > 1.0)
    float     speed;         // moveT progress added per tick
    Direction facing;
    int       layer;         // draw order (lower = drawn first)
    int       mana;
};
```

`EntityRegistry` owns all entities across all grids, keyed by `EntityID`. Each `Grid`
holds only a list of `EntityID`s that belong to it.

Movement state: while an entity is moving, it is registered in the `SpatialGrid` at
**both** its `pos` and its `destination` (see Collision). On arrival, it is removed
from `pos` and stays at `destination`, which becomes the new `pos`.

---

### Grid & World

```cpp
class Grid {
    GridID       id;
    SpatialGrid  spatial;
    Terrain      terrain;
    Scheduler    scheduler;
    vector<EntityID> entities;
};

class Game {
    unordered_map<GridID, Grid> grids;
    EntityRegistry              registry;
    Recorder                    recorder;
    GridID                      activeGrid;
};
```

Only the active grid is simulated and rendered each tick. Inactive grids are frozen
unless explicitly ticked (e.g. a parallel universe running in the background).

**Transferring an entity between grids:**
```
transferEntity(id, fromGrid, toGrid, TilePos destination)
  → remove from fromGrid spatial + entity list
  → add to toGrid spatial + entity list
  → set entity.grid = toGrid
  → set entity.pos = destination
```

---

### SpatialGrid

Hash map from `TilePos` to a fixed-size array of up to `TILE_ENTITY_CAP` (8)
`EntityID`s. Entities in motion are registered in **both** their `pos` and
`destination` cells simultaneously. This prevents any entity from entering a cell
that another entity has only partially vacated.

A move is blocked before it starts if the destination tile already holds 8 entities
(after accounting for any that are departing this tick).

**Multi-tile entities** (e.g. 2×1, 3×3) register in every cell their bounds overlap:
```
x0 = floor(pos.x),  x1 = floor(pos.x + bounds.x)
y0 = floor(pos.y),  y1 = floor(pos.y + bounds.y)
register entity in all (x, y) in [x0..x1] × [y0..y1]
```

When an entity moves, only the delta of old vs new cell sets is updated — cells that
remain covered are untouched.

**Querying** (broad phase): given an AABB, return all unique `EntityID`s registered
in any overlapping cell. Duplicates (from multi-cell registration) are deduplicated
before the narrow phase.

The `SpatialGrid` manages entities only. Stimulus fields and terrain are stored
separately in `TileGrid` and are not affected by entity occupancy.

---

### Collision Detection

Two-phase:

**Broad phase** — `SpatialGrid.query(AABB)` → candidate `EntityID` list. Cheap O(1)
lookup per cell, avoids O(n²) all-pairs checking.

**Narrow phase** — AABB intersection test between the moving entity's hitbox and each
candidate's hitbox. Fires only when boxes actually overlap in world space, not just
when tile boundaries touch. This correctly handles sprites smaller than a tile.

**Collision resolution** is determined by a lookup on `(mover type, occupant type)`:

```
          │ Player   Goblin   Mushroom  Poop
──────────┼──────────────────────────────────
Player    │  —       Block*   Collect   Pass   *bump combat: push fires on block
Goblin    │ Combat   Block    Pass      Pass
Poop      │  Pass    Hit      Pass      Pass
```

Each cell is independently configurable. `Block` prevents the move from starting.
All other results allow movement; the event fires on arrival.

**Swap prevention**: movement intentions are collected across all entities first, then
resolved together. If A wants B's tile and B wants A's tile in the same tick, it is
detected as a head-on conflict and treated as `Block` for both.

---

### Scheduler

A min-heap (priority queue) of `ScheduledAction`s ordered by `tick`. Each tick:
```
while scheduler.top().tick <= currentTick:
    execute(scheduler.pop())
```

```cpp
struct ScheduledAction {
    Tick       tick;
    EntityID   entity;
    ActionType type;
    variant<MovePayload, SpawnPayload, DespawnPayload,
            ChangeManaPayload, TilePosPayload> payload;
};
```

O(log n) insert and pop. No linear scanning.

Actions are never mutated after insertion. Transformations (translate, rotate, delay,
rescale) are applied when building a batch of actions (e.g. instantiating a
recording), before they are pushed into the scheduler.

---

### Events

```cpp
struct Event {
    EventType type;
    EntityID  subject;
    EntityID  other;      // for Collided
    int       magnitude;  // for Hit
};
```

`EventBus` holds a list of subscribers per `EventType`. Events are queued during the
tick and flushed at the end, so handlers always run on a consistent game state.

---

### Tile Fields

Terrain and stimuli are properties of tiles, not entities. They are stored in
`TileGrid` as flat arrays of `TileFields` structs indexed by `TilePos`. The
`SpatialGrid` is entirely separate.

```cpp
class TileGrid {
    FastNoiseLite                        noise;
    unordered_map<TilePos, TileFields>   tiles;   // sparse; default-constructed on miss
public:
    TileFields&       at(TilePos);        // returns default if not yet set
    const TileFields& at(TilePos) const;

    // Terrain
    void  dig(TilePos);                   // sets type = BareEarth
    void  restore(TilePos);              // sets type = Grass

    // Stimulus update — called once per tick
    void  stepStimuli();                  // propagate/decay all stimulus fields
};
```

**Terrain type** (`Grass`, `BareEarth`, ...) is a field on `TileFields`. Perlin
height is computed on demand and cached. `dig()` and `restore()` mutate the type
field directly.

**Stimulus fields** (`fire`, `cold`, `water`, `forceX/Y`) are floats on `TileFields`.
`stepStimuli()` runs each tick: fire spreads to adjacent tiles and decays, cold
radiates, water flows downhill, force dissipates. Terrain type can modulate
propagation (fire spreads faster on `BareEarth`).

Agents sample `TileGrid::at(pos)` each tick and react to stimulus values above
their per-type thresholds. Stimuli do not interact with the `SpatialGrid`.

---

### Routine VM

Routines are programs. Agents are threads. The GPU kernel is the interpreter.

Agents are not projectiles — they are autonomous robots. A Poop entity executing a
routine can move, dig, plant, and react to stimuli exactly as a player can. The only
difference is that its actions are driven by a `Recording` rather than live input.
Future agent types will share the same VM.

Each routine is a flat array of `Instruction`s stored in a GPU-resident buffer. Each
agent holds an `AgentExecState` — a program counter, a wait counter, and a fixed-
depth call stack. One GPU thread per agent executes one instruction per tick.

**GPU kernel (per tick, one thread per agent):**
```
if agent.waitTicks > 0:
    agent.waitTicks--
    return

instr = routines[agent.routineID][agent.pc]

switch instr.op:
    MOVE_REL:    intendedMove = resolve(instr.dir, agent.facing)
    MOVE_ABS:    intendedMove = instr.dir
    FACE:        agent.facing = instr.dir
    WAIT:        agent.waitTicks = instr.addr; agent.pc++; return
    JUMP:        agent.pc = instr.addr; return
    JUMP_IF:     if stimuli[agent.pos][instr.condition] > instr.threshold:
                     agent.pc = instr.addr; return
    JUMP_IF_NOT: if stimuli[agent.pos][instr.condition] <= instr.threshold:
                     agent.pc = instr.addr; return
    CALL:        callStack[callDepth++] = agent.pc + 1
                 agent.routineID = instr.addr
                 agent.pc = 0; return
    RET:         if callDepth == 0: halt
                 agent.pc = callStack[--callDepth]; return
    HALT:        return

agent.pc++
```

A second pass collects all `intendedMove`s, resolves conflicts (tile cap, swaps), and
commits. The VM and the collision pass are independent GPU kernels.

---

**Subroutines**

`CALL routineID` pushes the return address onto the agent's fixed-size call stack and
jumps to the start of the named routine. `RET` returns to the caller. Maximum nesting
depth is `CALL_STACK_DEPTH` (8) — fixed at compile time so agent state remains a
constant-size GPU struct.

This lets complex behaviours be built from named components:

```
routine "patrol":           routine "flee_from_fire":
  CALL  flee_from_fire        JUMP_IF_NOT  FIRE 0.3  done
  MOVE_REL  E                 MOVE_REL     BACK
  CALL  flee_from_fire        RET
  MOVE_REL  N               done:
  JUMP  0                     RET
```

Subroutines are just routines — they live in the same buffer and are referenced by
`RoutineID`. There is no distinction between a top-level routine and a subroutine.

---

**Recording**

The player records a routine by playing. Each action emits one or more instructions:

| Player action | Emitted instruction |
|---|---|
| Move key | `MOVE_REL <dir>` |
| Pause between moves | `WAIT <ticks>` |
| `f` (dig) | `DIG` |
| `c` (plant) | `PLANT` |
| Branch trigger + condition pick | `JUMP_IF <condition> <threshold> <addr>` |
| Subroutine trigger + routine pick | `CALL <routineID>` |
| End of loop | `JUMP 0` |

Conditional branches and subroutine calls are inserted via the **Routine Editor**
(see below) rather than recorded live, since they require the player to specify a
target address or condition. The recorder captures movement and timing; the editor
handles control flow.

---

### Renderer

Rendering is abstracted behind `IRenderer`. The simulation has no dependency on any
specific frontend. Two implementations are planned:

- **`SDLRenderer`** — SDL2 window, sprite blitting, tile shading. Primary target.
- **`TerminalRenderer`** — ANSI/ASCII output. No SDL dependency. Useful for rapid
  iteration and headless machines.

`IRenderer` receives game state and `alpha` (sub-tick interpolation factor). It has
no write access to game state.

**Per frame:**
1. Clear screen
2. For each tile in viewport: read `TileGrid::at()` for terrain type, height, and
   stimulus values; compute colour/character; draw
3. Sort entities in active grid by `layer` (ascending)
4. For each entity: compute render position, blit sprite or character

**Render position** (smooth movement):
```
Vec2f renderPos = lerp(toVec(entity.pos), toVec(entity.destination), entity.moveT)
```

`moveT` is the tick-level progress (updated by the movement system). `alpha` can be
applied on top for sub-tick smoothness if needed.

**SpriteCache** (`SDLRenderer` only): loads and caches textures by `EntityType` at
startup. Entities hold no rendering data.

---

### Routine Editor

The routine editor is an in-game UI for authoring, naming, and assigning routines.
It is a future phase — the simulation and VM must be in place first — but its
requirements shape the VM design.

**What the editor must support:**
- View the current routine as a list of numbered instructions
- Insert, delete, and reorder instructions
- Set `JUMP_IF` conditions and thresholds interactively
- Name and save routines to a persistent library
- Pick a subroutine by name when inserting a `CALL` instruction
- Assign a routine to a selected agent type or individual agent
- Run the routine on a single test agent in the studio grid for preview

**Relationship to recording:**
Recording and editing are complementary. The player records the movement skeleton
(a linear sequence of `MOVE_REL` and `WAIT` instructions), then opens the editor to
add branches, loops, and subroutine calls. Neither tool alone is sufficient; both are
required for authoring non-trivial routines.

**UI requirements:**
The editor requires a basic immediate-mode UI system: text rendering, a selectable
list, directional navigation, and modal input (for naming routines, entering
thresholds). This implies a font/glyph rendering system and a minimal widget layer
on top of `IRenderer`. SDL2's built-in capabilities are insufficient; a font library
(e.g. SDL_ttf) or a baked bitmap font will be needed.

---

## Entity Configuration

Loaded from `assets/entities.json` at startup. Defines per-type properties:

```json
{
  "player":   { "speed": 0.1, "bounds": [0.8, 0.8], "layer": 0, "sprite": "player.png"   },
  "goblin":   { "speed": 0.1, "bounds": [0.8, 0.8], "layer": 1, "sprite": "goblin.png"   },
  "mushroom": { "speed": 0.0, "bounds": [0.6, 0.6], "layer": 2, "sprite": "mushroom.png" },
  "poop":     { "speed": 0.2, "bounds": [0.5, 0.5], "layer": 1, "sprite": "poop.png"     }
}
```

---

## Game Mechanics

| Input | Action |
|---|---|
| `WASD` | Move player one tile in direction. Updates `facing`. |
| `r` | Toggle recording. On stop, saves `Recording` to `recordings` deque. |
| `q` | Cycle `selectedRecording`. |
| `e` | Deploy `selectedRecording` as a `Poop` routine agent spawned in front of the player, inheriting player `facing`. |
| `f` | Dig tile in front of player (`terrain.dig()`). |
| `c` | If tile in front is `BareEarth` and player `mana >= 1`: spawn `Mushroom`, restore terrain, deduct 1 mana. |

| Collision | Result |
|---|---|
| Player + Mushroom | Player gains 3 mana. Mushroom despawns. |
| Player + Goblin | Bump combat: player mana as damage, goblin pushed back. |
| Poop + Goblin | Goblin takes hit. |

---

## File Structure

```
grid_game/
├── DESIGN.md
├── CMakeLists.txt
├── assets/
│   ├── entities.json
│   └── sprites/
│       ├── player.png
│       ├── goblin.png
│       ├── mushroom.png
│       └── poop.png
├── vendor/
│   └── FastNoiseLite.hpp
└── src/
    ├── main.cpp
    ├── types.hpp                    ← TilePos, Vec2f, Bounds, TileFields, enums, lerp, toVec, TilePosHash, constants
    ├── game.hpp / .cpp              ← Game, game loop, top-level tick
    ├── entity.hpp / .cpp            ← Entity struct, EntityRegistry
    ├── grid.hpp / .cpp              ← Grid, multi-grid world management
    ├── spatial.hpp / .cpp           ← SpatialGrid (entity occupancy, hard cap 8)
    ├── tilegrid.hpp / .cpp          ← TileGrid (terrain type, height, stimulus fields)
    ├── scheduler.hpp / .cpp         ← ScheduledAction, Scheduler (min-heap)
    ├── events.hpp / .cpp            ← Event, EventBus
    ├── routine.hpp                  ← Instruction, OpCode, Condition, AgentExecState
    ├── routine_vm.hpp / .cpp        ← RoutineVM: GPU kernel, routine buffer, step()
    ├── recorder.hpp / .cpp          ← records player actions → Instruction stream
    ├── input.hpp / .cpp             ← Input snapshot
    ├── irenderer.hpp                ← IRenderer interface
    ├── renderer.hpp / .cpp          ← SDLRenderer (SDL2, SpriteCache)
    └── terminal_renderer.hpp / .cpp ← TerminalRenderer (ANSI/ASCII)
```

---

## Libraries

| Library | Purpose | Integration |
|---|---|---|
| SDL2 | Window, input, 2D rendering | `find_package(SDL2)` |
| FastNoiseLite | Perlin noise (single header) | Drop into `vendor/` |
| nlohmann/json | Parse `entities.json` (single header) | Drop into `vendor/` |

Build system: CMake.
