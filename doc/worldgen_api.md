# World generation slice

`worldgen/worldgen.h` is a deterministic planning and materialization API, not a
world manager. It owns no entities, cache, persistence, or shared RNG. Callers
provide an immutable `ContentCatalog`, seed, and coordinates. The same catalog,
seed, and `world_generation_version` produce the same plan and tiles. Plans must
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
where the complete town fits, three streets on each axis form four blocks,
each split into two 53×26/27 parcels with north/south frontage. Parcel identity is the pair
`(region, parcel ordinal)`; building owner identity uses the same pair. Hashes
are deterministic local mixing, not globally unique IDs.

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

`GenerateChunk` lays out row-major tiles (`z * 16 + x`). Generation version 3
preserves the legacy surface at Y=0, uses solid rock below it and air above it,
with building roofs at Y=1. Partial edge-chunk slots
outside valid world bounds are undamaged wall. `ValidateWorldgenContent` returns
`ContentResult<void>` and optional diagnostics for missing `floor`/`wall` or
impassable `floor`, `grass`, `road`, or `rubble`. Applications validate before
generation; bypassing validation with invalid content is an invariant failure.
Optional `grass`, `tree`, `road`, and
`rubble` terrain fall back respectively to floor, wall, floor, and floor. The
legacy demo square `[-32,32] × [-32,32]` remains floor and is masked from road
queries; no town or building intersects it.

## World integration and debug inspection

`World` owns only a one-region plan cache while generating version-3 chunks. It
regenerates that plan when moving to another region; the pure generator itself
has no cache. Supported generation metadata values 1 and 2 retain their legacy
surface generation; nonsurface slices use the layered terrain rules. This does
not make old save formats readable: disk saves require format 6. Generated tiles become ordinary world chunk tiles
and are thereafter saved as terrain IDs/damage.

The in-game F11 world-generation debug menu previews a plan using the current
world's seed and generation version. It is a generated baseline: saved terrain
edits and live entities are excluded. A world using generation version 1 shows
an unavailable message because the current regional preview cannot reproduce
the legacy generator. The menu does not load chunks, advance simulation, or
mutate the world.

Controls: arrows/hjkl move the cursor by the current scale; uppercase HJKL move
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
