# Content packs

`LoadContentPack(path, diagnostics)` loads a JSON5 manifest with exactly
`format_version: 1` and a nonempty `files` array. Paths are relative to the
manifest directory. Absolute paths, drive paths, parent components, embedded
NULs and duplicate normalized paths are rejected. Both slash styles are treated
as separators; `.` components and repeated separators are normalized. The
registry overload supplies custom item behavior parsers.

All sources are loaded together, so references can point to definitions in a
later source. Errors in the manifest carry its path and field/index; errors in
definitions carry their source path. `LoadContentFile` continues to load one
definition document, not a manifest.

The manifest directory is also the data root for item art references such as
`/art/items/rock/art.txt`; the leading slash is virtual, not an OS root. Asset
paths are canonical and confined to that root, including symlink resolution. Art
files are dependencies referenced by item definitions, **not** entries in the
manifest `files` array. Deploy the entire data tree with the manifest. Add
`flags/core.json5` to each pack containing items that reference its flags, and
include all item and recipe source dependencies required by that pack.

For standalone definition files with external art use
`LoadContentFiles(paths, dataRoot[, diagnostics])`; in-memory sources use
`LoadContent(sources, ContentAssetReader[, diagnostics])`. The reader is a
borrowed synchronous callback returning owned `String` bytes. No-reader and
no-root overloads reject external references. Assets are read once per path per
load and the catalog owns the resolved art; fingerprinting uses resolved RGB art,
not paths. See [content format](content_format.md) and
[item presentation](item_presentation.md) for details.

| Manifest | Purpose and sources |
| --- | --- |
| `data/demo.json5` | Shared legacy definitions from `terrain/`, `items/`, `crafting/`, `monsters/`, `objects/`, and `worldgen/buildings/`. |
| `data/first_house.json5` | Curated FirstHouse pack: shared skills, learning, combat, semantic item/crafting sources, power and workshop content; demo dependencies, household loot, furniture, cellar monsters and the furnished house. |
| `data/content.json5` | Expanded Town pack: shared FirstHouse sources plus urban resources, legacy workshop recipes, specialists and urban building layouts. |
| `data/zombie_showcase.json5` | Demo dependencies plus the showcase monster definitions. |

The manifests reference shared definition files; they do not duplicate those
definitions within a loaded pack. See [urban content](urban_content.md) for the
complete roster and scenario guide.

Town and FirstHouse also share W3A/W3B's `workshop_forging.json5` and
`workshop_electrical.json5` sources under `items/`, `crafting/`, `objects/` and
`worldgen/loot/`. That roster introduced 22 items, 17 crafting recipes, 19 explicit
recyclers and five objects. I1 also moves the existing current-material sword recipe
into workshop forging. FirstHouse retains only the current sword producer; Town also loads
its legacy-binding alternative. See [workshop rosters](workshop_rosters.md) for
resource budgets, acquisition, and the unfinished-sword save-upgrade policy.

The [60-item slice](item_slice.md) and [80-item expansion](item_expansion.md)
are historical authoring records. Their counts describe additions at those
milestones, not the current catalog or source ownership.
`worldgen/loot/retail.json5` supplies shared themed pools for both packs.
Compact pools respect existing furniture limits, while dedicated ground-only pools
place bulky workshop stock, camping gear, outerwear, the toaster and bucket without
nesting them in general furniture loot. Expansion recipes include four craft-only
weapons; the remaining expansion items have loot routes.

The house has two equally weighted cellar layouts. The workshop variant has a
30% chance of a zombie away from the stairs. Movable lamps light both surface
rooms and each cellar without blocking movement.

The kitchen cabinet always contains the current `chef_knife`, and the bedroom cabinet contains
clean rags. Collect the knife, then craft improvised
bandages within reach of the bedroom cabinet (or collect the rags too). Both
placements have guaranteed rolls with single-entry pools, so this recipe path is
independent of seed and cellar choice.

The old rag/bandage tutorial route remains available with obsolete dependency
items. Current cloth-patch bandages and the other new acquisition routes are
documented in [the broad item slice](item_slice.md).

## Source organization

I1 organizes the catalog by semantic ownership. Keep the definition-type
directories (`items/`, `crafting/`, `objects/`, `worldgen/loot/`) and put a definition
in exactly one source. A batch is an authoring work list, not a lasting file boundary.

- **Items follow primary purpose.** Stock, fasteners and repair consumables belong
  to materials; electronic parts and mechanical assemblies have their own domains.
  Hand tools and instruments belong to tools; batteries and charging equipment
  belong to power. Storage bags/belts belong to containers, packaged food to food,
  and protective garments to clothing. Retain every item's other capabilities.
- **Recipes follow the operation.** Keep preparation, assembly, textile work and
  dismantling with their relevant domain; forging belongs to workshop forging.
  A recipe's output directory alone does not decide its owner. Keep a generated
  recycler attached to its recipe and explicit recyclers with their operation.
- **Loot follows acquisition context.** A pool may intentionally mix tools,
  components and consumables from several item domains. Move complete pools;
  preserve entry order because weighted selection and generation keys use it.
- **Subsystem and scenario boundaries are meaningful.** Power, workshop, learning
  and combat sources remain useful owners. Demo and urban compatibility sources
  preserve their pack-specific membership. FirstHouse's current sword recipe
  must not pull in Town's legacy-binding alternative.
- **Manifests remain explicit.** All sources in a manifest resolve together.
  A semantic source is not an independently loadable pack. Add dependencies to
  each consuming manifest; do not introduce includes, automatic discovery or
  duplicated definitions.

