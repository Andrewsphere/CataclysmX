# House content JSON5 schemas

These additions retain content `format_version:1`. Definitions from every source
passed to `LoadContent`/`LoadContentFiles` resolve together, before a catalog is
returned. IDs match `[a-z][a-z0-9_]*` and are globally unique across all definition
kinds, objects, and AI profiles. Numeric catalog IDs are lexical within each kind;
authored arrays retain their order. Unknown and duplicate fields are errors at
every schema object. Integers must be JSON integers within the specified bounds.

## Furniture

Objects remain in the root `objects` array. Existing required `id`, `name`, and
`behavior` and existing machine configuration keep their meanings. New optional
fields apply to both `none` and `crafting_machine` behaviors:

```json5
{id:'cabinet', name:'Cabinet', behavior:'none',
 capacity_volume_ml:10000, kind:'generic',
 blocks_movement:true, blocks_sight:true, movable:false,
 light:{intensity:100, range:4}}
```

`capacity_volume_ml` is 1..UINT32_MAX when present; absence does not expose player
storage. `kind` is `generic` (default), `chair`, `table`, or `lamp`. The three flags
are booleans, default false. No machine configuration is allowed for `none`.
Optional `move_cost` is 1..1,000,000 base ticks (default 100), used by grabbed
movement before diagonal and actor-speed scaling; see [grabbing](grabbing.md).
Optional `light` defaults to zero emission and requires integer `intensity` and
`range`, following terrain's emission rules: both zero or both positive, intensity
at most 255, range at most `maxLightRange`. `CreateObject` copies this initial
light to the runtime object, whose enabled state gates emission. A lamp with
nonzero light therefore illuminates its surroundings.

### Grid equipment

The root `objects` array additionally supports these closed behavior schemas:

```json5
{id:'generator',name:'Portable fuel generator',behavior:'generator',
 generator:{fuel_item:'generator_fuel_packet',fuel_capacity:24000,
            energy_per_fuel:6000,output_per_tick:4}}
{id:'grid_battery_station',name:'Installed grid battery',behavior:'grid_battery'}
{id:'grid_scrap_press',name:'Grid scrap press',behavior:'crafting_machine',
 machine:{input:'steel_scrap',input_count:20,output:'steel_sheet',output_count:1,
          duration_ticks:300,energy_capacity:0,energy_per_tick:2,grid_powered:true}}
```

`generator` requires exactly its `generator` config; `fuel_item` resolves after
item loading. `fuel_capacity` is uint64 chemical-energy storage, **not a count of
packets**, and must be at least `energy_per_fuel`. The two rates are positive
uint32 integers. Fuel is consumed only for supplied energy, including battery
charging; no idle burn or autonomous free production is authored here.
`grid_battery` takes neither machine nor generator config and cannot expose
`capacity_volume_ml`. It holds the original installed battery item through the
power-specific installation path. Other behaviors reject `generator` config;
only `crafting_machine` accepts `machine` config.

Machine `grid_powered` is an optional boolean defaulting to false. True requires
explicit `energy_capacity:0`; false retains the existing local-energy-capacity
validation. Population creates equipment stopped; content does not auto-start it.

The furnished house guarantees starter power equipment on the kitchen floor at
authored `(5,2,0)` and a grid scrap press at `(9,2,-1)` in either cellar variant.
Ground placement deliberately accommodates the bulky generator. Workshop towns
also have a grid press; hardware/electrical pools supply wiring and fuel, and
the ground-only workshop bulk pool can supply generators.
The starter cache includes four separate copper-wire items for wiring, battery
installation, and the simple soldering recipe. The kit itself is reusable and
has no charge balance; installation's four-tile cost consumes one copper-wire
item at the default 16 tiles per material.

### Generated building grid descriptors

`worldgen/worldgen.h` exposes
`GenerateBuildingPower(content, plan, chunk, output)`. Like chunk placements, it
clears and reuses caller-owned storage. Every intersecting building emits its
complete inclusive `PowerBounds` and stable `origin` (surface footprint minimum,
Y=0), independent of requesting chunk/floor and placement order. All authored
layers, including roof layers, and the selected basement belong to one extent;
unselected basements and implicit unauthored roofs do not enlarge it. Park and
square sites emit no descriptor. Neighboring buildings remain separate records.
The same record may be emitted from several chunks; register it idempotently.

For the first-house/scenario path call
`DescribeBuildingPower(pattern, footprint, basementVariant)`, using the same
rotated half-open surface footprint and 1-based basement selection as terrain
generation (zero means none). Register its optional descriptor before devices or
player wiring. This helper requires no region cache or RNG.

