# Observer-facing terrain illumination

`core/lighting.h` declares:

```cpp
std::uint8_t TerrainLightLevel(const World& world, TileCoord observer, TileCoord target);
```

This is the authoritative terrain illumination query for point/bulk perception
and server presentation. Callers still establish geometric visibility and apply
their vision profile. It does not establish that the target is visible, load
chunks, update memory, or change the lighting field.

`World::GetLightLevel` retains its existing transported/exposure contract. Actor
exposure and propagation use that field, not neighboring surface brightness.
In particular, the existing direct-light value at an opaque ray endpoint stays
available there; it must not be used as an observer-independent wall-face value.

## Deterministic face rule

1. Invalid or nonresident observer/target positions return zero. Transparent
   terrain returns its own `World::GetLightLevel`, including floors and stairs.
2. For a lower opaque target (`observer.y > target.y`), select only its upper
   (+Y) face, matching perception's downward column projection regardless of
   horizontal displacement. Same-level targets select each horizontal side
   facing the observer, using the signs of observer-minus-target X/Z rather than
   a dominant axis. Zero displacement along an axis selects neither of its faces.
   Upward illumination queries retain the old
   3D dominant-axis/tie helper behavior, but perception rejects upward targets.
   Illumination itself does not establish visibility.
3. Sample the immediately adjacent tile outside each selected face. Every sample
   must be resident and transmitting (`!blocksSight`). Vertical samples must
   additionally satisfy `OpenVerticalFace(sample, target)`: the upper cell is
   open space, or both cells are matching stairs. A rejected same-level sample is
   skipped without vetoing another exposed face. Vertical queries retain the
   conservative rule that a rejected sample rejects the incident contribution.
4. Max-combine transported light in those adjacent cells with **direct incident
    light recorded for those same faces**. Each opaque endpoint visible in a
    local source's plane records its distance-attenuated direct level only on its
   source-facing entry face(s), retaining the direct-source dominant-axis/tie rule. Sky records
   its direct opaque endpoint on the upper (+Y) face. Neither record is a spill
   seed. Max-combine with the terrain's own emission intensity. When the
   observer equals an opaque target there is no incident face, only intrinsic
   terrain emission.

The sample is transmitting **space**, never another opaque endpoint scalar.
Thus daylight 192 makes an exposed facade Bright instead of quarter-strength
Dim. A lamp behind that facade cannot lend its direct endpoint value to the
opposite face. A shallow-angle or diagonal view can receive from the facade's
exposed observer-facing side even when the other candidate points into a solid
neighbor along the wall. This avoids visible daylight facades becoming dark as
the observer crosses a dominant-axis boundary. Only observer-facing sides qualify;
the back face and opaque neighbors cannot lend their endpoint light. Target visibility is the caller's
responsibility: horizontal shadowcasting followed by an open-space downward column.

Do **not** require illumination of the observer-plane air above a lower target:
a remote lit top may be visible through several entirely dark air cells. Its
illumination comes from the top's immediately adjacent transmitting space and
top incident sky, independently of those intermediate cells. Nor should a ray
to an adjacent sample's center establish target visibility. Face-specific direct
endpoints preserve receiving illumination without borrowing the solid's scalar
or light from the opposite face. A hidden target can still have a nonzero
illumination result; geometric visibility remains a separate query.

## Vertical geometry and transport

X/Z are horizontal and Y is elevation. Above an opaque wall, transmitting air
can illuminate the exposed top face. Closed floors block vertical queries;
light above a roof is never borrowed for its underside or interior wall faces.
The existing model does not make an opaque upper cell's closed underside
vertically visible. Transparent floors use their own space light rather than
borrowing from above/below, and matching stairs retain existing transport.

