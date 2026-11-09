# Bulk terrain perception and World revisions

## Public World API

```cpp
bool CanPlayerSee(ActorHandle observer, TileCoord target) const;
std::uint64_t GetRevision() const;
std::uint64_t GetMapRevision() const;
WorldResult<void> SetVisionProfile(VisionProfile vision);
VisionProfile GetVisionProfile() const;
PerceptionStats GetPerceptionStats() const;
void ResetPerceptionStats() const;
std::uint8_t GetIncidentLightLevel(TileCoord position, int face) const;
```

`CanPlayerSee` is authoritative **terrain** visibility for an active, nonparked
player. Invalid/stale handles, invalid coordinates, parked players, and inactive
players return false. It may refresh disposable caches, but never learns memory,
requests chunks, advances simulation, or consumes RNG. Const server projections
and explicit `UpdatePlayerObservations` boundaries share the same observer cache.
Visibility is residency-clipped and independent of viewport size.

Default maximum sight is 64; source range remains independently bounded to 12.
Dim/dark ranges remain 6/1, with the existing glare cap. `SetVisionProfile`
validates the complete profile and returns `WorldError::invalid_request` without
changing state on failure. The profile is World/session-local benchmark/gameplay
policy, not a new save or wire field. Loading a save does not install a different
profile. Range 12 can be selected for comparisons without recompilation.

`GetRevision` advances on presentation-relevant World mutations, including item
inventory/state, action timing/counters, and direct administrative edits.
`GetMapRevision` advances for terrain, actor/ground-item overlays, object
appearance, light inputs, residency/activity, observer policy, and learned memory.
Pure counters, scheduling, carry capacity, and item runtime status do not advance
the map revision. Revisions are conservative invalidation tokens, not mutation
counts: a compound operation may advance several times. Reading caches does not
advance presentation revisions. Callers should compare tokens for equality.

`GetIncidentLightLevel` forwards the derived lighting field's direct incident
surface sample. Faces are +X, -X, +Z, -Z, +Y, -Y (indices 0..5); invalid faces or
coordinates return zero. It refreshes pending lighting but does not observe.

## Exact shared 3D geometry

`BuildVisibilityMask` retains `LineVisible` as an independent point-query oracle.
The mask covers a spherical radius, with Y clamped to actual world bounds. It
uses a reusable `OpticalField` snapshot of one byte of flags per resident voxel;
absent residency blocks. Snapshot rebuilds follow optical/residency mutations,
not observer movement. No entity/chunk pointers survive a structural mutation.

Three complementary exact operations share occlusion work:

1. Uniform slab certificates prove clear ground/sky slabs or closed separating
   floor sheets. These avoid both per-target traversal and summed-volume setup
   in the important flat, open movement case.
   Chunk summaries and clipped 16-bit optical row masks establish these proofs
   without sampling the full volume. Clipped edge chunks exclude unrelated
   blockers outside the observer's box.
2. Summed volumes count opaque/missing cells and closed vertical faces. An empty
   ray bounding box proves visibility; a completely closed separating face plane
   proves invisibility. Opaque origin/target cells receive precisely the point
   traversal's endpoint exemptions. These tests are sufficient proofs only;
   finding an obstruction in the box does **not** imply the ray is blocked.
3. Unresolved targets traverse a shared prefix tree of ordered integer 3D face
   events, with eight sign-octant roots. Optical/corner/vertical checks are made
   once per new prefix. A prefix distinguishes clear continuation, an opaque
   endpoint, and a closed transition. Siblings reuse the resolved prefix.
   Every fourth event also attempts a clear-volume proof for the remaining
   corridor, avoiding expansion of long, unobstructed sky tails after passing
   nearby geometry. This does not restart or approximate the ray's integer phase.

The fallback uses the same integer odd numerators and cross-product comparisons
as `LineVisible`. Every subset at a tied crossing is checked, including the
vertical faces touched by side cells. Opaque endpoints are visible, opaque
intermediate/corner-touch cells block, and an opaque origin is exempt. A vertical
face opens only for upper open space or matched stairs. A floor certificate must
cover the complete possible crossing area; partial floors always use the exact
path. This is one 3D mask, never independent 2D fields on different elevations.

