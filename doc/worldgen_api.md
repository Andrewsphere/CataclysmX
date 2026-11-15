# World generation slice

`worldgen/worldgen.h` is a deterministic planning and materialization API, not a
world manager. It owns no entities, cache, persistence, or shared RNG. Callers
provide an immutable `ContentCatalog`, seed, and coordinates. The same catalog,
seed, and generation version produce the same plan, tiles, and placements. Plans must
be unmodified outputs of `GenerateRegionPlan`; materialize them with the same
seed and catalog. Returned query pointers borrow the plan and expire when it is
changed or destroyed.

## Space and region plans

Tiles use X/Z horizontal coordinates and Y elevation (`-10..10`); coordinate
fields are ordered `{x,z,y}`. Chunks are single-layer 16×16 X/Z tiles, and each
region covers 256×256 tiles. `RegionOf` uses mathematical floor division (including negative
coordinates). Valid tiles are `[-1,000,000, 1,000,000]` per axis; plans are
half-open and clipped to this world range. Nominal edge geometry can extend past
the world and is clipped during rasterization. Invalid public coordinates abort
before arithmetic; broken plan/seed/catalog assumptions are always-on assertion
invariants, not recoverable gameplay errors.

`SampleEnvironment(seed, position)` is integer-only and returns moisture,
temperature, and woodland values in `[0,1000]`, plus meadow/woodland classification.
It does not consume RNG state. A region plan contains roads, parcels, buildings,
boundary crossings, and stable feature ownership. Generation 6 uses a 176×176
maximum town envelope and three street-led profiles. Lots follow four-way street
frontages and are packed to the rotated footprint plus two side-margin tiles;
front setback is one tile and rear gardens are 3..8 tiles when space permits,
otherwise at least one. Landmark, park and square opportunities fall back to
ordinary lots when no eligible pattern fits. Parcel identity is the pair
`(region, parcel ordinal)`; building owner identity uses the same pair. Hashes
are deterministic local mixing, not globally unique IDs.

`RegionPlan::parcels` and `buildings` are value-owned `Vector`s: counts depend on
authored footprint widths, and bulk output buffers can be moved cheaply into the
World cache. Inline capacity would cost space and element moves even for empty
plans. Do not retain element pointers across appends. `roads` remains a checked
`StaticList<RoadSegment, 18>`: legacy emits at most six streets and twelve
connectors; v6 emits at most four streets and twelve connectors. `serviceAlleys`
is a vector of pavement rectangles; parcel and building owner ordinals remain stable.

`TownProfile` reports `LegacyGrid`, `MainStreet`, `Crossroads`, or
`OffsetNeighborhood`. Full v6 towns choose equal-weight profiles. MainStreet has
an east/west main street with independent 56..72 reaches and a short north/south
feeder; only the main street has frontage. Crossroads adds frontage on both axes,
with independent east/west reaches 64..80 and north/south reaches 72..80.
OffsetNeighborhood adds two full east/west branches at independent distances
60..64 north and south, with independent reaches 60..76. MainStreet strip depth
is 28..40; the other profiles use 28..32. Complete towns use region-local
`[40,216)` as an envelope, not a tight urban boundary. Clipped edge regions use
legacy-style roads without a town or parcels.

V6 attempts connected rear service lanes for suitable primary strips, rejecting
an obstructed opportunity rather than splitting it. One-tile rear access spurs
connect shallower parcels; alleys are not `IsRoad` segments. Natural terrain
remains outside explicit roads, driveways, parcel front pavement and alleys; there
is no blanket pavement. Site counts depend on content and selected footprints;
with the current bundled pack and preferences, a seeds-0–31 sample in region
`(0,0)` produced 10–12 MainStreet, 19–23 Crossroads and 27–35 OffsetNeighborhood
sites (including parks and squares). These are observations, not content limits.

Each of the four region edges derives its crossing from a canonical
`BoundaryKey`: axis, geometric border coordinate (a multiple of 256), and the
along-border region coordinate. Neighboring regions therefore independently
derive the same width-three crossing and its 16-tile approach. A crossing's
position is on the geometric border, rather than the last tile owned by a
region. Fixed rectilinear routes connect crossings and town streets; the routes
reserve approaches and avoid parcels. One crossing per shared edge gives this
initial rectilinear backbone connectivity across full regions.
At the finite world's clipped edges, surviving road stubs connect inward;
nominal crossings outside valid bounds are absent. Clipped corner regions can
contain no roads.

Buildings are selected from catalog patterns that fit parcel bounds and
setbacks. Eligible pattern weights select among fits; rotations are quarter
turns clockwise, aligning the entrance with the parcel's frontage. V6 supports
north, east, south and west; `streetZ` describes horizontal frontages and
`streetX` vertical frontages, with the unused component zero.
Corner entrances use north, east, south, west priority to choose their authored
side. Building abandonment is deterministic. Materialization
first builds the complete pattern buffer and applies abandonment there, then
crops that buffer to the requested chunk. It never reads adjacent live chunks,
so chunk load order cannot change the result. No-fit parcels simply have no
building plan.

