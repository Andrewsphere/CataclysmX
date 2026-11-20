# Expansion equipment: 20-item authoring record

> Historical authoring/integration record: source paths, counts, and verification
> below describe that pass, not current ownership. See
> [current source organization](content_pack.md#source-organization) for semantic
> ownership.

Scope: 20 new IDs, no upgrades; six tools, ten functional wearables, four
craft-only weapons. Atmospheric and humorous exceptions: **none**; all descriptions
are plain. Reading deferred.
Sources: `data/items/expansion_equipment.json5` and
`data/crafting/expansion_equipment.json5`. Canonical art is
`data/art/items/<id>/art.txt`; complete monochrome masters below precede colorization.

## Reviewed monochrome masters

All 20 complete grids were drawn first and reviewed together in source. Review uses
terminal 1:2 cell proportions: broad shirts have short bodies, the coat extends
below its sleeves, the staff/spear stay long and narrow, and the duffel has a
separate shoulder loop. The coordinator subsequently inspected native SDL previews
and re-inspected the focused refinements; see [verification and art review](item_expansion.md#verification-and-art-review).
Whitespace inside each fence is part of the master; colorization must
preserve every glyph.

### retractable_utility_knife
```text
                       ______
  ____________________/_____/|
 /  ┌──────┐  ░░░░░░ /______/ |
|   │ ▄▄▄▄ │  ░░░░░░|______|/
 \__└──────┘_________/
```

### tin_snips
```text
                  __________
             ____/_________/
         ___/  o _________/
    ____/   /───\
  /    /___/     \____
 /  __/             \  \
|  /                 |  |
 \_\                 /__/
```

### wood_chisel
```text
                     _______
       _____________/______/
  ____/____________/_____/
 / ░░░░░░░░ /===|______/
|  ░░░░░░░░|===|
 \__________\___|
```

### hand_drill
```text
       __________
      / ░░░░░░░░ \
      \__________/
           ║
       ┌───╨───┐
       │  /|   │──────┐
       │ /o|   │     │
       │/ /|   │   ┌─┴──┐
       │ / |   │   │░░░░│
       └───┬───┘   └────┘
           ║
           ║
          /|\
          \|/
           V
```

### multimeter
```text
     ╭────────────────╮
     │ ┌────────────┐ │
     │ │  0.00      │ │
     │ └────────────┘ │
     │   ·  ·  ·  ·   │
     │      ╭──╮      │
     │   ·  │ /│  ·   │
     │      ╰──╯      │
     │    o      o    │
     ╰────│──────│────╯
     ╭────╯      ╰──────╮
     │                 │
     ╰──────╮     ╭─────╯
            │     │
           /|     |\
           ||     ||
```

### socket_wrench_set
```text
  ╭──────────────────────────╮
  │ ┌──┐ ┌──┐ ┌──┐ ┌──┐ ┌─┐ │
  │ │oo│ │oo│ │oo│ │oo│ │o│ │
  │ └──┘ └──┘ └──┘ └──┘ └─┘ │
  ├────────────┬─────────────┤
  │  ╭──╮      │ ▄ ▄ ▄ ▄ ▄  │
  │  │o ╰══════╪════════╗   │
  │  ╰──╯      │        ║   │
  │            │        ║   │
  ╰────────────┴─────────────╯
```

### cotton_socks
```text
   ┌──────┐    ┌──────┐
   │||||||│    │||||||│
   │      │    │      │
   │      │    │      │
   │      │    │      │
   │      ╰─╮  │      ╰─╮
   │        ╰╮ │        ╰╮
   ╰───░░░░░░│ ╰───░░░░░░│
       ╰─────╯     ╰─────╯
```

### hooded_sweatshirt
```text
           ╭──────╮
          / ╭────╮ \
         /  │    │  \
     ___/___╰────╯___\___
    /       \    /       \
   /   /     |  |     \   \
  /   /│     |  |     │\   \
 /___/ │     o  o     │ \___\
       │             │
       │   ╭─────╮   │
       │   │     │   │
       │   ╰─────╯   │
       └─────────────┘
```

### sneakers
```text
    ╭──────╮       ╭──────╮
    │ ╭──╮ │       │ ╭──╮ │
    │ ╰──╯ ╰╮      │ ╰──╯ ╰╮
    │   ╳╳╳ ╰──╮   │   ╳╳╳ ╰──╮
    │    ╳╳╳   ╰╮  │    ╳╳╳   ╰╮
    ╰───────────│  ╰───────────│
    ╰═══════════╯  ╰═══════════╯
```

### winter_coat
```text
           ╭──────╮
          / ░░░░░░ \
     ____/___╲╱_____\____
    /        ║          \
   /   /     ║     \     \
  /   /│     ║     │\     \
 /   / │     ║     │ \     \
|___/  │     ║     │  \____|
       │     ║     │
       │ ┌─┐ ║ ┌─┐ │
       │ │ │ ║ │ │ │
       │ └─┘ ║ └─┘ │
       │     ║     │
       │     ║     │
       │     ║     │
       └─────╨─────┘
```

### work_coveralls
```text
       ____╲  ╱____
    __/     ╲╱     \__
   /    /┌─┐║┌─┐\    \
  /    /│└─┘║└─┘│\    \
 /____/ │   ║   │ \____\
        │===╬===│
        │   ║   │
        │  / \  │
        │ │   │ │
        │ │   │ │
        │ │   │ │
        │░│   │░│
        │ │   │ │
        │ │   │ │
        └─┘   └─┘
```

### hard_hat
```text
          ╭────────╮
       ╭──╯  ┌──┐  ╰──╮
     ╭─╯     │  │     ╰─╮
    /        │  │        \
   │         │  │         │
   │         │  │         │
  ╭┴─────────┴──┴─────────┴╮
  ╰═══════════════════════╯
```

### welding_gloves
```text
   ╭─╮╭─╮╭─╮       ╭─╮╭─╮╭─╮
 ╭─┤ ││ ││ │     ╭─┤ ││ ││ │
 │ │ ││ ││ │     │ │ ││ ││ │
 │ │ ╰╯ ╰╯ │     │ │ ╰╯ ╰╯ │
 │         │╭─╮  │         │╭─╮
 │         ╰╯ │  │         ╰╯ │
 ╰╮          /   ╰╮          /
  │         /     │         /
  │────────│      │────────│
  │ ░░░░░░ │      │ ░░░░░░ │
  │ ░░░░░░ │      │ ░░░░░░ │
  └────────┘      └────────┘
```

### tool_belt
```text
   ╭══════════════════════╮
  ╭╯          ┌────┐     ╰╮
  ╰═══════════│ [] │══════╯
    │ ┌────┐  └────┘ ┌──┐
    │ │    │  │  │   │  │
    │ │    │  │  │   │  │
    │ ╰────╯  │  │   ╰──╯
    ╰─────────╯  ╰──────╯
```

### canvas_duffel
```text
         ╭──────────╮
        /            \
       /              \
      /   ╭──────╮     \
  ╭──┴────┴──────┴──────┴──╮
 /    ║    ─────────  ║     \
│     ║               ║      │
│     ║    ┌──────┐    ║      │
│     ║    │      │    ║      │
│     ║    └──────┘    ║      │
 \____║_______________║_____/
```

### padded_jacket
```text
           ╭──╮╭──╮
      ╭────╯  ╲╱  ╰────╮
     /  /\  /\ ║ /\  /\ \
    / \/  \/  \║/  \/  \ \
   / /\│ /\  /\║ /\  /\│\ \
  / /  │/  \/  ║/  \/  │ \ \
 / /\ /│\  /\  ║\  /\  │\ \ \
╰────╯ │ \/  \/║ \/  \/│ ╰────╯
       │ /\  /\║ /\  /\│
       │/──\/  ║/  \/──│
       │\__/\  ║  /\__/│
       │ /\  \/║ \/  /\│
       ╰═══════╩═══════╯
```

### wooden_club
```text
                   __,────╮
          __,────~~   ────  ╲
  ╭───,──~     ──────  ──   )
  ( |||  ─────     ───(─)   )
  ╰───~──.__     ──────    ╱
            ~───._______.─╯
```

### quarterstaff
```text
 ╭────────────────────────────╮
 │─── ───── ────||||||─────────│
 ╰────────────────────────────╯
```

### salvaged_machete
```text
               _______________
          ____/              /|
     ____/__________________/ /
 ___|____|__________________|/
| o   o  |/
|________|
```

### steel_spear
```text
                         ╭────╲
 ══════════════════════╪═╪─────>
                         ╰────╱
```

## Final art-only SDL feedback refinement

Read the actual `out/item-expansion_equipment_contact_2.png` and the two stock
contact sheets. Replaced these three complete masters before colorization:
steel spear now has a small seven-column leaf point, minimal attachment and a
majority-length shaft; wooden club has a continuous rounded wood taper, grain
and a small grip; padded jacket has a **17-column torso** and broad diamond
quilting within a **31×13** grid. Source-grid self-inspection used terminal 1:2
proportions. Spear is **32×3**; club is **29×6**.

The focused read-only audit passed exact master/decoded glyph-and-whitespace
equality, RGB declaration/default/local-reference checks, the native single-cell
glyph allowlist, UTF-8/header/newline/file limits and ≤32×32 decoded bounds for
all nine revised stock/equipment assets. The coordinator subsequently rendered,
re-inspected and checked the refinements as documented in [verification and art review](item_expansion.md#verification-and-art-review).

## Tracker and per-equipment balance briefs

Every row is new; art/master and colored asset are complete. Data/recipe authoring
and the focused starting-balance review are complete. The current Release combat
runner loads the integrated Town catalog. The loot column records integrated themed
routes; native integration also validated placement capacities.
Units are whole objects except the explicitly paired clothing/set.

| ID | Unit; g/ml | Role, stage | Loot context | Recipes | Advantage / cost versus current peers |
| --- | --- | --- | --- | --- | --- |
| retractable_utility_knife | one knife; 180/150 | compact cutting, early salvage | hardware, home tools | recycler | Cutting 1 like chef knife, smaller; 5 vs 8 HP slash, no block and only 1 HP parry capacity. |
| tin_snips | one pair; 400/350 | cutting, early salvage | hardware, auto shop | recycler | Cutting 1 like hacksaw; compact but no higher tier or special sheet-metal action. |
| wood_chisel | one chisel; 220/180 | retained woodworking tool, early | hardware, workshop | staff input; recycler | Smaller than metal file; explicit staff prerequisite, no hammering or melee upgrade. |
| hand_drill | one drill with bit; 900/1200 | retained fitting tool, intermediate | hardware, workshop | spear input; recycler | Hand-powered spear fitting; larger than screwdriver, no universal drilling action. |
| multimeter | one meter with leads; 350/500 | electronics salvage tool, early | electronics, auto shop | recycler | Compact plastic donor versus whole appliances; no full 200 g copper-wire unit can plausibly be reclaimed, and measurement remains unsupported. |
| socket_wrench_set | one cased set; 2500/3000 | dismantling alternative, early | hardware, auto shop | recycler | Twenty steel scraps recovered; much bulkier than adjustable wrench, no invented wrenching trait. |
| cotton_socks | one pair; 80/200 | under-layer foot clothing, early | homes, clothing, laundry | recycler | 0.2/0.3 HP blunt/slash; 1 encumbrance per foot vs boots 4, negligible protection. |
| hooded_sweatshirt | one garment; 650/2000 | normal clothing, early | homes, clothing, laundry | recycler | 0.9/1.3/0.3 HP blunt/slash/pierce vs shirt 0.7/1/0; encumbrance 3 vs 2, partial hood coverage. |
| sneakers | one pair; 700/1800 | light footwear, early | homes, clothing, sporting goods | recycler | Foot encumbrance 2 vs boots 4; protection 1/1.2/0.5 vs boots 3/3.5/2 HP. |
| winter_coat | one coat; 1500/4500 | bulky civilian outerwear, early | homes, clothing | recycler | 1.5/2/0.5 HP protection, more than rain jacket; torso/arm encumbrance 7/6, no warmth mechanic. |
| work_coveralls | one garment; 1000/3200 | full work clothing, early | workshop, auto shop, laundry | recycler | 1.2/1.8/0.6 HP; torso/arms/legs 4/3/4 encumbrance, no storage or separate shirt slot discount. |
| hard_hat | one helmet; 400/1500 | local rigid work protection, early | hardware, workshop, warehouse | recycler | Head-only 6/3/1.5 HP, 8500 coverage, encumbrance 3; no face/eye/ear protection. |
| welding_gloves | one pair; 350/800 | heavy gauntlets, early | workshop, auto shop | recycler | 2/3.5/1.5 HP vs gloves 1.5/2.5/1; hand encumbrance 7 vs 4 plus partial arms at 2; no heat claim. |
| tool_belt | one belt; 600/1000 | small strapped storage, early | hardware, workshop | make + recycler | 4000 ml vs backpack 20000; torso encumbrance 3 vs 8, 1000 coverage, not armor progression. |
| canvas_duffel | one shoulder bag; 800/1800 | bulk storage, early | homes, sporting goods | make + recycler | 24000 ml vs backpack 20000; torso 10 and both arms 3 model shoulder carry cost, not free storage. |
| padded_jacket | one quilted jacket; 2200/6500 | deliberate low-tech armor, intermediate | sporting goods, preparedness/home storage | make + recycler | Torso/arm 5/6.5/3 HP; encumbrance 10/8 vs shirt 2/2 and hoodie 3/3, bulk/work/tailoring 2 investment. |
| wooden_club | one club; 1000/1600 | cheap dedicated blunt, initial craft | **none: craft-only** | make + recycler | 11 HP at cost 115 vs hammer 12/110 and baton 9/100; one plank and Cutting 1, no skill gate or specific saw/knife, no hammer utility, one hand, 3.5 HP block. |
| quarterstaff | one staff; 1400/3000 | defensive blunt, early craft | **none: craft-only** | make + recycler | 11 HP/cost 105, two-hand rating +0.5, 9.5 HP block vs baton 4.5 and hammer 2.5; both hands, fabrication 1/saw/chisel/800 work. Best justified by guarding and trained Defensive use, not faster hammer kills. |
| salvaged_machete | one blade; 1000/1300 | cheaper cutting weapon, intermediate craft | **none: craft-only** | make + recycler | 15 HP/cost 115 and Cutting 1; measured advantage over hammer, below sword 18/130; one-hand only, 3 HP block vs sword 4.5, fabrication 1/2400 work/10 steel scraps plus tools. |
| steel_spear | one spear; 2000/3500 | fitted piercing polearm, intermediate craft | **none: craft-only** | make + recycler | 17 HP pierce, 1.5 HP penetration, cost 145, 6.5 HP block vs coil 13/150/4; fabrication 1, mechanics 1, 3200 work, four retained tools, both hands. Stronger than coil, weaker than reachable sword; earlier work/skill investment, no reach/electricity. |

## Recipe and mass ledger

All seven craft recipes explicitly use `known_by_default:true`. Retained tools do
not enter mass accounting. Input counts are whole stock/packet units, never nails,
bolts or thread lengths. No generated recyclers; exactly one explicit recycler per
new item. `house_cutting` and `hammering` are existing traits, not new vocabulary.

| Recipe ID | Consumed units | Retained tools | Skill; work | Input → product g |
| --- | --- | --- | --- | --- |
| make_wooden_club | timber_plank 1 | Cutting 1 trait provider | fabrication 0 training; 500 | 2000 → 1000 |
| make_quarterstaff | timber_plank 1 | hacksaw, wood_chisel | fabrication 1; 800 | 2000 → 1400 |
| make_salvaged_machete | steel_scrap 10, wood_scrap 1, steel_wire 1 | claw_hammer, hacksaw, metal_file | fabrication 1; 2400 | 1400 → 1000 |
| make_steel_spear | steel_rod 1, steel_pipe 1, steel_wire 1, wood_scrap 1 | claw_hammer, hacksaw, metal_file, hand_drill | fabrication 1, mechanics 1; 3200 | 2400 → 2000 |
| make_padded_jacket | cloth_patch 20, canvas_patch 6, foam_padding 4, sewing_thread 1 | retractable_utility_knife, sewing_supplies | tailoring 2; 5000 | 2350 → 2200 |
| make_tool_belt | leather_patch 4, nylon_webbing 1, sewing_thread 1, nuts_and_bolts 1 | retractable_utility_knife, combination_pliers, sewing_supplies | tailoring 1; 1800 | 830 → 600 |
| make_canvas_duffel | canvas_patch 5, nylon_webbing 2, sewing_thread 1 | retractable_utility_knife, sewing_supplies | tailoring 1; 2200 | 1000 → 800 |

Thread is a whole 50 g spool; nuts_and_bolts a whole 200 g packet of ten sets.
Unused trimming/packaging is process loss; it is not a second output. No recipe
depends on self-crafted cloth or tools. The spear reaches the eight-input-type
limit exactly; other recipes stay below it. The sword retains its 18 HP damage,
two grip options and 6000-work fabrication-2 route; its legacy route also exists.

| Recycler ID | Target | Returned units; total g | Work |
| --- | --- | --- | ---: |
| salvage_retractable_utility_knife | retractable_utility_knife | steel_scrap 1; 100 | 160 |
| salvage_tin_snips | tin_snips | steel_scrap 3; 300 | 240 |
| salvage_wood_chisel | wood_chisel | steel_scrap 1; 100 | 180 |
| salvage_hand_drill | hand_drill | steel_scrap 6, wood_scrap 1; 800 | 500 |
| salvage_multimeter | multimeter | rigid_plastic_scrap 2; 200 | 300 |
| salvage_socket_wrench_set | socket_wrench_set | steel_scrap 20; 2000 | 400 |
| cut_up_cotton_socks | cotton_socks | cloth_patch 1; 40 | 100 |
| cut_up_hooded_sweatshirt | hooded_sweatshirt | cloth_patch 12; 480 | 200 |
| salvage_sneakers | sneakers | rubber_sheet 1, cloth_patch 4; 460 | 250 |
| cut_up_winter_coat | winter_coat | cloth_patch 20; 800 | 350 |
| cut_up_work_coveralls | work_coveralls | cloth_patch 20; 800 | 300 |
| strip_hard_hat | hard_hat | nylon_webbing 1; 100 | 120 |
| cut_up_welding_gloves | welding_gloves | leather_patch 2; 240 | 220 |
| cut_up_tool_belt | tool_belt | leather_patch 2, nylon_webbing 1; 340 | 250 |
| cut_up_canvas_duffel | canvas_duffel | canvas_patch 3, nylon_webbing 1; 550 | 300 |
| cut_up_padded_jacket | padded_jacket | cloth_patch 12, canvas_patch 4, foam_padding 3; 1530 | 600 |
| salvage_wooden_club | wooden_club | wood_scrap 4; 800 | 250 |
| salvage_quarterstaff | quarterstaff | wood_scrap 6; 1200 | 350 |
| salvage_salvaged_machete | salvaged_machete | steel_scrap 7; 700 | 500 |
| salvage_steel_spear | steel_spear | steel_pipe 1, steel_scrap 5; 1700 | 650 |

## Integrated acquisition and mechanics

Town and FirstHouse include the equipment sources and their prerequisites. Sixteen
tools and wearables have reachable themed loot routes; the four weapons are
craft-only. Capacity-checked ground pools accommodate bulky outerwear and the
socket set. Plank and retained tools, as well as rod/pipe/wire and the spear's
fitting tools, are obtainable. Tool belt and duffel hold ordinary volume-based storage;
the engine has no special tool quickdraw or hand-free bag mode.
The three sewn products also retain the existing loot-accessible `sewing_supplies`
kit for its needles and notions; the separate spool pays for consumed thread.

Unsupported: warmth/weather, certified electrical/heat safety, live multimeter
measurement, generic drill/wrench actions, sewing durability/repair, item reach,
electrical attacks and material simulation. Flags are metadata. Ordinary tools
except the utility knife use incidental fallback melee rather than invented roles.
No proficiency, recipe-book or new tool-trait definitions are needed.

## Starting-balance decisions

Review follows `item_balance.md`: equal shopper/cellar/police comparisons, recipe
AI, fixed policies, production combat and no abilities. The completed 96-seed
equipment screen was inspected by matchup, injury, stamina and absorption. Important
relationships were confirmed with 384 separate seeds, 1001–1384, at skills 0/3/6;
armor confirmation uses skill 3. Raw fights, move counts, commands, paired
seed-cluster bootstrap intervals and summaries remain local under
`out/item-expansion-followup`. These support starting roles, not universal tiers.

- **Club:** leave its combat profile alone and remove the specific saw/knife
  acquisition requirement. A plank plus any Cutting 1 tool now buys a known,
  fabrication-0 fallback for 500 work. Hammer remains the better found weapon and
  a useful tool. Novice attack-only duels are extremely dangerous with either;
  cautious shopper runs mostly escape, with substantial injury. Arena escapes do
  not establish town survival odds.
- **Staff:** original block absorption was real but insufficient compensation for
  its weak offense. Raise damage 10→11 HP, timing factor 110→105 and two-hand rating
  bonus 0.25→0.5; reduce shaping work 1400→800. Keep 9.5 HP block capacity. Fresh
  equal-window Defensive exposure shows less injury than both club and hammer in
  every matchup at each training level. Skill-3 mean injury falls by about 18 HP
  versus hammer. Trained Defensive use improves over Balanced (skill-6 panel wins
  +8.25 points, paired 95% interval +3.91 to +12.85). Hammer-versus-staff duel win
  differences remain uncertain: the staff is a guarding alternative with longer
  fights and more exhausted actions, not a demonstrated universal upgrade.
  Sword/shield is decisively stronger in the tested trained duels; the staff buys
  defense from a single plank rather than matching that later loadout.
- **Machete:** 13 HP/cost 120 failed to earn dedicated steelwork over a hammer.
  Set 15 HP/cost 115 and 2400 rather than 3600 work, keeping the ten-scrap material
  bill, fabrication 1, three retained tools and Cutting 1 utility. Fresh comparisons
  favor it over hammer in every matchup at skills 0/3/6, with lower mean injury and
  fewer exhausted actions. It remains below the two-handed sword and trades block
  capacity/piercing performance against the spear while leaving the offhand free.
- **Steel spear:** retain the successful combat upgrade over coil spear, but cut
  work 6500→3200 and fabrication 2→1; mechanics 1 and all four fitting tools remain.
  It loses substantially to the reachable two-handed sword at every tested skill
  and matchup. Its niche is earlier, lower-work dedicated polearm progression,
  not superior reach or a claim that the sword is fabrication-gated: the legacy
  skill-free 6000-work sword recipe remains reachable. Finding rod/pipe/wire and
  drill/file/saw/hammer is still a real route-dependent acquisition cost.

### Wearable decisions

Keep all ten profiles, masses and volumes. Everyday garments are loot and salvage
choices, not ten successive combat upgrades. The source routes are
`loot_everyday_clothing` (socks/sneakers/duffel), `loot_work_protection`
(hat/gauntlets/belt) and ground-only `loot_outerwear_ground`
(hoodie/coat/coveralls/jacket, jacket weight 1 versus hoodie 5). Household/urban
pools and house, clothing-store, laundry, workshop, auto-shop and sporting-store
placements supply themed access; this is source reachability, not a measured
spawn-frequency or encounter-risk estimate.

| Garment | Starting niche and decision |
| --- | --- |
| cotton_socks | Negligible under-layer foot protection, tiny salvage unit; optional everyday clothing, no demonstrated duel advantage over bare feet. Do not sell warmth as a mechanic. |
| hooded_sweatshirt | Found torso/arm protection with partial head/ear coverage, moderate normal-layer cost; useful exposure protection, not a proven work-shirt duel upgrade. Twelve cloth patches if not worn. |
| sneakers | Lower foot encumbrance than boots in exchange for much less local protection; light found footwear, not a demonstrated panel-win improvement. |
| winter_coat | Found outer-layer protection/partial hood and twenty cloth patches; more exposure protection but more handling burden than a hoodie. Cotton shell and cloth-only 800 g salvage retained; no foam or warmth benefit. |
| work_coveralls | Found single normal-layer torso/arm/leg protection, strongest civilian passive coverage in the screen; moderate regional costs, not a discount for overlapping other normal garments. |
| hard_hat | Found head-only blunt specialization with coverage gaps; actual absorption is visible, but whole-body wins do not establish a head/face armor tier. |
| welding_gloves | Found thicker hand protection plus partial forearms at a substantial dexterity cost; local sidegrade to work gloves, not a proven whole-fight upgrade or heat-protection mechanic. |
| tool_belt | Low-volume, low-encumbrance storage alternative: 4 L at torso 3 versus backpack 20 L at 8. Craft only when the smaller carrying requirement justifies leather/webbing/thread and 1800 work. |
| canvas_duffel | Found or 2200-work sewn 24 L storage, versus backpack 20 L; torso 10/arms 3 charge for capacity. Its poor duel screen supports removing it for combat, not buffing its armor. No loaded-bag comparison is claimed. |
| padded_jacket | Deliberate torso/arm armor: tailoring 2, textiles/foam/thread, 5000 work, large carry volume; alternatively low-weight outerwear loot. Strong local absorption and better survival, paid for by slow, tiring fights and potentially worse injury. |

Jacket confirmation includes no clothing, isolated T-shirt, isolated jacket,
gloves/boots plus T-shirt, that same outfit plus jacket, and the standard work
outfit. The intended jacket-over-cotton combination reduces equal 1000-tick
unarmed exposure injury by about 11 HP versus cotton/gloves/boots, with no deaths
in either armor observation window. Active two-handed-sword duels instead produce
more injury and exhausted actions despite better survival; the police survival
difference is small. Keep protection and encumbrance together: passive mitigation
does not imply cleaner victories, and neither the original small ranking lead
nor the confirmed aggregate advantage establishes armor superiority in every fight.
Natural zombie contacts mainly exercise blunt/slashing protection; piercing,
regional targeting and fully loaded storage remain outside these confirmations.

## Actual checks and delivery status

- **20/20 author-complete**, all new: definitions, plain factual prose, shared
  flags, physical units, reviewed monochrome masters, exact colored assets, and
  unique mass-bounded recycling. Seven craft recipes cover all four weapons plus
  padded jacket, tool belt and duffel. No atmospheric or humorous exceptions.
- A one-off read-only Python content audit passed against the final 23 owned
  files. It normalized the JSON5 subset to Python literals (not the engine JSON5
  loader), checked seven crafting mass inequalities using the actual current
  stock/supplies definitions, all twenty recycler mass inequalities, input count
  bounds and distinctness, positive work, known-by-default recipes, ten armor
  profiles and five authored melee profiles (four weapons plus utility knife).
- All **20 colored assets exactly match their complete masters** after markup
  stripping. RGB header/local palette/default references, allowed single-cell
  glyphs, final newlines and whitespace checks passed. Every illustration is
  24–32 columns wide, 3–16 rows high. The review widened the meter's lead loop and
  coverall sleeves and synchronized their masters before the final equality audit.
- A declaration/reference scan found **47 unique new item/recipe IDs**, no detected
  collisions with existing matching declarations, valid existing shared flags,
  and all prerequisite item/trait/skill names present, including crossbatch stock
  and thread. This is a static scan, not transactional catalog validation.
- Existing `sewing_supplies`, saw, file and hammer loot references were confirmed;
  newly authored tools also have integrated placements. The balance follow-up used
  81 screening cells / 7776 fights and 162 confirmation/exposure cells / 62208
  fights. Loaded-storage combat remains unmeasured.
- These author-side audits are distinct from the coordinator's completed native
  checks and visual review: [verification and art review](item_expansion.md#verification-and-art-review).
