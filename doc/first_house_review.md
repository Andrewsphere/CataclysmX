# First-house frontend review

## Launch from the repository root

```powershell
./out/build/win-msvc-debug/cataclysmx_game.exe --scenario first-house --seed 0
./out/build/win-msvc-sdl/cataclysmx_sdl_game.exe --scenario first-house --seed 0
./out/build/win-msvc-debug/cataclysmx_headless.exe --scenario first-house --seed 0 --script
```

For headless review, enter `view`, `inspect`, then `quit`. `inspect` reports actor
positions and world invariants. Generation 6 and revised content change new-game
starts, entrance directions and basement draws. Current curated-pack observations
use project coordinate order **X,Z,Y**:

| Seed | Player start | Move toward door | Basement |
| --- | --- | --- | --- |
| 0 | `(-71,-62,0)` | South | Workshop |
| 13 | `(-89,61,0)` | North | Workshop |
| 42 | `(78,-66,0)` | South | Workshop |

Existing saves retain their generation version, but reproducing ungenerated areas
also requires the same catalog. Both cellar variants are covered by a bounded-seed
server walkthrough test.

Walk toward the entrance twice: the first action opens the door, the second
enters. One more step in the same direction reaches the stairs; `<` descends.

Integration entry checks:

```powershell
./out/build/win-msvc-debug/cataclysmx_game.exe --scenario first-house --seed 0 --render
./out/build/win-msvc-debug/cataclysmx_game.exe --scenario first-house --seed 13 --render
./out/build/win-msvc-sdl/cataclysmx_sdl_game.exe --scenario first-house --seed 0 --smoke
./out/build/win-msvc-sdl/cataclysmx_sdl_game.exe --scenario first-house --seed 13 --smoke
```

House SDL smoke waits for session startup, assets, a prepared map, and a rendered
map/health frame. The default demo smoke retains its scripted interaction checks.
House smoke is a startup/render check, not a walkthrough or basement acceptance test.

## Content, seed, and saves

Demo is the default scenario and loads `LoadContentPack(CATACLYSMX_TERRAIN_FILE)`.
First-house selects `CATACLYSMX_FIRST_HOUSE_PACK` (`data/first_house.json5`). Explicit `--content` always calls
`LoadContentFile`; `--content-pack` always calls `LoadContentPack`. Supplying both
is a usage error. An explicit content path takes precedence over scenario defaults.

`--seed` accepts an unsigned 32-bit decimal value, including zero. It overrides
only the new-game construction seed; configuration files are not rewritten by
this override. Existing configured pacing, residency, and other preferences still
apply. Headless and terminal reject `--seed` with `--load`, so save seeds cannot
silently change. Their later save/load operations retain the existing semantics.

```powershell
./out/build/win-msvc-debug/cataclysmx_game.exe --scenario first-house --seed 0 --save house.db
./out/build/win-msvc-debug/cataclysmx_game.exe --content-pack data/first_house.json5 --load house.db
```

The same save/load flags work in headless. SDL retains its existing in-memory
session lifecycle. No scenario bit is persisted. Scenario validation runs before
new-game construction, not on saved-game loading; loading requires the matching
pack regardless of the original scenario.

## Controls and review actions

- Arrows move; `.` waits. `<` descends and `>` ascends matching stairs.
- Open the generic interaction menu (default `e`), choose a nearby tile and an
  action with arrows/Enter. `F1` shows current bindings; `F2` edits them. Terminal
  also supports directional interaction with `o` then a direction.
- Cabinets expose **Take contents**. Carry limits still apply. Crafting can use
  eligible cabinet ingredients/tools in place within the configured crafting
  radius (currently 1), subject to walls, corners and layer restrictions.
  Storage volume and item transfer APIs are implemented; a deposit/browser UI
  is a follow-up. For this review, use collection or craft directly from storage.
- Press **G**, then any adjacent direction, to grab movable furniture. Movement
  pushes, pulls or drags it sideways, preserving the offset in all eight directions.
  Both destinations must be clear; **G** releases the object. See [grabbing](grabbing.md).
- `x` examines; `g` picks up ground loot; `&` opens crafting. Use the existing crafting menu and
  inventory UI; no house-specific UI or art is required.

### Lighting and loot

Both surface rooms and both cellar variants contain enabled movable lamps using
normal world lighting. The headless walkthrough confirms a lit cellar after
descending; perception is not bypassed. Furniture currently uses the existing
generic/chair/table/lamp visual vocabulary, with precise names in interactions.

The kitchen always supplies a knife, and the bedroom cabinet supplies clean rags.
Collect the knife, stand next to the bedroom cabinet, and craft **Cut an improvised
bandage** using its stored rag. The workshop guarantees a hammer plus weighted
tools/materials. Storage draws from nested household-supply and material pools.

## API and integration notes

`AppOptions` appends a scenario and optional seed override. `SessionOptions`
appends the scenario after its existing settings value. Defaults preserve demo
callers and aggregate prefixes. The app copies settings for session construction
and applies the seed override there; the worker validates the scenario and owns
the resulting Server. These are startup-only values: no world entity, snapshot,
wire, or saved-state layout is enlarged.

Build integration defines `CATACLYSMX_FIRST_HOUSE_PACK` for the curated
`data/first_house.json5` and `CATACLYSMX_CONTENT_PACK` for expanded
`data/content.json5`, and points `CATACLYSMX_TERRAIN_FILE` at `data/demo.json5`. The scenario and pack
implementations are linked through the existing engine/content targets.

Coverage includes cellar walkthroughs, storage/crafting, push/pull, and
archive/save non-respawn behavior. Final verification is recorded by the coordinator.
