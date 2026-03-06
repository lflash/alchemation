# World Interactions — Verb Master List

Authoritative reference for all world interaction verbs, their effects, and which entity
types can perform them. All entries are placeholders — subject to change as the design evolves.

---

## Verb Table

| Verb    | Effect                                                        | Actor(s)                        | Phase |
|---------|---------------------------------------------------------------|---------------------------------|-------|
| Hoe     | Converts Grass tile to BareEarth                              | Player, Farmer Golem            | TBD   |
| Plant   | Places a resource entity on a BareEarth tile                  | Player, Farmer Golem            | TBD   |
| Summon  | Spawns a golem from a medium tile                             | Player, (others TBD)            | 12    |
| Push    | Shoves a pushable entity one tile in facing direction         | Player, any Golem               | TBD   |
| Pull    | Pulls a pushable entity one tile toward actor                 | Player, any Golem               | TBD   |
| Grab    | Picks up a carryable entity (carried on top of actor)         | Player, any Golem w/ canCarry   | TBD   |
| Chop    | Deals damage to a Tree entity; reduces to Stump then Log      | Wood Golem                      | 12    |
| Mine    | Deals damage to a Rock entity; yields Stone medium            | Stone Golem                     | 12    |
| Carry   | Transports a held entity to a destination tile                | any Golem w/ canCarry           | TBD   |
| Consume | Uses a consumable entity (e.g. mushroom → mana)               | Player                          | TBD   |
| Dig     | Converts a terrain tile to BareEarth                          | Player, Digger Golem            | 5     |

---

## Notes

- Verbs map to `OpCode` instructions in the VM where applicable (e.g. `DIG`, `PLANT`).
- New verbs require: a capability flag on `Entity`, a VM opcode if agent-executable, and a
  recorder hook if player-recordable.
