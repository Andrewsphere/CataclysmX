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
boundary crossings, and stable feature ownership. Town geometry is 112×112;
where the complete town fits, three streets on each axis form four blocks.
Generation 5 splits each block into two 53×25 frontage strips, separated by a
three-tile paved rear alley. Strips alternate two and three lots (26/27 or
17/18 tiles wide), with one-tile building setbacks. A seed-selected strip is
reserved whole for a landmark when that site class exists in content. Separate
strips reserve a park and a square lot. The bundled pack therefore supplies 17
buildings plus two civic outdoor sites per complete town; catalogs without those
site classes use ordinary lots instead. Versions 2..4 retain eight 53×26/27
parcels with two-tile setbacks. Parcel identity is the pair
`(region, parcel ordinal)`; building owner identity uses the same pair. Hashes
are deterministic local mixing, not globally unique IDs.

Plan roads, parcels, and buildings use `StaticList` with capacities 18, 24, and 24.
The geometric bound is four blocks × two strips × at most three lots; it is not
inferred from the number of content definitions. The generator emits six streets
and twelve connector segments, and at most one site per lot. Checked appends make changes
that violate those bounds fail fast. Use `Size`/`Empty`, indexing, or iteration
to inspect the used records; inline storage avoids three growing heap buffers
per plan without affecting generation order or version.

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
turns clockwise, aligning the entrance with the parcel's north/south frontage.
Corner entrances use north, east, south, west priority to choose their authored
side. Building abandonment is deterministic. Materialization
first builds the complete pattern buffer and applies abandonment there, then
crops that buffer to the requested chunk. It never reads adjacent live chunks,
so chunk load order cannot change the result. No-fit parcels simply have no
building plan.

`GenerateRegionPlan(content, seed, region, version)` accepts versions 2..5;
the three-argument overload selects `world_generation_version` (5). The plan
records its version. Versions 2/3 retain the exact legacy weighted-selection,
placement and abandonment algorithm, excluding every pattern with authored
layers, basements or population. Adding such patterns therefore does not alter
legacy candidate weights. Existing legacy definitions must still be present and
unchanged to reproduce their old terrain.

Versions 4/5 protect authored extended patterns from abandonment decoration. Each
selects one weighted basement per building and records its 1-based local index
in `BuildingPlan::basementVariant` (zero means none). Chunk materialization never
reselects this choice.

Version 5 adds `ParcelPlan::site`, matched against `BuildingPattern::site`
(`BuildingSite::Building`, `Landmark`, `Park`, or `Square`). A small closed enum
keeps eligibility explicit and prevents a tiny house from consuming a civic plot.
Weights still choose among fitting patterns within that class. This uses the
existing feature ownership and population pipeline for parks as well as buildings,
so benches, loot and inhabitants have ordinary chunk ownership and persistence.
No separate park entity system or planner cache is needed. All unoccupied ground
inside the town is pavement (floor fallback), with authored lawns and gardens
inside outdoor patterns. Roads retain their distinct road role and connected
driveways; paving is pedestrian space, not additional `IsRoad` centerlines.

`GenerateChunk` lays out row-major tiles (`z * 16 + x`). Versions 2/3
preserve the legacy surface at Y=0, use solid rock below it and air above it,
with rectangular building roofs at Y=1. Versions 4/5 overlay each pattern's explicit
layers and selected basement at their authored Y, using exactly the same X/Z
rotation as the surface and entrance. An explicit Y=1 layer replaces the roof;
without one, version 4 retains its rectangle roof fallback. Version 5 masks that
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

`GenerateChunkPlacements(content, seed, plan, chunk, output)` clears the three
output vectors while retaining capacity. Versions 1..3 emit nothing. Versions 4/5
returns furniture, expanded item leaves and monsters, cropped by each transformed
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
Weighted selection uses authored order and checked positive sums <= UINT32_MAX.
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
lots and civic outdoor sites. Saved versions 1..4 continue using their recorded
algorithm, while new worlds use 5. Reproduction still requires the same content.
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
