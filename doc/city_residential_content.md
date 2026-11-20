# Pass 4 residential and outdoor content

`data/worldgen/buildings/city_residential.json5` adds six newer-era patterns.
These use the Pass 4 `era`, `min_settlement_size`, `occupied_floors` and
palette `space` fields; manifest integration and CMake verification belong to the
coordinating implementation.

## Roster and selection

| Pattern | Footprint | Occupied floors | Minimum settlement | Recognizable layout |
| --- | --- | ---: | --- | --- |
| `modular_cottage_row_single_storey` | 31 × 13 | 1 | Small | Three compact homes, individual kitchens/bathing areas, front doors and rear exits onto a shared garden/service strip |
| `modular_cottage_row_two_storey` | 31 × 13 | 2 | Small | Three homes with private stairs, ground-floor kitchens/bathing areas and upstairs bedrooms |
| `courtyard_apartment_block_three_storey` | 25 × 21 | 3 | Small | Nine flats, shared stair gallery and utility cupboards, two wings around a planted open courtyard |
| `courtyard_apartment_block_five_storey` | 25 × 21 | 5 | Large | Fifteen flats using the same legible circulation, with a blue concrete exterior |
| `managed_micro_apartments` | 25 × 21 | 3 | Medium | Eighteen private sleeping pods, shared kitchens/baths/utility rooms, terminal stations, notice boards and access checkpoints |
| `linear_neighborhood_park` | 15 × 31 | Outdoor ground only | Small | Long pedestrian spine, three cross-block links, planted edges and seating bays |

The two cottage variants share `modular_cottage_row`, family weight **1100**,
variant weights **2/3**, residential use and building site. The apartment patterns
and managed variant share `courtyard_apartment_block`, family weight **1400**,
variant weights **5/3/3**, residential use and building site. The managed variant
does not add another family prevalence roll. The park is its own family, weight
**900**, civic use and park site. Eligibility filters precede variant selection;
adding the large-only five-floor pattern does not multiply apartment prevalence.
Three-storey housing remains possible, but not mandatory, in small settlements.

## Routes, space and acquisition

Each cottage has independent front/rear ordinary exits. Apartment blocks have a
north entrance, two southern wing exits and ground-floor courtyard doors; the
managed block instead routes its southern exits down the shared wing corridors.
Private doors and room openings leave beds, cupboards, lights and kitchen
fixtures approachable without moving furniture. Wing dining tables sit beside,
not directly in front of, bedroom partition doors on every apartment floor.
Stairs stay at identical
coordinates through every adjacent occupied floor; final roofs have no stairs.

Courtyards, gardens, service strips and park ground explicitly use
`space:'exterior'`, including trees and outdoor furniture recipes. Upper courtyard
voids and air over setbacks use `space:'none'`. Explicit roofs also use `none`,
not exterior: a supported roof is not eligible street ground. `occupied_floors`
excludes those roofs. The park has no roof layer and no implicit-roof requirement;
all its supported walkable ground is exterior.

Finds are ordinary per-site opportunities: food, kitchen utensils, clothes,
bathing/linen supplies and occasional household tools. Gallery utility cupboards
and managed shared stores provide linen and small maintenance finds, not
industrial stock. Existing dedicated digital pools provide phones
(`loot_digital_residential`) and rare independent legacy laptops
(`loot_digital_hobby`): 25%/1% per ordinary wardrobe, 20%/0.5% per managed pod
wardrobe. These are separate chance rolls, not changes to either pool.
The park offers only two 15%-chance visitor-food finds.

Wall terminals, access checkpoints and service notices are passive setting
objects. They do not implement hacking, powered access control, protected
information, free computing or a working shared service. No general electronics
or computer-parts pool is used as household salvage. Advanced hardware belongs
to specialist/infrastructure destinations, not these homes. No equipment or
building family is guaranteed per settlement; this file adds no printer budget.

## Exact authored population baseline

These are **pre-D4 expected-actor anchors**, not final population and not a
second multiplier. Every residential marker uses `monsters_residential`.
For comparable compact household area, each ordinary occupied home/flat-floor
contributes **0.25**, matching the named bungalow baseline. Three compact units
occupy comparable floor area in the cottage row and apartment layouts.

| Pattern | Authored sum | Small 5× | Medium 6× | Large 7× |
| --- | ---: | ---: | ---: | ---: |
| Cottage, one floor | 3 × 0.25 = **0.75** | 3.75 | 4.50 | 5.25 |
| Cottage, two floors | 3 × 2 × 0.25 = **1.50** | 7.50 | 9.00 | 10.50 |
| Apartments, three floors | 3 × 3 × 0.25 = **2.25** | 11.25 | 13.50 | 15.75 |
| Apartments, five floors | 3 × 5 × 0.25 = **3.75** | Ineligible | Ineligible | 26.25 |
| Managed micro-apartments | 3 × 3 × 0.30 = **2.70** | Ineligible | 16.20 | 18.90 |

The managed baseline is only **20% denser** than the same three-floor apartment
shell: six smaller pods replace three flats while private kitchens/baths move
into shared facilities. Doubling the household count must not double the
comparable-area baseline. Its three anchors per floor identify shared facilities
and sleeping wings; generation multiplies the single site budget by settlement
scale, then redistributes it among legal interior positions. Roofs and courtyard
voids add no markers.

The park's three 10% markers sum to **0.30 outdoors**. They identify visitor
activity within the **single settlement outdoor budget**; they receive neither
the interior multiplier nor an additive park population. Cottage strips and
apartment courtyards likewise contribute eligible exterior area, not additional
interior actors.

## Visual brief and verification status

**SDL artwork: Brief pending; no PNG assets authored.** Functional terrain
glyphs/colors and existing furniture presentation are reused.

- Cottages: rust-red concrete shells, white composite front panels, modular
  party walls, pale paving and dark/white tiled domestic rooms. Show repeated
  narrow frontages and rear garden access rather than a single large house.
- Apartments: sand concrete for three floors, blue concrete for five; modular
  partitions, dark/white tile, terrazzo-lit galleries and brick/pale courtyard
  paving. Preserve the U-shaped skyline, exposed court and repeated flat doors.
- Managed housing: charcoal concrete, graphite composite frontage, rubber-service
  corridors and white shared-facility tile. Emphasize cramped pods, terminals,
  checkpoint passages and operator notices without suggesting implemented locks.
- Park: pale connecting paths, brick seating bays, existing trees/benches and
  visible open links on all four sides. Keep the main spine clear of furniture.

After integration, the already-built `cataclysmx_tests.exe '[city][routes]'
--reporter compact` passed **8,397 assertions in one test case**, including occupied
floor connectivity and equipment approaches. No rebuild, CTest run, PNG generation
or tooling tests were performed by this content task. Broader population placement
and integration verification remain with the coordinating agent's Pass 4 checks.
