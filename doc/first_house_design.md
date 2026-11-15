# First furnished house: implemented design

The first-house slice is implemented: an authored house with two basement variants,
furniture, loot and monsters is integrated with generation, World and the
`FirstHouse` new-game scenario. Verification and review commands are recorded in
[the review guide](first_house_review.md).

## Scenario and content

`Server(content, seed, NewGameScenario::FirstHouse)` starts the player on the
planned driveway outside a generated house entrance, carrying a backpack with a
bandage inside. The existing constructor continues to select the demo scenario.
The curated first-house pack is `data/first_house.json5`; `data/demo.json5` preserves the demo
pack. Both reference shared files including `data/terrain/base.json5`,
`data/items/demo.json5`, and `data/objects/demo.json5`. The house pack adds
household items, recipes, furniture, loot, cellar monsters and the furnished
building from the paths in its manifest. See [content packs](content_pack.md)
and [house content](house_content.md).

The existing regional road and parcel planner places the building. Authored
surface and basement layers, furniture, loot and monster markers are rotated
together. Basement selection and population use deterministic generation inputs;
generation emits placement values rather than owning runtime entities. World
materializes them for previously unsnapshotted chunks after terrain and restores.
Snapshots, including empty ones, prevent population from respawning after reload.
See [world generation](worldgen_api.md) and [World archives](world_api.md#generation-archives-and-brief-implementation-notes).

This milestone originally used generation version **4**. New worlds now use
generation **6**; see [street-led towns](urban_generation_v6_design.md). The
save format is not bumped for this slice. This milestone introduced network
protocol `0x43584e0e`; the later [specialist identity increment](urban_content_design.md)
uses `0x43584e0f`.
Placement keys are transient generation identities, not persistent entity IDs.
Reproducibility depends on the same seed, generation version and content; runtime
entity IDs depend on materialization history.

## Furniture and interaction

Shared immutable object definitions provide semantic appearance, movement and
sight blocking, movability, optional finite storage and initial light emission.
Runtime objects reuse their existing type, appearance, contents, light and behavior
fields rather than adding per-furniture properties. Passive furniture has no
per-furniture tick behavior. Lamps with authored light are actual light sources;
runtime enabled state gates their emission. There is no furniture dismantling in
this slice.

`World::MoveObject` is administrative relocation; `QueryObjectMove` and
`TryMoveObject` implement gameplay push/pull. Push and pull preflight both actor
and object positions and commit atomically; direct World calls do not schedule the
actor. Server charges successful actions once through normal scheduling.
Generated placements and moves require supported passable terrain; legacy
administrative `CreateObject` still permits fixtures on wall or air terrain.
Movement-blocking objects cannot overlap an unparked actor. See
[objects API](objects_api.md) and [object gameplay](object_gameplay.md).

Storage is opt-in and volume bounded. Crafting can use reachable object storage
through its existing bounded flood fill, including blocked furniture as an endpoint,
while its other recipe and selection policies remain in place. See
[crafting](crafting.md#selection-and-funding).
