# Street-led towns: generation 6 API and implementation contract

## Status and ownership

**Implemented and integrated.** `worldgen.cpp` implements the v6
planner and selective materialization, `world_generation_version` is **6**, and
`tests/worldgen_v6_tests.cpp` supplies focused coverage. Family parsing,
cross-source validation, neighborhood-use preferences and network fingerprint
fields have also landed. The final post-tuning, post-preference Debug run passed
**896/896 selected tests**, with the two documented baseline demo failures excluded.
See [urban content](urban_content.md) for current content and verification details.

### Review findings and implementation deviations

The initial review's test registration/container migration, four-direction scenario
start, and rear-access gaps have been addressed during integration. Service lanes
now include checked one-tile access spurs for shallower parcels. The hypothetical
one-tile-building output reservation has also been removed: vectors grow only as
actual sites are committed.

Nonblocking deviations/improvements:

* Alley emission rejects a whole lane/connection opportunity on collision rather
  than splitting it into surviving rectangles. This is a reasonable conservative
  simplification for this increment, provided accepted lanes have real rear
  access. It can reduce alley frequency; do not document clipping as implemented.
* Primary intervals of width >=12 always reserve a three-tile end gap, even when
  the eventual alley is rejected or fewer than two lots fit. This is safe but
  leaves some unnecessary gaps. Reclaiming them is optional, not a reason to add
  a second packing framework.
* `townBounds` stays the fixed 176-square envelope for complete v6 regions;
  occupied reaches, parcel depths and silhouettes vary inside it. This matches
  the envelope contract below, not a tight occupied-area bounding box.
* Family and variant streams additionally fold the site enum. This is a sensible
  deterministic separation of ordinary/civic draws. Civic selection uses the
  reservation ordinal; emission uses per-strip lot ordinals for yards/owners.

Source inspection found the four-way rotation/placement formulas, road reservation
subtraction, hospital-sized primary opportunity, canonical crossings, clipped-edge
fallback and family-first grouping consistent with the design. Legacy regional
branches retain their selection/geometry math; v5 blanket paving is now explicitly
`==5`. Version 1 remains World's existing path. Integrated regression coverage
and the completed verification run are recorded below.

### Runtime scale tuning

The first bundled-pack probe exposed shallow occupied silhouettes despite varied
road profiles: seeds 0..31 in region (0,0) had 10..13 MainStreet, 11..16 Crossroads
and 12..19 OffsetNeighborhood sites. Primary strips 28..40 tiles deep reserved
most of the frontage along 48..64-tile north/south reaches. Seed 42 Crossroads
occupied only x[61,183), z[106,153), with no side-street buildings.

The correction changes only v6 geometry parameters, not packing or family weights:

* MainStreet retains its previous geometry and 28..40 strip depths.
* Crossroads/OffsetNeighborhood strips are 28..32 deep. This retains the 27-deep
  hospital opportunity while leaving room on perpendicular arms.
* Both larger profiles use independent E/W reaches 64..80 and N/S reaches 72..80.
* OffsetNeighborhood always has two full E/W branches at independent north/south
  distances 60..64, each with independent west/east reaches 60..76. The spine
  always reaches both branches. Some inward strips can still be conservatively
  removed where reserved depths overlap; outer frontage supplies additional sites.

Center coordinates remain 124..132. Maximum primary reach 80 gives centerline
endpoints 44..212; radius-one caps occupy [43,214), inside [40,216). Branch
endpoints and caps also fit. Clipped outer strips remain eligible only where
actual footprints fit. There are still four town streets at most, hence 16 road
segments including connectors; no API/storage increase is needed.

After geometry tuning, before adding neighborhood-use preferences, the same
bundled-pack 32-seed region-(0,0) probe observed:

| Profile | Sample towns | Sites |
| --- | ---: | ---: |
| MainStreet | 8 | 10..13 |
| Crossroads | 11 | 19..23 |
| OffsetNeighborhood | 13 | 25..36 |

