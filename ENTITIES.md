# Entity Master List

Authoritative reference for all entity types — current placeholder names, intended
final names, capabilities, and which phase they land in. Update this file whenever
an entity type is added, renamed, or has its capabilities settled.

---

## Golems (summoned by player)

Eight golem types, one per summoning medium. All golems execute player-recorded routines via the VM.

| Medium   | Golem type    | Capabilities (draft)                                          | Phase |
|----------|---------------|---------------------------------------------------------------|-------|
| Mud      | Mud Golem     | Farming; immune to Wet; slow; high health                     | 12    |
| Stone    | Stone Golem   | Mining; immune to Fire; slow; very high health                | 12    |
| Clay     | Clay Golem    | Building/construction; medium speed; shapeable                | 12    |
| Water    | Water Golem   | Traverses water; extinguishes fire; fast; fragile             | 12    |
| Bush     | Bush Golem    | Plant harvesting; fast; fragile                               | 12    |
| Wood     | Wood Golem    | Tree chopping; medium speed; medium health                    | 12    |
| Iron     | Iron Golem    | Combat specialist; high damage; slow; high health             | 12    |
| Copper   | Copper Golem  | Electricity conduction; circuit-based automation; fast        | 12    |

---

## Placeholder entities (to be renamed/replaced)

Current code names that will be replaced as the design solidifies.

| Placeholder name | Category   | Likely final role                                        | Rename phase |
|------------------|------------|----------------------------------------------------------|--------------|
| `Goblin`         | Enemy      | Generic enemy type — final name/lore TBD                 | TBD          |
| `Mushroom`       | Resource   | Basic harvestable resource — name likely kept            | TBD          |
| `MudGolem`       | Agent      | Default deployed routine agent; also spawned from Mud tiles | 12        |
| `Campfire`       | Structure  | Fire stimulus source — final form TBD (brazier? hearth?) | TBD          |
| `TreeStump`      | Terrain obj| Burnable/choppable object — rename to `Tree` or similar  | 12           |
| `Log`            | Resource   | Dropped resource from chopped tree                       | 12           |
| `Battery`        | Structure  | Galvanic source — will be an alchemy product; high Positive principle | 22 |
| `Lightbulb`      | Structure  | Galvanic indicator — will be an alchemy product                       | 22 |

---

## Future entity types (not yet implemented)

| Name         | Category  | Description                                              | Phase |
|--------------|-----------|----------------------------------------------------------|-------|
| Tree         | Terrain obj| Blocks movement; choppable by Wood Golem                | 12    |
| Rock         | Terrain obj| Permanent blocker; mineable by Stone Golem              | 12    |
| Chest        | Structure  | Interactable; yields mana on collect; one-use            | 12    |
| NPC          | Character  | Static; dialogue tree; `canTalk` flag                    | TBD   |
| King         | Boss       | Final boss; defeated by golems, not player               | TBD   |
| Philosopher's Stone | Item | Terminal alchemy product; enables endgame              | TBD   |