This is a derived receiving-only result: it is never written into the field or
used as a spill seed. Local direct light is strictly **2D on the source Y**:
the common `BuildPlanarVisibility` bulk shadowcast determines receiving cells,
then horizontal integer circular attenuation uses `ceil(sqrt(dx*dx + dz*dz))`.
For range `r`, direct level is `intensity * (r + 1 - distance) / (r + 1)`.
Maximum combining, source range **12**, and exactly **two** connected-neighbor
spill steps are retained: first `direct / 4`, then `direct / 8` (integer division).
Only spill crosses Y for local sources, using the existing open-face rule.
Thus even an open vertical shaft receives at most quarter-strength light one
level away, eighth-strength two levels away, and none three levels away.
Sky retains direct vertical column traversal from the world ceiling, then uses
up to `skySpillDistance` connected transmitting-neighbor steps (currently **12**).
To tune the reach, edit this constant in `src/core/light_types.h` and rebuild;
the supported range is **1..15**, enforced at compile time. With `r` equal to
that configured reach, the level at shortest connected distance `d` in 1..r is
`2 * skyLight * (r + 1 - d) / (3 * r)`. Use one integer division on the original
sky intensity, never repeated attenuation. Direct sky remains unchanged, and
distances beyond `r` receive no sky spill. The first step always receives
two-thirds intensity, rounded down; increasing reach stretches the fade.
At reach 12 and sky 192, d0..d12 are
**192, 128, 117, 106, 96, 85, 74, 64, 53, 42, 32, 21, 10**. The nonzero-reach optimization uses
the same formula, so low sky levels cannot extend the tail or wrap a light byte.
Each hop respects transmitting cells and vertical open faces; opaque direct
endpoints never seed transport. Sky and local contributions combine by maximum.
`lightSpillDistance = 2` still applies exclusively to ordinary/local sources.
No new persistent or wire data is required.

## Regression coverage

`tests/lighting_tests.cpp` and `tests/surface_lighting_tests.cpp` cover a roofed
facade lit beyond dim sight, a sealed dark interior, opposite-side local light,
receiving without relaying, shallow and diagonal exposed faces in four rotations,
point/bulk perception agreement, opening/reclosing a missing facade and viewing
its daylit interior beyond dim sight, occluded targets, exposed tops,
closed floors/roofs, matched and unmatched stairs, negative chunk seams,
residency clipping, world bounds, planar direct light, and two-step vertical spill.
Dedicated regressions cover lower walls at underground and elevated levels,
remote projected tops with dark sides and dark observer-plane air, and closed
roofs. Input/output revision tests check unchanged rebuilds, emitter movement,
parking and toggles, conservative sky columns, and unique residency
removals while comparing incremental fields to fresh scalar/incident fields.

## Incremental lighting

`LightingField::Prepare` scans resident chunk metadata and compares
`World::GetLightingInputRevision(ChunkCoord) const` with each cached revision.
The revision is zero for absent chunks and changes monotonically on optical or
terrain emission edits and emitting actor/object movement, toggles, parking,
creation, or removal. Only changed chunks have their tiles and source lists
rescanned. Cached source descriptors avoid global terrain/source rediscovery
when one lamp moves. No World chunk or entity pointers survive synchronization.
`World::LoadedChunks()` enumerates columns in unspecified order, each as a
contiguous sequence of all layers in ascending Y.

Optical edits invalidate only source patches whose horizontal range-plus-spill
bounds and source-Y +/- two layers intersect an edited optical chunk. Sky
invalidates the edited vertical chunk columns and a one-chunk horizontal halo,
which bounds the configured sky reach. Added and removed chunks are optical changes;
surviving caches outside their dependencies retain both storage and revisions.
An ordinary `ankerl::unordered_dense::map<ColumnCoord, unique_ptr<LightColumn>, ColumnHash>`
owns heap-stable columns. Each column contains one inline array of all 21 Y-layer
`LightChunk` caches, matching World's whole-column residency; there is no per-layer
allocation. Map growth and dense-entry relocation during erasure move owning pointers,
not surviving tile arrays. Residency is reconciled by column key; all map insertion
and erasure finishes before scanning resident layers directly in map order. No
separate sorted pointer list is maintained. Chunk lookup hashes
the horizontal column and directly indexes Y, returning absence outside world Y bounds.
Direct sky indexes the above layer in the same column without rebuilding neighbor links.
Sky and source patch gathering look up each intersected column once and copy its layer
intersections rather than hashing per tile. Source patches store chunk coordinates,
never indices that residency insertion or removal could remap.

Metadata synchronization is independent of column iteration order: input refreshes
are chunk-local, invalidation flags accumulate by union, and affected outputs receive
the same Prepare generation. Source descriptors are still sorted because matching
them against the previous source list uses an ordered merge. Optical-change chunks
and horizontal sky-invalidation columns use ordinary dense hash sets: these small
coordinate keys need uniqueness but no order. Transport retains explicit spatial
traversal and max-combination, so cache enumeration cannot alter light values.

