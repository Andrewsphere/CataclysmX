# Urban content and specialist identity

## Scope and decisions

**Implemented.** The expanded pack has 26 populated building types including
furnished_house, plus three legacy patterns, with themed furniture, loot,
inhabitants, and selected basement variants. It retains the existing eight-parcel
region planner. See [urban content](urban_content.md) for the current guide and
[urban resources](urban_resources.md) for definition details. The following
sections retain the design rationale and integration contract.

Specialists are ordinary actors created from existing monster definitions and
weighted groups. Use names such as `Firefighter zombie`, `Nurse zombie`, `Police
zombie`, and `Construction worker zombie`. Different speed, damage, accuracy,
and existing AI profiles provide gameplay differences. A firefighter can be a
slower, harder-hitting threat; do not describe it as having more health or armor
unless those mechanics are actually implemented. No new durability multiplier,
profession system, faction hierarchy, or starting-gear system is needed here.
Themed building loot already supplies equipment rewards without new spawn RNG.

## Identity and ownership contract

* `actors/actor_types.h` owns `MonsterTypeId`, a four-byte catalog-local value.
  Zero means untyped. The existing content APIs retain their signatures.
* `Actor::monsterType` retains the type assigned by `World::CreateMonster`.
  `CreateActor` and players remain zero. The catalog owns the name; actors store
  neither names nor definition pointers. Catalog lifetime already covers World.
* `SavedActor::monsterType` is an owned stable definition ID string, empty for
  untyped actors. Capture resolves the numeric ID to `MonsterDefinition::id`;
  restoration resolves the string through `FindMonster`. Reject an unknown
  nonempty ID or a typed player with the existing invalid-data error path.
* Archive capture, eviction/reload, snapshot validation, and disk persistence
  must all preserve identity. Validate before publication. Restoration copies
  combat, controller, speed, deadline, and inventory state from the archive;
  it must not call `CreateMonster` to reapply defaults or heal a damaged actor.
* This adds one scalar to live actors and view cells, subject to structure
  alignment, and one owned string to archived actors. No per-cell name allocation,
  new actor container, or change to region-plan storage is warranted.

### Save compatibility

Keep save format 10 with an optional actor field `monster_type`. The new reader
accepts absent or empty string as untyped, including existing v10 saves; a
present value must be a string. Write the field only for a nonempty type. Extend
the strict actor field allowlist for v10, and retain the existing v7-v9 migration
behavior with untyped actors. Do not infer type from AI, faction, or statistics.

Older executables reject typed saves because their field allowlist is strict;
compatibility here means the new executable reads old saves, not the reverse.
The agreed optional-v10 contract does not require a SQL schema migration.

## Filtered presentation and LAN

`ViewCell::monsterType` is populated only when `actor != 0`, `player == false`,
and `overlayVisibility == Visible`. It is zero for remembered/unknown overlays,
empty cells, and players. The normal visible actor may still be untyped. Use the
existing projected actor and overlay depth, including downward views; never
query hidden actors to fill this field. It is not part of terrain/object memory.

`VisibleActorName(content, cell)` returns a borrowed catalog name for a valid
typed visible actor, `Player`/`Creature` for untyped visible actors, and an empty
view when no actor is visible. A bad catalog lookup falls back to `Creature`;
the network validator must reject bad IDs before publishing a remote view.
This helper goes in the existing tile-inspection implementation and works with
catalog plus projection alone. Preserve the existing authoritative visibility
and position checks when DescribeTile additionally displays live combat details.

Both SDL and terminal must expose the name in inspection or their existing
selected-tile/status text. Remote clients can display the name without a World.
Type names, not a hidden health/gear read, are the minimum recognition feature.
Keep the existing glyph/colors and `GenericSprite::Npc` semantic mapping. A
per-type tileset table is unnecessary for this increment. Whenever presentation
creates a terrain-only ViewCell copy, clear monsterType with actor metadata.

Wire protocol advances from `0x43584e0e` to `0x43584e0f`. Append one U32
monsterType to the existing cell payload, on both writer and reader. RLE equality
already includes the field via ViewCell's default equality. Payload becomes 40
bytes plus a two-byte run length; the worst-case cell runs are 698922 bytes at
129 x 129. Retain the existing one-MiB packet bound and total-size checks.

Structural validation rejects nonzero types on hidden/remembered/empty/player
cells. `wire::ValidMonsterDefinitions` checks every nonzero ID against the
catalog and is called alongside terrain/item validation before publishing LAN
snapshots. Keep the existing matching-content handshake and canonical catalog
ordering; numeric indices are session values, never save identities. Existing
monster name and recipe fingerprint coverage should be retained.

## Buildings, FirstHouse, and Town

Use separate root manifests. `data/first_house.json5` preserves exactly the old
full manifest source list; `data/content.json5` contains 26 populated building
patterns and three legacy patterns.
Both manifests reference shared definition files without duplicating definitions.
The curated manifest retains furnished_house weight 1000 and its existing
competitors so FirstHouse remains the same seed-based preview, apart from the
approved wall correction. Expanded pattern weights are approximately 500-1500
each, with furnished_house staying at 1000 (weights are relative, not ratios).
Do not edit shared house weight solely for the expanded pack.