`world_generation_version` is 7. Version 7 uses the v6 town/terrain algorithm and
the same v6 feature-seed domain for basements and population; versions 2..6 retain
their existing routes. World registers generated electrical grids only for v7+;
the descriptor helpers themselves leave registration/version policy to the caller.

## Loot and monsters

These records go in the root `definitions` array:

```json5
{type:'loot_pool', id:'pantry', entries:[
  {item:'food', weight:3, count:{min:1,max:2}},
  {pool:'supplies', weight:1}
]}
{type:'monster', id:'zombie', name:'Zombie', ai_profile:'hunter',
 body:'simple', faction:'undead', speed:100, attack_damage:12, accuracy:9000}
{type:'monster_group', id:'cellar_monsters', entries:[
  {monster:'zombie', weight:1}
]}
```

Pools and groups require a nonempty `entries` array. Each loot entry names exactly
one `item` or `pool`; a group entry names one `monster`. Entry `weight` defaults
to 1 and is 1..UINT32_MAX, with total weight <= UINT32_MAX. Optional loot `count`
defaults to 1..1; when supplied, both `min` and `max` are required, with
1 <= min <= max <= 1024. Count on a pool entry repeats independent child draws;
one pool invocation selects one alternative. Internal unfinished-craft items
cannot be loot. Forward references are supported, including across files.

Monsters require `id`, `name`, and `ai_profile`. Other fields default as shown.
Bodies are `simple` or `detailed`; factions are `neutral`, `survivors`, or `undead`.
Speed is 1..UINT32_MAX; damage is 0..100000 whole HP. Accuracy is 0..10000 in
ratio-scale units and must be divisible by 100 to convert exactly to the current
combat percent representation. Constructed combat state also passes the existing
combat validator.

Every pool must be acyclic and satisfy these worst-case bounds:

- Longest path <= 16 pool invocations, including the root.
- Expansion work <= 4096 pool invocations, including root draws.
- Output <= 1024 item instances.

The loader uses iterative postorder traversal with scratch state rather than
native recursion or cached fields in catalog records. Bounds use the maximum
over alternatives separately for work, leaves, and external volume; pool counts
multiply child bounds and add the parent invocation to work. This makes limits
independent of weighted probability and preserves small immutable definitions.

## Building extensions

The existing required `rows`, `palette`, and boundary `entrance:{x,z}` remain the
surface at Y=0. Width and height are inferred from 1..64 equal-width printable
ASCII rows, each 1..64 characters. Existing `weight` defaults to 1 (1..1000000).
The optional fields below extend `building_pattern` definitions:

Optional `family` is a stable ID using the same syntax as a building ID. Its
optional `family_weight` is an integer in 1..1000000 and defaults independently
to 1 on each member; it is invalid without `family`. All members of a named
family across all sources must agree on resolved `family_weight`, `site`, and `use`.
An omitted family denotes a singleton, distinct from a named family with the
same ID; its effective family weight is its existing `weight`. For family-first
selection, a named family's `family_weight` sets its prevalence once among
families with fitting variants, and `weight` selects among those variants.
This prevents adding variants from multiplying family prevalence. Generation
versions 1..5 ignore family metadata and retain their existing weight behavior.

Optional `use` is `mixed` (the default), `residential`, `commercial`, or
`industrial`. It may be specified without a family and on any site class. It is
a soft selection preference, not a site eligibility restriction; landmark, park,
and square selections receive no use bias. Each family member resolves its own
default: omitted `use` agrees with explicit `mixed` but conflicts with any other
use. Generation versions 1..5 ignore this selection metadata.

**Prefer palettes for routine population.** The rows should show furniture,
loot and monsters alongside walls and doors. Coordinate arrays remain an escape
hatch for exceptional placements. A palette value is a terrain ID string or:

```json5
C:{terrain:'floor', object:'cabinet', facing:'east',
   loot:[{pool:'supplies',rolls:2,destination:'furniture'}]},
Z:{terrain:'floor', monsters:[{group:'cellar_monsters',chance:5000}]},
i:{terrain:'floor', loot:[{pool:'supplies',chance:8000}]}
```

`terrain` is required. `object`, its `facing`, `loot`, and `monsters` are optional.
Facing defaults north and requires an object. Loot is an array so a cupboard can
combine several pools; monsters is an array with at most one entry because only
one monster may occupy a tile. Entries accept the same pool/group, chance, rolls
and destination fields as coordinate markers, but reject coordinates. Ground loot
and monsters need no object. All references, including unused glyphs, resolve
across sources. Glyph recipes apply on the surface, explicit layers and selected
basement, using the shared palette. Reuse glyphs for identical recipes and distinct
glyphs for different facing, terrain or population.

