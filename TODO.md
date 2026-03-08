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
- [x] `R` toggles recording; `Q` cycles selected; `E` summons a golem
- [x] `RoutineVM::step()` — one instruction per tick, `MOVE_REL` relative to facing
- [x] All routine agents despawn when VM reaches `HALT`

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

**Tests** ✓ (all passing)

---

## Infrastructure ✓

### Camera ✓
- [x] Smooth exponential lerp tracking player position
- [x] Manual pan with arrow keys; Backspace re-centres
- [x] Ctrl + scroll zoom (`[0.25×, 4.0×]`)
- [x] Infinite world: `drawTerrain` computes visible tile range from camera
- [x] Camera snaps instantly on grid switch (no lerp artefact)

### Persistent State ✓
- [x] Binary save format v7: all grids, portals, terrain overrides, entities (with z), recordings, flat Instruction layout
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
- [x] One-point perspective: `f = 1 + (tileZ - cam.z) / Z_PERSP` (Z_PERSP=30)
- [x] `screen_x = cx + (tileX - cam.x) * TILE_SIZE * zoom * f` — tiles widen with elevation
- [x] `screen_y = cy + (tileY - cam.y) * TILE_H * zoom - (tileZ - cam.z) * Z_STEP * zoom * f`
- [x] Parallax: higher tiles scroll faster by factor `f` when camera moves
- [x] South cliff face: gap between tile bottom and south neighbour top (from toPixelY)
- [x] East/west cliff faces: natural width-gap from perspective scale; east visible left of
      screen centre, west visible right of centre
- [x] `TILE_H = 20`, `TILE_SIZE = 32`, `Z_STEP = 12` (all unzoomed)
- [x] Draw order: sort by `world_y` ascending, then `world_z` ascending within same y
- [x] Entity render z interpolates `pos.z → destination.z` via `moveT`
- [x] Camera gains `float z` / `targetZ`; tracks player render z with lerp/snap logic
- [x] Entity shadow and sprite anchored at tile centre; facing indicator centred on body

### Tests ✓
- [x] `TilePos` hash includes z — (1,2,3) and (1,2,4) are distinct keys
- [x] Two entities at same (x,y) but different z do not collide
- [x] Entity at same (x,y,z) still collides
- [x] `heightAt` ignores z coordinate
- [x] `levelAt` formula: `round(heightAt * 4)`, consistency, z-invariance
- [x] `SpatialGrid::query` z-plane filtering (returns at matching z, excludes others)
- [x] Player z matches terrain level at spawn
- [x] Player destination z set from terrain when initiating a move
- [x] Player blocked from stepping to tile with height diff > 1 (integration)

---

## Phase 10 — Animations & Visual Effects (core done; sprite animation + tile variants pending)

Sprite animation system and a general-purpose particle/effect layer. All effects
are renderer-side only — game logic is unaffected.

### Sprite animation infrastructure ✓
- [x] `AnimFrame` — sprite region + duration in ticks (`effects.hpp`)
- [x] `Animation` — named sequence of `AnimFrame`s, looping or one-shot, `frameAt()`
- [x] `AnimState` enum — `Idle`, `Walk`, `Hit`, `Die`

### Sprite animation renderer integration (pending)
- [ ] `SpriteCache` extended: loads animation strips; keyed by `(EntityType, AnimState)`
- [ ] Per-entity animation state tracked in renderer (not game logic)
- [ ] Walk animation synced to movement (`moveT`)
- [ ] Idle animation: N-frame loop while entity is not moving

### Entity effects ✓
- [x] Hit flash — `EntityFlash` in renderer; tints sprite red for N frames on damage
- [x] Death fade — `DyingEntity` list; fading sprite rendered after despawn
- [x] Drop shadow — `drawShadow()` drawn beneath each entity

### Tile effects ✓
- [x] Fire tile — orange-red colour with per-frame flicker
- [x] Portal shimmer — pulsing colour shift each frame
- [x] Puddle ripple — ripple animation driven by `rendererTick_`
- [ ] Water ripple — pending Water tile type implementation
- [ ] Neighbour-aware tile variants — edge/corner sprite selection

