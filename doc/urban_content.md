# Urban content and Town

The expanded `data/content.json5` pack supplies **26 populated building types**
and the three legacy surface-only patterns `compact_house`, `divided_house`, and
`long_house`. Each populated type has authored furniture and loot; themed monster
markers and basement variants appear where authored. The existing region planner
chooses eligible patterns by relative weight for its eight parcels. This adds
variety to that planner, rather than introducing a large-city generator: a single
region cannot contain 20 distinct buildings, and no town is guaranteed to contain
20 types. Selection and population depend on seed, coordinates, generation version,
and content; chance-based markers need not produce an item or monster every time.

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
FirstHouse scenario uses `data/first_house.json5`, retaining the existing review
seeds; see [first-house review](first_house_review.md). Both packs reference shared
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

The seven urban content tests are designed to validate the expanded catalog and
populations, the furnished-house divider, cardinal connectivity and basement stair
access, parcel eligibility and sampled variety, deterministic rotated placement,
specialist matching at sites, and crafting a usable cutting tool. These checks
sample generation rather than guaranteeing a particular town's composition.

Debug and SDL builds succeeded. The broad Debug run passed 869 of 871 selected
tests; its two integration failures were fixed and passed a four-test focused
rerun. The two previously documented demo/diagonal-AI failures were excluded.
All 42 selected SDL/session/monster tests passed. Town SDL startup/render smoke
passed for seeds 42 and 13, and the curated FirstHouse smoke passed for seed 13.
A headless Town walkthrough also entered a store and displayed the visible
shopper zombie's name with world invariants intact.
