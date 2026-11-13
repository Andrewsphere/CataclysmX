# Core lighting, perception, and player memory

`LineVisible` is illumination-independent: it shadowcasts in the observer's X/Z
plane, then follows the target column downward through open space. Opaque planar
endpoints can be visible; missing tiles block. Targets above the observer are not
visible. Queries never request residency. A point query builds a local planar FOV,
so a batch of point queries is not a substitute for the bulk mask.

`CanSeeTile` uses the **target's** light band and horizontal X/Z distance,
including for lower targets. Default vision is range 64, dim range 6, dark range 1. An observer at
light 240 or above has dim/dark range capped at 3; ordinarily lit targets remain
unaffected. `ValidVisionProfile` requires ordered nonnegative ranges bounded by
64, glare range no larger than dim range, and a bright-band glare threshold.
Passing an invalid profile is an invariant violation.

`WithinSightRange` shares range filtering across terrain, authoritative observation
footprints, and live actor/item/object overlays. A positive range includes all eight
immediate same-layer neighbors (horizontal squared distance at most two), including
in darkness. Zero range remains self-only; distance-two cells and lower diagonals
receive no exception. Longer ranges keep their Euclidean boundary. Geometry still
controls visibility: walls and closed doors are visible endpoints, missing tiles
block, and downward projection requires open columns. For a configured maximum
range of one, the bulk planar pass uses radius two cropped to the one-tile square;
only the observer plane receives diagonals, including implicit clear layers/rows.
This keeps bulk queries consistent with the point query's ceil-distance planar pass.

## Derived lighting

`World::GetLightLevel` lazily refreshes one shared resident-world field. Geometry,
emission, source movement/parking, sky changes, and residency changes invalidate
it. Time counters, views, and observations do not invalidate it. Content is
immutable for the lifetime of a World.

Terrain light, enabled object light, and nonparked actor light contribute from all
resident chunks, including inactive chunks. Direct local sources propagate in
their own X/Z plane using planar visibility and horizontal radius; intensity at integer ceil-distance `d` is `intensity * (range+1-d) /
(range+1)`. Integer division truncates. Contributions max-combine, never sum.
Intensity and range are independent; valid emissions are either both zero or
have nonzero intensity and range 1–12.

Sky (default 192) enters at `world_max_y`, travels straight down through open
vertical faces, and stops at opaque geometry. A closed floor face blocks sky
even when its terrain is horizontally transparent. There is no ambient basement
light. Illuminated opaque faces cannot seed or transmit indirect light.

Local indirect light uses exactly two open six-neighbor graph steps, including vertical
spill between layers: direct /4 at the
first step, direct /8 at the second. First-step values are stored separately;
second-step values never read final light. This bounds spill at Dim (63), avoids
query-order dependence, and prevents repeated-query creep. Skylight instead uses
up to `r = skySpillDistance` connected steps, with
`2 * sky * (r+1-d) / (3*r)` at shortest path distance `d` in 1..r.
The compile-time reach in `core/light_types.h` supports 1..15 and is currently 12.
At default sky 192 and reach 12 this gives 128 at the first step and 10 at the
twelfth, with no spill at thirteen or more. This makes open-front interiors readable
while sealed rooms remain dark; walls and closed vertical faces still block every
hop. Lighting retains sky
and per-source patches; changed sources recombine affected chunks, while optical
or residency changes rebuild affected shadow footprints. `World::GetLightingInputRevision(ChunkCoord)`
tracks local lighting inputs; `LightingField::GetChunkRevision` and `ChangedChunks()`
track recombined output and let observers invalidate locally. `ChangedChunks()`
contains unique coordinates in unspecified order, is borrowed until the next
Prepare (including Rebuild), and includes removed chunks and uncomputed outputs.
Observers only test whether any entry intersects their region. See [surface lighting](surface_lighting.md)
for invalidation and the observer-facing opaque-face rule. Terrain perception uses
that face illumination; actor exposure uses transported space light. Opaque faces
receive light without relaying it.

World and light-field lookups hash the X/Z column and index its Y-layer array.
Lighting owns heap-stable columns and scans them in map order without a separate
sorted pointer index. Chunk-local synchronization and order-independent invalidation
keep lighting results independent of insertion/erase history. World light queries
synchronize pending inputs and lazily ensure the requested resident output.

Default active-radius 1 / resident-radius 5 bubbles provide sight 64 plus a
14-tile source-and-spill margin. Explicit smaller bubbles deliberately clip light
and sight at residency boundaries. See [sight defaults](sight_defaults.md).

## Owned observations

Call `World::UpdatePlayerObservations` explicitly after an action/event boundary.
It visits resident active nonparked players in persistent-ID order and observes
perceivable tiles within the configured horizontal sight circle, projected downward
and clipped to residency independently of viewport. Bulk visibility reuses planar
optics and a workspace for downward projection; see
[bulk perception](bulk_perception.md) for cache lifetimes and invalidation.
Neither perception queries nor arbitrary low-level edits perform observations.
The server invokes it on construction and player activation, after each successful
player action, after each same-tick batch of due effects in `AdvanceUntilInput`,
and after each NPC action. Effects due at one tick are observed together after
the batch, before the input barrier. Failed player commands do not themselves
teach. Views compute current visibility from current world state but never refresh
owned memory; saves likewise serialize memory as-is. Offline and Joining actors
do not see live map changes. Administrative edit batches must explicitly call
`UpdatePlayerObservations()` if intended to teach resident active players.

World privately owns sparse memories keyed by player ID and chunk coordinate.
`GetPlayerMemories()` returns a by-value owned snapshot sorted by player ID and
then chunk `(x,z,y)`; saving captures the same canonical order. Ordering at this
boundary avoids maintaining sorted player and chunk collections during observation.
Each allocated chunk has 256 terrain IDs (zero means unknown) and
sparse objects sorted by tile index. Object snapshots contain only kind, facing,
active, and broken flags. No actors, items, live handles, or behavior state are
remembered. Observing an empty location removes stale furniture. Parking,
inactivity, and unload preserve memory without learning; player destruction
removes it. Save snapshots include memories and skylight; chunk archives include
actor/object emission and semantic object appearance. Derived light is not saved.
Memory checkpoints rewrite all memory rows; incremental checkpoint updates are not
implemented.

## Historical performance check

Before the integrated radius-64 work, an isolated MSVC harness measured the then-default
25-column / 21-layer resident bubble (resident radius 2, sight range 12)
with one active player. Lighting builds, cached observation updates, and cached
queries were timed separately. Three runs on the development machine gave these
median milliseconds (profiling executables and builds lived outside the project):

| Operation | Debug before | Debug optimized | Release optimized |
| --- | ---: | ---: | ---: |
| First lighting build, sky only | 457.4 | 11.3 | 2.06 |
| Rebuild with one range-12 source | 490.4 | 30.9 | 4.17 |
| Repeated observation, cached lighting | 31.7 | 19.2 | 2.06 |
| 100,000 cached light queries | 26.5 | 5.14 | 1.31 |
| Server construction | 499.0 | 37.4 | 5.44 |
| Two servers, six commands, snapshot-cadence replay | 4030.8 | 883.4 | 107.0 |

The replay includes construction, views at radii 0/12/3, actions, and per-command
memory comparisons. Full resident-light checksums matched before/after and across
debug/release. These are local diagnostic measurements, not timing assertions in
tests and do not describe the current implementation. For the integrated benchmark
method and results, see the [benchmark matrix](perception_benchmarks.md).
