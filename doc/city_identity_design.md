# D4: settlement scale, identity, danger and rewards

**Status: Pass 4 implemented and functionally verified; expedition balance and SDL artwork follow separately.**

This contract develops the accepted city goals in the
[urban workshop plan](urban_workshop_progression.md#1-accepted-goals).
It follows the accepted [power](power_design.md),
[workshop](workshop_design.md) and [home-printing](home_printing_design.md)
contracts. The user accepted the scale, population, reward and visual package,
then the centralized-computing setting and dystopian locations below. Dimensions,
distributions and budgets are accepted initial targets; new-world measurements
and playtesting remain pending.

The intended experience is to establish a workshop using smaller settlements,
then make deliberate expeditions into large old-industrial or futuristic cities.
Size, architectural era and district purpose vary independently. Preparation
improves the player's options; city access has no character-level gate.

Content schemas, APIs, planning algorithms, saves, networking and version numbers
belong to implementation. This contract defines the places and gameplay they
must deliver.

### Pass 4 implementation agreement

Pass 4 delivers I5 and the city-specific I6 content together: settlement planning,
scale/era/district identity, population, starts, all thirteen place families and
three variants, forty terrain variants, useful current equipment/loot, diagnostics
and functional verification. The user approved replacing the legacy generator;
new worlds are required and older generation saves are rejected explicitly.
Retaining old generation algorithms or seeded layouts is not a requirement.

Advanced destinations provide useful supported equipment, materials and references.
Advanced production and protected-information interactions retain their later
subsystem scope. Infrastructure furniture can communicate the setting without
claiming unsupported operating capabilities.

Start safety is scenario-configurable from 0 through 64 tiles, with 0 disabling
exclusion. Town defaults to 16; FirstHouse defaults to 0 to preserve its curated
encounters. The user selected a horizontal Euclidean circle on the starting floor:
walls do not affect its radius, other floors are unaffected, and there is no
line-of-sight exclusion. Startup options may override the scenario default.
The initial-generation exclusion survives save/load without deleting subsequently
arriving actors or clearing the area for another player.

SDL PNG artwork is a separate effort. New terrain, objects and any agreed items
use functional terminal/glyph presentation and receive an explicit artwork brief.
Generation measurements, persistence/LAN checks and dense-city performance evidence
remain part of this pass.

## 1. Settlement scale and spacing

Count a **site** as one building footprint or civic outdoor location. An apartment
block is one site, irrespective of its flats and floors. Count ground level as an
occupied floor; roofs, unused air and storage basements do not inflate the
above-ground floor count.

| Scale | Share of settlements | Typical developed envelope, tiles | Sites | Common occupied floors | Intended experience |
| --- | ---: | --- | ---: | --- | --- |
| Small settlement | 60% | 96–144 across | 10–18 | 1–2; occasional 3 | A short main street, homes and a few services; reachable early supplies and retreat routes |
| Medium town | 30% | 192–256 across | 36–64 | 1–3; occasional 4 | Several connected neighborhoods; multiple useful destinations and sustained scavenging |
| Large city | 10% | 384–512 across | 120–200 | 2–5 in the center, 1–3 outside | Repeated expeditions, dense interiors, busy approaches and valuable specialist sites |

Envelope widths describe the longer developed axis; the shorter axis is normally
at least half as long. These are typical footprint/site bands, not required
rectangles or a promise that every generated plan hits both upper bounds.
Preserve irregular edges, usable streets, room for the selected building
footprints and believable outdoor space.
Most large-city housing occupies more floors than small-settlement housing.
Residential districts still contain some lower buildings.

Large cities form one continuous urban place even when they cross existing
256-tile region boundaries. Streets, district identity and population density
must not visibly reset at such boundaries. Implementation chooses the planning
and ownership arrangement.

Initial spacing target: usually **128–384 tiles of countryside between developed
edges**, with occasional near-neighbors separated by 64–128 tiles. Connecting
roads cross meadow/woodland and remain useful travel corridors. Population and
loot apply to actual developed sites, not the entire rectangular envelope or the
full length of every connecting road.

The size shares count settlements, not occupied land or visited buildings. Large
cities consequently account for much more than 10% of urban sites. They are
uncommon destinations, not hidden endgame exceptions.

### Layout priorities

- Small settlements can use one main street with short branches. Medium towns
  need several connected blocks. Large cities need through-routes, side streets,
  an identifiable center and an industrial/service edge.
- Aim for at least two independent street approaches into each medium/large
  settlement and connected public routes between its districts.
- Keep residential rear lanes, yards and service entrances useful. They can
  reduce exposure without being guaranteed safe corridors.
- Important multi-room reward buildings should have a second ordinary ground
  access route where their function permits it. A visible window is not an
  escape route unless the game actually supports traversing it.
- Upper-floor rewards use reachable stairs and clear approaches. The first city
  pass can express futuristic architecture with existing walking/stair movement.

## 2. Architectural identity

Use the same initial era distribution within every size class:

| Identity | Share | Approximate futuristic/redeveloped sites | Character and reward emphasis |
| --- | ---: | ---: | --- |
| Older | 25% | 20% | Brick streetscapes, older housing, workshops, warehouses and retrofitted industry; strong conventional tools and bulk stock |
| Mixed/redeveloped | 45% | 55% | Old residential blocks beside newer retail, apartments, civic buildings and industrial additions |
| Newer/cyberpunk | 30% | 85% | Modular housing, dense mixed-use blocks, broad public forecourts, composite/glazed fronts and clean fabrication facilities |

The site fractions are tuning targets across settlements, not a checkerboard
choice for each wall or room. Group eras into coherent blocks and building
families, with deliberate retrofits at boundaries. Preserve material identity
through a building's entrances, upper floors and service rooms.

This mix puts roughly half of urban sites in visibly newer construction when
size and era are sampled independently. Futuristic construction must appear in
ordinary homes and services as well as rare specialist destinations.

An older city is technologically contemporary with the setting: a brick factory
can contain modern controls and advanced equipment. A new settlement still needs
food shops, laundry, repair space and ordinary hand tools. Era changes the
distribution of opportunities, not whether a settlement is useful.

### Society-wide centralized computing

Personal general-purpose computing is obsolete throughout contemporary society,
not only in newly constructed districts. Homes rely on terminals and personal
access devices connected to shared computing infrastructure. Residents do not
own the machines running their services; housing, employment, purchases and
service access depend on the operator.

Standalone laptops and computers survive as old salvage, valuable for their
independence. An old building need not contain old computing equipment: architectural
era and computing capability remain distinct. Ordinary households contain terminals,
consumer electronics and occasional legacy machines, not powerful computing hardware.
Such hardware belongs to dedicated infrastructure or specialist workplaces.

A neighborhood compute house is house-sized **service infrastructure**, not a
household or a residential-loot exception. Machine controllers still perform their
supported local jobs, and compatible devices retain D3's ordinary information
storage/transfer roles. The setting does not make every local file operation
depend on a newly implemented network service.

Newer/cyberpunk districts should communicate this dependence through visitable
places and everyday details: cramped private rooms, extensive waiting areas,
staff-only conveniences, equipment cages and service-suspension notices. Their
physical layouts and useful salvage belong to D4; interacting with protected
systems follows the later hacking design.

## 3. Districts and building roster

Districts organize adjacent places by use. They need not be equal-sized or have
hard visible borders.

| District | Typical places | Route character | Main practical rewards |
| --- | --- | --- | --- |
| Residential | Houses, flats, courtyards, corner services | Broken sightlines, yards and smaller groups | Everyday supplies, clothing, books, household tools and hobby equipment |
| Commercial | Shops, markets, offices, electronics service | Exposed frontages, shoppers, connecting side streets | Concentrated supplies, tools, electronics and prepared printer feedstock |
| Industrial/service | Workshops, warehouses, foundries, fabrication | Loading yards, long rooms, worker groups | Forging/welding equipment, motors, metal stock and production equipment |
| Civic/technical | Schools, libraries, clinics, hospitals, municipal sites | Lobbies, shared halls, stairs and gathering spaces | References, medical supplies, technical equipment and teaching workshops |

Small settlements mix these functions along a few streets; they do not require
four separate districts. Medium towns express two or three clusters. Large cities
include all four, with an industrial/service area reachable from an outer road
without crossing the entire commercial center.

### Accepted new place families

Retain and improve the existing house, shop, workshop, warehouse and civic
roster. Add the following recognizable places rather than recoloring every old
layout:

| Place | Physical identity | Floors | Useful content / delivery dependency |
| --- | --- | ---: | --- |
| Modular cottage row | Compact repeated homes, shared service strip and individual rear exits | 1–2 | Ordinary residential supplies and occasional hobby storage |
| Courtyard apartment block | Several flats around an open court, shared stairs and service rooms | 3–5 | Multiple household searches, common utility/storage space |
| Covered market arcade | Shop units off a walk-through public passage, separate rear loading | 1–2 | Food, clothing and hardware businesses with distinct stock |
| Electronics service center | Display floor, repair benches and parts room | 1–2 | Electrical tools, components, one supplied-design home printer and prepared filament using the accepted retail budget |
| Neighborhood clinic | Public waiting area, treatment rooms and staff store | 1–2 | Existing medical/everyday supplies; medical action extensions follow their own design |
| Retrofitted foundry | Old masonry shell with newer workshop/service annex | 1–2 | Functional forge/anvil, welding/forming opportunities and bulk metal stock |
| Polytechnic school | Teaching rooms, library and practical workshop wing | 2–3 | Existing references, electrical tools and the accepted teaching-printer opportunity |
| Municipal service hub | Public hall, maintenance rooms and records offices | 2–3 | Utility stock, tools and accessible reference material |
| Transit forecourt | Shelter/canopy, pedestrian approaches and neighboring retail | Outdoor | Recognizable gathering place and route junction; transport operation is future work |
| Linear neighborhood park | Paths joining residential blocks, planting and seating | Outdoor | An alternative approach with limited visitor supplies |
| Automated fabrication plant | Production hall, loading yard, control rooms and stock stores | 1–3 | Advanced production machines, compatible stock and later production-software acquisition |
| Clean assembly laboratory | Reception, controlled workrooms, stores and technical offices | 2–3 | Precision electronics, advanced materials and later specialist designs |
| Neighborhood compute house | House-sized infrastructure building with server racks, cooling and backup-power equipment serving nearby homes | 1 | Electrical/computing salvage, wiring, cooling components and backup-power equipment; later information/access opportunities |

The accepted roster is **thirteen place families**, including the compute house,
plus three dystopian variants of families already listed:

| Variant | Parent family | Visible identity | Practical visit |
| --- | --- | --- | --- |
| Managed micro-apartments | Courtyard apartment block | Cramped private rooms, shared facilities, wall terminals and conspicuous access checkpoints | Household supplies, service-room equipment and dense residential encounters |
| Resident services office | Municipal service hub | Housing allocation, work assignments and service disputes handled through terminals and waiting booths | Office electronics and maintenance stores; later administrative information |
| Device reclamation depot | Electronics service center | Returned terminals and appliances sorted for refurbishment or destruction | Concentrated electronic salvage and repair equipment |

These variants use their parent families' population and acquisition roles.
The electronics-service variant retains its accepted printer/feedstock opportunity.
Neither a terminal nor an access checkpoint implies a new usable hacking action.
Computing hardware identities, salvage quantities and usable equipment capabilities
follow their relevant content/feature implementation; site appearance alone grants
no processing capability.

The plant and laboratory are advanced-content destinations. Their signature
machines/materials must have real downstream uses before those rewards are
advertised as playable progression. D4 establishes their place and route in the
world; advanced printing and hacking supply the remaining mechanics. Ordinary
workshops and the accepted home printer already provide useful earlier rewards.

Housing, retail, civic and industrial places all need visible newer layouts.
Artwork and terminal names should make their purpose legible from the approach.
Signs, canopies and structural rhythm can express a futuristic city without
depending on new movement systems or pretending static decoration is a powered
device.

## 4. Interior population: one explicit increase

Use **5× in small settlements, 6× in medium towns and 7× in large cities**
relative to the pre-D4 expected interior population of equivalent buildings and
floors. This preserves the requested 5–7× increase while making settlement scale
matter. Small settlements are quieter primarily because they have fewer sites,
fewer occupied floors and much less street population.

These are expected populations across many examples, not a minimum of five
zombies in every house. Empty rooms and occasional empty homes remain possible.
The final budget includes all interior markers/groups; do not multiply marker
counts, marker chances and settlement size independently into another increase.

### Named baseline

Baseline **pre-D4-v7**, inspected at repository revision
`8926fde99bc434e039805a98b069b38da02826aa`, uses current authored building
population and monster groups. The following are sums of authored probabilities,
not measured spawned actors. The referenced groups select one actor per
successful marker.

| Equivalent building/floors | Baseline expected actors | Small 5× | Medium 6× | Large 7× |
| --- | ---: | ---: | ---: | ---: |
| `bungalow`, ground floor | 0.25 | 1.25 | 1.50 | 1.75 |
| `tall_row_house`, all three occupied floors | 0.85 | 4.25 | 5.10 | 5.95 |
| `workshop`, ground and parts basement | 1.70 | 8.50 | 10.20 | 11.90 |
| `hospital`, all three occupied floors | 5.25 | 26.25 | 31.50 | 36.75 |

Hospital baseline breakdown: ground 2.40, ward floor 1.70, offices 1.15.
Workshop baseline: ground 1.40, basement 0.30. Tall row house: upper floors
0.50 and 0.35; its currently unpopulated ground floor does not force the new
population to remain upstairs.

Distribute a building's final budget according to its spaces: households in
living/sleeping areas, staff in workrooms, gatherings in waiting rooms and halls.
Include basements once. Ordinary empty roofs are not another inhabited floor.
New/larger layouts use comparable use and occupied area to establish their
budget; a five-storey apartment cannot inherit one bungalow's total.

Use existing themed inhabitants: residents/shoppers/office workers in ordinary
streets, mechanics and construction/warehouse workers around industry, medical
staff at clinics, and police/firefighters at their own sites. Settlement size
does not multiply individual hit points, speed or damage. Initial difficulty comes
from numbers, position, sightlines and the existing specialist mix.

## 5. Outdoor population

Initial expected densities, before distributing actors into quiet and busy
patches:

| Scale | Actors per 100 eligible outdoor tiles | Example: 2,000 eligible tiles |
| --- | ---: | ---: |
| Small | 0.5 | 10 |
| Medium | 1.5 | 30 |
| Large | 3.0 | 60 |

Eligible area means supported, walkable exterior ground in actual developed
streets, yards and civic spaces. Buildings, blocked furniture, trees, roofs,
open air and undeveloped countryside do not count. Empty space within the city
envelope is not automatically a street or a spawn zone.

Redistribute this **single outdoor total** toward commercial frontages, hospital
entrances, civic squares and transit forecourts. Make residential fringes, rear
yards and service lanes sparser. Initial groups are usually 1–3 in small
settlements, 2–5 in medium towns and 4–10 around large-city gatherings, with gaps
between groups and some isolated actors.

Authored park/square/yard inhabitants are part of this outdoor total. They do not
receive the interior multiplier or an extra full street budget. Indoor/outdoor
classification follows the actual space: an open apartment courtyard is outdoors.
One actor occupies one legal initial tile.

Use several existing civilian groups for ordinary streets, rather than filling
every road from the worker-heavy `monsters_outdoors` group. Existing specialties
remain tied to believable destinations. D4 does not depend on new enemy abilities.

Generated population is finite. Walking away, crossing a region boundary,
rejoining or saving/reloading does not replenish cleared places. Existing actors
can subsequently move through cleared streets through ordinary simulation.

## 6. Reward progression and acquisition opportunities

Better rewards come from **more useful destinations, richer specialist stores
and advanced capabilities**, rather than a blanket loot multiplier on every
cupboard.

| Scale | Acquisition composition targets |
| --- | --- |
| Small | Everyday food/household supplies, basic hardware/tool sources and repair/hobby-work opportunities; functions can share sites |
| Medium | More conventional workshop and electronics/technical-teaching opportunities; substantial stock in a few themed stores |
| Large | Several conventional workshop/industrial opportunities, electronics and technical-reference access, and multiple specialist expedition destinations once their usable content is available |

**D5 review adjustment:** these are composition targets, not mandatory
per-settlement coverage or progression-loot guarantees. The user deferred
guaranteed generation in favor of sufficient alternative acquisition routes.
Tune actual frequencies and alternatives through playtesting before reconsidering
guarantees. No individual settlement must contain every building type.

Small/medium settlements support the basic-workshop loop; advanced equipment,
software and schematics concentrate in large cities, especially modern ones.
Food/medicine roles use only their actually implemented effects. Small settlements
can lack a printer; the accepted residential and retail finds remain relevant.

### Why visit each kind of large city?

- **Older industrial city:** more conventional workshop/foundry and warehouse
  opportunities; bulk steel/copper, useful motors, forging/forming equipment,
  electrical protection and repair references. Retrofitted factories can contain
  advanced equipment too.
- **Newer city:** more electronics service, technical teaching, precision assembly
  and fabrication opportunities; prepared feedstock, compatible components,
  ordinary digital references and eventually advanced production capability.
  Neighborhood compute houses and dystopian housing/service/reclamation sites
  make centralized infrastructure and dependency tangible destinations.
- **Mixed city:** a practical combination, with visibly different districts and
  alternative routes to workshop progression.

Multiple specialist destinations in a large city should offer different useful
capabilities or stock themes. An old city can pair a foundry and a retrofitted
factory; a newer one can pair a fabrication plant and an assembly laboratory.
Location names alone do not satisfy this reward target. During staged delivery,
report missing advanced capability as pending rather than substituting a token
unlock item.

Put valuable stock inside the relevant stores, workrooms and loading areas.
The risk can be a crowded yard, a worker-filled hall or an occupied upper floor.
Reward quality does not need every approach to be blocked by a mandatory fight;
scouting and a less exposed service entrance should sometimes pay off.

### Power and information continuity

- Generated buildings retain D1's ordinary mains and eventual outage rules.
  Newer districts do not receive permanent free electricity.
- A found machine needs the supply, feedstock and setup its own contract requires.
  Large-city equipment is not automatically portable or player-owned.
- Preserve the accepted printer/filament opportunities and per-site quantities.
  A larger city offers more relevant sites, rather than multiplying every find.
- Ordinary readable/copyable information and supplied printer designs work under
  D3-A/B. Protected software, damaged controllers and schematic extraction remain
  targets for the later hacking design.
- Public homes/libraries/shops provide basic and professional information supporting
  the basic workshop. Advanced information is found in protected laboratories,
  military facilities and comparable specialist spaces designed later.
- The advanced machine must not be required to obtain the basic tools needed to
  approach its own acquisition site.

## 7. Starts, approaches and visible cues

**Town:** start on the residential edge of a small settlement, with a
nearby ordinary house entrance, a road outward and an unobstructed retreat into
countryside. Apply the scenario's one-time initial-hostile exclusion in a horizontal
circle on the starting floor, default radius 16 tiles and configurable from 0 to
64 inclusive. Distance is straight-line X/Z distance, independent of walls.
Radius 0 disables exclusion. There is no line-of-sight exclusion or clearing on
other floors. This is a start condition, not a continuing safety aura or a cleared
whole town.

Select the start and its initial population together. Later approaches and LAN
arrivals do not remove already generated actors. Default Town should not place
an unequipped character directly inside a large-city gathering.

**FirstHouse:** default the safety radius to 0 and preserve the curated house/basement population policy and its
existing startup equipment. It does not receive the Town density multipliers or
general city street population. It retains the accepted optional home-printer
acquisition route. The same house type in Town uses Town's population policy.

City boundaries, larger silhouettes, repeated apartments, broad commercial
approaches and visible crowds communicate scale before entry. Named shops,
loading doors, workshop machinery and technical-school frontage communicate
likely rewards. Player-facing inspection uses observed information; exact hidden
spawn budgets belong to development diagnostics.

## 8. Terrain and visual identity

Use **40 new terrain variants**, with a named use for each. Reuse existing
generic wood, brick, concrete, metal, tile, windows and pavement where they already
fit. The entries below are art/content briefs; final stable IDs follow authoring.

| Family | Count | Variants | Generated uses |
| --- | ---: | --- | --- |
| Masonry and partitions | 8 | Weathered red brick, soot-dark brick, repaired brick, fresh red brick, pale engineering brick, gray blockwork, aged painted plaster, clean modular partition | Old homes/foundry shells; new infill; service rooms; old/new interiors |
| Painted concrete walls | 6 | Off-white, sand, rust-red, blue, green, charcoal | Apartment blocks, schools, clinic/service wings and coherent neighborhood accents |
| Industrial and newer wall systems | 6 | Riveted steel, ribbed steel, insulated panel, white composite, graphite composite, glazed curtain wall | Foundry/warehouse annexes, fabrication buildings, newer shops and public fronts |
| Interior floors | 10 | Weathered timber, dark tile, white tile, terrazzo, sealed concrete, gray epoxy, green epoxy, white clean-room finish, steel plate, rubber service flooring | Older homes; shops/clinics; public halls; workshops; assembly/control/service rooms |
| Exterior surfaces | 6 | Worn asphalt, patched asphalt, pale pavers, dark pavers, brick paving, service concrete | Older streets, repair patches, newer squares, arcade/park paths and loading yards |
| Door states | 4 | Open/closed industrial personnel door; open/closed glazed entrance door | Service entrances and newer public/retail frontages |
| **Total** | **40** | | |

Choose coherent palettes per building/block; never randomly scatter colors across
individual tiles. Distinguish public frontage, private rooms and service areas.
Floor transitions also help players read internal routes.

Wall/floor variants initially use ordinary supported collision and visibility.
Glazed walls/closed glazed entrances block movement but admit sight; an open
entrance permits movement. Industrial doors use ordinary opaque closed/open
behavior. Color and material names promise no new armor, fire, conductivity,
cleanliness or powered-lock mechanics.

Terrain variants have terminal names/glyph/colors and [external SDL briefs](city_artwork.md),
including connected wall edges, door states and rotated entrances. New artwork
status is **Ready for artwork**. Functional and visual review remain separate
completion evidence.

## 9. Two expedition examples

### First workshop from a quiet start

Start at a small settlement's residential edge. Search a house and basic tool
source, use a hobby/repair site for early supplies, then prepare for the nearest
medium town's workshop or electronics/teaching site. Establish power and bring
back a found printer and filament when available. Run the accepted supplied-design
printing loop. Neither access to a large city nor hacking is required.

### Prepared expedition into a large city

Approach an older industrial city from its service edge, observe the loading-yard
group and locate a secondary entrance. Use existing combat equipment, carrying
capacity, lighting and a planned withdrawal route to reach metal stock and a
functional workshop. A later expedition can target the retrofitted factory.

In a newer city, the analogous trip passes apartment/retail approaches toward a
technical or fabrication district. Early rewards are usable electrical stock,
references and prepared filament; advanced machinery and extracted production
software become goals as their respective systems arrive. A second district
gives another valuable destination rather than making one cleared building the
entire city's reward.

## 10. Implementation evidence and accepted decisions

Preserve the named baseline above and distinguish three measurements:

1. Equivalent-building indoor increase, including the same floors/basements.
2. Actual settlement totals, which also reflect size, height and building mix.
3. Outdoor density per eligible developed tile, including civic-site population.

Use seeds **0–31 and 42**, at region coordinates **(0,0), (-1,0), (0,-1),
(1,1)** as a reproducible comparison panel, deduplicating settlements that cross
regions. Add representative examples for every size/era pairing and at least one
large city crossing a region boundary. Select and record those actual example
seeds during implementation. The measured IDs and results are recorded in the
[city probe](city_probe.md#pass-4-reference-measurements).

Acceptance should demonstrate legal unique population, connected streets and
stairs, usable reward approaches, ordinary starting/retreat routes, and unchanged
cleared state after revisiting/save/load. Same generation inputs and different
chunk visitation orders must produce the same initial places and inhabitants.
Existing saved places in the new generation must not gain fresh enemies, supplies
or equipment. Pre-pass-4 generation saves are unsupported under the accepted
new-world policy.

Measure dense-city runtime with actual actors, including nearby offscreen work
and multiplayer views. If the accepted target density cannot run acceptably, return with
measurements and a gameplay tradeoff rather than silently thinning the city.
Schemas, algorithm/version choices and the checks needed to deliver these
outcomes are implementation work.

Current generation and runtime evidence is recorded in the
[city probe](city_probe.md#pass-4-reference-measurements). The older 10–35-site
town sample is historical. Functional checks cover authored routes, generation,
scenario safety, persistence and LAN; an end-to-end expedition playthrough and
balance iteration remain separate follow-up work.

**Accepted package:**

1. Three scales, 60/30/10 prevalence, separated settlements and continuous large cities.
2. Independent 25/45/30 old/mixed/new identity and broadly common futuristic construction.
3. 5×/6×/7× interior targets, sparse-to-busy outdoor populations and no size-based stat inflation.
4. Useful specialist acquisition, distinct old/new city rewards, small-settlement
   Town starts and curated FirstHouse population.
5. Thirteen place families, three dystopian variants and forty terrain variants,
   with advanced acquisition
   completed alongside its real production/recovery mechanics.
6. Society-wide centralized computing, standalone computers as old salvage and
   powerful computing hardware located outside ordinary households.