`GenerateRegionPlan(content, seed, region, version)` accepts versions 2..6;
the three-argument overload selects `world_generation_version` (6). The plan
records its version. Versions 2/3 retain the exact legacy weighted-selection,
placement and abandonment algorithm, excluding every pattern with authored
layers, basements or population. Adding such patterns therefore does not alter
legacy candidate weights. Existing legacy definitions must still be present and
unchanged to reproduce their old terrain.

Versions 4..6 protect authored extended patterns from abandonment decoration. Each
selects one weighted basement per building and records its 1-based local index
in `BuildingPlan::basementVariant` (zero means none). Chunk materialization never
reselects this choice.

Version 5 adds `ParcelPlan::site`, matched against `BuildingPattern::site`
(`BuildingSite::Building`, `Landmark`, `Park`, or `Square`). A small closed enum
keeps eligibility explicit and prevents a tiny house from consuming a civic plot.
Weights still choose among fitting patterns within that class. This uses the
existing feature ownership and population pipeline for parks as well as buildings,
so benches, loot and inhabitants have ordinary chunk ownership and persistence.
No separate park entity system or planner cache is needed. Version 5 fills
unoccupied town ground with pavement; version 6 uses selective paving as above.

V6 groups fitting patterns by named `family`, draws family prevalence once using
`family_weight`, then draws among that family's fitting variants using `weight`.
An unnamed pattern is a singleton whose `weight` supplies its prevalence. Family
members must agree on site class, resolved family weight and use across all sources;
`family_weight` requires `family` and defaults to 1. This metadata belongs once
per catalog pattern, not per placed entity. See [house content](house_content.md)
for the authoring rules.

### Neighborhood composition

`BuildingPattern::use` is a typed `BuildingUse` catalog value: optional JSON
`use` accepts `mixed` (default), `residential`, `commercial`, or `industrial`.
It is stored once per catalog pattern. A named family's members must agree on
resolved use as well as site and family weight. No per-entity zoning metadata or
zoning framework is needed.

For generation 6 ordinary `BuildingSite::Building` choices, classify the slot
before choosing a family, using its leading prospective footprint tile. Relative
to each street's center and actual arm endpoints, multiply a fitting family's
base prevalence as follows:

| Use | Preferred location | Multiplier |
| --- | --- | ---: |
| Residential | Side street, not the main street | 3× |
| Commercial | Main street, within 24 longitudinal tiles of town center | 4× |
| Industrial | Outer quarter of either actual street arm | 4× |
| Mixed | Any location | 1× |