All 11 sampled Crossroads had occupied N/E/S/W arms; minimum counts per arm were
4/5/4/5 respectively. That intermediate seed-42 plan had 21 sites occupying
x[53,187), z[61,193). With the final content/use preferences, the 32-seed ranges
are **10–12 / 19–23 / 27–35**, and seed 42 has 22 sites with bounds
x[53,189), z[61,194). These are observations, not quotas or guarantees for
arbitrary catalogs. Shape parameters alone achieved the scale goal without
content-specific IDs, repeated town searches or a new packing algorithm.

Verification rebuilt only the Debug worldgen object and linked temporary probe
and focused test executables against it and existing Debug libraries/objects.
The existing `[worldgen]` cases in `worldgen_tests.cpp` and `worldgen_v6_tests.cpp`
passed: **24 cases, 2,102,106 assertions**. This includes legacy checks and new v6
geometry/access coverage. Integration subsequently rebuilt Debug and SDL and
passed the full 896-test selected Debug suite and 34 selected SDL checks.

## Public records and memory

* `BuildingPattern` appends `String family` and `uint32_t familyWeight = 1`.
  Existing fields through `basements` retain order and meaning. Selection metadata
  lives once per catalog pattern, never in every placed building or tile.
* `Frontage` appends `East` and `West`, preserving `North=0`, `South=1`.
  Convert directions explicitly to clockwise N/E/S/W indices; enum ordinals
  are **not** rotation indices.
* `ParcelPlan` appends `int streetX = 0`. Existing `streetZ` remains the horizontal
  street centerline for north/south-facing entrances; `streetX` is the vertical
  street centerline for east/west-facing entrances. Set the unused component to 0.
* `TownProfile : uint8_t` is `LegacyGrid`, `MainStreet`, `Crossroads`, or
  `OffsetNeighborhood`. `RegionPlan` appends `townProfile = LegacyGrid` and
  `Vector<TileRect> serviceAlleys` after `generationVersion`.
* `RegionPlan::parcels` and `buildings` become `Vector` at their existing field
  positions. Remove `maxRegionParcels` and `maxRegionBuildings`: 24 is a property
  of the old subdivision, not a valid limit for building-sized frontage packing.
  `roads` remains `StaticList<RoadSegment, 18>`; boundaries remain an array of four.

Road storage has a small proof: legacy emits 6 streets + 4*3 connector segments;
v6 emits at most 4 streets + 4*3 connectors = 16. Check every static append.
Alleys are pavement rectangles, not extra through-road segments. Do not put
driveways in `roads`; each `BuildingPlan` already owns its width-one segment.

Parcel/building counts depend on actual authored widths, as small as one tile.
Inflating inline arrays to a worst-case bound initializes and moves many unused
large records, including for empty edge regions. `SmallVector` with a meaningful
town occupancy similarly bloats every plan and moves elements even after spill.
These are bulk, value-owned output buffers returned and moved into World's region
cache: `Vector` provides cheap buffer transfer. Reserve from measured/common
occupancy or the computed strip lengths, not an invented content limit; retain
capacity for scratch reused within a generation call. Do not retain pointers
into a vector while appending. Keep feature references as owner ordinals.

Service alleys need stored geometry because clipping a shared lane against other
streets and reserved lots cannot be reconstructed from a single building's rear
setback. Rectangles suffice: no outdoor-plot hierarchy, per-tile ownership map,
new park entity, or generic street graph. Backyards derive from their parcel;
parks/squares use existing `BuildingSite` and population ownership.

## Content schema and family-first selection

Optional building-pattern JSON fields:

```json5
family: 'row_house',       // Stable family ID, same ID syntax as building IDs.
family_weight: 1200,      // Optional positive integer 1..1,000,000; default 1.
weight: 3,               // Existing field/range; relative variant weight.
```

Exact loader rules, applied across all resolved source files before publication:

1. Missing `family` means an implicit singleton. Store the empty string. An
   explicitly empty, null, or invalid ID is invalid data. A present
   `family_weight` without `family` is invalid data, even if its value is 1.
2. A named family has one resolved `familyWeight`, `BuildingSite`, and `BuildingUse` across
   **all** its members. Missing `family_weight` resolves to 1 independently on
   each member; it does not inherit another member's value. Thus 10 plus omitted
   is inconsistent, while explicit 1 plus omitted is valid. Reject disagreement
   using `ContentError::InvalidData` and optional source-located diagnostics.
   Report the family and conflicting definition. A family cannot span site classes
   or uses. Missing `use` resolves independently to `Mixed`, not to another member's use.