The map lives in a private storage implementation in `lighting.cpp`, keeping its
third-party header out of the public lighting API and exception-enabled consumers.
Construction, destruction and move operations are defined out of line. Moving a field
transfers its owned caches; a moved-from field
can be prepared again. Physical address stability does not extend the public borrow
contract: callers still reacquire output spans after every Prepare.
Lamp movement never invalidates sky. A sky-level change invalidates sky but
reuses local source contributions. Equal same-coordinate reload inputs retain
their output revisions; fresh input tokens still trigger descriptor/optics checks.
Each output's complete sky patch is cached separately from its final combined
lighting, so emitter changes reuse it without repeating the sky flood.

The localized sky boundary is sufficient: direct sky can change only in edited
horizontal chunk columns, with all Y layers refreshed. Changes to a transmitting
path can affect a receiver only within `skySpillDistance` hops of the edited geometry.
Since the supported reach is less than chunk width 16, either effect stays within the edited columns
plus one horizontal chunk halo (including diagonal neighbors), at every Y.
Residency changes obey the same bound. Cached outputs outside this set remain valid.

For a requested output, `BuildSky` gathers optics and lazily cached direct sky in
a tile-aligned box: the output's 16x16 plane plus a `skySpillDistance`-tile horizontal halo
and that many Y layers above/below. Every path within the configured reach ending in that
plane fits in this box, even if it turns corners or leaves/reenters the output
chunk. A deterministic multi-source FIFO seeded by **all transmitting direct-sky
cells** visits each reachable cell once at its shortest distance. Distances map
to the curve above; direct opaque endpoints retain only their direct value and
upper-face incident record. No final sky/output cache is a transport seed.

This bounds scratch to `(16 + 2*r)^2 * (2*r + 1)` cells (40x40x25 at the current
reach of 12) and one queue entry per cell, independent
of the number of direct-sky sources or total residency. Two field-owned scratch
vectors reuse capacity across output requests; there is no per-tile or per-source
allocation. Chunk intersections are copied with one lookup per chunk, not per
tile. Low sky intensities shrink the box to the last distance with nonzero output.
Zero/sub-spill sky, uniform opaque outputs and fully direct-lit outputs skip the
flood. Uniform direct fills and closed/opaque upper dependency early exits remain.

Only direct caches are lazy dependencies. They are computed before gathering,
including required resident columns above the box; neighboring final outputs are
never ensured. Direct cache invalidation uses the same conservative column halo.
There are no cached partial spill passes or distance fields to become stale, so
query ordering and no-op Prepare calls with pending outputs cannot change results.

The uniform-sky regression independently computes `skySpillDistance` synchronous, world-wide
reachability passes, retaining direct cells in every pass. It compares all scalar
and incident values at zero, night/rounding thresholds, 192 and 255, plus localized
roof closure/reopening. Focused tests cover exact cutoff, light bands, bent paths,
matched stairs/closed floors, opaque nonseeds, opening/closing, negative seams,
never-finalized seed chunks, pending invalidation, and residency removal/reload.

Sky is retained separately, including its configured spill and upper-face incident
values. Each local source lazily retains a complete sparse contribution patch, computed
within a horizontal range-plus-two patch and exactly five Y layers, avoiding
radius-cubed work. Sources with unchanged position/emission and optics reuse
their patch. Source movement, removal, toggling, parking or emission changes mark
resident chunks intersecting the old/new range-plus-spill bounds, including chunks
whose output or source patch has **never been computed**. Requested final chunks
are reset to sky and max-combined with **all** remaining intersecting source patches. No contribution is
subtracted from a maximum. Duplicate equal sources are valid and deterministic.

This decomposition is exact: max distributes through each integer division and
each connected-neighbor maximum. Computing direct/4 and direct/8 independently
per source, then max-combining, gives the same result as combining direct first.
First-step seeds and final values remain separate, so there is no recursive spill.
The source plane maps packed lighting flags to the shared helper's resident=1,
opaque=2 flags. A caller-sized byte mask receives bulk visibility; no per-target
rays are cast. This deliberately shares planar shadowcasting semantics with bulk
perception instead of requiring equality with 3D point LOS. A reference regression
compares planar direct faces and combined spill against shared bulk visibility
and an independently computed transport field. Another
compares incremental and fresh fields after overlapping-source removal, movement,
intensity/range changes, geometry, sky, and parking changes.

### Field access APIs

