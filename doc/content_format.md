# Content format, version 1

`content/content.h` provides an immutable, owning `ContentCatalog`. The loader
accepts JSON5, including comments, unquoted keys, single-quoted strings, and trailing
commas. `data/terrain.json5` bundles ten terrains, three house patterns, and eleven items. Content may define
terrain, building patterns, and items in the same catalog; terrain-only catalogs remain
valid and generate terrain/roads without buildings.

```json5
{
  format_version: 1,
  definitions: [
    {
      type: 'terrain',
      id: 'closed_door',
      name: 'closed door',
      movement: {passable: false},
      blocks_sight: true,
      appearance: {symbol: '+', foreground: 'light_gray'},
      opens_to: 'open_door',
    },
    {
      type: 'terrain',
      id: 'open_door',
      name: 'open door',
      movement: {passable: true},
      blocks_sight: false,
      looks_like: 'closed_door',
      appearance: {symbol: '/'},
      closes_to: 'closed_door',
    },
  ],
}
```

## Schema and validation

The root has exactly `format_version` and `definitions`, both required. Version
must be an integer; an unsigned integer other than 1 returns `UnsupportedVersion`.
Definitions is an array (possibly empty). Each definition requires `type` and
`id`; types are `terrain`, `building_pattern`, and `item`. Terrain definitions require:

| Field | Value |
| --- | --- |
| `type` | Exactly `terrain` |
| `id` | Nonempty ASCII `[a-z][a-z0-9_]*` |
| `name` | Nonempty string without embedded NUL |
| `movement` | Object with the required boolean `passable` |
| `blocks_sight` | Boolean |

Optional fields are `appearance`, `looks_like`, `opens_to`, `closes_to`,
`open_space`, `stairs`, and `light`. `open_space` and `stairs` are booleans defaulting to false.
`open_space: true` requires passable terrain that does not block sight and is
not stairs. `stairs: true` requires passable terrain that does not block sight
and is not open space. Open space has no floor support and transmits downward;
ordinary terrain, including walls, provides support. Stairs connect matching
stairs one Y layer apart at the same X/Z. See [Y layers](y_layers.md) for
movement, gravity, sight, and generation behavior.
Unknown fields and duplicate keys are errors at every object level, even when
duplicate values agree. Unknown definition types and duplicate IDs across any
input sources and definition types are errors. `movement.cost` is intentionally unsupported until its
gameplay meaning is defined. No ID prefixes are reserved or required.

### Appearance inheritance

`appearance` accepts only `symbol`, `foreground`, and `background`. `looks_like`
names another terrain by stable string ID and inherits **only unspecified
appearance fields**. Explicit fields override inherited fields individually.
Name, passability, sight blocking, light, and transition references are never inherited;
required gameplay fields must always be present locally.

All sources are collected before resolving references, so forward and cross-file
references work. Unknown references and any `looks_like` cycle are errors, even
if the terrain already specifies every appearance field. Resolution uses an
iterative chain walk rather than the C++ call stack.

Every resolved appearance needs a symbol and foreground. A root without a
background defaults to black only after inheritance resolution reaches that
root: an intermediate missing background cannot mask an ancestor's explicit
background. A terrain may omit `appearance` entirely when inheritance supplies
its required fields.

`symbol` is exactly one valid UTF-8 Unicode scalar, excluding C0 controls,
DEL, and C1 controls (U+0000–001F and U+007F–009F). Invalid UTF-8, surrogates,
and multiple scalars are rejected. Space, combining scalars, and wide scalars
are accepted: terminal display width and fallback belong to the frontend, not
the simulation/content schema.

### Terrain light

An optional `light` object defines gameplay emission, for example
`light: {intensity: 255, range: 2}`. It requires exactly both `intensity`
and `range`: integers from 0 to 255 and 0 to 12 respectively. Both must be
zero (disabled) or both positive. Omission also disables emission. Booleans,
fractions, negative values, out-of-range values, unknown keys, and duplicate
keys are errors. Intensity and range are independent, allowing both a bright,
short-range light and a dim, wide-range light. `looks_like` never inherits light;
each terrain specifies its own emission. Emission is gameplay content rather
than an appearance or renderer setting.

### Colors

Colors are case-sensitive names below or exactly `#RRGGBB`; hexadecimal digits
may use either case. Short hex, alpha channels, whitespace, and undocumented names
are not accepted. These are fixed RGB values, independent of terminal palette settings.
`brown` is an explicit alias for `yellow`, both RGB (128, 128, 0), for compatibility
with traditional readable terrain palette names.