3. Missing metadata keeps the old `weight` as the singleton's effective family
   weight. For named families `familyWeight` sets prevalence and `weight` chooses
   among fitting variants. No runtime optional is necessary: parse-time presence
   checks enforce rule 1; the resolved scalar suffices thereafter.
4. Singleton identities and named-family identities are distinct tagged keys.
   A singleton with ID `row_house` must not accidentally join a named family
   `row_house`. No registration or family-definition section is introduced.
5. Existing dimensions (1..64 on each authored axis), entrance validation,
   palettes, layers, population and basement rules remain unchanged. Valid content
   need not fit any town site; nonfitting templates remain valid but ineligible.

For each available frontage interval, first collect only variants matching its
site class and fitting its remaining width/depth after rotation and setbacks.
Then choose a family **once**, counting its effective family weight once if at
least one member fits. Finally choose among that family's fitting variants by
existing `weight`. Never sum variant weights to obtain family prevalence. Adding
an equally fitting variant does not multiply its family's chance at that choice.
Different footprint sizes can still change subsequent packing opportunities;
this is intentional, not a promise of identical final counts across variants.

Order candidate families by `(kind, key bytes)`, singleton kind first; order
variants by stable pattern ID bytes. Use a local reusable sorted vector of IDs
and borrowed views into the immutable catalog, not per-lot strings or hash-map
iteration. Sum weights in uint64 and use a uint64 rejection-bounded draw. Existing
one-million per-pattern weights and uint32 catalog IDs keep these sums within
uint64; assert overflow rather than silently wrap. This avoids imposing new
UINT32 sum limits on previously valid building catalogs. The existing uint32
population RNG helper need not change.

## Soft neighborhood composition

This follow-up adds only the content API and this contract. Parsing, family-use
validation, selection multipliers, fingerprinting, content annotations and tests
still need implementation. The earlier scale probe and test results predate this
behavior; they do not verify neighborhood composition.

`content.h` adds `BuildingUse : uint8_t { Mixed, Residential, Commercial,
Industrial }` and appends `BuildingPattern::use = BuildingUse::Mixed` after
`familyWeight`. All existing aggregate prefixes remain intact. This is one scalar
per catalog pattern (subject to struct padding), with no per-building/region
metadata, zone graph, registry or cached zoning data. `BuildingSite` remains the
eligibility class; `BuildingUse` is exclusively a soft preference.

### JSON and consistency rules

Optional building-pattern field:

```json5
use: 'residential', // 'mixed', 'residential', 'commercial', or 'industrial'
```

Missing `use` resolves to `Mixed`; explicit strings must match exactly. Reject
null, empty, unknown or non-string values as `ContentError::InvalidData`, with
optional source-located diagnostics. It may appear without `family`. Every member
of a named family must agree on resolved use, in addition to site and family
weight. Explicit residential plus omitted use conflicts; explicit mixed plus
omitted use agrees. Diagnose both conflicting definitions as for family weights.
Allow use on any site class, but apply no bias to Landmark/Park/Square selections.
This avoids a new unnecessary content restriction while keeping civic weights
unchanged. No family-level inheritance or use inference from IDs/materials.

### Slot classification without new plan fields

Classify the available slot once, before selecting a family or variant. Use
existing `FrontageInterval` scratch and its town street `plan.roads[strip.street]`:

* `q = strip.cursor + 1` is the prospective footprint's leading longitudinal
  tile. It does not depend on a candidate's width or authored entrance position.
* `c` is `townCenter.x` for horizontal streets, otherwise `townCenter.z`.
* `lo`/`hi` are the street's minimum/maximum inclusive longitudinal endpoints,
  excluding connector extensions. Current profile streets all span `c`.
* `distance = abs(q - c)` and `reach = q < c ? c - lo : hi - c`.
  Require positive reach. An **outer end** satisfies `4 * distance >= 3 * reach`:
  the last quarter of either independently sized street arm. This is measured
  against street geometry, not each intersection-split frontage interval.
* A **central main-street slot** has `strip.street == 0` and `distance <= 24`.
* A **side-street slot** has `strip.street != 0` (N/S spine or either neighborhood
  branch). Connectors never supply slots. This is not a persistent zone label.