### Pickup / interaction effects ✓
- [x] Dig particles — brown burst (`spawnBurst` on `VisualEvent::Dig`)
- [x] Collect sparkle — yellow burst on `CollectMushroom`
- [x] Summon burst — blue burst on `Summon`
- [x] Footstep dust — small burst on `PlayerLand`

### Screen-level effects ✓
- [x] Screen shake — `triggerShake()`, decays exponentially each frame
- [x] Fade — `triggerFade()`, black overlay for portal entry and grid switch

### Procedural tile detail ✓
- [x] Position-based decoration — hash-based flowers/stones on Grass; cracks on BareEarth
- [ ] Detail sprites respect z-level / perspective occlusion
- [ ] Neighbour-aware tile variants (cliff edge caps, grass-to-earth transitions)

### Atmospheric ✓
- [x] Ambient dust particles — drifting motes in world grid (capped at 150)
- [x] Day/night tint — sinusoidal colour shift on terrain (`dayNightT_`)

### Per-entity fire / electrical effects ✓
- [x] `burning` flag → pulsing orange overlay + rising fire sparks (~15/sec)
- [x] `electrified` flag → flickering cyan overlay + random electric sparks (~12/sec)

### VisualEvent system ✓
- [x] `VisualEventType` enum + `VisualEvent` struct in `game.hpp`
- [x] `Game::tick()` emits events; `main.cpp` drains and routes to renderer

### Tests ✓
- [x] `Animation::frameAt` — looping, one-shot, edge cases (15 tests)
- [x] `Particle::tick` — decay, movement, death
- [x] `shakeDecay` — monotone, convergence, zero

---

## GPU Rewrite (future — post Phase 23)

**Decision**: The CPU simulation is a feature prototype, not the final architecture. Once the feature set is fleshed out (roughly post-Phase 20), do a full redesign targeting GPU compute (Vulkan compute shaders or similar).

Design implications for the rewrite (not the current code):
- Struct-of-arrays layout for all entity/tile data
- Fixed-size entity slots, no pointers or hash maps as canonical store
- Tick logic as embarrassingly parallel compute shaders
- Renderer integrated with GPU sim (no CPU→GPU upload per frame)

Current code does **not** need to accommodate this — just build features cleanly on CPU. The rewrite will start fresh.

---

## Known Issues
- [ ] Rendered tiles are not consistently sized — minor visual inconsistency in the perspective projection, low priority

---

## Pre-Phase 11 Refactors

Small fixes and tech-debt items identified in code review. Do these before starting
Phase 11 so the codebase is in good shape for the next wave of features.

- [x] **Grid tick coupling** — already a single loop with `activeAtStart` capture; no change needed
- [x] **Height logic duplicated** — extracted to `static bool resolveDestHeight(TilePos&, const TilePos&, const Grid&)` in `game.cpp`
- [x] **Single-slot `pendingTransfer_`** — replaced `std::optional<PendingTransfer>` with `std::vector<PendingTransfer> pendingTransfers_`; multiple portal arrivals in the same tick now queue correctly
- [x] **No fire/voltage tests** — `burning` and `electrified` flag tests already present in `test_fire_voltage.cpp`

Pre-Phase 13:
- [x] **Agent state fragmentation** — merged `agentStates_` + `agentRecordings_` into `agentSlots_: unordered_map<EntityID, AgentSlot>` where `AgentSlot { AgentExecState state; Recording rec; }` (HIGH)

Pre-Phase 14:
- [x] **`Game` class too large** — split `tickFire`/`tickVoltage`/`tickWater` into `stimulus.cpp`; `tickMovement`/`tickGoblinWander` into `movement.cpp`. `game.cpp` now handles only player input, VM, scheduling, persistence, and accessors (MEDIUM)
- [ ] **StimulusField abstraction** — deferred. Fire, voltage, and water have fundamentally different spread models (timer-based, BFS, volume-conserving) that don't compose cleanly into a generic struct without obscuring the logic. Revisit if a fourth stimulus type is added. (was: "fire and voltage not abstracted")

