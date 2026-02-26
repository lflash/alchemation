# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

A C++ rewrite of a Python pygame grid game. The Python original is at
`~/Desktop/code/python/grid_game/` and can be referenced for gameplay behaviour,
but the architecture is being redesigned from scratch. See `DESIGN.md` for the current
design and `TODO.md` for build order and progress.

**This is an active, evolving project.** Design decisions are made incrementally
through conversation with the user. `DESIGN.md` and `TODO.md` reflect the current
thinking, not a frozen spec. Before implementing anything non-trivial, discuss the
approach. Do not treat any design as fixed without checking with the user first.

## Working Style

- **Ask before acting on anything ambiguous.** The user makes design calls; Claude
  implements them.
- **Read before writing.** Understand the existing code before suggesting changes.
- **Don't add unrequested features.** If a task is "add X", don't also refactor Y.
- **Commit after each completed phase** so the user has reliable rollback points.
  Use `git log` to see what has been committed.
- **One instance at a time.** A previous Claude instance went rogue and added
  unsolicited code (spatial.cpp, collision.cpp) that had to be manually reverted.
  If you are unsure whether to write a file, ask first.

## Build

```bash
cmake -B build -S .
cmake --build build
./grid_game          # run game (from project root — asset paths are relative)
cd build && ctest    # run tests (run the binary directly for full doctest output)
build/tests
```

Dependencies: `libsdl2-dev`, `libsdl2-image-dev`, `cmake`

## Current Architecture Thinking

These are the directions the design is heading. They are based on decisions made so
far in conversation, but the user may revise any of them.

- **Two coordinate types**: `TilePos {int x,y}` for game logic; `Vec2f {float x,y}`
  for rendering interpolation only.
- **Fixed 50 Hz timestep**; rendering runs uncapped and interpolates with `alpha`.
- **Spatial grid + AABB**: broad phase via tile-resolution hash map, narrow phase via
  AABB intersection. Sprites are smaller than tiles so tile-boundary collision is wrong.
- **Dual registration**: entities in motion register in both `pos` and `destination`
  cells simultaneously. This is still being thought through.
- **Collision resolution by `(mover, occupant)` type pair** — not a property of
  either entity alone.
- **Single spatial grid per `Grid`** — not split by collision layer.
- **Multiple `Grid` instances** are independent simulation spaces (main world, studio,
  interiors, parallel universes). Not collision layers.
- **Recordings store relative tile deltas** `{dx, dy, delayTicks}`, not absolute positions.
- **Terrain overrides** (`BareEarth`) are state on the `Terrain` object, not entities.
- **Scheduler is a min-heap** ordered by tick.

## Naming Conventions

Established names. Prefer these over inventing new ones, but raise it if something
feels wrong.

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
  entity.hpp / .cpp      ← Entity, EntityRegistry          [Phase 2 — done]
  input.hpp / .cpp       ← Input snapshot                  [Phase 2 — done]
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
  test_phase2.cpp        ← Entity, EntityRegistry, Input, stepMovement tests
assets/
  sprites/               ← PNGs copied from Python project
  entities.json          ← entity type config [not yet written]
```