The leading-tile convention introduces a small directional bias for wide lots,
but keeps family probabilities independent of variant sizes and avoids choosing
a candidate to decide the zone used to choose that same candidate. Do not use
pattern-specific midpoint/entrance positions or add RNG draws for classification.
Current differences are <=160 tiles, so the integer comparisons are comfortably
bounded even near world-coordinate limits; subtract coordinates before scaling.

### Family-weight multipliers

Only when `generationVersion == 6` **and** the requested site is
`BuildingSite::Building`, apply this explicit switch to each fitting family's
base weight (named `familyWeight`, implicit singleton `weight`):

| Family use | Condition | Multiplier; otherwise 1x |
| --- | --- | ---: |
| Mixed | Always neutral | 1x |
| Residential | Side-street slot | 3x |
| Commercial | Central main-street slot | 4x |
| Industrial | Outer-end slot | 4x |

These are positive relative preferences, not hard exclusions or population
quotas. An outer side-street slot can favor residential families by 3x and
industrial families by 4x simultaneously. Each family receives **one** multiplier
based on its use, never a product of multiple multipliers. Mixed remains 1x,
even at preferred locations. Requested Landmark/Park/Square classes use 1x for
every family, including fallback park selection during ordinary interval packing.

Represent multipliers using `ratioScale`: 1x=`ratioScale`, 3x=`3 * ratioScale`,
4x=`4 * ratioScale`. These are non-unit multipliers, not probabilities bounded by
`ratioScale`. Compute `effective = uint64(base) * multiplier / ratioScale` with
explicit integer division. All proposed multipliers are integral multiples of
the scale, so division is exact and every positive base stays positive. Do not
introduce fractional penalties, rounding carry or minimum-weight clamps here.

With base weights <=1,000,000 and multiplier <=40,000, the numerator is at most
40,000,000,000 and effective weight at most 4,000,000. At most UINT32_MAX families
(bounded by catalog pattern IDs) give a sum below 2^54. Cast before multiplication,
retain the existing checked uint64 summation and `Bounded64`, and recompute the
same effective weight during cumulative selection subtraction. Never narrow the
total to uint32. Runtime checks should fail fast on broken catalog invariants.

Count each fitting family once, irrespective of variant count. Select variants
within the chosen family using unchanged pattern weights and the existing variant
stream. Keep sorted-family ordering, existing seed domains and number of draws;
do not fold `use` into seeds. All-Mixed catalogs must therefore produce exactly
the pre-preference v6 plans. Existing scale/civic reservation rules remain intact.

### Integration and validation

* Add `use` to the strict parser allowlist and extend cross-source family
  consistency checks. Add the resolved enum to the content fingerprint: peers
  with different preferences must not silently agree on content identity.
* Versions 1..5 ignore use entirely, preserving their output on identical content.
  Keep this change within the unreleased v6 increment; do not change save schema.
* Annotate content by intended use, with all family variants agreeing. Omitted
  metadata stays neutral; it is not an implicit residential classification.
* Test omitted/explicit Mixed equivalence, every accepted spelling, invalid values,
  cross-file family conflicts and source reordering. Verify legacy output ignores use.
* Test all-Mixed v6 output equivalence, boundary slots (center distance 24/25 and
  exact outer-quarter threshold), all orientations, positive eligibility outside
  preferred locations, unbiased civic sites, and equal-fit variant multiplicity.
* Test uint64 totals exceeding UINT32_MAX, and fixed-seed distribution samples
  showing central shops, side-street homes and outer-end industry receive greater
  relative weight. Do not assert every town contains every use or exact site counts.

## Concrete street and frontage planner

All coordinates below are **region-local** before adding the signed region origin.
Use half-open rectangles and inclusive centerline endpoints. Only roads may have
nominal endpoints on/outside the clipped world border. Every emitted parcel,
building and alley rectangle must fit the region's valid `bounds` completely.

### 1. Envelope, profile and streets

For full regions reserve the maximum urban envelope `[40,216) x [40,216)`.
It avoids the inclusive demo square `[-32,32]^2` even in all four origin-adjacent
regions. Choose center x/z independently in `[124,132]`. `townCenter` is the
intersection of the main street and north/south feeder, hence always on a road.
Choose profiles with equal integer weight and choose each size independently:

