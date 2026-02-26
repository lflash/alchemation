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
Player    │  —       Combat   Collect   Pass
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

### Recorder

Records player movement as **relative tile deltas**, not absolute positions. This
makes instantiation trivial: no coordinate normalisation, no prototype ID remapping.

```cpp
struct RecordingFrame {
    int      dx, dy;        // -1, 0, or 1
    uint32_t delayTicks;    // ticks since previous frame
};

struct Recording {
    RecordingID         id;
    vector<RecordingFrame> frames;
};
```

**Recording**: on each player move, append `{dest - src, currentTick - lastTick}`.

**Cycling**: `recordings` is a `deque<Recording>`. `q` advances `selectedRecording`
cyclically.

**Instantiation** — converting a recording into scheduled actions for a projectile:

```
toAngle(fireDirection) → angle
cursor = spawnPos
t = spawnTick

for each frame in recording.frames:
    t += frame.delayTicks
    delta = rotate({frame.dx, frame.dy}, angle)  // rotate relative to North
    cursor += round(delta)
    push ScheduledAction{ tick=t, entity=projectileID, Move{cursor} }

push ScheduledAction{ tick=t + travelTime, entity=projectileID, Despawn{} }
```

No prototype ID. No post-insertion mutation. The projectile entity is spawned first,
its ID is known, and actions are scheduled directly against it.

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
| `e` | Instantiate `selectedRecording` as a `Poop` projectile from player's `facing` direction. |
| `f` | Dig tile in front of player (`terrain.dig()`). |
| `c` | If tile in front is `BareEarth` and player `mana >= 1`: spawn `Mushroom`, restore terrain, deduct 1 mana. |

| Collision | Result |
|---|---|
| Player + Mushroom | Player gains 3 mana. Mushroom despawns. |
| Player + Goblin | Combat (mana = damage). |
| Poop + Goblin | Goblin takes hit with poop's damage value. |

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
    ├── types.hpp                   ← TilePos, Vec2f, Bounds, TileFields, enums, constants
    ├── game.cpp / game.hpp         ← Game, game loop, top-level tick
    ├── entity.cpp / entity.hpp     ← Entity struct, EntityRegistry
    ├── grid.cpp / grid.hpp         ← Grid, multi-grid world management
    ├── spatial.cpp / spatial.hpp   ← SpatialGrid (entity occupancy, hard cap 8)
    ├── tilegrid.cpp / tilegrid.hpp ← TileGrid (terrain type, height, stimulus fields)
    ├── scheduler.cpp / scheduler.hpp ← ScheduledAction, Scheduler (min-heap)
    ├── events.cpp / events.hpp     ← Event, EventBus
    ├── recorder.cpp / recorder.hpp ← Recording, RecordingFrame, Recorder
    ├── input.cpp / input.hpp       ← Input snapshot
    ├── renderer.hpp                ← IRenderer interface
    ├── sdl_renderer.cpp / .hpp     ← SDLRenderer (SDL2, SpriteCache)
    └── terminal_renderer.cpp / .hpp ← TerminalRenderer (ANSI/ASCII)
```

---

## Libraries

| Library | Purpose | Integration |
|---|---|---|
| SDL2 | Window, input, 2D rendering | `find_package(SDL2)` |
| FastNoiseLite | Perlin noise (single header) | Drop into `vendor/` |
| nlohmann/json | Parse `entities.json` (single header) | Drop into `vendor/` |

Build system: CMake.