```cpp
void LightingField::Prepare(const World& world);
void LightingField::EnsureChunk(const World& world, ChunkCoord position);
void LightingField::Rebuild(const World& world);
std::uint8_t LightingField::Get(TileCoord position) const;
std::uint8_t LightingField::GetIncident(TileCoord position, int face) const;
eastl::span<const std::uint8_t> LightingField::GetChunkLevels(ChunkCoord position) const;
std::uint64_t LightingField::GetChunkRevision(ChunkCoord position) const;
eastl::span<const ChunkCoord> LightingField::ChangedChunks() const;
const LightingField::WorkStats& LightingField::GetWorkStats() const;
```

`Prepare` publishes dependency invalidation and revisions, with **no output,
source transport, or sky transport computation**. `EnsureChunk` consumes that
prepared snapshot and computes only the requested final chunk plus its transport
dependencies. Its World parameter makes the calling protocol explicit; it does
not resynchronize. Always call `Prepare` after input/residency changes before
ensuring output. `Rebuild` is the eager convenience: Prepare, then EnsureChunk
for every loaded chunk. Existing eager callers continue to work.

Face order is `+X, -X, +Z, -Z, +Y, -Y`. After ensuring the corresponding chunk,
`Get`, `GetIncident` and `GetChunkLevels` read its final values. Reading an
unensured resident output fails fast instead of returning stale/zero bytes.
Invalid/nonresident positions return zero; absent chunk spans are empty.
`GetIncident` returns zero for an invalid face. `GetChunkLevels` returns the
256-cell row-major transported field. Borrowed spans expire at the next Prepare
(Rebuild includes Prepare). No query acquires residency, teaches memory, changes
simulation state, or depends on an observer or player Y.

`GetChunkRevision` returns zero for absent chunks. Every field has a monotonic
nonzero synchronization generation; during Prepare, invalidated resident chunks
receive that generation **before computation**. EnsureChunk never changes a
revision. Unchanged chunks retain their revision, including across a no-op
Prepare while their output is still pending. Generation exhaustion fails fast rather than
reusing zero or an older generation. These are derived output revisions, separate
from World's input revisions: a remote lamp can change a chunk's lighting without
changing its terrain or emitter inputs.

`ChangedChunks` borrows the last Prepare's unique invalidated coordinates in
**unspecified order**, including removed chunks whose current revision is zero.
The set is conservative and includes pending/uncomputed outputs and outputs
whose resulting bytes may be equal. The list is empty before the first Prepare
and after a no-op Prepare. Consume it immediately when syncing: a later no-op
Prepare clears the list but preserves revisions and outstanding pending work.
Each removed layer is appended once during keyed residency reconciliation, and
each surviving dirty layer once during the final scan. These sets are disjoint,
so the vector-backed span requires neither sorting nor a separate deduplication
container. World's observer invalidation only asks whether any changed coordinate
intersects its region. Consumers needing canonical diagnostic output can sort a
copy; processing this notification must not depend on its enumeration order.

### Coordinator integration in World

1. Replace the global eager refresh's `lighting.Rebuild(world)` with
   `lighting.Prepare(world)` when lighting inputs/residency are dirty. Immediately
   consume `ChangedChunks` to invalidate intersecting observer lighting footprints.
   This synchronization must run **before** an observer cache-validity comparison,
   even if no observer will request lighting output on this call.
2. `GetLightLevel(p)` and `GetIncidentLightLevel(p, face)` must synchronize first,
   then call `lighting.EnsureChunk(world, ChunkOf(p))` before forwarding to Get or
   GetIncident. Apply the same rule for every caller and elevation.
3. Bulk sample refresh must synchronize first, compare GetChunkRevision with its
   cached sample revision, then EnsureChunk before GetChunkLevels/copying the
   bytes on a mismatch. Publish the sample revision only after copying. New sample
   slots start at revision zero. Removed chunks have revision zero/empty output.
4. Ensuring output does not publish new invalidations. Do not resynchronize or
   clear ChangedChunks between Prepare and consuming it, and do not use output
   computation itself as the trigger for observer invalidation.

`WorkStats` resets at Prepare and counts final `outputChunks`, dependency
`skyDirectChunks`, `skyFirstChunks`, and `sourcePatches` built since
then. The legacy `skyFirstChunks` member now counts bounded multi-step sky spill
patches built for requested chunks (fast-path outputs do not increment it).
These are deterministic work counters, not timings. Prepare leaves all
counters zero. A source may compute a whole bounded patch as a dependency of
one requested chunk; other final chunks remain uncomputed. Metadata scanning
and descriptor sorting still scale with residency/source count. Cache allocation
is residency-sized; tile optics are eagerly synchronized, output work is lazy.