| Profile | Town streets, all width 3 |
| --- | --- |
| MainStreet | E/W main street: independent west/east reaches 56..72. N/S feeder through the center: independent north/south reaches 12..20. Only the main street receives frontage strips. |
| Crossroads | E/W main with independent west/east reaches 64..80; N/S street with independent north/south reaches 72..80. Both streets receive frontage strips. |
| OffsetNeighborhood | E/W main and N/S street as Crossroads; two E/W branches at center.z minus/plus independent distances 60..64, each with independent west/east reaches 60..76. |

All endpoints stay within the envelope, including road radius. Independent
reaches and asymmetric branch ends produce uneven silhouettes without jittering tiles
or imposing a rectangular grid. Process the main street first, the N/S street
second, then north and south branches. Branches are connected by construction;
no pathfinding graph is required. Keep all generated street records even when
their frontage is empty. A connector crossing a strip still reserves its road
footprint; connector roads do not create additional frontage strips.

### 2. Canonical edges and connectors

Keep `Crossing` and its domain constants **unchanged**: one canonical crossing per
N/E/S/W edge, width 3, approachLength 16, offset 80..176. Route each crossing in
three axis-aligned segments: perpendicular to the edge to staging line 24 (N/W)
or 232 (S/E), along that line to the corresponding town street endpoint's axis,
then inward to that endpoint. N/S target the N/S street ends; E/W target the main
street ends. Staging lines lie outside the urban envelope, so the last segment
extends the selected street, rather than crossing arbitrary occupied lots.
Emit N,E,S,W connectors after town streets. Reserve their full width before lots.

For clipped world-edge regions use the existing v5 nominal road layout and
connectors **without parcels/buildings**, keeping `hasTown=false`,
`townProfile=LegacyGrid`, and alleys empty. This preserves tested inward road
connectivity and empty extreme corners without an extra clipped-town algorithm.
Do not reroll/shrink a town into the remaining sliver. Retain demo masking in
both materialization and `IsRoad`.

### 3. Reserve usable strips and civic space

Create strips on the two permitted sides of each street, north before south for
horizontal streets and west before east for vertical streets. A strip starts at
centerline distance 2, immediately outside the road's radius-one footprint.
Choose desired depth independently per side in 28..40 for MainStreet and 28..32
for Crossroads/OffsetNeighborhood; clip to the envelope.
Shorten the longitudinal start/end independently by 0..6 tiles. Street ends,
clipped depths and reserved intersections create varied town edges.

Use simple interval subtraction: project every intersecting through-road footprint
and earlier reserved strip onto this strip's longitudinal axis and subtract that
whole interval. This conservatively removes an obstructed cross-section rather
than making overlapping L-shaped parcels. Visit remaining intervals in increasing
world x (horizontal) or z (vertical); keep minimum useful width 3 and depth 3.
Process primary strips before branch strips. Store temporary strip records in a
reused vector; they are not a public or persistent API.

Before packing ordinary lots, reserve **one** landmark opportunity on a primary
main-street interval. Guarantee a north/south interval of at least **42 frontage
by 27 depth**: with minimum main reach 56, center-road exclusion and at most six
tiles end trimming this fits; process these strips before secondary streets'
frontages. Select among available qualifying intervals with a separate stream.
This accommodates the existing **40x23 hospital** with one tile on each side,
one front tile and at least three rear tiles. No mandatory small-lot split may
consume that space first. Prefer a larger available depth/width for other fitting
landmarks, up to authored limits. Select the family/pattern against that reserved
space, then trim the lot to actual footprint plus margins. If no landmark fits,
release the reservation for ordinary use; do not fail generation or force an
oversized building. One landmark opportunity per town is the minimum civic policy,
not a requirement that a particular hospital appear in every town.

Reserve one fitting park and one fitting square opportunity on remaining primary
intervals, if present. Use the same family-first selection and actual-footprint
sizing. Fallback is ordinary packing when the class is missing or cannot fit.
Additional short leftovers may become a fitting authored park; otherwise leave
them as grass. Do not label an empty random gap a populated pocket park.

### 4. Pack around the selected building