The loader expands glyphs into ordinary `PatternFurniture`, `PatternLoot` and
`PatternMonster` records. Runtime placement, saving, collision, storage capacity
and spawn limits remain shared. For each population kind, explicit markers come
first, then surface glyphs in row-major order, then layers in authored order (each
row-major). Multiple loot entries on a glyph retain their array order. Each
basement expands its own glyphs after its explicit population. Overlapping
glyph/explicit furniture or monsters are errors rather than overrides; blocking
the entrance is also rejected. Changing authoring from coordinate order to
row-major glyph order can change seeded chance/loot results within a content
revision, even when all marker positions and recipes are equivalent.

Optional `site` is `building` (default), `landmark`, `park`, or `square`. Generation
version 5 matches it to the lot class; large landmark plots and outdoor civic
sites have dedicated slots. Park/square patterns do not receive implicit roofs.

```json5
{
 type:'building_pattern', id:'furnished_house',
 rows:['..Z.','...>','.C..'],
 palette:{'.':'floor','>':'stairs',
   C:{terrain:'floor',object:'cabinet',facing:'east',loot:[{pool:'supplies',destination:'furniture'}]},
   Z:{terrain:'floor',monsters:[{group:'cellar_monsters'}]}}, entrance:{x:0,z:1},
 layers:[{y:1,rows:['....','....','....']}],
 basements:[
   {id:'storage_cellar',weight:1,rows:['....','...>','....'],
    furniture:[],loot:[],monsters:[]},
   {id:'workshop_cellar',weight:1,rows:['....','...>','....'],
    furniture:[],loot:[],monsters:[]}
 ]
}
```

Layer objects contain exactly `y` and `rows`. Y must be nonzero, unique and within
the world's -10..10 bounds. Rows match surface dimensions and use the surface
palette. Y=-1 cannot appear in `layers` when `basements` is supplied. Basement
arrays are nonempty when present; each variant requires a locally unique `id`
and `rows`, accepts optional population arrays and positive `weight` (default 1),
and owns exactly Y=-1. Basement weight sums must fit UINT32_MAX. Variants retain
authored order; their IDs participate in generation seeding, not global IDs.

Coordinate population fields are optional arrays, including in variants. Every
coordinate marker requires explicit integer `x`, `z`, `y`; X/Z must be within the unrotated surface
rectangle. Coordinate order is X,Z,Y throughout parsing and catalog storage.
The Y must identify an authored surface/layer/selected basement. Variant markers
must specify Y=-1. Implicit roof fallback does not provide an authored spawn layer.

- Furniture requires `object`; optional `facing` is `north` (default), `east`,
  `south`, or `west`.
- Loot requires `pool`. Optional `chance` is 0..10000 (default 10000); optional
  `rolls` is 0..1024 (default 1). `destination` is `ground` (default) or `furniture`.
- Monsters require `group`; optional `chance` is 0..10000 (default 10000).

Population requires passable terrain with floor support and no stairs. There is
at most one furniture marker and one monster marker per tile. A monster cannot
overlap movement-blocking furniture. Common population is checked together with
each variant, including cross-population collisions. Multiple loot markers may
share a tile. Furniture destinations require a furniture marker at that exact
coordinate with explicit capacity, even for a zero-output placement.

Placement work and leaf bounds include all rolls. Chance zero or rolls zero
produces no output, but schema, references, graph validity, coordinates and
destination legality still validate. Capacity sums worst-case generated external
volume of every nonzero-chance placement targeting that furniture, across common
and variant populations. Probability never discounts maximum possible volume.
Arithmetic is bounded before accumulation and output is never truncated.

Surface and Y=-1 stairs must match at every X/Z for every basement variant (or an
explicit Y=-1 layer). Stair tiles cannot contain furniture, loot, or monster
markers. Every upstairs stair must match at least one adjacent authored landing
at the same X/Z (up, down, or both). Occupied bundled floors connect all the way
to the ground; their final roofs have no stair. Legacy surface-only patterns retain their existing behavior. General
building connectivity is specific to the authored sample rather than a generic
connectivity solver.

## Integration

The curated `data/first_house.json5` and expanded `data/content.json5` manifests include `data/objects/furniture.json5`,
`data/worldgen/loot/household.json5`, `data/monsters/cellar.json5` and
`data/worldgen/buildings/furnished_house.json5`. The demo manifest uses the
shared base and demo sources. See [content packs](content_pack.md) and the
[implemented first-house design](first_house_design.md), and the
[urban content guide](urban_content.md).