New lazy regressions cover never-computed dependency invalidation, repeated no-op
syncs with pending work, arbitrary elevation/query ordering, inactive sources,
remote residency retaining addresses/revisions, negative-index insertion with
source patch reuse, column-map growth followed by early-column erasure and cached
source recombination, different insertion/erase histories with same-sized residency
replacement, mixed removed/dirty notification sets, roof/seam/source changes and
removals. Notification tests check uniqueness and set equality rather than order.
The independent full
sky and combined-source transport regressions also exercise lazy computation and
compare **both scalar and all six incident faces**, independently of fresh-field
equivalence.

### Eight-step skylight and facade validation (2026-11-10)

Built `cataclysmx_tests` and `cataclysmx_game` with the `win-msvc-debug` build
preset. The final focused CTest run using that preset passed **64/64 cases**,
covering lighting, exposed wall faces, point/bulk perception, lazy invalidation,
and related rendering/server projection behavior. The first run exposed an old
"sealed interior" fixture with open corridor ends; the longer flood correctly
reached those ends. Closing the fixture's ends restored the intended sealed-room
regression. The new open-facade regression verifies the brighter interior and its
return to darkness after reclosing.

For the configurable-attenuation follow-up, temporarily setting only
`skySpillDistance` to **12** passed both the independent transport reference and
configured seam/falloff regressions with `win-msvc-debug`. After restoring **8**,
the Debug game and tests rebuilt and **14/14 focused CTest cases** passed,
including the original eight-step curve and ordinary-light spill regressions.

### Historical validation before the planar model (2026-11-09)

The measurements below describe the previous spherical/ray implementation only.
They are not validation or performance claims for the planar implementation.

Built `cataclysmx_tests` with the `win-msvc-debug` configure preset in a separate
temporary directory, terminal disabled, reusing the existing Debug vcpkg
installation without running package installation. The final lighting plus
unchanged elevated network projection run passed **20 cases / 344,104 assertions**
in both Debug and Release:

```powershell
$build = 'C:/Users/RELAXE~1/AppData/Local/Temp/opencode/surface-lighting-debug'
cmake --build $build --target cataclysmx_tests -j 4
& "$build/cataclysmx_tests.exe" '[lighting],Actual elevated server projections roundtrip independent terrain and overlay depths'
& "$build/cataclysmx_tests.exe" '[lighting-benchmark]'
```

The explicit hidden benchmark performs 20 genuine alternating source moves per
resident radius, timing only `LightingField::Rebuild` outside simulation. It uses
a range-12/intensity-192 actor source, default sky, a flat floor/air clearing,
and generated resident surroundings. Initial construction is untimed. Each move
times the retained field first and a fresh field second. The final field is
compared against fresh scalar and incident data over every resident cell. No
hardware-sensitive timing assertions are used.

| Build / resident radius | Incremental median / p95 | Fresh median / p95 |
| --- | --- | --- |
| Debug / 2 (525 chunks) | 5.068 / 5.243 ms | 17.715 / 18.075 ms |
| Debug / 5 (2541 chunks) | 8.315 / 8.421 ms | 70.441 / 71.642 ms |
| Release / 2 | 1.040 / 1.254 ms | 2.925 / 3.256 ms |
| Release / 5 | 1.545 / 1.599 ms | 11.089 / 11.455 ms |

Release used the same configuration overrides in the isolated
`surface-lighting-release` directory with preset `win-msvc-release`. Each explicit
benchmark run also passed 4,724,271 field-equivalence/setup assertions.

Full isolated Debug CTest was run with `--output-on-failure -j 4`. The first run
passed 382/392 tests; ten headless/script tests could not run successfully because
only the test target had been built. After building `cataclysmx_headless`,
`--rerun-failed --output-on-failure -j 4` passed nine of those ten. The remaining
failure was `headless_lan_roundtrip`: after successful initial discovery, two
guest waits and checkpoint, resumed Alice's queued command remained unresolved.
Thus 391/392 tests passed across the two runs; terminal tests were not configured
in this isolated build. Concurrent integration edits continued during validation.

These are lighting-only measurements, not a replacement for the earlier 79 ms
Debug movement/frame baseline or a claim that the whole radius-64 interaction is
below 16 ms. The resident optics/source discovery scan still scales with resident
area; source ray work and final recombination are localized. End-to-end timing
remains an integration task.
