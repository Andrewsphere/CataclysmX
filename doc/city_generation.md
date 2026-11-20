# Continuous cities: generation 8

**Status: implemented; functional integration verified (see checks below).** This is the technical
companion to the accepted [city gameplay contract](city_identity_design.md).
Pass 4 replaces the old regional town generator. New worlds are required.

## Ownership and API

`worldgen/worldgen.h` retains pure planning/materialization functions. Inputs are
the immutable catalog, seed, version and coordinates; there is no World RNG,
entity ownership, runtime handle, persistent-ID allocation or global cache.
`worldgen/generation_settings.h` defines generation version 8 and new-world
population/start policy.

- `GeneratePlanningPatch(seed, region)` describes the containing 1536×1536 planning
  patch. Its canonical origin is expressed in 256-tile region coordinates, with
  both components divisible by six, including negative coordinates.
- `SettlementId` is the full `(patch origin, ordinal)` tuple. `FeatureOwner`
  adds a local parcel ordinal. Hashes seed randomness; they are not unique IDs.
- `GenerateSettlementPlan(content, seed, descriptor)` prepares one complete city's
  roads, parcels, selected buildings, access lanes and initial population.
- `GenerateRegionPlan(content, seed, region)` provides a disposable spatial slice.
  Buildings keep their complete bounds when crossing regions; population belongs
  to the region containing its actual position.
- `GenerateChunk` and `GenerateChunkPlacements` crop immutable plans to a chunk.
  Placements clear caller-owned output vectors while retaining capacity.
  `PopulationMode::Urban` uses city budgets; `Authored` uses the original marker
  probabilities for curated scenarios.
- `GenerateBuildingPower` emits complete building extents and the same surface
  origin from every intersecting chunk/floor. Buildings remain separate grids.

The explicit-version planning overload accepts only version 8. Region coordinates
still use mathematical floor division; world bounds and X/Z/Y conventions are
unchanged. Regions are query/storage units, not settlement boundaries.

Plans own bulk buffers with `Vector`, allowing cheap ownership transfer. They do
not initialize a maximum city's storage in every record. The old eighteen-road
bound is removed. Classification lives in immutable catalog tiles and plans, not
on every actor, object or item. World owns a disposable eight-entry regional-plan
cache; callers finish borrowed-plan use before another cache insertion.

## Planning and population

Planning patches use a constructive ten-settlement arrangement: six small,
three medium and one large, with independently selected older/mixed/newer identity.
The packing rows contain large+three small, three medium, and three small;
permutations, dimensions and orientation vary by seed. This avoids rejecting
large candidates more often than small ones. Finite-world clipping is excluded
from distribution measurements.

Detailed cities use connected streets and fitting family-first building selection.
District and construction identity belong to the full settlement. A region seam
must not restart their selection. Countryside connections are distinct from
developed street/yard coverage.

Interior marker chances define one baseline expected budget. City size applies
5×/6×/7× once, then actors are placed on legal unique inhabited tiles. Roofs and
explicit exterior spaces are excluded. The selected basement counts once.
Prepared outdoor population uses a single budget of eligible developed ground
times 50/150/300 divided by `ratioScale`. Civic-site outdoor markers belong to that
budget. Start exclusion removes initial hostiles without redistributing them.

Budgets, population counts and performance are measured separately from their
accepted targets. The named pre-D4 baseline remains in the gameplay document.

## Catalog fields

`building_pattern` adds:

| Field | Values / default |
| --- | --- |
| `era` | `older` (default), `newer` |
| `min_settlement_size` | `small` (default), `medium`, `large` |
| `occupied_floors` | 1–10, default 1; ground counts, roof/basement do not |
| `use` | Existing values plus `civic` |

Object-valued palette entries accept `space:'interior'|'exterior'|'none'`.
The default is `interior`, including shorthand terrain-string entries.
Classification expands alongside terrain into `BuildingPattern::spaces` and
`PatternLayer::spaces`, including basement variants. It avoids inferring whether
a courtyard is indoors from a particular terrain ID or color. Terrain collision,
floor support, stairs and furniture still determine placement legality.

Family variants share use, site and family weight, but may differ in construction
era, minimum scale and occupied floors. The network content fingerprint includes
all these fields and expanded classifications.

## Scenario policy and persistence

`Server(content, seed, scenario, optionalSafetyRadius)` selects the scenario
default unless overridden. `DefaultStartSafetyRadius` returns 16 for Town and 0
for other scenarios. Town uses urban population; FirstHouse retains authored
population, starter equipment and its optional printer acquisition.

`World::ConfigureGeneration(GenerationSettings)` is a new-world operation, called
before requests/materialization. Settings contain population mode, original start
coordinate and safety radius. `GetGenerationSettings` exposes a const borrow.
They are captured with the World snapshot in save format 18.

An initial hostile is excluded exactly when radius is nonzero, it is on the
starting Y layer, and `dx² + dz² <= radius²`. The allowed range is 0–64. Walls,
paths, lighting and LOS have no role. Upper and lower floors are unaffected.
The stored initial policy also applies to new chunks first visited later; loaded
snapshots, walking monsters, replacement characters and LAN arrivals are never
cleared. Empty snapshots remain authoritative and cannot regenerate supplies.

All three executables accept:

```text
--scenario town --start-safety-radius 16
```

Zero disables exclusion. The option applies only to new games and is rejected
with `--load`. Saves carrying an older generation version are explicitly
unsupported rather than mixing old terrain with new unexplored generation.

## Presentation and artwork

F11's generated-plan details show settlement size/era, canonical identity,
intersecting sites, selected district/construction era and initial region
population before scenario exclusion. These are development diagnostics, not
player knowledge about unseen inhabitants.

Terrain and passive infrastructure have readable terminal presentation.
[SDL artwork briefs](city_artwork.md) remain a separate effort. Place rosters are
documented with [residential](city_residential_content.md),
[civic](city_civic_content.md) and [industrial](city_industry_content.md) content.

## Verification

The prescribed seed/region panel, nine named size/era examples, distributions,
site counts, spacing, population totals and live two-player/offscreen-work
observations are recorded in [the city probe](city_probe.md#pass-4-reference-measurements).

Debug and SDL Debug builds succeed. Coverage includes parser/fingerprint
validation, all new occupied-floor/equipment routes, forty used terrain identities,
printer acquisition budgets, exact indoor scaling and outdoor density, unique
legal placements, cross-region ownership and visitation order, complete building
power, diagonal/circular same-floor safety, delayed first materialization,
authoritative cleared saves, and join/reconnect without population clearing.
All three startup parsers rejected -1, 65, noninteger values, and overrides with
`--load`. SDL/terminal rendering, client save roundtrip, headless LAN and SDL LAN
checks passed using the shipped executables.

The full SDL CTest run covered 1,680 cases: four platform-dependent tests skipped;
three stale/new test-fixture expectations were corrected and rerun separately.
The remaining known failure is the pre-existing `arc_welder` sprite expectation
in `Gen1 item sprites load across scenarios and preserve unmapped fallbacks`
(`tests/sdl_source_tileset_tests.cpp:327,350`), also tracked by the
[power artwork handoff](power_design.md#verification). This pass does not supply
SDL PNG artwork. Expedition balance/playthrough review remains a follow-up;
the measured medium-site outlier and mixed-era construction share are explicit
tuning observations rather than changed targets.