| Name | RGB hex | Name | RGB hex |
| --- | --- | --- | --- |
| `black` | `#000000` | `dark_gray` | `#808080` |
| `red` | `#800000` | `light_red` | `#FF0000` |
| `green` | `#008000` | `light_green` | `#00FF00` |
| `yellow` | `#808000` | `light_yellow` | `#FFFF00` |
| `blue` | `#000080` | `light_blue` | `#0000FF` |
| `magenta` | `#800080` | `light_magenta` | `#FF00FF` |
| `cyan` | `#008080` | `light_cyan` | `#00FFFF` |
| `light_gray` | `#C0C0C0` | `white` | `#FFFFFF` |
| `brown` | `#808000` | | |

### Terrain transitions

`opens_to` and `closes_to` are optional stable terrain IDs. Both on one terrain
are an error. Targets must exist, including targets in other sources. These
references are resolved directly to runtime `TerrainId`s, without traversing
their graph. Reciprocal closed/open doors and self-references are legal; they
are transitions, not inheritance. Absence becomes the invalid ID zero. The
loader does not impose extra gameplay rules on target passability or appearance.

## Building patterns

A `building_pattern` definition has exactly these fields (unknown and duplicate
keys are errors):

```json5
{
  type: 'building_pattern',
  id: 'small_shop',
  rows: ['#####', '#...#', '#...#', '##D##'],
  palette: {'#': 'wall', '.': 'floor', 'D': 'closed_door'},
  entrance: {x: 2, z: 3},
  weight: 3,
}
```

`id` uses the same nonempty ASCII identifier syntax as terrain IDs. `rows` is a
nonempty rectangular array of 1–64 rows, each 1–64 printable ASCII characters;
all rows have equal width. `palette` maps each single printable ASCII glyph
used by the rows to a terrain ID. Every referenced terrain must exist across
all input sources. Unused palette entries are allowed, but their terrain references
must also resolve. `entrance` requires
integer `x` and `z` coordinates within the pattern, on its boundary. The
resolved entrance terrain must be passable or open to passable terrain. Optional
`weight` is an integer from 1 to 1,000,000 (default 1). Pattern IDs, like terrain IDs, are
sorted independently into stable catalog-local numeric IDs; duplicate strings
are prohibited across all definition types.

`FindBuildingPattern(string_view)` returns a typed `BuildingPatternId`, zero
when absent. `TryGetBuildingPattern(id)` returns null for zero/out-of-range;
`GetBuildingPattern(id)` aborts on invalid IDs. `BuildingPatternCount()` counts
patterns. `BuildingPattern` owns its stable string ID, integer width/height,
row-major resolved `Vector<TerrainId>`, `PatternPoint` entrance, and weight.
These are authored coordinates; placement rotations belong to world generation.
Pattern lookup borrows have the same catalog lifetime and catalog-local ID
restrictions as terrain lookup.

## Items and capabilities

An `item` requires exactly `type`, stable `id`, nonempty `name`, `mass_g`, and
`volume_ml`, with an optional `capabilities` array (zero to sixteen entries).
Mass is an unsigned 32-bit integer including zero; external volume is a positive
unsigned 32-bit integer. Fractions, negative values, booleans, overflow, duplicate
keys, and unknown fields are errors. Items do not inherit terrain appearance.

```json5
{
  type: 'item', id: 'field_charger', name: 'field charger', mass_g: 500, volume_ml: 500,
  capabilities: [
    {type: 'charger', id: 'charger', power_source: 'power', battery_class: 'aa', energy_per_turn: 3},
    {type: 'energy_store', id: 'power', capacity_energy: 2000, rechargeable: false},
  ],
}
```

Every capability requires `type` and an item-local unique stable `id`, using
`[a-z][a-z0-9_]*`. Each type accepts only the additional fields below:

| Type | Required fields | Optional fields |
| --- | --- | --- |
| `container` | `capacity_volume_ml`: positive uint32 | None |
| `energy_store` | `capacity_energy`: positive uint32; `rechargeable`: boolean | `initial_energy`: uint32 from zero to capacity, defaults to capacity; `battery_class`: stable ID |
| `charger` | `power_source`: local capability ID; `battery_class`: stable ID; `energy_per_turn`: positive uint32 | `action_cost`: positive uint32, default 100 |
| `door_controller` | `delay_ticks`: positive uint32 | `action_cost`: positive uint32, default 100 |