Rotate the authored entrance side to the parcel's direction (corner preference
remains N,E,S,W). Frontage is the direction **from building toward street**.
Measure the rotated footprint along/across the street, not always world width.
Select only candidates fitting remaining frontage minus two side margins and
available depth minus one front and one minimum rear tile. Commit lot width
`footprintFrontage + 2`: one tile on each side, producing two tiles between
neighboring footprints. Place at longitudinal cursor + 1, never random-center a
small house in a large fixed lot. Advance cursor by the committed lot width.

Set front setback to one tile; choose rear garden depth in 3..8, capped by
available depth after footprint and front setback, with minimum one. Thus each
ordinary parcel depth is footprintDepth + 1 + rearDepth, not the entire strip.
This makes small buildings tightly packed with deliberate grass behind them.
For a reserved hospital the 27-deep envelope guarantees rearDepth >=3.
No fit terminates that interval, without retry loops or an empty building record.
Large/deep non-landmark buildings may use any fitting ordinary interval; never
exclude a family solely because a narrow variant was inspected first.

The width-one driveway runs perpendicularly from entrance to `streetZ` or
`streetX`. It may cross only its own front setback and the adjoining street.
The front one-tile strip is pavement; side margins and rear gardens are grass.
Neither street roads nor another parcel may intersect a building footprint.

For a primary strip with at least two occupied lots, attempt one three-tile-wide
rear service lane immediately beyond the deepest committed parcel. Its
longitudinal span covers the occupied lots and the reserved end gap. The current
implementation rejects the whole opportunity if the lane intersects a road or
either rectangle intersects a parcel or previously accepted alley; it does not
split obstructed lanes. Add a width-three perpendicular pedestrian connection at
an available end gap to the street; reserve that gap before committing the lane.
If no clear connection fits, omit the lane rather than paving isolated space.
Store the resulting nonempty rectangles in `serviceAlleys`. They must fit the
envelope and valid region bounds, never overlap a parcel, and connect through
pavement to a street. Road overlap at the final connection is allowed. This is
shared rear access: one-tile spurs clear the gap from shallower parcel rear
boundaries to the lane. It is not a through road in `IsRoad`.

### 5. Identity and bounds

Reservation/selection can happen early, but assign parcel ordinals only on final
emission in street, side, surviving interval, increasing longitudinal lot order.
Assign building owners to those ordinals. Emit at most one building per parcel;
outdoor authored sites count as buildings for this existing API. Final bounds
checks include sites on every authored floor (all share the same X/Z footprint).
No region-border building clipping is allowed.

There is no arbitrary maximum site count: every committed lot consumes at least
3 longitudinal tiles and at least 3 depth tiles, parcels do not overlap, and
the envelope has finite area. This proves termination and a conservative bound
`floor(176*176/9)=3441` parcels, well within uint32 owner ordinals. It is a proof,
not an inline capacity or target density. Real 10..40-tile buildings yield much
smaller counts. Town size varies with chosen reaches, depths and eligible content.
`townBounds` is the chosen maximum envelope, not a claim that every enclosed tile
is urbanized. Never restore v5's blanket pavement fill inside that rectangle.

## Materialization, architecture and windows

V6 surface order: natural environment; tree-free grass within parcels and alley
rectangles; parcel front sidewalks and explicit service-alley pavement; roads
and driveways; authored pattern tiles. Demo floor masking remains authoritative.
Ground outside these explicit features remains environment, including irregular
gaps between branches. Optional terrain-role fallbacks remain exactly as today.
Use v5's roof rules, authored layers and basement/population path for v6.
Upper floors and final roofs remain explicit, with air in setbacks/courtyards.

Architectural materials are ordinary terrain definitions referenced by explicit
glyph mappings. Distinguish exterior masonry/brick/timber, interior partitions,
and wood/tile/concrete floors as content warrants. Different glyphs distinguish
exterior and interior walls even in the same pattern. A variant may copy rows
with a different concrete palette, sharing its family ID; no runtime material
template, inheritance framework, or palette generator is needed. Existing
`{terrain, object, facing}` glyph entries continue to expand on each authored layer.