This hybrid was selected instead of changing LOS semantics to conventional 2D
shadowcasting. It gives constant-time whole-volume proofs for empty sky and
shared blocked prefixes for geometry, while keeping point/bulk results identical.
The prefix tree is transient and discarded after a rebuild. Its worst-case cost
is higher than the uniform flat path; complex partially open volumes still need
benchmarking when assessing the complete frame budget.

## Cache and observation lifetimes

The narrow helpers in `perception.h` also expose `VisibilityMask::Contains` for
bounded lookup, `TerrainWithinVision` for illumination/range filtering without a
geometry query, and `VisionRange` for the common target-band/glare policy.
`VisibilityMask::Get` uses bit 0 for visibility and bit 1 for an opaque endpoint.
Certified `clearLayers` are implicit spherical slabs: `cells` may be empty even
when many tiles are visible. `Contains`, `Get`, and integer `ClearRow` account for
this representation. `MaterializeLayer` expands only a layer whose light filter
actually needs to remove cells; other clear layers stay implicit.
`visibleLayers` is a conservative nonempty-layer accelerator; filtering can leave
it true for a now-empty layer. `Index` requires an in-bounds coordinate. These
small value helpers let World filter chunk-local arrays without repeating catalog
lookups or maintaining a separate vision policy.

`OpticalField::Rebuild` owns copied chunk flags, optical row masks, and a new
revision. `VisibilityWorkspace::Prepare` returns whether it rebuilt a copied,
chunk-aligned optical volume and summed-volume tables. A workspace is reused
while its field identity/revision and bounding chunk rectangle match. The
supplied field must outlive the workspace and change through `Rebuild`. Passing
no field to `BuildVisibilityMask` constructs temporary optics and ignores any
supplied reusable workspace, preventing accidental reuse of a temporary identity.
Flat certificates allocate no working volume. A radius-64 fallback working
volume is approximately 4 MiB per observer (9x9 single-layer chunks, full height),
retained for reuse rather than recreated on each tile of movement. Geometry
mutations and chunk-window changes rebuild it; point rays remain independent.

World owns geometry, light, appearance, and presentation generations. Tile edits
only invalidate geometry when optical properties change; terrain appearance-only
edits invalidate observation data. Actor movement updates overlays and relocates
that observer; a nonemitting NPC does not invalidate other observers' geometry.
Emitter movement (including gravity through `MoveActor`), parking, enabled-source
changes, sky changes, and residency invalidate derived light. A light rebuild
refilters cached geometry without recasting it. Terrain filtering uses
`TerrainLightLevel(world, observer, target)` for opaque endpoints; transmitting
tiles use the identical space-light result directly. Actor exposure continues to
use `GetLightLevel`.

World snapshots chunk-contiguous space-light levels through
`LightingField::GetChunkLevels` once per lighting generation. Ordinary-or-brighter
transparent chunks need no per-tile filtering: their geometric sphere is already
the exact vision result. Dark/mixed chunks and opaque endpoints retain the full
target-band/glare and observer-facing surface policy.

Each observer retains geometry, filtered visibility, and the visibility last
learned at an observation boundary. Const projections may refresh the first two
without overwriting the last-learned footprint. An unchanged boundary returns before
memory scanning. Newly perceived cells and visible cells in appearance-changed
chunks are compared against owned memory, and only changed values are written.
Footprints use four 64-bit words per chunk. Clear spherical rows construct their
bits directly; filtered/dense tiles produce bits during filtering. At observation
boundaries, sorted chunk footprints are merged and bitwise-differenced. Only set
bits in the new/appearance-changed footprint visit memory. A few dark cells no
longer force a full dense-sphere memory scan. The last-learned footprint is only
32 bytes of bits plus its coordinate per chunk, rather than another voxel mask.
Chunk and sparse object-memory lookups are reused within a chunk. Hidden edits
are not learned until a later visible boundary. Opening and closing a door in
one tick still produces two independent observations if both boundaries run.
Observer cache entries are removed on player destruction; parking/unloading
preserve owned memory and do not teach it.