There is at most one container per item. A container has no mass limit. Its
external volume remains fixed regardless of contents; capacity measures the
contained items' external volumes. Installed batteries are separate from container
contents and occupy a charger's runtime battery slot.

Capabilities are sorted lexically by their names before assigning one-based
`ItemCapabilityId` values and resolving `power_source`. Forward references work;
the source must be an `energy_store` on the same item without a `battery_class`.
Two chargers cannot share a source, keeping their energy transfers independent
of processing partition/order. Multiple ordinary internal energy stores are legal.
A store with a `battery_class` identifies an installable battery: it must be the
item's sole energy store, and that item must have no charger capabilities. This
keeps battery targeting unambiguous and battery definitions passive. Class names
are matching labels, not references to another catalog definition.

`FindItem(string_view)`, `TryGetItem(ItemTypeId)`, `GetItem(ItemTypeId)`, and
`ItemCount()` follow the terrain lookup conventions below. `ItemDefinition` owns
physical values and a sorted vector of named `ItemCapabilityDefinition` metadata
(`id`, `type`, and canonical owned `DataObject config`, including effective defaults).
`FindItemCapability(definition, name)` returns zero when absent;
`TryGetItemCapability(definition, id)` returns null for zero/out-of-range IDs;
`ItemContainer(definition)` returns the sole typed container or null;
`ItemEnergyStore(definition, capability)` returns the named typed energy resource
or null; `ItemBehaviorConfig(definition, capability)` returns the immutable behavior
definition or null. The optional `container` and `energyStores` vector are common
typed resources, separate from the `behaviors` vector of capability bindings.
Item IDs are sorted independently of terrain and pattern IDs. Persist item and
capability names, never numeric ordinals. The bundled items include ordinary
props, a 20,000 ml backpack, two AA batteries, a charger, a door timer, and a
combined utility controller for multi-capability use.

### Registering behaviors

`items/item_registry.h` provides `ItemBehaviorRegistry`, an explicitly owned small
vector of type-name/factory pairs. `Add(type, parser)` returns `ContentResult<void>`:
names must match `[a-z][a-z0-9_]*`, be unique, and must not be the reserved shared
resource names `container` or `energy_store`; null factories are invalid.
`Find(type)` returns the factory or null. `RegisterBuiltinItemBehaviors(registry)`
registers the built-in charger and door controller factories and requires their
names to be unused. There are no global registries or static registration hooks.

To add a behavior, implement `ItemBehaviorDefinition` and its runtime behavior,
then register a `ParseItemBehavior` factory under a new stable type name. The
factory receives the full capability object as an owned-value `const DataObject&`,
the item's resolved capability identities and typed common resources, its own
`ItemCapabilityId`, and an optional `Diagnostics*`. It validates its closed schema
and returns `ParsedItemBehavior{definition, config}` with effective defaults in
`config`. Resource reference checks belong in the factory; cross-behavior checks
belong in `ValidateComposition`, called after **all** behavior bindings exist.
The content parser needs no new type branch. Behavior factories should not depend
on previously constructed bindings: factory order is lexical capability order.

Before publishing a catalog, the loader applies the shared
`ValidItemBehaviorSchema(const ItemBehaviorSchema&)` validator to every factory's
definition, even when it inherits the default successful composition check.
Implementation versions and action base costs must be positive. Action IDs match
`[a-z][a-z0-9_]*`, have at most `maxItemActionName` (64) bytes, and must be unique
and strictly lexically sorted within each schema. Argument/category enums must
be in their declared ranges. A countdown requires an activity and positive
`maximumDelay`; a non-countdown requires zero `maximumDelay`. Activities without
countdowns are supported. Each schema and the sum of all declared actions across
an item's behaviors are bounded by `maxItemActionsPerItem` (64), including actions
that may be unavailable at runtime. Invalid metadata returns `InvalidData` with
the capability's source file/path, rather than reaching runtime consumers.

`ValidItemText(const ItemText&)` validates both the key and fallback independently:
each may be empty, is at most `maxItemText` (192) bytes, and contains no C0 bytes
(0–31) or DEL byte (127). These are byte bounds shared with wire validation;
non-ASCII bytes are permitted. `ValidItemUseReason(ItemUseOutcome, const ItemUseReason&)`
rejects invalid outcomes, accepts an entirely empty reason for every valid outcome,
and permits a nonempty reason only for `Refused`, with a valid action-style code
and valid message text. Shared validators live in `items/item_types.h/.cpp` so
loading and wire consumers can enforce identical metadata bounds without
depending on each other.

