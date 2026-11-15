# Urban content and Town

The expanded `data/content.json5` pack supplies **31 populated building layouts,
a neighborhood park and a civic square**, plus the three legacy surface-only
patterns `compact_house`, `divided_house`, and `long_house` (36 patterns total).
Generation **5** creates **17 buildings plus a park and square** in each complete
town with this pack, replacing the former eight-building layout. Lots have
one-tile setbacks, paved sidewalks and shared rear alleys. A full-width civic
plot holds a hospital, police station or fire station, selected by relative weight.
Authored gardens, trees, benches, planters and a dry fountain give the outdoor
sites a distinct purpose. Wilderness outside the town retains its biome terrain.

Selection and population depend on seed, coordinates, generation version and
content; a particular town need not contain every building type. Existing saves
retain their recorded generation algorithm. Start a new Town to see the new lot
layout; generation-5 FirstHouse review positions also differ from generation 4.

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
[first-house review](first_house_review.md) for current seed positions. Both packs reference shared
definition files without duplicating definitions. Pack selection and explicit
overrides are described in [content packs](content_pack.md).

## Building roster

| Group | Populated types |
| --- | --- |
| Residential | furnished house, bungalow, duplex, row house, farmhouse, apartment |
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
layout is a separately weighted pattern, so variants share the normal generation
and population system rather than requiring a second random-layout mechanism.

| Layout | Footprint | Occupied above-ground floors |
| --- | --- | ---: |
| Hospital | 40×23 | 3 |
| Police station | 32×21 | 3 |
| Fire station | 34×19 | 2 |
| Courtyard house | 16×15 | 2 |
| Tall row house | 14×13 | 3 |
| Office annex | 20×17 | 2 |

Ground is Y=0; upper occupied floors are Y=1 and, for three-storey buildings,
Y=2. Matching stairs connect them using the existing `>`/`<` controls. Upper
floors contain furniture, supplies and themed inhabitants. Roofs are explicit
layers above the last occupied floor; courtyards and recessed wings remain open
air. Single-storey implicit roofs follow the indoor footprint, and parks/squares
remain entirely open to the sky.

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

Five of the six residential types have basements: furnished house has storage or
workshop; duplex has shared laundry/utility or hobby/pantry; row house has
laundry/pantry or preparedness/workshop; farmhouse has pantry/preserves or farm
workshop; apartment has communal laundry/utility or tenant workshop/storage.
Bungalow has none. Each residential basement type has two equally weighted
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
protocol 15 carries visible type identity; older protocol peers are incompatible.
See [actors](actors_api.md), [save format](save_format.md), and
[networking](networking.md) for the contracts.

## Resources and crafting

Furniture, themed loot pools and items support these sites. Food, clothing,
books and medical supplies are mostly props or crafting inputs: their names do
not add survival, eating, wearing, healing or armor mechanics. Some items are
ordinary storage containers, and cutting tools provide the existing cutting
trait. The urban recipes carve a wooden handle from a plank and salvage wire from
electrical cord using a cutting tool; a handle, wire and scrap make a scrap knife
that itself provides cutting. Cutting up a work shirt yields three clean rags,
and trousers yield four. See [urban resources](urban_resources.md) for exact IDs,
loot pools, tools and furniture and [crafting content](crafting_content.md) for
recipe semantics.

## Validation coverage

Coverage includes all authored floors and basement variants, furniture approaches,
matched stairs, class/size eligibility, all 33 populated site patterns appearing
in a bounded generation sample, rotation and chunk-order independence, specialist
populations and crafting. Live World checks climb and descend all six multi-floor
layouts and unload/reload their upstairs occupants and furniture. Save tests
verify that generation-4 and generation-5 checkpoints retain their planner and
generate missing chunks with the recorded version.

Debug and SDL builds succeeded. The broad Debug run passed 876/877 selected tests;
the remaining debug-menu expectation still named generation 4, was updated
for generation 5, and passed a 4/4 focused rerun alongside the density/roof,
multi-floor streaming and saved-generation tests. The two known baseline failures
(`Server creates the deterministic demo and advances only successful commands`
and `headless_streaming_script`) were excluded. All 34 selected SDL/session/tileset tests passed. Town SDL startup/render
smoke passed for seeds 42 and 13, and FirstHouse smoke passed for seed 13.
Headless Town seed 42 reported intact world invariants, and FirstHouse seed 0
opened its door and descended to the cellar through normal movement commands.