These rules keep ownership searchable without coupling future content to old
batch names or forcing one file per item. Definition order may change; nested
authored arrays retain their order. Numeric IDs are assigned lexically by stable
name. Asset references remain rooted at `data/`, independently of source location.

### Current source map

Paths below are relative to `data/`. Items and operations need not share a
one-to-one filename: sewing a storage bag belongs to textiles, and forging a sword
belongs to metalworking.

| Domain | Item ownership | Crafting/recycling ownership |
| --- | --- | --- |
| Stock, fasteners, repair consumables | `items/materials.json5` | `crafting/materials.json5` |
| Electronic parts and assemblies | `items/electronics.json5` | `crafting/electronics.json5`, including donor-device dismantling |
| Motors, servo and pump | `items/mechanical_components.json5` | `crafting/mechanical_components.json5` |
| Hand tools and instruments | `items/tools.json5`; existing household/combat/workshop providers retain their subsystem owners | `crafting/tools.json5`; specialized manufacture stays with its workshop |
| Clothing and portable storage | `items/clothing.json5`, `items/containers.json5` | `crafting/textiles.json5` for sewn equipment and garment recovery |
| Weapons and ammunition | `items/weapons.json5`; combat-specific equipment in `items/combat.json5` | `crafting/weapons.json5`; current sword forging in `crafting/workshop_forging.json5` |
| Food, medicine, household and camping | `items/food.json5`, `items/medical.json5`, `items/household.json5`, `items/camping.json5` | Cloth bandages in `crafting/textiles.json5`; shelter salvage in `crafting/camping.json5` |
| Power and batteries | `items/power.json5` | `crafting/power.json5` |
| Forging and electrical workshop | `items/workshop_forging.json5`, `items/workshop_electrical.json5` | Matching `crafting/workshop_*.json5` files |
| Reading and combat learning | `items/learning.json5`, `items/combat.json5`; information identities in `books/starter.json5` and `combat/core.json5` | Existing skill/knowledge references on the relevant recipes |

Placed equipment stays in the matching `objects/power.json5` and
`objects/workshop_*.json5` sources. Loot stays in `worldgen/loot/household.json5`,
`retail.json5`, `urban.json5` and `workshop_*.json5`, by acquisition context.
`items/demo.json5`, `items/urban.json5` and the existing
`crafting/{demo,household,urban,low_tech}.json5` retain scenario/compatibility
ownership. `crafting/urban.json5` and `crafting/low_tech.json5` are Town-only.

The retired `items/slice_*.json5`, `items/expansion_*.json5` and
`items/low_tech.json5` are distributed into these item owners.
`crafting/slice.json5` and `crafting/expansion_*.json5` are distributed by operation;
the old expansion loot source is now `worldgen/loot/retail.json5`.

### Catalog membership

The I1 checkpoint preserved the existing per-pack catalogs, including dependency-only obsolete
items and injected runtime items. Counts below describe loaded definitions, not
new authoring or future content limits; they are the pre-renewable checkpoint:

| Pack | Items | Crafting recipes | Recyclers | Tool traits | Objects | Loot pools | Buildings | Skills | Books |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Demo | 17 | 1 | 1 | 0 | 1 | 0 | 3 | 18 | 0 |
| FirstHouse | 197 | 33 | 67 | 13 | 23 | 40 | 4 | 18 | 4 |
| Town | 203 | 38 | 69 | 13 | 41 | 63 | 37 | 18 | 4 |

The subsequent [pass-2 renewable slice](renewable_power_design.md) adds nine items,
six recipes, four recyclers and two objects to both FirstHouse and Town, using
the existing semantic source owners and manifests. Demo is unaffected.
Its `behavior:'solar'` schema requires `solar:{peak_output_per_tick:N}`; canonical
export preserves this configuration and the shared fingerprint includes it.

Source paths do not participate in the resolved network content fingerprint.
I1 requires identical before/after fingerprints and effective definitions for
each pack; it changes no save, protocol, content-schema or generation version.
Historical authoring records retain their original paths and verification
evidence, with this page supplying current ownership.

### I1 verification

**Verified, 2026-11-20.** The reorganization moves 142 item definitions from the
seven slice/expansion item sources plus the two-item low-tech source. It replaces
five historical crafting sources with seven operation sources, places the current
sword recipe with workshop forging, and renames the shared expansion loot source
to retail. No definition identities or effective values change.

A local verifier loaded the pre-move archived `data/` tree and the integrated
tree using `LoadContentPack`, validated both with `ValidateWorldgenContent`,
and compared `network::ContentFingerprint`. All three checks passed:

| Pack | Before and after fingerprint |
| --- | --- |
| Demo | `74aeb2ce36ecdc29` |
| FirstHouse | `9330d33fa4450b95` |
| Town | `7ada226c240a11f5` |

Baseline, verifier and comparison output are local under `out/i1-catalog/`.
The focused CTest run also independently loads all four shipped manifests,
including the zombie showcase.

Commands and results:

```powershell
./scripts/build.ps1 -Preset win-msvc-debug -SkipTests
./scripts/test.ps1 -Preset win-msvc-debug -R 'Content packs|All shipped manifests|Town slice|Current (cloth|coil|recyclers|batteries)|Item expansion|W3 |Full and first-house|Expanded urban|Furnished house|Every urban|Urban (patterns|chunk|professional|resource)|Authored multi-storey|Spawn catalog IDs|Loot graph|Furniture capacity|Item IDs are source-order|[Ff]ingerprint'
```

Configure/build succeeded; Ninja reported no work to do. **65/65 selected tests
passed**, including actual crafting/recycling, workshop bootstrap and saved paid
work, capacity checks, source-order identities and urban placement/reload behavior.
No tooling tests were added or run.