Deferred / as needed:
- [ ] **ECS migration** — target architecture is ECS. Introduce `ComponentStore<T>` (wraps `unordered_map<EntityID, T>`) as the component store primitive. `FluidComponent { float h, vx, vy }` is the first instance (Phase 18). Migrate other type-specific state (burn timers, voltage, lit flag) to components incrementally. Core `Entity` struct retains identity fields only.
- [ ] **Collision resolution hardcoded** — `resolveCollision()` is a nested `switch`. When entity types expand in Phase 12, convert to a 2D lookup table `constexpr CollisionResult COLLISION_TABLE[ET_COUNT][ET_COUNT]` (MEDIUM)
- [ ] **Save format versioning policy** — bump version on every layout change; mismatch = fresh world (no migration). Document what changed in a comment next to the version constant. Current: v7. (Policy decided — just needs discipline per phase)
- [ ] **Entity pointer instability** — `EntityRegistry` stores entities in `unordered_map`; pointers/references invalidate on rehash. Callers that cache `Entity*` across ticks may see stale pointers. Audit usages; switch to ID-only access pattern or use a slot-map (MEDIUM)
- [ ] **Z-level queries unchecked** — a few `SpatialGrid::at()` calls pass a `TilePos` with `z=0` when the intent is "any z". Add a `atAnyZ(x,y)` helper or audit call sites (LOW)
- [ ] **Entity placeholder audit** — all current `EntityType` names (`Goblin`, `Mushroom`, `Poop`, `Campfire`, `TreeStump`, `Log`, `Battery`, `Lightbulb`) are temporary placeholders. Before Phase 12, update `ENTITIES.md` with final names and rename throughout the codebase. (See `DESIGN.md § Entity Placeholders`)

---

## Phase 11 — Mana Economy & Script Costs ✓

Mana is the central resource. Every deployed script and every summoned entity draws
from the same pool, creating a real trade-off between automation and unit diversity.

- [x] `constexpr instrCost(OpCode, RelDir)` cost table in `routine.hpp` — single source of truth for balancing
- [x] `Recording::manaCost()` — sums instruction costs across the full instruction vector
- [x] Cost shown in the recordings panel next to the step count (`♦ N`)
- [x] Deploying an agent checks `player.mana >= recording.manaCost()`; deducts on success; blocked silently if insufficient
- [x] Agents that reach `HALT` do not refund mana — the cost is the price of automation, not a deposit
- [x] Costs defined in one place (`routine.hpp`) so balancing is a single-file edit

**Tests** ✓ (20 new tests)
- [x] `instrCost` for all opcodes and RelDir variants
- [x] `manaCost()` for known instruction sequences matches expected total
- [x] Deploy blocked and mana unchanged when insufficient; succeeds and deducts when sufficient

---

## Phase 12 — Golem System & Entity Diversity ✓

Implement the golem summoning system.

- [x] `Entity` gains a `capabilities` bitfield — `CanExecuteRoutine`, `Pushable`, `CanFight`, `ImmuneFire`, `ImmuneWet`
- [x] `entityCaps(EntityType)` — returns capability flags for every entity type
- [x] `isGolem(EntityType)` — returns true for all 8 golem types
- [x] Summon verb (`E`): spawns golem from medium tile ahead (defaults to Mud Golem on any tile); mana deducted; tile NOT consumed
- [x] Golems execute player-recorded routines via the VM; despawn at HALT (same as all routine agents)
- [x] Summon preview in HUD: golem name + mana cost; gold when affordable, red when not
- [x] New entity types: Tree, Rock, Chest, MudGolem, StoneGolem, ClayGolem, WaterGolem, BushGolem, WoodGolem, IronGolem, CopperGolem
- [x] New tile types: Mud, Stone, Clay, Bush, Wood, Iron, Copper (medium summoning tiles)
- [x] Pushable objects (Log, Rock) — shoved one tile on player bump; blocked if destination occupied
- [x] Chest collected on arrival: +5 mana
- [x] Collision table extended: golem vs goblin, golem vs player, pushable bump logic
- [x] Facing indicator exclusion extended for Tree, Rock, Chest, and all golems
- [x] `VisualEvent::Summon` — blue burst + camera shake
- [x] Save format bumped v7 → v8

