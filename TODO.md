# TODO

Build order: each phase leaves the program in a runnable, testable state.
Tests are written alongside the system they cover.

---

## Phase 1 — Window & Renderer ✓

- [x] `CMakeLists.txt` — SDL2, compiler flags, test runner (doctest)
- [x] `types.hpp` — `TilePos`, `Vec2f`, `Bounds`, `EntityID`, all enums
- [x] SDL2 window opens, renders a black screen, closes cleanly on quit
- [x] Renderer draws a flat 20×20 tile grid in a checkerboard pattern
- [x] `Terrain` — `heightAt()` via FastNoiseLite, cached
- [x] Renderer queries terrain per tile and shades tiles by height
- [x] `SpriteCache` — loads PNGs from `assets/sprites/`, keyed by `EntityType`
- [x] Renderer draws a single hardcoded entity sprite on the grid

**Tests** ✓ (all passing)
- [x] `TilePos` arithmetic (add, subtract, equality)
- [x] `Vec2f` lerp correctness
- [x] `Terrain.heightAt()` returns consistent values for the same `TilePos`
- [x] `Terrain.typeAt()` returns `Grass` by default, `BareEarth` after `dig()`

---

## Phase 2 — Entity & Input ✓

- [x] `Entity` struct with all fields
- [x] `EntityRegistry` — `spawn()`, `destroy()`, lookup by `EntityID`
- [x] `Input` — per-tick snapshot, `held()`, `pressed()`, `released()`, `scroll()`
- [x] Player entity spawns at `(0,0)`, moves one tile per WASD press
- [x] `facing` updates on movement; `Shift` to strafe
- [x] `moveT` advances each tick, entity renders at lerped position

**Tests** ✓

---

## Phase 3 — Spatial Grid & Collision ✓

- [x] `SpatialGrid` — `add()`, `remove()`, `at()`, `query(Bounds)`
- [x] Dual registration: entity in both `pos` and `destination` while moving
- [x] Broad + narrow phase collision (AABB)
- [x] Collision resolution table (`resolveCollision(EntityType, EntityType)`)
- [x] Two-phase movement: collect intentions, resolve conflicts, commit
- [x] Swap detection: A→B and B→A in same tick → both blocked

**Tests** ✓

---

## Phase 4 — Scheduler & Events ✓

- [x] `ScheduledAction` with `variant` payload
- [x] `Scheduler` min-heap — `push()`, `popDue(tick)`
- [x] `EventBus` — `subscribe()`, `emit()`, `flush()` at end of tick
- [x] `Arrived` event fires when `moveT` reaches 1.0
- [x] `Despawn` and `ChangeMana` actions

**Tests** ✓

---

## Phase 5 — Terrain Interaction & Mana ✓

- [x] `F` digs tile ahead; dug tiles render as BareEarth
- [x] `C` plants mushroom on `BareEarth` if `mana >= 1`
- [x] Player collects mushroom on arrival: +3 mana, mushroom despawns
- [x] Mana visible in HUD overlay

**Tests** ✓

---

## Phase 6 — Goblin & Combat ✓

- [x] Goblin wanders randomly each tick (1/80 chance per tick)
- [x] Player + goblin bump combat: goblin takes `player.mana` damage, pushed back
- [x] Goblin despawns at 0 health
- [x] Poop + goblin: goblin takes hit

**Tests** ✓

---

## Phase 7 — Recorder & Routine Agents ✓

- [x] `Recording` struct wrapping `vector<Instruction>` + name string
- [x] `Recorder` — `start()`, `stop()` saves recording named "Script N"
- [x] Movement emits `MOVE_REL`; pauses emit `WAIT`; recording ends with `HALT`
- [x] `R` toggles recording; `Q` cycles selected; `E` deploys as Poop agent
- [x] `RoutineVM::step()` — one instruction per tick, `MOVE_REL` relative to facing
- [x] Poop despawns when VM reaches `HALT`

**Tests** ✓

---

## Phase 8 — Multiple Grids ✓

- [x] `Grid` class: `SpatialGrid`, `Terrain`, `Scheduler`, `EventBus`, entity list
- [x] `Game` owns `unordered_map<GridID, Grid>`; tracks `activeGridID`
- [x] **All non-paused grids tick every frame** (not just active); player input only to active grid
- [x] Active grid ID captured before tick loop to prevent double-input on grid switch
- [x] `transferEntity()` — moves entity between grids cleanly
- [x] Studio grid (GRID_STUDIO = 2): blue-grey terrain, entered via Tab
- [x] `PendingTransfer` applied between ticks (never mid-loop)
- [x] Portal tile (`TileType::Portal`, purple) created with `O` key
- [x] Portal links current grid to new bounded 20×20 room; return portal at room centre
- [x] Any entity (player, goblin) teleports through portals on arrival
- [x] Return from room lands on the portal tile itself (safe: detection fires on movement arrival only)
- [x] `gridJustSwitched_` flag → camera snaps instantly on grid switch, no lerp artefact
- [x] Bounded room: void tiles (dark) rendered outside `[0,w)×[0,h)`
- [x] Goblin and player clamped to grid bounds in bounded grids

