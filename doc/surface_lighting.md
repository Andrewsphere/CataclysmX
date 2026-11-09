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
2. For an opaque target, compute the absolute observer-minus-target displacements
   in X, Z, Y. Select the axis with the largest displacement and the face on the
   observer's side of that axis. This is the final entry face of the existing
   integer center-to-center voxel ray, not every face pointing loosely toward
   the observer. Equal largest displacements select all tied entry faces.
3. Sample the immediately adjacent tile outside each selected face. Every sample
   must be resident and transmitting (`!blocksSight`). Vertical samples must
   additionally satisfy `OpenVerticalFace(sample, target)`: the upper cell is
   open space, or both cells are matching stairs. A failure rejects the incident
   contribution for the entire edge/corner tie.
4. Max-combine transported light in those adjacent cells with **direct incident
   light recorded for those same faces**. Each successful local-source ray to an
   opaque endpoint records its distance-attenuated direct level only on its
   source-facing entry face(s), using the same dominant-axis/tie rule. Sky records
   its direct opaque endpoint on the upper (+Y) face. Neither record is a spill
   seed. Max-combine with the terrain's own emission intensity. When the
   observer equals an opaque target there is no incident face, only intrinsic
   terrain emission.

The sample is transmitting **space**, never another opaque endpoint scalar.
Thus daylight 192 makes an exposed facade Bright instead of quarter-strength
Dim. A lamp behind that facade cannot lend its direct endpoint value to the
opposite face. A shallow-angle observer samples the actual entry face, and a
corner tie cannot select a bright face past a blocked face. Target LOS is the
caller's responsibility, including the full 3D corner stencil.

Do **not** cast an observer ray to the adjacent sample's center. Its boundary
ties can differ from the actual target ray: an observer on a floor can see a
lower wall through adjacent upper air while the ray to the lower sample center
ties against the observer's floor. Nor is the sample center guaranteed to receive
the source ray that hits the face. Retaining face-specific direct endpoints fixes
both failures without borrowing the solid's observer-independent scalar or light
from the opposite face. A hidden target can still have a nonzero illumination
result; geometric visibility remains a separate query.

## Vertical geometry and transport

X/Z are horizontal and Y is elevation. Above an opaque wall, transmitting air
can illuminate the exposed top face. Closed floors block vertical queries;
light above a roof is never borrowed for its underside or interior wall faces.
The existing model does not make an opaque upper cell's closed underside
vertically visible. Transparent floors use their own space light rather than
borrowing from above/below, and matching stairs retain existing transport.

This is a derived receiving-only result: it is never written into the field or
used as a spill seed. The direct spherical source falloff, maximum combining,
sky entry, blockers, source range **12**, and exactly **two** open-neighbor spill
steps remain unchanged. Source/geometry/residency invalidation remains owned by
World. No new persistent or wire data is required.

## Regression coverage

`tests/lighting_tests.cpp` covers a roofed facade lit beyond dim sight, a sealed
dark interior, opposite-side local light, receiving without relaying, dominant
face selection, conservative tied corners, occluded targets, exposed tops,
closed floors/roofs, matched and unmatched stairs, negative chunk seams,
residency clipping, and world bounds. A dedicated regression reproduces the
different sample-center floor tie at underground and elevated levels; the
unchanged mixed-depth network projection test also passes. Existing tests retain
the scalar endpoint and transport assertions.

## Incremental lighting

`LightingField::Rebuild` snapshots resident coordinates, terrain identities,
packed optical flags and source position/emission values. It holds no chunk or
entity pointers across rebuilds. Unchanged terrain reuses cached flags/emission;
the resident scan detects all optical edits without requiring new World revision
APIs. Residency or optical changes rebuild sky and every source's full shadow
footprint. A sky-level change rebuilds sky but reuses unchanged local sources.

Sky is retained separately, including its two-step spill and upper-face incident
values. Each local source retains a complete sparse contribution patch, computed
within range + two spill cells. Sources with unchanged position/emission reuse
their patch. Source movement, removal, toggling, parking or emission changes mark
chunks touched by the old/new patches; only those final chunks are reset to sky
and max-combined with **all** remaining source patches. No contribution is
subtracted from a maximum. Duplicate equal sources are valid and deterministic.

This decomposition is exact: max distributes through each integer division and
each connected-neighbor maximum. Computing direct/4 and direct/8 independently
per source, then max-combining, gives the same result as combining direct first.
First-step seeds and final values remain separate, so there is no recursive spill.
The source-neighborhood LOS uses the same conservative integer voxel supercover
as point `LineVisible`, over packed flags rather than repeated World/content
lookups. A reference regression compares packed direct faces and combined spill
against point LOS and an independently computed shared transport field. Another
compares incremental and fresh fields after overlapping-source removal, movement,
intensity/range changes, geometry, sky, and parking changes.

### Field access APIs

```cpp
std::uint8_t LightingField::GetIncident(TileCoord position, int face) const;
eastl::span<const std::uint8_t> LightingField::GetChunkLevels(ChunkCoord position) const;
```

Face order is `+X, -X, +Z, -Z, +Y, -Y`. `GetIncident` returns direct incident light,
zero for an invalid face or nonresident position. World owns lazy cache refresh:
`World::GetIncidentLightLevel(position, face)` forwards to this query after the
same refresh as `GetLightLevel`. `GetChunkLevels` returns the final transported
256-cell row-major chunk field, or an empty span for a nonresident chunk. The
borrowed span expires at the next `Rebuild`; it owns nothing and acquires no
residency. This lets bulk perception consume contiguous light without per-tile
chunk searches.

### Validation (2026-11-09)

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