**Tests** ✓ (23 new tests)
- [x] `isGolem` — all 8 golem types; Player/Goblin not golems
- [x] `entityCaps` — Pushable on Log/Rock; CanExecuteRoutine/ImmuneWet on MudGolem; ImmuneFire on StoneGolem; CanFight on IronGolem/WoodGolem
- [x] Collision: IronGolem/WoodGolem vs Goblin → Hit; MudGolem vs Goblin → Block; Golem vs Player → Block; Player vs Golem → Block
- [x] Capability flags are distinct powers of two

---

## Phase 13 — Routine VM Expansion ✓

Extend the instruction set so agents can interact with terrain and react to stimuli.

### New opcodes ✓
- [x] `DIG` — agent digs the tile in its facing direction; emits Dig audio + visual event
- [x] `PLANT` — agent plants mushroom ahead if tile is `BareEarth`
- [x] `JUMP addr` — unconditional jump to instruction index
- [x] `JUMP_IF cond threshold addr` — jump if `stimulus[cond] > threshold`
- [x] `JUMP_IF_NOT cond threshold addr` — jump if `stimulus[cond] <= threshold`
- [x] `CALL addr` — push return address onto 8-slot call stack, jump to subroutine
- [x] `RET` — pop call stack, jump back; unmatched RET halts safely
- [x] Call stack overflow (depth > 8) halts safely

### Stimulus sampling ✓
- [x] `tickVM` computes `uint8_t stimuli[8]` per agent before calling `vm_.step()`
- [x] `Fire` — agent's current tile is `TileType::Fire`
- [x] `Wet` — agent's current tile is `TileType::Puddle`
- [x] `EntityAhead` — any entity occupies the tile directly ahead of the agent
- [x] `AtEdge` — agent is on the boundary tile of a bounded grid

### Recorder support ✓
- [x] `Recorder::recordDig()` — emits WAIT (if paused) then DIG
- [x] `Recorder::recordPlant()` — emits WAIT (if paused) then PLANT
- [x] `tickPlayerInput` calls `recorder_.recordDig()` / `recordPlant()` when player acts
- [x] Mana cost table complete: `DIG = 3`, `PLANT = 2`, `JUMP* = 0`, `CALL/RET = 0`

**Tests** ✓ (25 new tests)
- [x] DIG/PLANT set correct VMResult fields, advance PC
- [x] JUMP sets PC to target; backward jump works
- [x] JUMP_IF: jumps when stimulus present; falls through when absent; null stimuli = all zero
- [x] JUMP_IF_NOT: complement behaviour
- [x] CALL: pushes return address, jumps to subroutine
- [x] CALL/RET round-trip restores PC and leaves stack clean
- [x] CALL overflow (depth 9) halts safely
- [x] RET with empty stack halts safely
- [x] Recorder recordDig/recordPlant: emit correct instruction sequences with WAIT
- [x] instrCost for all new opcodes

---

## Phase 14 — Environmental Interactions ✓

(Alchemy engine deferred: combination rules, StimulusField abstraction, and discovery
UI all TBD in ALCHEMY.md — not ready to implement.)

### Environmental interactions ✓
- [x] `Water` tile type — added to `TileType` enum; rendered as animated blue with slow wave
- [x] `tickWater()` per grid each tick — water expands to adjacent tiles at same or lower height (diff ≤ 1), skips Fire/Portal; batched so one expansion step per tick
- [x] Water slows movement — on arrival at a Water tile, entity speed halved (reset on exit)
- [x] Fire × Water extinguish — any Fire tile adjacent to a Water tile is immediately extinguished (→ BareEarth) in tickFire
- [x] `Wet` Condition updated — now fires on both `Puddle` and `Water` tiles
- [x] Save format bumped v8 → v9 (new Water tile type)
- [x] Player mana floor: never drops below 1 after any mana-spending action

**Tests** ✓ (10 new tests)
- [x] Water is a distinct TileType; Terrain can set/read it
- [x] tickWater: water expands to same-level adjacent tile in one tick
- [x] tickWater: existing water tile stays Water
- [x] tickWater: does not overwrite Portal or Fire tiles
- [x] tickFire: Fire adjacent to Water is extinguished; Fire not adjacent to Water is unaffected
- [x] Mana floor: player mana stays ≥ 1 after repeated Plant actions