Applications select `CATACLYSMX_FIRST_HOUSE_PACK` for FirstHouse and
`CATACLYSMX_CONTENT_PACK` for Town. `NewGameScenario::Town` is a distinct startup
mode using `FindTownStart`, not an alias for Demo. The existing two-argument
Server constructor retains Demo behavior. Explicit custom manifest overrides
remain explicit and scenario validation can still fail on unsuitable packs.
Do not silently load a different catalog when restoring a save or joining LAN.

### Pure town-start query

The public declaration in `core/first_house_scenario.h` is:

```cpp
using BuildingStart = FirstHouseStart;
ContentResult<BuildingStart> FindTownStart(const ContentCatalog& content,
    std::uint32_t seed, Diagnostics* diagnostics = nullptr);
```

The alias preserves the original FirstHouseStart type and FindFirstHouseStart
signature. Both result fields are world coordinates: `playerPosition` is the
exterior spawn tile and `entrance` is the selected building entrance. No pattern
ID, plan, allocated population buffer, or runtime handle escapes this query.

Validate worldgen content, then evaluate every building in all nine region plans
with region x,z in [-1,1], using the same seed and current generation path as
FirstHouse. Eligibility requires at least one authored furniture, loot, or
monster marker in the common pattern population or the plan's selected basement
population. This means an authored populated building, not a guarantee that a
probabilistic loot/monster roll succeeds. Unselected basement variants do not
qualify it. There is no furnished_house ID filter or dependence on its weight.

Use FirstHouse's exterior-driveway test: the adjacent tile toward the driveway's
outer end must be outside building bounds, on a road, passable, supported, and
not stairs. Generated furniture, items, or monsters must not occupy this spawn
tile. Entrance terrain must be passable or openable to passable terrain and must
not be open space. Select minimum entrance x*x + z*z using 64-bit arithmetic;
ties compare entrance x, entrance z, owner region x, owner region z, then parcel.
Evaluate all nine regions before selecting the result; traversal order must not
decide ties. Reuse placement scratch while checking spawn tiles.

Return `eastl::unexpected(ContentError::InvalidData)` on failed content
validation or no qualifying usable entrance, with optional diagnostic context.
The query performs no World/residency mutation and consumes no simulation RNG.
Applications validate before constructing Server, with the same catalog and
seed; the Town constructor branch uses that same pure query. Keep the existing
FirstHouse selection behavior and checks unchanged.

This changes content, not generation algorithms. Existing generation versions
remain supported; reproducibility is for the same content as well as seed and
version. An expanded manifest is not a promise of identical future ungenerated
chunks for an old save. No generation v5 is needed for manifest-only expansion.

Implemented 26-type roster (count furnished_house once):

| Building | Loot theme | Inhabitants / selected basement theme |
| --- | --- | --- |
| Furnished house | Domestic supplies | Ordinary undead / existing basement |
| Duplex | Shared domestic supplies | Residential inhabitants / laundry or hobby pantry |
| Apartment | Dense domestic storage | Residential inhabitants / utility or workshop storage |
| Bungalow | Domestic supplies | Residential inhabitants |
| Row house | Domestic supplies | Residential inhabitants / laundry or preparedness |
| Farmhouse | Domestic and outdoor supplies | Residential inhabitants / pantry or workshop |
| Grocery store | Food and containers | Shopper and worker |
| Convenience store | Snacks and drinks | Shopper |
| Pharmacy | Medical supplies | Medical inhabitants |
| Clinic | Medical supplies and linens | Nurse and doctor / medical stock |
| Fire station | Tools and emergency supplies | Firefighter |
| Police station | Security supplies | Police |
| Hardware store | Tools and materials | Workshop inhabitants / bulk tools |
| Auto repair | Repair supplies | Mechanic and construction worker |
| Warehouse | Bulk materials | Warehouse worker |
| Workshop | Crafting materials | Worker / materials storage |
| Restaurant | Kitchen supplies | Cook / pantry |
| Diner | Food and kitchen supplies | Cook and shopper |
| School | Books and basic supplies | School inhabitants |
| Library | Books and paper | Office workers and shoppers |
| Office | Stationery and electronics | Office worker / records archive |
| Laundromat | Cleaning supplies and clothing | Ordinary undead |
| Sporting goods store | Outdoor equipment | Outdoors worker |
| Clothing store | Clothing and sewing supplies | Shopper |
| Bookstore | Books and stationery | Shoppers / book archive |
| Electronics store | Electronics and components | Shoppers / electrical spares |

These are distinct layouts and populations, not merely renamed copies. Weighted
monster groups favor specialists at matching locations while mixing in ordinary
undead. Basements are authored variants only where appropriate, not mandatory per
type. See the current guide for the exact basement roster.

## Validation design

Focused tests cover identity and persistence, filtered presentation and transport,
FirstHouse and Town start selection, and the expanded catalog. The urban content
tests are designed to check connectivity, rotations, eligibility, specialist-site
matching, and crafting; see [urban content](urban_content.md).
