# Urban content and Town

**Pass 4 / generation 8 supersedes the regional-town behavior below.** The current
[city contract](city_identity_design.md) and [generation API](city_generation.md)
cover small/medium/large settlements, old/mixed/new architecture, district-based
places and population. The expanded pack includes the new residential, civic and
industrial place families and forty terrain variants. New worlds are required.
Town starts on a small settlement's residential edge; use
`--start-safety-radius 0..64` to override its 16-tile same-floor circle.
SDL PNG artwork is a separate effort. Historical generation-6 measurements below
are not evidence for the new generation.

The expanded `data/content.json5` pack supplies **32 populated building layouts,
a neighborhood park and a civic square**, plus the three legacy surface-only
patterns `compact_house`, `divided_house`, and `long_house` (37 patterns total).
Generation **6** creates variable street-led towns: MainStreet, Crossroads, and
OffsetNeighborhood. Lots fit actual building footprints, with one-tile front
setbacks, side margins and rear gardens. Connected shared rear alleys and access
spurs are added where clear. Civic opportunities can hold a hospital, police
station, fire station, park or square when fitting content is available.
Authored gardens, trees, benches, planters and a dry fountain give the outdoor
sites a distinct purpose. Wilderness outside the town retains its biome terrain.

Selection and population depend on seed, coordinates, generation version and
content; a particular town need not contain every building type. Existing saves
retain their recorded generation algorithm for the same catalog. Start a new Town
to see the new layout; changed generation and content also change FirstHouse starts.

Site counts depend on the selected patterns as well as street geometry. With the
bundled pack and neighborhood preferences, seeds 0–31 in region `(0,0)` produced:

| Profile | Observed sites (buildings plus civic outdoor sites) |
| --- | ---: |
| MainStreet | 10–12 |
| Crossroads | 19–23 |
| OffsetNeighborhood | 27–35 |

These are sample ranges, not fixed counts or limits. Seed 42's region `(0,0)` is
a 22-site Crossroads town. The Town start query may choose a nearby region instead;
F11's generated-plan details identify its profile and site count.

## Try it

From the repository root, launch a new Town in the terminal or SDL frontend:

```powershell
./out/build/win-msvc-debug/cataclysmx_game.exe --scenario town --seed 42
./out/build/win-msvc-sdl/cataclysmx_sdl_game.exe --scenario town --seed 42
```

Town uses the expanded pack and starts outside a qualifying populated building.
The start query considers nine nearby region plans and chooses a usable entrance
near the origin; eligibility requires authored population in the building or its
selected basement, not a successful random loot or monster roll. The curated
FirstHouse scenario uses `data/first_house.json5`; see the updated
[first-house review](first_house_review.md) for launch and inspection. Both packs reference shared
definition files without duplicating definitions. Pack selection and explicit
overrides are described in [content packs](content_pack.md).

## Building roster

| Group | Populated types |
| --- | --- |
| Residential | furnished house, bungalow, duplex, row house, farmhouse, apartment, apartment walkup |
| Food and everyday services | diner, restaurant, laundromat |
| Retail | grocery store, convenience store, pharmacy, hardware store, bookstore, clothing store, electronics store, sporting goods store |
| Public and professional | office, clinic, fire station, police station, school, library |
| Industrial | auto repair, warehouse, workshop |
| New shape/height variants | courtyard house, tall row house, office annex, corner store |
| New landmark and outdoor sites | hospital, neighborhood park, civic square |

## Footprints and upper floors

The courtyard house has two wings around an open garden; the office annex and
corner store have stepped/L-shaped footprints. Internal divisions include narrow
stair halls, connected wards, crew dormitories and an attic studio. Each new
layout is a separate pattern. Named families choose prevalence once, then select
among fitting variants by their own weights; adding a variant does not multiply
family prevalence. Bundled families are `detached_home` (2200), `row_home` (1600),
`apartments` (1500), `dining` (1500), `office` (1200), and `convenience` (1200).
Other patterns are weighted singletons. Values are relative weights, not ratios.

### Neighborhood composition