### Deferred to later phases
- [ ] Generic `StimulusField` abstraction replacing hardcoded fire/voltage (pre-GPU rewrite)
- [ ] Element combination rules + discovery (ALCHEMY.md all TBD)
- [ ] Alchemy UI panel (needs UI layer from Phase 16)
- [ ] Fire agent flee sub-routine (authored routine, not code)
- [ ] World gen water pre-seeding (Phase 17)

---

## Phase 15 — Studio Overhaul ✓

Upgrades the studio from a blank recording space into a full multi-agent editing
environment.

### Path overlay ✓
- [x] `routinePath(Recording, TilePos origin, Direction facing)` — simulate a recording from fixed origin `{10,10}` / Direction::S; returns `PathStep` sequence; capped at 512 steps
- [x] Renderer draws the path as colour-coded arrows on the studio floor; WAIT steps shown as dots
- [x] Path recomputed on studio entry and on every instruction edit
- [x] All recordings shown simultaneously, each with a distinct 8-colour palette

### Step scrubber ✓
- [x] Timeline bar at bottom of screen: one cell per instruction; conflict ticks red
- [x] Advance/rewind with `[` / `]`; pause/play toggle with Space
- [x] Ghost entity — translucent sprite at scrub position for selected recording

### Instruction list panel ✓
- [x] Right-side panel listing raw instructions with index prefix
- [x] Scrub position highlighted yellow; selected row highlighted; scrolls to stay visible
- [x] Rows selectable with Up/Down when panel focused (`P`)
- [x] Panel hidden when any menu (recordings/controls/rebind) is open

### Instruction editing ✓
- [x] Delete selected instruction (Backspace in panel-focused mode): removes, recomputes path; JUMP/CALL addresses auto-fixed
- [x] Insert `WAIT N` at cursor (`W` + digit entry + Enter/Esc)
- [x] Insert `MOVE_REL dir` at cursor (`M` + arrow + Enter/Esc)
- [x] Reorder: Shift+Up/Down moves selected instruction; JUMP/CALL addresses auto-fixed
- [x] Delete script from recordings panel: `Del` key while recordings panel open

### Multi-agent coordination ✓
- [x] All recordings shown simultaneously on the studio floor
- [x] Overlapping paths highlighted red; conflicting ticks marked on timeline bar

### Playback controls ✓
- [x] Loop mode (`L`): scrubber restarts at end
- [x] Speed control (`+` / `−`): 0.5×, 1×, 2×, 4×
- [x] Reset (`0`): scrubber returns to tick 0

**Tests** ✓
- [x] `routinePath` correct for empty, HALT, MOVE, WAIT, maxSteps cap, instrIdx tracking
- [x] `studioConflicts` single path / diverging / shared tile / multiple conflicts
- [x] `deleteInstruction` / `insertWait` / `insertMoveRel` smoke tests

---

## Phase 16 — Mouse Interaction & UI Layer ✓

### UI layer ✓
- [x] `TextCache` — maps `(string, packed-RGBA)` to `SDL_Texture*`; textures created on first use; eliminates per-frame allocation in `drawText`
- [x] `Rect` — screen-space rectangle with `contains(x,y)` for hit testing
- [x] `Panel` — background colour + border colour struct
- [x] `Label` — text + colour + left/centre/right alignment
- [x] `ListWidget` — scrollable rows; `itemAt(y)` + `scrollTo(index)`
- [x] `Button` — label + hover/pressed state
- [x] `UIState` struct — `activePanel` enum (`None`, `Controls`, `Recordings`, `Rebind`); replaces loose bools; `open()`, `close()`, `isOpen()`, `is()`
- [x] Input routing — mouse and keyboard events go to active panel first; fall through to game if no panel active