Otherwise the multiplier is 1×. All weights remain positive, so these are soft
preferences rather than exclusions or quotas. Count each fitting family once;
the selected family's variant draw retains its authored `weight`. Landmark,
park and square selections receive no use bias. An all-mixed catalog produces
exactly the same v6 output as before these preferences; versions 1..5 ignore
`use`. See [house content](house_content.md#building-extensions) for validation.

`GenerateChunk` lays out row-major tiles (`z * 16 + x`). Versions 2/3
preserve the legacy surface at Y=0, use solid rock below it and air above it,
with rectangular building roofs at Y=1. Versions 4..6 overlay each pattern's explicit
layers and selected basement at their authored Y, using exactly the same X/Z
rotation as the surface and entrance. An explicit Y=1 layer replaces the roof;
without one, version 4 retains its rectangle roof fallback. Versions 5 and 6 mask that
fallback by surface terrain: grass, tree, road, pavement and open space remain
air, and park/square sites receive no implicit roof. Multi-floor content authors
the final roof explicitly, using air for courtyards and setbacks. Partial edge-chunk slots
outside valid world bounds are undamaged wall. `ValidateWorldgenContent` returns
`ContentResult<void>` and optional diagnostics for missing `floor`/`wall` or
impassable `floor`, `grass`, `road`, `rubble`, or `pavement`. Applications validate before
generation; bypassing validation with invalid content is an invariant failure.
Optional `grass`, `tree`, `road`, `rubble`, and `pavement` terrain fall back
respectively to floor, wall, floor, floor, and floor. The
legacy demo square `[-32,32] × [-32,32]` remains floor and is masked from road
queries; no town or building intersects it.

## Pure population and deterministic streams

Bundled populated patterns author furniture, loot and monsters in their row
palettes, including upper floors and basements. The loader expands them to the
same typed records as coordinate placements; see [palette authoring](house_content.md#building-extensions).
Coordinate arrays remain available for exceptional placements. Palette migration
preserves layout and spawn recipes but reorders marker streams into row-major
order, so seeded chance outcomes differ from the preceding content revision.

`GenerateChunkPlacements(content, seed, plan, chunk, output)` clears the three
output vectors while retaining capacity. Versions 1..3 emit nothing. Versions 4..6
return furniture, expanded item leaves and monsters, cropped by each transformed
position's chunk (including Y). Feature order follows the plan; each list visits
common population before the selected basement, then authored marker order and
depth-first loot leaf order. Furniture facing rotates clockwise with X/Z.
Furniture-destination loot remains at its furniture's coordinate and therefore
has the same chunk owner. Output is independent of chunk request order and uses
no World RNG, handles or persistent IDs.

Each output carries a `GenerationKey`: owner `(region,parcel)`, basement index
(zero for common population), kind, authored marker index and expanded leaf
ordinal. Furniture/monster ordinal is zero. Keys are transient tuples scoped to
the immutable seed/catalog/version; their hashes are not persistent identities.
Materialization must issue normal persistent IDs and use full chunk snapshots
to prevent regeneration after looting, movement, deletion or reload.

The implementation uses the fixed SplitMix64 finalizer (xor shifts 30/27/31,
multipliers `bf58476d1ce4e5b9` and `94d049bb133111eb`). Field folding is
`state = Mix(state ^ Mix(field + 9e3779b97f4a7c15))`. Feature seed starts as
`Mix(seed ^ domain)`, then folds signed region x/z converted to uint64, parcel,
stable pattern ID and version. A string folds its byte length followed by each
unsigned UTF-8 byte. Basement domain is `626173656d656e74` ("basement"); population
domain is `706f70756c617465` ("populate"). Marker streams additionally fold the
variant ID (empty for common), kind ordinal and marker index. Numeric catalog IDs
never enter these seeds. The basement index identifies output keys, while its
stable string identifies the population stream.

Each draw increments state by `9e3779b97f4a7c15` and returns `Mix(state)`.
For positive bound B, reject values below `(uint64(0)-B)%B`, then use `value%B`.
Population weighted selection uses authored order and checked positive sums <= UINT32_MAX;
v6 family and variant selection uses stable ID ordering and 64-bit weight sums.
Chance consumes one bounded draw over `ratioScale`, including 0/10000 chances;
successful loot performs `rolls` draws. Each pool selects one entry, then an
inclusive count; item counts emit separate leaves and pool counts repeat child
draws independently. DFS ordinals span all rolls of that marker. Validated limits
are depth 16, 4096 pool invocations and 1024 leaves per placement, with counts and
rolls <=1024. Runtime invariant checks enforce the limits without truncation.
Monster markers test chance once and select one weighted group member.

`FindFirstHouseStart` is a pure query for the `furnished_house` ID. It visits the
origin's 3x3 regions in squared-distance, x, z order and considers every candidate.
The nearest entrance by squared tile distance wins; ties compare entrance x/z,
then owner region x/z and parcel. The starting tile is one driveway step outside
that entrance, supported/passable and free of generated furniture, loot and
monsters. Invalid content or no suitable entrance returns `InvalidData`, with
optional diagnostics. There is no unbounded search or terrain mutation.

## World integration and debug inspection

`World` owns a one-region plan cache while generating regional chunks. It
regenerates that plan when moving to another region; the pure generator itself
has no cache. Cache identity includes seed, region, version and immutable catalog.
Generation metadata 1 retains its World-owned surface generator; 2/3 use the
explicit legacy regional path; 4 adds authored layers and population; 5 adds dense
lots and civic outdoor sites; 6 adds street-led geometry and family selection.
Saved versions 1..5 continue using their recorded algorithm, while new worlds use
6. Reproduction still requires the same content.
Generation
version is separate from save schema (10). Generated tiles become ordinary world chunk tiles
and are thereafter saved as terrain IDs/damage.

The in-game F11 world-generation debug menu previews a plan using the current
world's seed and generation version. It is a generated baseline: saved terrain
edits and live entities are excluded. A world using generation version 1 shows
an unavailable message because the current regional preview cannot reproduce
the legacy generator. The menu does not load chunks, advance simulation, or
mutate the world.

Controls: arrows move the cursor by the current scale; Shift+arrows move
by one 256-tile region. Keys 1–5 select Roads, Parcels, Ownership, Biomes, and
Terrain; Tab and Shift+Tab cycle layers. **+** and **-** adjust scale between 1
and 16 tiles per displayed cell by powers of two. Home or **c** recenters at the
player's menu-entry position. PageUp/PageDown browse detail lines. F11, Escape,
or **q** closes the menu.

The frontend-independent API in [`client/worldgen_debug.h`](../src/client/worldgen_debug.h)
is available to C++ clients and tests. `BuildWorldgenDebugView(content, plan,
layer, scale)` returns an owning row-major image of `TerminalAppearance` values
and bounds/dimensions; `WorldgenDebugDetails(content, plan, selection)` returns
owning detail strings for the selected valid tile. Both query the supplied
generated plan without a terminal or world access, so tests can inspect plans
and image values directly. Terrain cells sample generated baseline terrain at
the cell's top-left tile; biome cells use the clipped cell center. Coarse
roads/parcels/ownership cells aggregate covered tiles so thin features remain
visible. The FTXUI-specific `CreateWorldgenDebugMenu` factory is in
`terminal/debug_menu.h` and belongs to the terminal frontend boundary.

Planning and materialization return values; invalid coordinates and violated
preconditions are programmer errors guarded by assertions. Content loading and
`ValidateWorldgenContent` are the recoverable boundaries (`ContentResult` and
optional diagnostics).
