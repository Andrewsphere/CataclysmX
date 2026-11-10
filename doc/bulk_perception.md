# Planar terrain perception (2.5D)

## Geometry contract

Perception first computes geometric field of view **only on the observer's X/Z
plane**. Distance is the inclusive integer circle `dx*dx + dz*dz <= radius*radius`;
vertical depth does not consume range. Missing tiles block and are never visible.
Resident opaque endpoints are visible, and cast shadows beyond themselves. A
resident opaque origin is visible and is exempt from blocking its own scan.

For each visible observer-plane tile, reveal that same X/Z column downward while
the current tile has `openSpace`. Include the first resident non-open-space tile
(floor, wall, stairs, etc.), then stop. A missing tile stops the column without
revealing anything beyond it. Depth is bounded by `world_min_y`. Never reveal
above the observer, spread sideways below an aperture, or reveal through a floor.
Matched stairs do not extend perception columns. `OpenVerticalFace` remains the
separate movement/light adjacency helper (upper open space or matched stairs).

Illumination is a later, per-target filter. Dark upper air cannot obstruct the
geometry of a lit surface below it. `TerrainWithinVision` applies the existing
target-light-band and observer-glare policy using horizontal distance; it does
not test occlusion. `CanSeeTile` combines that filter and geometric perception.
`LineVisible` is now a bounded point entry into this same geometry contract, not
an independent 3D supercover oracle. Invalid, upward, and out-of-range queries
return before extracting optics. A point query copies only clipped chunk rows
from the observer plane within the smallest integer radius containing the target,
invokes the shared planar helper, and then inspects only the target column down
to its requested Y. It never enumerates all resident chunks, builds an
`OpticalField`, invokes `BuildVisibilityMask`, or projects unrelated columns.
Work is bounded by O(r²) extraction/scan plus target-column depth, independent of
total residency. Bulk callers should still reuse masks for many target queries.

## Shared planar helper

```cpp
void BuildPlanarVisibility(eastl::span<const std::uint8_t> optics,
    int width, int depth, int originX, int originZ, int radius,
    eastl::span<std::uint8_t> visible,
    std::uint64_t* visitedCells = nullptr);
```

Declared in `core/planar_visibility.h`, with no World or lighting dependency in
the API. Input is row-major: bit 0 resident, bit 1 opaque; other bits are ignored.
Both spans must have exactly `width*depth` entries and must not alias. Dimensions
must be positive, the origin in bounds, and radius in `[0, playerSightRange]`.
Broken preconditions fail fast. Output is overwritten with 0/1, including when
the origin is missing. The optional work counter is **incremented**, not reset.

The algorithm is symmetric shadowcasting in four cardinal quadrants. Rows carry
rational start/end slopes; signed integer floor division and cross products
implement ties and comparisons without floating point. Start columns round ties
up and end columns round ties down. Floor centers must lie in the unshadowed
angular interval; wall endpoints may intersect its edge. Floor-to-wall
transitions split the next row, and wall-to-floor transitions narrow its start.
Recursion depth is bounded by radius (64). No rays, prefix tries, or summed 3D
volumes are built. The planar scan does roughly O(r²) cell work, plus output
clearing proportional to the supplied buffer size.

This intentionally chooses expansive walls and reciprocal **floor-to-floor**
visibility, with quarter-turn/reflection symmetry. Corner peeking is permissive:
two touching orthogonal walls can leave the diagonal floor visible. This is not
the previous supercover corner rule. Wall endpoints need not be reciprocal with
an observer placed inside a wall. Circular clipping controls revealed cells;
off-circle cells in scanned rows may still be inspected as angular blockers.
Out-of-buffer cells are treated as missing blockers.

## Owned optics and planar workspace

`OpticalField::Rebuild` owns one copied flag byte per resident tile, keyed by
chunk `(x,z,y)` in unordered storage, and advances its revision. Coordinate lookup
avoids maintaining a sorted runtime sequence; no iteration order is promised.
Bits 2/3 are open-space/stairs for
projection and existing optical compatibility. No World/chunk pointers survive
structural mutation. `Refresh(const World&, eastl::span<const ChunkCoord>)` accepts
exact edited/loaded/unloaded chunk coordinates, updates only those snapshots,
removes absent chunks, and advances revision only if optical bytes or residency
changed. Input order and duplicate coordinates are allowed. World retains changed
optical chunk coordinates and refreshes only those snapshots after initialization.
Appearance-only changes need no optical rebuild.

`Find` returns a read-only chunk snapshot or null. Its pointer is borrowed only
until the next `Rebuild`, `Refresh`, move, or destruction of the field: insertion
and removal can relocate even unrelated chunks. The field has unique ownership
and is movable but not copyable. Discard reusable workspaces referring to a field
before moving it or assigning another field to it.

`VisibilityWorkspace::Prepare(field, first, last)` now requires `first.y ==
last.y`: its `optics` is exactly `width*depth` bytes for one chunk-aligned plane.
It reuses unchanged field/window generations. After a field revision it compares
only local plane chunks and writes changed bytes; unrelated elevations/windows
do not count as workspace refreshes. A window change rebuilds the plane. The
field must outlive its reusable workspace and advance revisions after mutations.
Passing no field to `BuildVisibilityMask` creates a temporary snapshot and ignores
any supplied reusable workspace, avoiding dangling cache identities.