### Mouse interaction ✓
- [x] Tile picking — `Renderer::screenToTile()` inverse perspective; `screenToTileAccurate()` two-pass terrain-aware version in `main.cpp`
- [x] Entity picking — `game.entityAtTile(hoveredTile)` in draw order
- [x] Hover highlight — translucent overlay on hovered tile via `drawHoverHighlight()`
- [x] Entity hover tooltip — name drawn above cursor via `drawEntityTooltip()`
- [x] Left-click to move — `game.queueClickMove(hoveredTile)` on valid tile face only
- [x] Right-click context menu — `Panel` + item list; "Move here", "Dig", entity name label
- [x] Click ripple effect — `spawnBurst` at clicked tile position
- [x] Middle-drag to pan — `middleDragging` state; offsets `camOffset`
- [x] Cursor changes — `setHandCursor(bool)` swaps OS arrow ↔ hand cursor
- [x] Panel hit testing — left-clicks absorbed by active panel or context menu before reaching world

**Tests** ✓
- [x] `screenToTile` round-trips at default camera, offset camera, zoom > 1, elevated camera z
- [x] `entityAtTile` returns player at player pos; nullptr for empty tile
- [x] `UIState` open/close/is; opening closes previous panel
- [x] `Rect::contains` boundary conditions
- [x] `ListWidget::itemAt` and `scrollTo`
- [x] `queueClickMove` moves one step toward target; no-op at same tile

---

## Post-Phase 16 — Summon System Overhaul ✓

Reworked the golem summoning system after Phase 15/16:

