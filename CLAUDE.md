# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

A C++ rewrite of a Python pygame grid game. The Python original is at
`~/Desktop/code/python/grid_game/` and can be referenced for gameplay behaviour,
but the architecture is being redesigned from scratch. See `DESIGN.md` for the full
spec and `TODO.md` for build order and current progress.

## Build

```bash
cmake -B build -S .
cmake --build build
./grid_game          # run game (from project root — asset paths are relative)
cd build && ctest    # run tests
```

Dependencies: `libsdl2-dev`, `libsdl2-image-dev`, `cmake`

## Architecture Decisions (settled in design phase — do not relitigate)

- **Two coordinate types**: `TilePos {int x,y}` for all game logic; `Vec2f {float x,y}`
  for rendering interpolation only. Never use floats for collision, spatial queries,
  or recordings.
- **Dual registration**: entities in motion are registered in the `SpatialGrid` at
  both their `pos` and `destination` simultaneously. Only removed from `pos` on
  arrival. This prevents any entity entering a cell that is only partially vacated.
- **Collision is two-phase per tick**: collect all movement intentions first, then
  resolve. Swap conflicts (A→B's tile, B→A's tile same tick) are detected and blocked.
- **Broad + narrow phase**: `SpatialGrid` (tile-resolution hash map) for candidate
  lookup; AABB in world space for actual collision decision. Sprites are smaller than
  tiles so tile-boundary collision would be wrong.
- **Collision resolution is a lookup on (mover type, occupant type)** — not a property
  of either entity alone. The table is in `DESIGN.md`.
- **Single spatial grid per `Grid`** — not split by collision layer. Resolution table
  handles all interaction differences.
- **Multi-tile entities** register in every cell their bounds cover. Only the delta
  of old vs new cells is updated on movement.
- **Recordings store relative deltas** `{dx, dy, delayTicks}`, not absolute positions.
  Instantiation rotates deltas by fire direction and schedules directly against the
  projectile entity ID. No prototype ID remapping.
- **Terrain is not entities**. `BareEarth` is a `Terrain` override, not a spawned
  object. Digging calls `terrain.dig()`. Planting spawns a `Mushroom` entity and
  calls `terrain.restore()`.
- **Scheduler is a min-heap** (`std::priority_queue`) ordered by tick. Not a list.
- **Multiple grids** (`Grid` class) are independent simulation spaces — main world,
  studio (recording sandbox), room interiors, parallel universes. Not collision layers.
- **PIMPL on `Terrain`** — `FastNoiseLite.h` is only included in `terrain.cpp`.

## Naming Conventions

All names follow these conventions. Do not revert to Python-style names.

| Concept | Name |
|---|---|
| Game/simulation runner | `Game` |
| Single entity | `Entity` |
| Entity registry | `EntityRegistry` |
| Integer grid coordinate | `TilePos` |
| Float render coordinate | `Vec2f` |
| Entity type enum | `EntityType` |
| Direction enum | `Direction` |
| Action type enum | `ActionType` |
| Event type enum | `EventType` |
| Action scheduler | `Scheduler` |
| Input handler | `Input` |
| Landscape | `Terrain` |
| Hitbox | `bounds` (field), `Bounds` (type) |
| Draw order | `layer` |
| Move destination | `destination` |
| Entity type field | `type` |
| Progress toward destination | `moveT` |
| Step movement system | `stepMovement()` |
| Resolve collision | `resolveCollision()` |
| Draw order sort | `drawOrder()` |
| Remove entity | `destroy()` |
| Create entity | `spawn()` |
| Pop due actions | `popDue()` |
| Delay actions | `delay()` |
| Translate coords | `translate()` |
| Rebase tick origin | `rebaseFrom()` |
| Last scheduled tick | `endTick()` |
| Rescale action timing | `rescale()` |
| Retarget entity ID | `retarget()` |
| Vector magnitude | `length()` |
| Snap to tile | `toTile()` |
| Check collinear | `collinear()` |
| Tile to Vec2f | `toVec()` |

## File Structure

```
src/
  main.cpp
  types.hpp              ← TilePos, Vec2f, Bounds, enums, lerp, toVec, TilePosHash
  terrain.hpp / .cpp     ← Terrain (PIMPL over FastNoiseLite)
  renderer.hpp / .cpp    ← Renderer, SpriteCache (SDL2)
  entity.hpp / .cpp      ← Entity, EntityRegistry          [Phase 2]
  input.hpp / .cpp       ← Input snapshot                  [Phase 2]
  spatial.hpp / .cpp     ← SpatialGrid                     [Phase 3]
  scheduler.hpp / .cpp   ← Scheduler (min-heap)            [Phase 4]
  events.hpp / .cpp      ← EventBus                        [Phase 4]
  recorder.hpp / .cpp    ← Recording, Recorder             [Phase 7]
  grid.hpp / .cpp        ← Grid, multi-grid management     [Phase 8]
vendor/
  FastNoiseLite.h        ← single-header Perlin noise
  doctest.h              ← single-header test framework
tests/
  test_phase1.cpp        ← TilePos, lerp, Terrain tests
assets/
  sprites/               ← PNGs copied from Python project
  entities.json          ← entity type config [not yet written]
```
