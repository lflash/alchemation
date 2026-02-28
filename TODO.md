# TODO

Build order: each phase leaves the program in a runnable, testable state.
Tests are written alongside the system they cover.

---

## Phase 1 — Window & Renderer

The first thing that should work is seeing something on screen.

- [x] `CMakeLists.txt` — SDL2, compiler flags, test runner (doctest)
- [x] `types.hpp` — `TilePos`, `Vec2f`, `Bounds`, `EntityID`, all enums
- [x] SDL2 window opens, renders a black screen, closes cleanly on quit
- [x] Renderer draws a flat 20×20 tile grid in a checkerboard pattern
- [x] `Terrain` — `heightAt()` via FastNoiseLite, cached
- [x] Renderer queries terrain per tile and shades tiles by height
- [x] `SpriteCache` — loads PNGs from `assets/sprites/`, keyed by `EntityType`
- [x] Renderer draws a single hardcoded entity sprite on the grid

**Tests**
- [x] `TilePos` arithmetic (add, subtract, equality)
- [x] `Vec2f` lerp correctness
- [x] `Terrain.heightAt()` returns consistent values for the same `TilePos`
- [x] `Terrain.typeAt()` returns `Grass` by default, `BareEarth` after `dig()`

---

## Phase 2 — Entity & Input

Entities exist and the player can move. No collision yet.

- [x] `Entity` struct with all fields from design doc
- [x] `EntityRegistry` — `spawn()`, `destroy()`, lookup by `EntityID`
- [x] `Input` — per-tick snapshot, `held()`, `pressed()`, `released()`
- [x] Player entity spawns at `(0,0)`, moves one tile per WASD press
- [x] `facing` updates on movement
- [x] `moveT` advances each tick, entity renders at lerped position using `alpha`
- [x] Player walks off-grid (no bounds enforcement yet — that comes with collision)

**Tests**
- [x] `EntityRegistry` — spawn returns unique IDs, destroy removes entity, lookup after destroy returns null/invalid
- [x] `Input` — `pressed()` true only on first tick a key is down, not while held
- [x] Movement lerp — `moveT` reaches 1.0 after expected number of ticks given `speed`

---

## Phase 3 — Spatial Grid & Collision

Entities can block each other. The world becomes solid.

- [ ] `SpatialGrid` — `add()`, `remove()`, `at()`, `query(Bounds)`
- [ ] Entities register in all cells their bounds overlap (multi-tile support from day one)
- [ ] Dual registration: entity registers in both `pos` and `destination` when moving; removed from `pos` on arrival
- [ ] Broad phase: `query()` returns deduplicated candidates from all overlapping cells
- [ ] Narrow phase: AABB intersection test between mover and each candidate
- [ ] Collision resolution table (`resolveCollision(EntityType, EntityType)`)
- [ ] Two-phase movement per tick: collect all intentions, then resolve conflicts
- [ ] Swap detection: A→B's tile and B→A's tile in the same tick → both blocked
- [ ] Player is blocked by goblin, passes through mushroom (collect)

**Tests**
- [ ] `SpatialGrid.at()` — entity appears in all cells its bounds cover
- [ ] `SpatialGrid` — dual registration: entity appears in both `pos` and `destination` cells during movement, only `destination` after arrival
- [ ] `SpatialGrid` — multi-tile entity (2×1) registers in exactly 2 cells; moving east updates only the delta cells
- [ ] AABB narrow phase — overlapping boxes return true, adjacent boxes return false, partial overlap returns true
- [ ] Swap detection — two entities moving into each other's tiles are both blocked
- [ ] `resolveCollision` — player+mushroom returns `Collect`, goblin+goblin returns `Block`

---

## Phase 4 — Scheduler & Events

World events can be scheduled. Systems communicate through events. Note: per-agent
movement timing is handled by the Routine VM (`WAIT` instructions + `AgentExecState`),
not by this scheduler. The Scheduler is for low-frequency world events only.

- [ ] `ScheduledAction` with `variant` payload
- [ ] `Scheduler` as a min-heap — `push()`, `popDue(tick)` returns all actions due this tick
- [ ] `EventBus` — `subscribe()`, `emit()`, `flush()` at end of tick
- [ ] `Arrived` event fires when `moveT` reaches 1.0
- [ ] `Despawn` action removes entity from registry and spatial grid
- [ ] `ChangeMana` action modifies entity mana