- [x] **`SUMMON` opcode** (`OpCode::SUMMON`, cost 5) — new VM instruction; agents execute it to summon a golem at the tile ahead
- [x] **`Recorder::recordSummon(targetRecIdx)`** — records SUMMON intent unconditionally when `E` pressed (like DIG, regardless of tile); encodes selected recording index in `instr.addr`
- [x] **Target recording encoded in instruction** — `instr.addr` = selected recording index at record time; summoned golem receives that exact recording (not the summoning agent's own)
- [x] **No tile consumption** — summoning does not dig/consume the medium tile; medium tiles are reusable
- [x] **Mud Golem fallback** — summon on any tile (Grass, BareEarth, etc.) defaults to spawning a Mud Golem; medium tiles still yield their specific golem types
- [x] **All routine agents despawn at HALT** — golems and Poop agents all despawn when their script ends
- [x] **Deploy action removed** — `Action::Deploy` and Poop-spawning removed; `E` now summons a golem
- [x] **Strafe recording fix** — `MOVE_REL` instructions store a strafe flag in `instr.threshold`; VM passes `isStrafe` through `VMResult`; agent facing only updates on non-strafe moves; `routinePath` in studio also tracks facing correctly
- [x] **Studio medium tiles persist through load** — `load()` re-applies Mud/Stone/Clay tiles in GRID_STUDIO after `clearOverrides()`; tiles only restored if not already consumed
- [x] **tickVM deferred-add fix** — new agents spawned by SUMMON collected in `toAdd` and inserted into `agentStates_` after iteration completes (inserting during `unordered_map` iteration is UB)

---

## Phase 17 — Fluid Dynamics

Introduces `ComponentStore<T>` as the first ECS scaffold, and replaces the old
`TileType::Water` / `tickWater()` model with entity-based shallow water simulation.

### ComponentStore
- [ ] `ComponentStore<T>` — thin wrapper: `unordered_map<EntityID, T>` with `add()`, `get()`,
      `remove()`, `has()`, `all()`. Lives in `component_store.hpp`. No dependencies.
- [ ] `FluidComponent { float h, vx, vy; }` — first component. Stored in a
      `ComponentStore<FluidComponent>` owned by `Game`.

### Fluid entities
- [ ] `EntityType::Water` added; one Water entity per wet tile
- [ ] `TileType::Water` removed from enum; old `tickWater()` removed from `stimulus.cpp`
- [ ] Water entities have no sprite, no shadow, no facing indicator — rendered as a tile overlay
      based on `FluidComponent::h`

### Shallow water simulation (`tickFluid`)
- [ ] Per-tick update of `(h, vx, vy)` for every entity with a `FluidComponent`
- [ ] **Force step**: add gravity contribution from terrain height gradient to velocity
- [ ] **Advect step**: transfer mass between neighbouring cells proportional to velocity;
      clamp to avoid negative depth
- [ ] Spawn new Water entity + FluidComponent for newly wet neighbour tiles
- [ ] Despawn Water entity when `h` drops below threshold

### Preserved behaviours
- [ ] `Wet` stimulus condition: fires when entity's tile has a Water entity with `h > epsilon`
- [ ] Water slows movement (speed halved on arrival at tile occupied by Water entity)
- [ ] Fire × Water extinguish: Fire tile adjacent to Water entity → BareEarth (in `tickFire`)

### Save format
- [ ] Bump save version; serialise Water entities and their `FluidComponent` data

### Tests
- [ ] `ComponentStore`: add, get, remove, has; iterate all; get on missing ID returns nullptr
- [ ] `FluidComponent` present on Water entity; absent on Goblin
- [ ] Water spreads to adjacent lower tile over N ticks
- [ ] Water entity despawns when `h` reaches zero
- [ ] `Wet` condition fires when standing on Water entity tile; not on dry tile
- [ ] Fire adjacent to Water entity → extinguished

---

## Phase 18 — World Generation

### Biome map
- [ ] Second Perlin noise layer → biome type per tile (Grassland, Forest, Volcanic; Mountains from height threshold)
- [ ] `Terrain::biomeAt(TilePos)` — returns `Biome` enum; deterministic, cached like `typeAt`
- [ ] World gen seeds tile types and entities by biome on first load

### New tile type
- [ ] `TileType::Straw` — result of scything Grass; harvestable material

### New entity types
- [ ] `EntityType::Rabbit` — wanders, eats grass tiles, reproduces in warrens
- [ ] `EntityType::Warren` — static structure; houses rabbits; spawns new warrens when full; despawns when empty for too long
- [ ] `EntityType::IronOre`, `CopperOre`, `CoalOre`, `SulphurOre` — ore deposit entities; static, embedded in terrain/caves. Mining → gains `Pushable` + collectable (walk-over). Collected loose ore placed on Fire tile → transforms to RawMetal after N ticks.

### Procedural entity spawning
- [ ] Goblins spawn in clusters in Grassland
- [ ] Warrens + rabbits seeded in Grassland
- [ ] Trees seeded densely in Forest biome
- [ ] Ore entities seeded in clumps in Mountain / Volcanic terrain

### Caves
- [ ] Cave = bounded room grid containing ore entities; entrance = portal tile in world terrain
- [ ] Cave closes (portal removed) when all ore inside is mined; new cave opens elsewhere in same biome
- [ ] Cave interiors seeded with ore at creation

### Ecosystem simulation
- [ ] **Grass regrowth** — BareEarth in Grassland slowly converts back to Grass (random chance per tile per tick)
- [ ] **Tree spread** — Forest biome only: trees colonise adjacent BareEarth over time; player-planted trees outside forest do not spread
- [ ] **Goblin hunger** — `HungerComponent { int hunger; int threshold; int max; }` on Goblin entities; increments each tick; hunt mode above threshold; despawn at max
- [ ] **Rabbit AI** — wanders, eats adjacent Grass (→ BareEarth), reproduces into nearest warren
- [ ] **Warren logic** — spawns Rabbit up to capacity; spawns new Warren nearby when full; despawns after timeout when empty
- [ ] **Goblin hunt AI** — seeks nearest Rabbit when hungry; rabbit despawns on collision, goblin hunger resets

### New VM opcodes
- [ ] `SCYTHE` — scythes tile ahead: Grass → Straw. Cost TBD.
- [ ] `MINE` — mines ore entity ahead: entity gains Pushable + collectable state. Cost TBD.

### Tests
- [ ] `biomeAt` is deterministic across repeated calls with same seed
- [ ] Mountain tiles appear above height threshold regardless of biome noise
- [ ] World generates without crash on any seed
- [ ] Grass regrows on BareEarth in Grassland over time
- [ ] Warren spawns rabbits; despawns when empty
- [ ] Goblin despawns at max hunger; hunger resets on eating rabbit
- [ ] Mined ore entity becomes pushable/collectable
- [ ] Cave portal removed when ore count reaches zero

---

## Phase 19 — Passive Grid Simulation

### Design decisions (settled)
- **Hibernation trigger**: no player in grid → analyse routines, pre-schedule outputs, hibernate. Player enters → cancel pending outputs, snap agents, resume full sim.
- **Always exact**: outputs match what full simulation would have produced (no approximation).
- **Scheduler reuse**: passive outputs go into the existing per-grid Scheduler; no new infrastructure needed.

Interior grids contain agents on deterministic loops. When the player is outside,
skip full simulation and replace it with pre-scheduled output events.

### Cycle analysis
- [ ] `analyseRoutine(Recording, TilePos origin, Direction facing)` — simulate one full loop, return `{cycleLength, outputs[]}` where each output is `{tickOffset, OutputType, value}`
- [ ] `OutputType` enum: `ProduceMana`, `HarvestMushroom`, `SpawnEntity`, `DigTile` — anything a routine can cause
- [ ] Handle non-looping routines (end with HALT, no loop): treat as single-shot, no repeat scheduling
- [ ] Cache analysis result per recording; invalidate on instruction edit

### Dehydration (player leaves grid)
- [ ] Compute cycle position for each agent: `pos = (now - cycleStartTick) % cycleLength`
- [ ] Push scheduled output events for next N cycles into the grid's `Scheduler`
- [ ] Mark grid as `passive = true`; skip in the main tick loop

### Hydration (player enters grid)
- [ ] Cancel all pending passive output events from the `Scheduler`
- [ ] Snap each agent to its correct position/facing for `now % cycleLength`
- [ ] Set `passive = false`; resume full simulation from that tick

### Consistency guarantees
- [ ] Outputs fired while passive are identical to what full simulation would have produced
- [ ] Hydrated agent state is consistent with terrain changes caused by passive outputs (e.g. a tile dug by a passive output is dug when the player enters)
- [ ] If a passive output would cause a contradiction (e.g. harvest a mushroom that no longer exists), skip that output silently

**Tests**
- [ ] `analyseRoutine` returns correct cycle length and output offsets for a known recording
- [ ] Passive grid fires mana output at correct tick without being ticked
- [ ] Hydrated agent position matches what full simulation would have produced at the same tick

---

## Phase 20 — Assets, Audio & Polish

### New assets
- [ ] Proper sprite art for all entity types (player, goblin, mushroom, poop, campfire, tree, rock, chest, digger, farmer, guardian)
- [ ] Terrain tile sprites — textured tiles (grass, bare earth, stone, water, fire)
- [ ] Wall and structure tiles for room interiors
- [ ] Real audio — replace placeholder WAV with composed OGG tracks (SFX + music layers)

### Input polish
- [ ] Multiple controller profiles — last-used device priority; hot-swap keyboard/gamepad mid-session
- [ ] Conflict detection in rebind panel — warn when two actions share a key; reset-to-default option

### Bug fixes
- [ ] Rendered tiles not consistently sized — fix perspective projection tile-size inconsistency

---

## Phase 21 — Localisation

- [ ] String table — all in-game text moved to `assets/locale/en.ini`; source references by key only
- [ ] `Locale` class — `get(key)` returns `const std::string&`; falls back to key string if missing
- [ ] Language setting in rebind/settings panel — lists locale files in `assets/locale/`; takes effect immediately
- [ ] Locale persistence — selected language stored in `settings.dat`; loaded before any text is rendered
- [ ] Font coverage — replace DejaVuSansMono with a font covering Latin extended + common non-Latin scripts (e.g. Noto Sans)
- [ ] RTL support (stretch) — layout mirroring for Arabic/Hebrew; text right-aligned in panels

---

## Phase 22 — Combat System

Design decision on combat style (VATS-style slow-time, stop-time, or turn-based) must be made
before this phase begins. See `DESIGN.md § Combat`.

- [ ] Combat capability for golems — damage dealing, targeting, pathfinding to enemy
- [ ] Enemy AI routines — authored bytecode, same VM as golems
- [ ] Death, drops, and loot

---

## Phase 23 — Platform & Multiplayer

- [ ] Touchscreen input layer — all actions accessible via touch, co-equal with keyboard and gamepad
- [ ] Port to additional platforms per `DESIGN.md § Platform` (order TBD)
- [ ] Split-screen multiplayer — design must be settled before implementation