**Tests** ✓ (147/147 passing)

---

## Infrastructure ✓

### Camera ✓
- [x] Smooth exponential lerp tracking player position
- [x] Manual pan with arrow keys; Backspace re-centres
- [x] Ctrl + scroll zoom (`[0.25×, 4.0×]`)
- [x] Infinite world: `drawTerrain` computes visible tile range from camera
- [x] Camera snaps instantly on grid switch (no lerp artefact)

### Persistent State ✓
- [x] Binary save format v6: all grids, portals, terrain overrides, entities (with z), recordings
- [x] Auto-save on quit (Esc); auto-load on startup
- [x] Version check: mismatch → fresh world

### UI Overlay ✓
- [x] SDL_ttf integrated (DejaVuSansMono 13pt)
- [x] HUD: mana counter `♦ N` + recording indicator `● REC` (always visible)
- [x] Controls panel (H): keybinding reference
- [x] Recordings panel (I): list with rename (Enter); mutually exclusive with H
- [x] SDL_TEXTINPUT rename mode: empty Input passed to game.tick() while renaming
- [x] Facing indicators: filled triangle on all non-Mushroom entities
- [x] Studio blue-grey palette (muted, desaturated)

### Sound ✓
- [x] `AudioSystem` (SDL2_mixer): 8 SFX channels + 4 music layers + 1 ambient
- [x] SFX for all 12 game events; `AudioEvent` queue drained each render frame
- [x] Proximity-based music layers (WorldCalm, GoblinTension, Studio, RoomInterior)
- [x] Idle ambience: Minecraft-style sparse tracks after 30 seconds of quiet
- [x] Placeholder WAV assets generated with sox (replace with real audio)

---

## Phase 9 — Height-based movement ✓

Add integer z-coordinate to `TilePos` and entities. Terrain height is quantised from
Perlin noise (`levelAt = round(heightAt * 4)`). Movement is blocked if the height
difference between source and destination exceeds 1 level. Rendering uses oblique
projection so entities at higher z appear higher on screen.

### Core data changes ✓
- [x] `TilePos` gains `int z`: `struct TilePos { int x, y, z; };`
- [x] `TilePosHash` updated for 3D key
- [x] `Entity::pos` and `Entity::destination` include z; set from `levelAt` on spawn
- [x] `Terrain::levelAt(TilePos)` — `round(heightAt * 4)`, integer height for movement
- [x] `SpatialGrid` keyed on 3D `TilePos`; entities at different z don't collide
- [x] Save format v6: includes z in all TilePos fields

### Movement ✓
- [x] Before each move, `destination.z = terrain.levelAt(destination)` (world grids only)
- [x] Move blocked if `|destination.z - entity.pos.z| > 1`
- [x] Applies to player, goblin wander, and routine VM moves
- [x] Bounded rooms exempt (flat floor, z unchanged by movement)

### Rendering ✓
- [x] `toPixelY` uses oblique formula: `baseY + (tile_y - cam.y)*TILE_H - (tile_z - cam.z)*Z_STEP`
- [x] `TILE_H = 20`, `TILE_W = 32`, `Z_STEP = 12` (all unzoomed)
- [x] Draw order: sort by `world_y` ascending, then `world_z` ascending within same y
- [x] Entity render z interpolates `pos.z → destination.z` via `moveT`
- [x] Camera gains `float z` / `targetZ`; tracks player render z with lerp/snap logic
- [x] Entity shadow and sprite anchored at tile centre; facing indicator centred on body

### Tests ✓ (150/150 passing)
- [x] `TilePos` hash includes z — (1,2,3) and (1,2,4) are distinct keys
- [x] Two entities at same (x,y) but different z do not collide
- [x] Entity at same (x,y,z) still collides

---

## Backlog

### New Assets
- [ ] Proper sprite art for all entity types (player, goblin, mushroom, poop)
- [ ] Terrain tile sprites — textured tiles (grass, bare earth, stone, water)
- [ ] Wall and structure tiles for room interiors
- [ ] Animated sprites — idle/walk frames per entity type
- [ ] Real audio — replace placeholder WAV with composed OGG tracks

### New Interactions
- [ ] Fire stimulus — tiles ignite; fire spreads each tick; agents react (flee routine)
- [ ] Water — floods downhill tiles; slows movement; extinguishes fire
- [ ] Pushable objects — crates, boulders: `pushable` flag, shoved by player or agents
- [ ] More entity types — tree (blocks, choppable), rock (permanent), chest (yields mana)
- [ ] Goblin drops — loot entity on despawn
- [ ] Routine-triggered terrain — wire DIG/PLANT opcodes in VM to Terrain calls
- [ ] Conditional routines — JUMP_IF / JUMP_IF_NOT on fire/entity-ahead stimulus

### World Generation
- [ ] Biome map — second Perlin layer drives region type (forest, plains, swamp, desert)
- [ ] Procedural entity spawning — goblins in clusters, mushroom patches in forest
- [ ] Structures — houses (exterior + matching room grid), ruins, caves
- [ ] Rivers — flow simulation from high to low height; water stimulus pre-seeded
- [ ] Roads — connect structures; faster movement on road tiles