**Tests**
- [ ] `Scheduler` — actions pop in tick order regardless of insertion order
- [ ] `Scheduler` — actions with the same tick all pop together
- [ ] `EventBus` — subscriber receives event after `flush()`, not before
- [ ] `EventBus` — `Arrived` fires exactly once per entity arrival
- [ ] `Despawn` action — entity removed from registry and all spatial grid cells

---

## Phase 5 — Terrain Interaction & Mana

The world becomes interactive.

- [ ] `f` key digs tile in front of player — `terrain.dig()`
- [ ] Dug tiles render differently (colour change, no entity needed)
- [ ] `c` key plants mushroom on `BareEarth` if `mana >= 1` — spawns `Mushroom` entity, calls `terrain.restore()`, deducts 1 mana
- [ ] Player collects mushroom on arrival: `+3 mana`, mushroom entity despawns
- [ ] Mana value visible in window title or console (no UI system yet)

**Tests**
- [ ] Plant on grass tile does nothing
- [ ] Plant on `BareEarth` with 0 mana does nothing
- [ ] Plant on `BareEarth` with mana >= 1 spawns mushroom, deducts 1 mana, restores terrain
- [ ] Collecting mushroom increments mana by 3

---

## Phase 6 — Goblin & Combat

Entities have agency and can be harmed.

- [ ] Goblin wanders — simple scheduled move sequence on spawn
- [ ] Player + goblin collision triggers combat: goblin takes `player.mana` as damage
- [ ] Goblin health — despawns at 0
- [ ] Poop + goblin: goblin takes hit

**Tests**
- [ ] Goblin despawns when health reaches 0
- [ ] Combat with 0 mana deals 0 damage

---

## Phase 7 — Recorder & Routine Agents

Player actions are recorded as a `Recording` — a flat `Instruction` stream executed by
the `RoutineVM`. A deployed Poop entity is a routine agent: an autonomous robot that
replays the recorded actions (movement, digging, planting) independently of the player.
It is not a projectile. Future agent types will share the same VM.

This requires `RoutineVM` to exist; implement the minimal VM needed here.

- [ ] `Recording` struct wrapping a `vector<Instruction>`
- [ ] `Recorder` — `start()`, `record(key)`, `stop()` → saves `Recording` to `recordings` deque
- [ ] Player moves emit `MOVE_REL <dir>` (relative to player `facing`); pauses between moves emit `WAIT <ticks>`; recording ends with `HALT`
- [ ] `r` key toggles recording
- [ ] `q` key cycles `selectedRecording`
- [ ] `e` key deploys `selectedRecording` as a `Poop` routine agent spawned in front of the player, inheriting player `facing`
- [ ] `RoutineVM::step(AgentExecState&, Recording&, Grid&)` — advances one instruction per tick; `MOVE_REL` resolves direction relative to agent `facing`
- [ ] Poop despawns when VM reaches `HALT`
- [ ] *(future)* `DIG` and `PLANT` instructions recorded and executed by agent

**Tests**
- [ ] `record()` emits `MOVE_REL` for each move key with correct relative direction
- [ ] `record()` emits `WAIT` for gaps between moves; tick count matches actual delay
- [ ] Recording ends with `HALT`
- [ ] `RoutineVM` advances `pc` correctly through a linear sequence
- [ ] Facing North at launch — `MOVE_REL FORWARD` moves north
- [ ] Facing East at launch — `MOVE_REL FORWARD` moves east
- [ ] Poop despawns when `RoutineVM` reaches `HALT`

---

## Phase 8 — Multiple Grids

The world splits into independent simulation spaces.

- [ ] `Grid` class wraps `SpatialGrid`, `Terrain`, `Scheduler`, entity list
- [ ] `Game` owns a map of `GridID → Grid` and tracks `activeGrid`
- [ ] Only the active grid is ticked and rendered
- [ ] `transferEntity()` moves an entity between grids cleanly (removes from old spatial, adds to new)
- [ ] Studio grid — blank terrain, no consequences, used for recording
- [ ] Entering/leaving studio via a key or tile trigger

**Tests**
- [ ] Entity transferred between grids appears in new grid's spatial, absent from old
- [ ] Scheduler actions targeting an entity in an inactive grid do not execute
- [ ] Terrain in grid A is independent from terrain in grid B

---

## Backlog (post-core)

- [ ] Camera / viewport scrolling — world larger than 20×20
- [ ] Persistent grid state — save/load terrain overrides and entity positions
- [ ] Room grids — enter a house, transition to interior grid
- [ ] Parallel universe grid — runs simultaneously in background
- [ ] UI overlay — mana counter, recording indicator
- [ ] Sound