Capability JSON values support booleans, signed negative integers, unsigned
nonnegative integers, strings, lists, and objects, with at most 64 nested edges
from the capability root. Null, floating-point values (including NaN/infinity),
embedded NUL, duplicate object keys at any depth, and deeper trees are invalid.
Objects use ordinary content JSON semantics: archive signed-value/escape tags
are ordinary object keys, never interpreted as a persistence codec.
Canonical metadata config object keys are sorted recursively; list order is
preserved. Factories own their domain defaults and canonical field values.

Behavior bindings use `shared_ptr<const ItemBehaviorDefinition>` so catalog
copies can share immutable parsed definitions and action schemas without cloning
runtime behavior. Each live item creates its own mutable behavior state. Catalogs
retain no registry pointers; a registry and the input buffers may be destroyed as
soon as loading returns. Definition equality compares physical values, capability
metadata/config values, typed resources, binding identities, and behavior schemas,
never shared-pointer identity.

## API, ownership, and errors

- `LoadContent(eastl::span<const ContentSource>, Diagnostics* = nullptr)` parses
  named memory sources. Source names and texts are borrowed **only during the
  call**; successful catalogs and diagnostic frames own their strings.
- `LoadContentFiles(eastl::span<const eastl::string_view>, Diagnostics* = nullptr)`
  reads all files and loads them as one transaction. File path views need not be
  NUL-terminated. Empty paths and embedded NUL are I/O errors.
- `LoadContentFile(const char*, Diagnostics* = nullptr)` is the single-path
  convenience entry point. A null path returns `Io`.
- Each loading entry point also accepts `const ItemBehaviorRegistry&` immediately
  before the optional diagnostics argument: `LoadContent(sources, registry, diagnostics)`,
  `LoadContentFiles(paths, registry, diagnostics)`, and
  `LoadContentFile(path, registry, diagnostics)`. These use exactly that registry.
  The existing overloads construct a local registry and call
  `RegisterBuiltinItemBehaviors`. Applications can register built-ins and their
  own factories explicitly before loading.
- All return `ContentResult<ContentCatalog>` (`eastl::expected`). Expected
  failures are `Io`, `InvalidData`, and `UnsupportedVersion`; allocation failure
  is fatal. Stdio files and parser documents use RAII. Read errors and close
  failures are checked. There are no exceptions or partial catalog returns.
- Diagnostics are optional and appended without clearing existing frames.
  Validation frames carry source file and schema path; syntax failures also
  carry one-based line and byte-column. Frames follow the shared inner-first
  convention: field failure first, followed by terrain and content-source/catalog
  context. Unsupported definition types are dispatched and reported at the
  document layer's `type` field. Missing-reference diagnostics name both target
  and referring terrain; cycle diagnostics name an involved terrain. Duplicate-ID
  failures name the stable ID and carry the later definition's location first,
  then the prior definition's source/path as a separate context frame. A null
  pointer avoids diagnostic strings/frames/path allocations, including these
  contextual messages.
- `FindTerrain(string_view)` returns zero when absent; `TryGetTerrain(TerrainId)`
  returns null for zero/out-of-range; `GetTerrain(TerrainId)` aborts on an invalid
  ID as an always-on invariant check. `TerrainCount()` counts definitions.
- Numeric IDs are the lexicographically sorted stable IDs' ordinal plus one.
  Input source and definition order cannot change them. IDs are **catalog-local**:
  adding/removing IDs can renumber them, and an in-range ID from another catalog
  cannot be detected. Persist stable string IDs, not numeric ordinals.
- Returned pointers/references remain valid while the owning catalog lives and
  is not moved from or replaced by assignment. Copies own independent metadata
  and typed resource storage and share immutable behavior definitions.
  The catalog has no public mutators, hidden defaults, or global instance.

The API exposes only typed owned values, keeping yyjson inside content loading.
Sorted contiguous storage gives deterministic IDs and simple binary-search
lookup without hash iteration semantics or an additional indexing dependency.
Appearance-only inheritance lets content share presentation while keeping
gameplay definitions explicit and reviewable. World, save, and renderer adapters
decide how to retain the catalog and use these typed values.
