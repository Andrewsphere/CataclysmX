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
   horizontal displacement. Same-level targets select the horizontal dominant
   observer-minus-target axis and its observer-facing side; equal X/Z
   displacements select both faces. Upward illumination queries retain the old
   3D dominant-axis/tie helper behavior, but perception rejects upward targets.
   Illumination itself does not establish visibility.
3. Sample the immediately adjacent tile outside each selected face. Every sample
   must be resident and transmitting (`!blocksSight`). Vertical samples must
   additionally satisfy `OpenVerticalFace(sample, target)`: the upper cell is
   open space, or both cells are matching stairs. A failure rejects the incident
   contribution for the entire edge/corner tie.
4. Max-combine transported light in those adjacent cells with **direct incident
    light recorded for those same faces**. Each opaque endpoint visible in a
    local source's plane records its distance-attenuated direct level only on its
   source-facing entry face(s), using the same dominant-axis/tie rule. Sky records
   its direct opaque endpoint on the upper (+Y) face. Neither record is a spill
   seed. Max-combine with the terrain's own emission intensity. When the
   observer equals an opaque target there is no incident face, only intrinsic
   terrain emission.

The sample is transmitting **space**, never another opaque endpoint scalar.
Thus daylight 192 makes an exposed facade Bright instead of quarter-strength
Dim. A lamp behind that facade cannot lend its direct endpoint value to the
opposite face. A same-level shallow-angle observer samples the dominant entry
face, and an illumination corner tie cannot select a bright face past a blocked
face. This receiving-face rule remains conservative even where the shared planar
shadowcast permits geometric corner visibility. Target visibility is the caller's
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
Sky retains vertical column traversal from the world ceiling and the same two
spill steps. No new persistent or wire data is required.

## Regression coverage

`tests/lighting_tests.cpp` covers a roofed facade lit beyond dim sight, a sealed
dark interior, opposite-side local light, receiving without relaying, dominant
face selection, conservative tied corners, occluded targets, exposed tops,
closed floors/roofs, matched and unmatched stairs, negative chunk seams,
residency clipping, world bounds, planar direct light, and two-step vertical spill.
Dedicated regressions cover lower walls at underground and elevated levels,
remote projected tops with dark sides and dark observer-plane air, and closed
roofs. Input/output revision tests check unchanged rebuilds, emitter movement,
parking and toggles, conservative sky columns, and sorted unique residency
removals while comparing incremental fields to fresh scalar/incident fields.

## Incremental lighting

`LightingField::Rebuild` scans resident chunk metadata and compares
`World::GetLightingInputRevision(ChunkCoord) const` with each cached revision.
The revision is zero for absent chunks and changes monotonically on optical or
terrain emission edits and emitting actor/object movement, toggles, parking,
creation, or removal. Only changed chunks have their tiles and source lists
rescanned. Cached source descriptors avoid global terrain/source rediscovery
when one lamp moves. No chunk or entity pointers survive rebuilds.

Optical edits invalidate only source patches whose horizontal range-plus-spill
bounds and source-Y +/- two layers intersect an edited optical chunk. Sky
refreshes the edited vertical chunk columns and a one-chunk horizontal halo,
which bounds both spill steps. Direct sky and first-step sky are cached
separately: each refreshed cell gathers from neighboring direct/first-step data,
including unchanged boundary data, before the final max combination. Lamp
movement never rebuilds sky. Residency changes conservatively rebuild everything;
a sky-level change rebuilds sky but reuses local source contributions.

The localized sky boundary is sufficient for both transport passes: direct sky
can change only in edited horizontal chunk columns, with all Y layers refreshed.
The surrounding one-chunk halo is at least 16 tiles wide, larger than the two-tile
spill reach. First-step values are recomputed throughout that halo before the
second pass begins. Neighboring cached direct/first-step values outside the halo
are therefore unchanged and safe to gather at the boundary. Final sky values
are never used as transport seeds.

