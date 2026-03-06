# Alchemy Master List

Authoritative reference for all alchemy elements, spreading effects, and combination rules.
All lists are placeholders — subject to change as the design evolves.

---

## Spreading Effects

A **spreading effect** is a named system that propagates across tiles each tick via a
**spread equation**. Each effect has a set of **underlying quantities** (per-tile float
fields) that the equation takes as inputs. The spread equations are the embarrassingly
parallel GPU compute kernel — one pass per effect per tick.

| Effect        | Underlying quantities          | Spread equation depends on        |
|---------------|-------------------------------|-----------------------------------|
| Fire          | heat, wetness                  | heat spreads; wetness suppresses  |
| Electricity   | voltage, conductivity          | voltage propagates through high-conductivity tiles |
| Cold          | TBD                            | TBD                               |
| Wet           | TBD                            | TBD                               |
| Wind          | TBD                            | TBD                               |

All entries are placeholders. New effects require: a named system, a set of quantities,
and a spread equation. Agents sense effects (not quantities) via `JUMP_IF` / `JUMP_IF_NOT`.

**Current implementation (pre-alchemy-engine):** Fire and Electricity are hardcoded as
two separate systems in `game.cpp`. These will be replaced by the generic spreading effect
framework in Phase 14.

---

## Base Elements

The base elements form the foundation of the combination system. All are placeholders.

| # | Element | Notes |
|---|---------|-------|
| 1 | Earth   | TBD   |
| 2 | Air     | TBD   |
| 3 | Water   | TBD   |
| 4 | Fire    | TBD   |
| 5 | Curse   | TBD   |
| 6 | Goo     | TBD   |
| 7 | Holy    | TBD   |
| 8 | Acid    | TBD   |

---

## Combination Rules

Doodle-God-style: combining two elements or materials produces a new one. Full table TBD.
The Philosopher's Stone is the terminal combination.

| Input A | Input B | Output              | Notes       |
|---------|---------|---------------------|-------------|
| Lead    | Sulfur  | Battery             | placeholder |
| Heat    | Water   | Steam               | placeholder |
| Mana    | Mud     | Mushroom            | placeholder |
| Fire    | Goo     | Acid                | placeholder |
| Holy    | Curse   | (annihilation)      | placeholder |
| …       | …       | …                   | TBD         |

**Philosopher's Stone:** terminal combination — requires all 8 elements and multiple
intermediate products. Recipe TBD.