Chunk requests retain a dirty-demand bit and the last successful effective
demand. Identical normalized requests return cheaply. Changes whose merged
demand is identical skip archive validation/materialization as well. New demand
still stages and validates incoming archives before changing live state. A
failure leaves demand dirty, allowing a retry; administrative requests and
activity changes participate in the same path.

## Work counters and verification

`PerceptionStats` is returned by value and contains `geometryRebuilds`,
`lightingRebuilds`, `pointLosCalls`, `traversedVoxels`, `observationScans`,
`memoryTilesChanged`, and `demandReconciliations`. Counters do not affect outcomes.
Additional work counters are `opticalRebuilds`, `volumeRebuilds`, `filterRebuilds`,
`filterTilesSampled`, and `memoryTilesExamined` (actual candidate-memory visits,
not the population of a bounding sphere).
Resetting them does not flush caches or revisions. `traversedVoxels` counts point
ray cursor visits and newly resolved bulk ray-event touched voxels, not snapshot,
slab, or summed-volume preprocessing. Point counters cover calls through
`LineVisible`; lighting's private source traversal is separate.
`demandReconciliations` counts effective-demand work, not
unchanged calls.

`tests/bulk_perception_tests.cpp` is registered in `cataclysmx_tests`. The `[bulk]`
cases compare every candidate against `LineVisible` in deterministic sparse
multilayer scenes, exercise corners/floors/stairs/bounds/missing residency,
compare filtered caches to `CanSeeTile`, and check invalidation/memory reuse.
They include a full horizontal radius-64 differential and spherical boundary
check. `[bulk-benchmark]` is hidden from ordinary runs and reports geometry and
movement-plus-observation median/p95 (nine samples, p95 is the maximum sample),
separately at ranges 12 and 64. Run explicitly, for example:

```powershell
./out/build/win-msvc-debug/cataclysmx_tests.exe '[bulk]'
./out/build/win-msvc-debug/cataclysmx_tests.exe '[bulk-benchmark]'
./out/build/win-msvc-debug/cataclysmx_tests.exe '[bulk-server-benchmark]'
```

Timing is entirely outside simulation. The benchmark uses seed 42, a resident
radius-4 full-height test region, cleared floor at Y=0, one nonemitting player,
and warm light. Geometry and cached filtering are timed separately. The World
movement fixture deliberately retains generated terrain just beyond its cleared
64-square boundary; the first move can require a fallback-volume preparation.

`[bulk-server-benchmark]` uses the actual Server movement/observation/projection
path and the integrated default resident radius 5. It clears a 76-square floor
and overhead air, removes the unrelated wandering NPC, and moves within the
same chunk with both footprints warmed. It times `Send(move)` followed by
`View(8)`, for 41 moves at each sight range; the viewport stays fixed. Reported
p95/p99 are sorted samples 38/40 (zero-based). These measurements exclude terminal
rendering and first-time streaming.

### Prior isolated measurements, before reusable optics/implicit masks

The following runs linked the current engine into a separate executable under
the approved temporary directory, avoiding shared Debug test-executable races.
Both configurations used the same fixture and nine-sample procedure above.

| Work | Range | Debug median / p95 ms | Release median / p95 ms |
| --- | ---: | ---: | ---: |
| Flat geometry | 12 | 0.306 / 0.330 | 0.064 / 0.072 |
| Flat geometry | 64 | 6.606 / 6.746 | 1.119 / 1.200 |
| Warm move + observation | 12 | 0.765 / 0.814 | 0.254 / 0.269 |
| Warm move + observation | 64 | 18.668 / 53.496 | 7.083 / 15.496 |
| Wall/stairs geometry | 12 | 1.988 / 2.017 | 0.504 / 0.560 |
| Wall/stairs geometry | 64 | 65.486 / 66.043 | 15.081 / 15.209 |

Wall/stairs adds a three-tile-high wall at X=6, Z=-4..4 and a matched stair
pair at (-1,-1), Y=-1/0. Flat certificates traverse zero ray-event voxels.
Wall/stairs traverses 3,527 events at range 12 and 24,707 at range 64 per rebuild.
Before remaining-corridor certification, range-64 wall/stairs resolved 1,258,548
voxels and cost about 152 ms Debug / 45.5 ms Release. The initial prefix-only
prototype cost about 147 ms Release even on flat sky; slab and volume certificates
are therefore essential parts of the algorithm, not optional refinements.