Intact glass window: `movement.passable=false`, `blocks_sight=false`.
Curtained window: `movement.passable=false`, `blocks_sight=true`.
Both have normal floor support, no stairs, and no `opens_to`/`closes_to` transition
unless a later feature explicitly adds one. No new movement action, curtain
component or automatic door behavior. Author windows away from required entrances
and routes; validate sight and collision independently. Existing implicit roofs
cover these non-open-space terrain cells. Keep canonical outdoor grass/road/
pavement IDs for outdoor cells or author explicit roofs, since v5's roof heuristic
does not recognize arbitrary new outdoor materials.

## Determinism, version dispatch and implementation checks

Retain existing SplitMix64 `Mix`, `SeedField`, `SeedString` and increment. For v6
planning start at `Mix(seed ^ domain)` and fold signed region x/z as uint64,
then version 6, then the stable street/side/interval/slot ordinals needed by the
operation. Use these separate numeric domains:

| Domain | Value |
| --- | --- |
| Profile, center, reaches and branch geometry | `0x763670726f66696c` |
| Strip depths and end trimming | `0x7636737472697073` |
| Civic opportunity reservation | `0x7636636976696373` |
| Family choice | `0x763666616d696c79` |
| Variant choice | `0x763676617269616e` |
| Rear setback | `0x7636796172647321` |

Geometry never depends on content iteration order. Family draw uses the available
slot key before choosing a pattern. Variant draw additionally folds family kind
and stable family key. Rear setback additionally folds stable pattern ID.
Use the existing feature-seed domains for basement and population with version 6
and finalized parcel identity; give abandonment a separate local domain
`0x76366162616e646f`. Never use runtime handles, global RNG, std::hash, catalog
numeric IDs as seed fields, or chunk visitation order. No retry-based town search.

Dispatch versions 2..5 to the old algorithms, with only container-method migration;
version 1 remains World's legacy generator. New geometry, family selection,
four-way rotation, and selective paving are v6-only. Preserve legacy site filters,
selection draws, ordering, abandonment, roofs and seed folding. Legacy plans have
`townProfile=LegacyGrid`, zero `streetX`, and empty `serviceAlleys`. Do not turn
v5 materialization into v6 by changing an existing `>=5` branch indiscriminately.
The active constant and header accepted range are now 6 and 2..6 respectively.
The existing API and content guides describe these version boundaries.

Generation metadata is separate from save schema; do not bump the save schema
just for plan fields, which are regenerated rather than serialized. Save/version
validators accept 6 while retaining 1..5. Content fingerprints include both family
fields and building use. Source reordering must not change resolved results.
Same-content compatibility does not mean newly edited palettes or added patterns
reproduce old worlds: algorithms 1..5 are preserved **on the same content**.
Adding only family metadata must not alter their terrain or population choices.

Integrated acceptance coverage:

* Loader defaults, explicit invalid fields, cross-file family weight/site conflicts,
  singleton/named-key collision and reversed source order.
* Equal-fit family variants do not multiply family draw weight; only fitting
  variants participate; no-fit catalogs terminate; 1x1 and 64x64 bounds cases.
* All three profiles and all four entrance directions, actual footprint-sized
  lots, hospital eligibility, absent civic classes, no overlaps and region bounds.
* Matching canonical edge crossings, connected roads/driveways, connected alleys,
  demo mask, clipped-edge fallback and no blanket pavement behind small buildings.
* Repeatable plans and forward/reverse chunk materialization/population, including
  authored upper floors, roof setbacks and basements.
* Golden legacy 1..5 output with a frozen catalog; keep old fixed-grid assertions
  on explicit v5 rather than weakening them to fit default v6. Update debug and
  scenario-start tests for variable vectors, all frontage directions and version 6.
* Collision rejects both window types; sight crosses intact glass and is blocked
  by the curtained variant. Existing doors/stairs remain usable.

The source review covered the planner/loader, container guide, and new v6 plus
existing worldgen, house-content and urban-content tests. Final verification used
the `win-msvc-debug` and `win-msvc-sdl` presets. The broad Debug run passed
896/896 selected tests with the two known baseline demo failures excluded;
34/34 selected SDL/session/tileset checks and four scenario startup/render smokes
passed. See [urban content](urban_content.md#validation-coverage) for exclusions
and the final headless walkthrough.