Sky transport takes advantage of uniform direct intensity: every nonzero direct
sky value equals `skyLight`. Opaque receivers cannot have a connected spill
neighbor. Direct-lit transmitting cells already have maximal illumination, so
the first pass gathers only for dark transmitting cells and stops at the first
connected direct-lit neighbor. All nonzero first steps equal `skyLight / 4`.
The second pass gathers only for still-dark transmitting cells, stopping at its
first connected first-step neighbor; the result is `skyLight / 8`. Integer
thresholds skip a transport pass entirely when its level would be zero, and zero
sky clears selected caches without tracing columns.

First-step cache entries on direct-lit cells deliberately remain zero. Omitting
these seeds is exact: any second hop from such a cell into a connected neighbor
would be at most `skyLight / 8`, already dominated by that cell's own direct
one-hop contribution of `skyLight / 4` to the same neighbor. Likewise, a cell
with any nonzero first step cannot be improved by a second step. All refreshed
first-step caches are cleared before recomputation, including newly direct-lit
cells. Cached entries outside the localized halo use the same reduced invariant
and remain unaffected by the edit; the boundary argument above is unchanged.

The uniform-sky regression independently computes full direct, first and second
transport, deliberately retaining first steps on direct-lit cells. It compares
all final scalar and incident values for zero, sub-spill and threshold intensities,
192 and 255, plus localized roof closure/reopening and fresh-field equivalence.

Sky is retained separately, including its two-step spill and upper-face incident
values. Each local source retains a complete sparse contribution patch, computed
within a horizontal range-plus-two patch and exactly five Y layers, avoiding
radius-cubed work. Sources with unchanged position/emission and optics reuse
their patch. Source movement, removal, toggling, parking or emission changes mark
chunks touched by the old/new patches; only those final chunks are reset to sky
and max-combined with **all** remaining source patches. No contribution is
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
std::uint8_t LightingField::GetIncident(TileCoord position, int face) const;
eastl::span<const std::uint8_t> LightingField::GetChunkLevels(ChunkCoord position) const;
std::uint64_t LightingField::GetChunkRevision(ChunkCoord position) const;
eastl::span<const ChunkCoord> LightingField::ChangedChunks() const;
```

Face order is `+X, -X, +Z, -Z, +Y, -Y`. `GetIncident` returns direct incident light,
zero for an invalid face or nonresident position. World owns lazy cache refresh:
`World::GetIncidentLightLevel(position, face)` forwards to this query after the
same refresh as `GetLightLevel`. `GetChunkLevels` returns the final transported
256-cell row-major chunk field, or an empty span for a nonresident chunk. The
borrowed span expires at the next `Rebuild`; it owns nothing and acquires no
residency. This lets bulk perception consume contiguous light without per-tile
chunk searches.

`GetChunkRevision` returns zero for absent chunks. Every field has a monotonic
nonzero rebuild generation; after final recombination, dirty resident chunks
receive that generation. Unchanged chunks retain their revision, including on a
rebuild with no dirty chunks. Generation exhaustion fails fast rather than
reusing zero or an older generation. These are derived output revisions, separate
from World's input revisions: a remote lamp can change a chunk's lighting without
changing its terrain or emitter inputs.

`ChangedChunks` borrows the last rebuild's sorted unique dirty coordinates, ordered
by X, then Z, then Y; the span expires at the next rebuild. A residency rebuild
reports the union of old and loaded coordinates, including removed chunks whose
current revision is zero. Other rebuilds report precisely the existing conservative
dirty set, which may include recomputed chunks whose resulting bytes are equal.
The list is empty before the first rebuild and after a rebuild with no dirty chunks.

World can retain residency-sized light sample storage, lazily refreshing samples
when `GetChunkRevision` differs from their cached output revision. It can invalidate
an observer's lighting revision only when `ChangedChunks` intersects that cached
volume's horizontal footprint and Y layers. These two APIs expose finalized cache
invalidation without coupling the field to observer caches or sample ownership.

### Historical validation before the planar model (2026-11-09)

The measurements below describe the previous spherical/ray implementation only.
They are not validation or performance claims for the planar implementation.
The planar changes have not been built or tested by the lighting owner: shared
target builds and integration validation are reserved for the coordinator.

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