Each warm movement sequence made eight real moves and one same-position call:
eight geometry rebuilds, zero lighting rebuilds, and 223/1,399 changed memory tiles
at range 12/64. Later back-and-forth movement only compares newly visible tiles;
it does not rewrite the entire visible sphere. Those results **did not meet** the
whole-path 16 ms Debug goal at range 64, especially beside partial multi-layer
occluders. Projection/rendering/streaming costs are additional; the integration
benchmark must retain and report this limitation rather than infer frame latency
from the fast flat geometry number.

### Optimized isolated MSVC v143 measurements (2026-11-09)

Built the registered test target from current sources in the approved temporary
directory (`bulk-opt-build`), using MSVC 14.44 and the existing v143 packages.
No shared `out/build` target was built. The isolated configuration disables the
terminal frontend; all engine code still uses normal Debug/Release flags and
disabled exceptions. No profiling clock runs inside simulation.

| Work | Range | Debug median / p95 ms | Release median / p95 ms |
| --- | ---: | ---: | ---: |
| Cached flat geometry | 12 | 0.016 / 0.021 | 0.002 / 0.005 |
| Cached flat geometry | 64 | 0.284 / 0.298 | 0.026 / 0.080 |
| Cached filter only, no geometry recast | 12 | 0.019 / 0.020 | 0.006 / 0.006 |
| Cached filter only, no geometry recast | 64 | 0.254 / 0.263 | 0.133 / 0.135 |
| World move + observation, mixed edge fixture | 12 | 0.064 / 0.084 | 0.012 / 0.021 |
| World move + observation, mixed edge fixture | 64 | 0.762 / 20.553 | 0.267 / 5.691 |
| Cached wall/stairs geometry | 12 | 0.773 / 0.990 | 0.138 / 0.306 |
| Cached wall/stairs geometry | 64 | 31.491 / 34.553 | 7.111 / 9.293 |
| Warm Server movement + View(8) | 12 | 0.888 / 0.944 | 0.120 / 0.158 |
| Warm Server movement + View(8) | 64 | **1.526 / 1.560** | **0.366 / 0.385** |

Server-path p99 was 1.373/1.571 ms Debug and 0.168/0.412 ms Release at ranges
12/64. All 41 real moves rebuilt geometry but performed zero optical snapshots,
zero lighting rebuilds, zero demand reconciliations, and zero tile-light samples
for filtering. Memory examined 9,143/57,359 newly entering footprint cells total
(223/1,399 per move), rather than scanning the entire visible sky every boundary.
Thus routine loaded empty movement plus a real projected view meets the 16 ms
Debug target with substantial headroom.

In the mixed-edge World fixture, radius-64 memory comparison fell from roughly
1.47 million examined tiles across eight moves before footprint bitsets to just
11,192. Median memory time was 0.153 ms Debug / 0.034 ms Release; geometry plus
filtering was 0.603 / 0.230 ms. Its first new-position fallback-volume preparation
still produces a separate approximately 20 ms Debug spike. Wall/stairs still
requires exact ray-event prefixes (24,707 touched voxels); reusing its optical
volume roughly halves the previous 65.5 ms Debug median, but complex occlusion
remains above 16 ms. This improvement does not claim that first-time streaming,
new optical volumes, moving lights, or every building scene meet the flat-path
budget.

Validation in that isolated tree:

- Debug registered target build succeeded, with ordinary Debug compiler flags.
- `Debug/cataclysmx_tests.exe '~[.]'`: **383 cases / 2,382,647 assertions passed**
  (the nonterminal Catch suite; this is not the coordinator's full frontend and
  headless CTest run).
- `Release/cataclysmx_tests.exe '[bulk],[lighting],[memory],[perception]'`:
  **59 cases / 536,184 assertions passed**.
- Explicit benchmark tags in each configuration: **2 cases / 274,417 assertions
  passed**. Benchmarks contain no hardware-sensitive latency assertions.
- Scoped `git diff --check` passed.