Only geometrically visible open columns probe lower layers directly through the
snapshot. There is no full-height observer working volume. The maximum layer
bound is the observer's Y, and the minimum is `world_min_y`; this bound does not
imply allocation or scanning of a dense 3D volume.

## VisibilityMask integration

Existing members `origin`, `radius`, `minimumY`, `maximumY`, `cells`,
`visibleLayers`, `clearLayers`, `Index`, `Get`, `Contains`, `MaterializeLayer`, and
`ClearRow` remain. Layer flags and new `layerOffsets` are indexed relative to
`minimumY`. `Get` returns bit 0 visible, bit 1 opaque endpoint.

Dense storage is **packed by actual touched layer**. `Index` uses `layerOffsets`
and requires an in-bounds, materialized coordinate. Callers must not calculate
their own Y-plane offsets. An absent dense layer has offset -1. Complete visible
nonopaque circles are represented implicitly by `clearLayers`, and allocate no
retained cells. Thus the usual open floor observer retains one implicit plane,
not overhead sky or a full-height volume. Sparse projected layers allocate one
dense square each; no untouched layers allocate cells. Builds may temporarily
materialize touched circles before recognizing the implicit representation.

`MaterializeLayer` appends exactly one plane, preserving all existing offsets,
and expands an implicit circle when needed. `ClearRow` returns inclusive X bounds
for an implicit horizontal circle, or `{1,0}` for an empty row. `visibleLayers` is
a conservative accelerator: filtering may leave it true after removing all cells.
Copied masks own both their cells and offsets, so filtering cannot corrupt geometry.

World filters each projected target independently using horizontal distance.
Optical edits invalidate observers whose horizontal range and downward layer
window intersect the edited chunk. Moving observers recast only their own FOV.
Lighting output changes invalidate intersecting observers independently, including
a one-cell surface-sampling margin. Chunk light samples are copied lazily by
output revision only for chunks participating in a filter. An emitter movement
does not copy every resident light level or rescan unchanged terrain for emitters.

Filtering traverses World's owned columns without spatial sorting. Observation
updates look up each chunk's previously learned visibility bits by coordinate,
so column insertion or dense-map erasure cannot disrupt footprint matching.
Unchanged visible cells are skipped unless the chunk's appearance revision changed;
light sample cache slots also check their coordinate and output revision before reuse.

### Bulk projection borrows

```cpp
const VisibilityMask* World::GetPlayerVisibility(ActorHandle observer) const;
eastl::span<const std::uint8_t> World::GetChunkLightLevels(ChunkCoord position) const;
```

The first returns null for an unavailable/parked/nonplayer observer; otherwise it
refreshes perception once and returns a read-only mask. The borrow expires at a
World mutation or subsequent perception refresh, including another observer's
cache creation. Chunk light is row-major, empty for nonresident chunks, lazily
refreshed, and borrowed until the next lighting rebuild. These projection-only
borrows never learn memory, load chunks, consume RNG, or advance simulation.
They avoid repeating observer-cache validation and chunk searches at each cell
of a 129×129 server snapshot. The server resolves perception first, then keeps
borrowed chunks and light spans only during its read-only projection operation.

## Counters and verification

`geometryRebuilds` counts mask
builds; `opticalRebuilds` counts snapshot rebuild/refresh operations; the legacy `volumeRebuilds`
now counts **planar workspace refreshes**, not volume construction.
`traversedVoxels` now counts planar shadowcast cell inspections (including the
origin, repeated quadrant boundaries, and missing/out-of-buffer blockers).
For an unobstructed radius-r scan it is `1 + 4*r*(r+2)`, independent of world
height. `VisibilityWorkspace::projectionSamples` is reset each build and counts
downward residency probes separately; builds accumulate this value into
`PerceptionStats::projectedCells`. Floor-only scenes perform zero such probes.
These counters do not include snapshot copying or dense-mask packing.
`pointLosCalls` still counts `LineVisible` calls; it no longer represents a ray.
For point queries, `traversedVoxels` counts resident patch tile decodes, helper
scan visits, and direct column tile probes (including a terminating missing
probe). A tile decoded and then scanned contributes twice: those are separate
operations. No geometry/optical/workspace rebuild counters advance on point
queries. Invalid/upward/out-of-range queries count the call but no inspections.

`tests/bulk_perception_tests.cpp` exercises real wall/doorway layouts, the explicit
diagonal corner rule, missing origin/cells, rotation and floor reciprocity,
downward surfaces/stairs/missing layers, negative chunk seams, radius 64, world
and buffer bounds, packed layer mutation, workspace reuse, and World cache/memory
behavior. Point/bulk comparisons are integration checks, not an independent
geometric oracle. Hidden benchmarks remain `[bulk-benchmark]` and
`[bulk-server-benchmark]`; counters now describe the planar contract above.

The previous exact-3D benchmark numbers are not applicable to this algorithm.
See [the benchmark matrix](perception_benchmarks.md) for integrated Debug results
and validation of the planar model and full-size terminal views.
