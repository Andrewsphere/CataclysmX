# Household and outdoors batch

Target: 20 new items, no upgrades: four empty containers, four packed
sleeping/shelter articles, three field supplies, three repair supplies, three
packaged foods, two hygiene supplies, one belonging. All are placed as early
civilian salvage. Only `toilet_paper` carries deliberate humor.

The following are complete, plain-grid monochrome masters. All 20 received a
source-grid silhouette/proportion review assuming terminal cells twice as tall as
wide, before RGB colorization. Four masters were revised: the bucket now separates
its raised wire bail, open rim and tapered body; the tent is a soft handled bundle
rather than another box; the candle is a short pillar with a recessed wick; the
toothpaste has a connected neck and cap. Packed gear is shown bundled, not deployed.
The coordinator subsequently inspected native SDL previews of all 20; see
[verification and art review](item_expansion.md#verification-and-art-review).

## Tracker and integration

All entries are new, early civilian salvage, with a definition and an external
RGB art file at `data/art/items/<id>/art.txt`. All 20 have integrated loot routes
in Town and FirstHouse, including capacity-safe ground pools for bulky items.

| ID | Unit and role | g / exterior ml | Loot location / pool | Recipe |
| --- | --- | ---: | --- | --- |
| utility_bucket | empty bucket, storage | 800 / 14000 | `loot_cleaning_bulk_ground` | — |
| water_jerrycan | empty 20 L can, storage | 1100 / 23000 | `loot_camping_bulk_ground` | — |
| metal_toolbox | empty steel box, storage | 2500 / 13000 | `loot_workshop_bulk_ground` | — |
| glass_jar | empty 1 L jar, storage | 350 / 1200 | household / `house_household_supplies` | — |
| sleeping_bag | packed sleeping bag | 1500 / 12000 | `loot_camping_bulk_ground` | salvage |
| wool_blanket | folded blanket | 1600 / 8000 | `loot_camping_bulk_ground` | — |
| foam_sleeping_mat | rolled open-cell foam mat | 450 / 10000 | `loot_camping_bulk_ground` | salvage |
| camping_tent | packed canvas tent, poles and stakes | 3200 / 18000 | `loot_camping_bulk_ground` | salvage |
| candle | unlit candle | 120 / 150 | household supplies / `house_household_supplies` | — |
| matchbox | box of matches | 30 / 100 | household supplies / `house_household_supplies` | — |
| compass | pocket compass | 70 / 100 | sporting goods / `loot_outdoors` | — |
| sewing_thread | one spool, repair stock | 50 / 100 | hobby / `loot_hobby` | — |
| epoxy_adhesive | one two-part pack, repair stock | 100 / 130 | hardware / `loot_hardware` | — |
| solder_wire | one spool, repair stock | 100 / 100 | electronics / `loot_electronics` | — |
| rolled_oats | full canister, food | 550 / 1800 | grocery / `loot_dry_staples` | — |
| canned_tuna | full sealed can, food | 185 / 220 | grocery / `loot_food` | — |
| trail_mix | full sealed packet, food | 220 / 400 | grocery / `loot_food` | — |
| toilet_paper | one roll, hygiene | 120 / 1400 | household / `house_household_supplies`, cleaning / `loot_cleaning` | — |
| toothpaste | full tube, hygiene | 150 / 180 | bathroom / `house_bathroom` | — |
| toy_robot | electronic toy, salvage donor | 600 / 1400 | hobby / `loot_hobby` | dismantle |

Pool nesting makes capacity important: `loot_outdoors` is also used in furniture
and nested through `loot_preparedness`. Bulky gear uses dedicated ground pools;
oats use `loot_dry_staples` rather than the smaller `loot_food` pool. Native
integration validated placement capacities and referenced prerequisites.

The bucket, jerrycan, toolbox and jar have implemented generic item storage of
12, 20, 10 and 1 L respectively; rigid exteriors exceed capacities. Compare the
existing 600 ml glass bottle with 500 ml storage. Other package/container flags
are descriptive, not extra functional storage. The jerrycan is empty, and generic
storage is not a liquid simulation. Packed sizes include straps, poles and bags;
the tent is a compact canvas model. Default improvised melee applies, rather than
granting household objects weapon profiles.

## Recovery, properties and physical contracts

There are **four explicit recycling recipes and zero crafting recipes**:

| Target | Recovered units | Returned / target mass | Work |
| --- | --- | ---: | ---: |
| foam_sleeping_mat | foam_padding ×2 | 300 / 450 g | 250 |
| sleeping_bag | nylon_webbing ×1 | 100 / 1500 g | 400 |
| camping_tent | canvas_patch ×6, nylon_webbing ×2 | 1100 / 3200 g | 700 |
| toy_robot | tiny_electric_motor ×1, rigid_plastic_scrap ×2, microcontroller_board ×1, light_emitting_diode ×2 | 252 / 600 g | 300 |

Cutting requires `house_cutting` tier 1; the robot requires retained
`flathead_screwdriver` and `combination_pliers`. All returns are conservative
salvage, with unrecovered material implicit rather than converted into unrelated
stock. The blanket has no recycler: wool is not the clean cotton represented by
`cloth_patch`. The synthetic bag returns only its nylon compression straps, not
foam from its fiber fill. The mat is explicitly an older open-cell foam pad,
matching the stock definition of `foam_padding`; it is not a closed-cell pad
magically converted to another foam. Canvas, webbing and foam stock are authored
in `items/expansion_stock.json5`; the 1 g LED is authored in
`items/expansion_electronics.json5`. The existing board is 30 g. These definitions
are present and their masses were inspected; native integration exercises
final-pack loading.

All 20 IDs and original mass/exterior-volume pairs were preserved, including the
repair-stock contracts: thread 50 g, epoxy 100 g, solder 100 g. Bulky packed gear
keeps its placement footprint. These are packaged exterior volumes, not computed
solid-material volumes; empty rigid containers exceed their usable capacities.
Shared flags now also identify the mat's elasticity/porosity, dry textile
insulation, the exposed match striker's abrasiveness, flexible solder and sealed
trail-mix packaging. The plastic bucket's wire bail precludes whole-body electrical
insulation. No new flag definitions or implied capabilities were introduced.

Food consumption, hygiene, fire, light, shelter, sleeping, navigation, sealing,
liquid handling, repair chemistry, and robot operation are metadata or atmosphere
only. The four routes above and generic container storage are the implemented
capabilities. Default household melee is unchanged. Atmosphere leads the prose;
repair history and recoverable materials provide grounded survivor perspectives.
Only `toilet_paper` uses deliberate humor.

## Art review and validation

Every finished file uses six distinct local RGB colors, all applied to visible
glyphs. Upper/left contours receive restrained highlights, with darker undersides
and right edges; labels, fasteners and different materials have their own colors.
Color does not alter the approved grid. Size below includes intentional leading
spaces; the glyph bounding silhouette can be narrower.

| ID | Grid cells × rows | Reviewed form and material/color treatment |
| --- | ---: | --- |
| utility_bucket | 26 × 10 | Separate metal bail over elliptical mouth; tapered blue-gray plastic, dark interior and rim wear |
| water_jerrycan | 27 × 10 | Corner cap and open molded handle; olive plastic with recessed X reinforcement |
| metal_toolbox | 28 × 9 | Raised carry handle and two latches; worn red paint, bright steel and a dark lid seam |
| glass_jar | 24 × 10 | Neck below ribbed lid; cool transparent contours and vertical reflection, warmer metal closure |
| sleeping_bag | 30 × 7 | Rounded compressed bundle, diagonal folds and twin tan straps with metal buckles |
| wool_blanket | 28 × 7 | Folded layered rectangle, muted wool weave and contrasting striped edge |
| foam_sleeping_mat | 32 × 8 | Broad rolled spiral with dark core, foam-layer highlights and separate retaining straps |
| camping_tent | 32 × 9 | Soft bulging carry bag, loop handles and small tent emblem; canvas folds and green webbing |
| candle | 23 × 9 | Short wax pillar, recessed top and unlit brown wick; warm wax highlights and shaded base |
| matchbox | 29 × 7 | Shallow sliding carton, paper label, rough striker and wood/red match illustration |
| compass | 29 × 9 | Round dial at 1:2 cells; brass rim, glass, red north needle and pale opposite end |
| sewing_thread | 24 × 8 | Flanged spool, diagonal wound cotton, light thread face and shaded wooden core |
| epoxy_adhesive | 28 × 8 | Separate capped resin/hardener tubes, amber/blue identifiers and pale printed labels |
| solder_wire | 30 × 8 | Wound silver reel with dark bore, blue-gray flanges and loose wire tail |
| rolled_oats | 26 × 9 | Cylindrical canister, pale lid, green lettering and oat-colored grain motif |
| canned_tuna | 32 × 7 | Low oval tin, bright metal rim, fish illustration and blue label edge |
| trail_mix | 28 × 8 | Crimp-sealed flexible packet; separate nut/fruit colors and pale label |
| toilet_paper | 31 × 11 | Circular winding, brown cardboard core and unrolling paper tail with shaded edge |
| toothpaste | 32 × 5 | Flattened horizontal tube, metal crimp, cool label and connected ribbed cream cap |
| toy_robot | 27 × 14 | Distinct head, antenna, arms and feet; cool plastic, green eye lenses, red controls and dark grille |

Author-side static checks:

- Ad-hoc content inspection decoded all 20 canonical assets and compared every
  glyph, row and space exactly with the masters below, including `<<` decoding:
  **20/20 equal**. The same pass checked the documented single-cell allowlist,
  local palette declarations/references, distinct RGB values, visible use of all
  six colors, and header/file/row/body size limits: **passed**.
- A separate ad-hoc content-data pass parsed the batch's JSON5 subset and checked
  20 unique item IDs against item sources, unchanged physical pairs, canonical art
  paths, shared flag references/item domains, distinct flags, storage/exterior
  bounds, four unique recycler targets, counts/work/slot bounds, existing tool and
  return item references, and returned-mass bounds: **passed**. This is static
  content inspection, not the engine's full schema/reference loader.
- Three overwide assets were corrected; final decoded mat, tuna and toothpaste
  sizes are **32 × 8**, **32 × 7** and **32 × 5**. A subsequent static check
  passed the native dimensional requirements and exact master equality for all 20.
  The coordinator's later passing native runs and preview inspection are recorded in
  [verification and art review](item_expansion.md#verification-and-art-review).

## Monochrome masters

### utility_bucket
```text
        .------------.
      .'              '.
     /                  \
    o .----------------. o
    |/  ______________  \|
    |\__________________/|
     |  |             : |
     |  |             : |
      \                /
       \______________/
```

### water_jerrycan
```text
        _____     _______
       |_____|___/  ___  \
      /          __/   \  |
     /          |       | |
    |   ______   \_____/  |
    |  |  \\  / |         |
    |  |   \\/  |         |
    |  |   /\   |         |
    |  |__/  \__|         |
    |_____________________|
```

### metal_toolbox
```text
        ______________
       /  __________  \
      |  |          |  |
   ___|__|__________|__|___
  /________________________\
  |  []                []  |
  |========================|
  |                        |
  |________________________|
```

### glass_jar
```text
       ______________
      |==============|
      |______________|
      /              \
     /                \
    |  |               |
    |  |               |
    |  |               |
    |                  |
     \________________/
```

### sleeping_bag
```text
     ______________________
   /  / / / / / / / / /   /\
  |  / / / / / / / / /   |  |
  |  |====|       |====|  |  |
  |  |    |       |    |  |  |
   \_|____|_______|____|_/  /
     \____________________\/
```

### wool_blanket
```text
    ________________________
   /  / / / / / / / / /  /|
  /______________________/ |
  |  ##################  | |
  |  ------------------  | |
  |  ##################  | /
  |______________________|/
```

### foam_sleeping_mat
```text
     _____________________
  .-'  __________________  '-.
 /   .'  _____________   '.   \
|   /  .'             '.   |   |
|  |  |      _____      |  |   |
|   \  '.___(_____)__.'  /   /
 '.__'._______________.'__.-'
     |===|         |===|
```

### camping_tent
```text
          ____    ____
         /    \__/    \
    ____/______________\____
  .'   |  |          |  |   '.
 /     |==|  /\      |==|     \
|  /   |  | /__\     |  |   \  |
| |    |  | TENT     |  |    | |
 \     |==|__________|==|     /
  '.___|__|__________|__|___.'
```

### candle
```text
              |
        .-----|------.
       /   ___|___    \
       \_____________/
       |  |        : |
       |  |        : |
       |  |        : |
       |             |
        \___________/
```

### matchbox
```text
     ______________________
    /_____________________/|
   |  . . . . . . . . .  | |
   |    MATCHES           | |
   |       / / /          | |
   |      * * *           | /
   |______________________|/
```

### compass
```text
        ______________
     .-'      N       '-.
   .'    ____________    '.
  /     /     /\     \     \
 |  W  |     /  \     |  E  |
 |     |     \  /     |     |
  \     \_____\/_____/     /
   '.         S          .'
     '-.______________.-'
```

### sewing_thread
```text
       ________________
      /________________\
       |==============|
       |//////////////|
       |//////////////|
       |//////////////|
       |==============|
      /________________\
```

### epoxy_adhesive
```text
    ____              ____
   | A  |            | B  |
   |____|            |____|
   /    \            /    \
  | EPOXY|          | EPOXY|
  | RESIN|          |HARDEN|
  |      |          |      |
   \____/            \____/
```

### solder_wire
```text
        ________________
     .-'  ____________  '-.
    /   .'============'.   \
   |   /================\   |
   |  |======(____)======|  |
   |   \================/   |
    \   '.__========__.'   /
     '-.________________.-'---
```

### rolled_oats
```text
       ________________
     /==================\
    |                    |
    |    ROLLED OATS     |
    |      \\ | //       |
    |       \\|//        |
    |        /|\         |
    |                    |
     \__________________/
```

### canned_tuna
```text
     ______________________
  .-'                      '-.
 /        T U N A             \
|       ><(((('>               |
 \____________________________/
 |                            |
  '--------------------------'
```

### trail_mix
```text
   /\/\/\/\/\/\/\/\/\/\/\/\
  |                        |
  |       TRAIL MIX        |
  |     (o)  .  ( )  o     |
  |      .  (o)  o  .      |
  |     ( )  .   (o)       |
  |                        |
   \/\/\/\/\/\/\/\/\/\/\/\/
```

### toilet_paper
```text
        ______________
     .-'              '-.
   .'    ____________    '.
  /    .'            '.    \
 |    /     ______     \    |
 |    |    (______)    |    |
  \    '._          _.'    /
   '.     '--------'     .'
     '-.______________.-'____
                         \   \
                          \___\
```

### toothpaste
```text
    __________________
   |: TOOTHPASTE      \
   |:   ~~~~~~~~~      |===┬───┐
   |:_________________/===│|||│
                          └───┘
```

### toy_robot
```text
            __|__
           (  o  )
       ____|_____|____
      |   o       o   |
      |       ^       |
      |   '-------'   |
    __|_______________|__
   /  |  [||||||||]   |  \
  | o |   _________   | o |
  |___|  |  o   o  |  |___|
      |__|_________|__|
        |  |     |  |
       _|  |_   _|  |_
      |______| |______|
```