Generation 6 softly favors residential families on side streets (3×), commercial
families on the main street within 24 longitudinal tiles of the town center (4×),
and industrial families in the outer quarter of an actual street arm (4×).
Elsewhere the multiplier is 1×. These are relative weights, not zones or quotas:
every fitting family stays eligible, and landmark, park and square choices are
unbiased. Homes are residential; shops, offices and the clinic are commercial;
auto repair, warehouse and workshop are industrial. School and library remain
mixed. The preference applies once to family prevalence, not to variant weights.
See [world generation API](worldgen_api.md#neighborhood-composition) for the
content field and compatibility rules.

### Occupied floors

| Layout | Footprint | Occupied above-ground floors |
| --- | --- | ---: |
| Hospital | 40×23 | 3 |
| Police station | 32×21 | 3 |
| Fire station | 34×19 | 2 |
| Courtyard house | 16×15 | 2 |
| Tall row house | 14×13 | 3 |
| Office annex | 20×17 | 2 |
| Apartment walkup | 20×18 | 2 |

Ground is Y=0; upper occupied floors are Y=1 and, for three-storey buildings,
Y=2. Matching stairs connect them using the existing `>`/`<` controls. Upper
floors contain furniture, supplies and themed inhabitants. Roofs are explicit
layers above the last occupied floor; courtyards and recessed wings remain open
air. Single-storey implicit roofs follow the indoor footprint, and parks/squares
remain entirely open to the sky.

The walkup has three flats on each occupied floor and an explicit roof. All 33
previously populated sites were reviewed alongside it: homes have enclosed
bathrooms, improved kitchens, dining, lounges and workstations, with purposeful
basements where authored. Commercial and civic interiors also received a detail
pass. New wood, brick, concrete, metal and plaster walls and wood, tile and
concrete floors distinguish rooms and buildings. Clear windows block movement
but admit sight; curtained windows block both. Neither has an open or smash action.

## Character-based furniture

Palette glyphs can place both terrain and an object:

```json5
rows: ['#####', '#B..#', '##+##'],
palette: {
  '#': 'wall', '.': 'floor', '+': 'closed_door',
  B: {terrain: 'floor', object: 'bookcase', facing: 'south'},
},
entrance: {x: 2, z: 2},
```

The same palette works on upper layers and basement variants. Explicit furniture
arrays remain useful for special placements; overlaps are rejected. Loot can
target a glyph-placed container by its coordinate. See [house content](house_content.md)
for expansion order, strict validation and the `site` placement class.

## Basements

Five residential layouts have basements: furnished house has storage or
workshop; duplex has shared laundry/utility or hobby/pantry; row house has
laundry/pantry or preparedness/workshop; farmhouse has pantry/preserves or farm
workshop; apartment has communal laundry/utility or tenant workshop/storage.
Bungalow, courtyard house, tall row house and apartment walkup have none.
Each residential basement type has two equally weighted
variants. Other basements include restaurant bulk pantry, office records archive,
hardware bulk tools, electronics spares, bookstore archive, clinic medical stock,
and workshop parts storage. These seven buildings each have one variant.
Stairs and access are authored per layout; not every building has a basement.

## Inhabitants and identity

Weighted groups mix ordinary cellar zombies with specialists suited to their
locations, such as firefighters at fire stations and nurses and doctors in medical
sites. All use the existing sighted-zombie AI, simple body, and undead faction.
Their timing and attack damage vary; lower speed values act faster. Current
specialists share 9000 accuracy and the same sighted AI profile.

| Zombie | Speed | Damage | Accuracy (out of 10000) |
| --- | ---: | ---: | ---: |
| firefighter | 120 | 16 | 9000 |
| nurse | 90 | 8 | 9000 |
| doctor | 100 | 10 | 9000 |
| police | 100 | 14 | 9000 |
| mechanic | 105 | 13 | 9000 |
| construction worker | 110 | 15 | 9000 |
| cook | 100 | 11 | 9000 |
| office worker | 100 | 10 | 9000 |
| shopper | 100 | 10 | 9000 |
| warehouse worker | 105 | 14 | 9000 |
| outdoors worker | 95 | 12 | 9000 |

Specialists retain generic actor glyphs and sprite art; their names are available
through examination and terminal status for visible actors. Runtime
`MonsterTypeId` is a four-byte catalog-local typed ID, with zero for untyped
actors. Archives and disk saves use the stable definition ID string instead of
the catalog index. The optional `monster_type` field in save format v10 may be
absent in older saves, leaving those actors untyped. Restoration preserves saved
combat state rather than reapplying monster defaults or healing damage. LAN
protocol 16 carries visible type identity and grab state; older protocol peers are incompatible.
See [actors](actors_api.md), [save format](save_format.md), and
[networking](networking.md) for the contracts.

## Resources and crafting

Furniture and themed loot expose the [semantic catalog](content_pack.md#source-organization),
including material stock, three motor sizes, tools, clothing, emergency equipment,
food, medical supplies and workshop equipment. Reading-room and hobby loot include
workshop/salvage references and combat manuals; electronics loot can supply an
ebook reader. See [skill content](skills_content.md) for learning and reference use.
Storage, charging, authored melee, clothing protection/encumbrance and crafting
use their implemented systems. Food/medical classifications and ranged firearm
profiles alone do not supply consumption, healing or firing actions.

Current recipes cut cloth bandages and wood scraps, forge the pilot sword from
suitable continuous steel, and assemble a coil spear. W3A/W3B adds the
[forging and electrical workshop ladder](workshop_rosters.md), fixed workshop
anvil/forge stations, and specialist ground-stock loot. Clothing, sheet steel,
appliance motors and pumps have useful recycling returns. Both sword IDs now
require Fabrication 4 and the same workshop capabilities; other original
urban/pilot recipes retain their explicitly obsolete dependency items. See
[integration and acquisition](item_slice_integration.md),
[urban resources](urban_resources.md) and [crafting content](crafting_content.md).

## Validation coverage

Coverage includes all authored floors and basement variants, furniture approaches,
seven live multi-storey stair/streaming checks, four-way starts and frontage,
connected rear access over tree-bearing terrain, source/chunk order independence,
family multiplicity and neighborhood preferences, window sight/collision, and
legacy generation compatibility.

The final Debug run passed **896/896 selected tests**. The two known baseline
failures, `Server creates the deterministic demo and advances only successful commands`
and `headless_streaming_script`, were excluded. Debug and SDL builds succeeded;
**34/34 SDL/session/tileset checks** passed. Final Town SDL startup/render smoke
passed for seeds 0, 13 and 42, and FirstHouse smoke passed for seed 0. The headless
FirstHouse seed-0 walkthrough opened the door and descended normally to
`(-71,-60,-1)`. The v6 cases selected by name also passed **10/10** after removing
an unreachable test-only fallback statement.
