# W3A/W3B workshop roster and implementation

**Status: Implemented and functionally verified; pacing playtesting and external SDL map artwork remain follow-ups.** This roster applies the
[accepted progression](crafting_progression.md) and
[delivery plan](crafting_delivery_plan.md). They replace the historical
[nine-item W3 proposal](workshop_content.md).
The ordinary accessible/shared-trait protection rule is already accepted.

The implementation request accepts the 11 + 11 scope, provider upgrades, learning
revisions and default-known recipes below. The roster-level descriptions retain
the design rationale; the authored definitions and implementation record below
own final quantities, work, fuel, salvage and placement choices. Existing IDs
retain their identity.

## 1. Delivered boundaries and coverage

| Slice | New item candidates | Existing item-definition upgrades | Crafting changes | Objects |
| --- | ---: | ---: | --- | --- |
| W3A forging and useful practice | 11 | 7 | 9 new recipes; revise both sword producers | 4 new placed stations |
| W3B electrical assembly and welding | 11 | 1 | 8 new recipes; revise 2 existing recyclers' learning gates | 1 new packable storage object; reuse D1 battery station |
| Combined | **22** | **8** | **17 new crafting recipes, 2 revised sword recipes, 2 learning revisions** | **5 new objects** |

New recycling definitions are selected during the conservation audit rather than
counted speculatively. Adding a recipe for an existing item is reuse, not another
new item or item-definition upgrade.

Delivered functional coverage:

- W3A supplies the accepted forge/tongs 2 -> forming equipment 3 -> sword 4 chain,
  with both crafted-station and found-station routes.
- Fabrication 2 has at least three useful outputs beyond station construction:
  tongs, tool blanks, crates and pry bars. Fabrication 3 has hammer, fabricated
  anvil and chisel production. All have a concrete current-system consumer.
- Electronics 1 and 2 each get at least two learning activities. The first iron
  can be assembled without another iron; the first welder without welding.
- W3B supplies two welding providers, both protection tiers and an actual consumer
  of each welding tier. A stronger found welder also performs easier jobs.
- Every found input/provider has a named placement context; charcoal and electrodes
  each have at least two location types. These are coverage targets, not guarantees
  that every town or single building contains a complete recipe kit.
- Every new item has a functional consumer or supported use. All new equipment
  receives an acquisition route, a recycling decision and presentation coverage.

W3A is independent of W3B. W3B follows W3A's shared tool vocabulary and adds an
alternative producer of its fabricated anvil. All selected powered crafting
providers are handheld grid tools, so this roster uses D1/W1/W2. Powered placed
manual benches remain in M-Bench; printers remain in W4/D3.

## 2. Shared provider upgrades

These are the seven W3A item-definition upgrades plus W3B's multimeter upgrade:

| Existing ID | Owner | Proposed change / concrete consumer |
| --- | --- | --- |
| `combination_pliers` | W3A | `metal_holding` 1 for the first tongs |
| `hacksaw` | W3A | Add `metal_saw` 1; preserve existing `house_cutting` for existing consumers |
| `hand_drill` | W3A | `metal_drilling` 1 for station/rack assembly; represents the existing tool with suitable ordinary bits |
| `adjustable_wrench` | W3A | `wrenching` 1 for ordinary assembly |
| `socket_wrench_set` | W3A | `wrenching` 2; also satisfies ordinary assembly at tier 1 |
| `flathead_screwdriver` | W3A | `screwdriving` 1 for the selected compatible fasteners |
| `arming_sword` | W3A | Revise cold-shaped-scrap prose to match the new forged-blade routes |
| `multimeter` | W3B | Add `electrical_testing` 1 for lead/tool/pack assembly; retain its existing grid-meter action |

Definitions now live in `data/items/tools.json5` and `data/items/weapons.json5`;
I1's [source map](content_pack.md#source-organization) replaces the original
slice/expansion ownership.
The existing `claw_hammer`, `metal_file`, `wood_chisel`, `soldering_iron`,
`wiring_kit`, cells and stock are reused.

Ten new trait IDs are defined across the two slices:

- W3A: `anvil`, `forge_heat`, `metal_holding`, `metal_saw`, `metal_drilling`,
  `wrenching`, `screwdriving`.
- W3B: `welding`, `welding_protection`, `electrical_testing`.

Keep existing `hammering` and `soldering`. Filing and wood preparation can use
explicit retained tools in this roster; a larger woodworking vocabulary is not
needed to make these recipes function. New test/assembly traits express recipe
capabilities; they do not introduce unimplemented repair or diagnostic actions.

## 3. W3A: forging, stock and storage

### New-item tracker

Owner: `data/items/workshop_forging.json5`.
Art and salvage status for every row are specified in section 9.

| Candidate ID | Represented unit | Practical role / construction band | Proposed acquisition |
| --- | --- | --- | --- |
| `solid_steel_block` | One heavy recovered block | Pre-existing working mass for crude and fabricated anvils | Hardware bulk stock; workshop ground stock |
| `refractory_brick` | One firebrick | Hearth lining; purchased/recovered industrial material | Hardware bulk stock; workshop spares |
| `charcoal_charge` | One bag of usable charcoal | Whole-item W2 forge fuel | Hardware stock; camping/outdoor bulk stock; workshop supplies |
| `crude_anvil_kit` | One block mounted on a stand | Place anvil 1; Fabrication 1 mounting work | Craft; occasional recovered workshop equipment |
| `charcoal_forge_kit` | One hearth and hand-bellows assembly | Place forge heat 1; Fabrication 2 | Craft; recover a found cold/empty forge |
| `forging_tongs` | One pair | Metal holding 2; Fabrication 2 | Craft; workshop tools |
| `smithing_hammer` | One handled forming hammer | Hammering 2; Fabrication 3 | Craft; workshop tools |
| `fabricated_anvil_kit` | One fitted workshop anvil and stand | Place anvil 2; Fabrication 3 | Craft; occasional workshop bulk equipment |
| `spring_steel_bar` | One continuous blade-length bar of suitable carbon steel | Prepared blade/tool stock; preserves the distinction from arbitrary scrap | Workshop stock; auto-repair spring stock |
| `tool_steel_blank` | One small blank cut from suitable bar stock | Repeat-use input for hammer/chisel manufacture; Fabrication 2 preparation | Craft; workshop spares |
| `wooden_workshop_crate` | One empty rigid wooden crate | Actual item-container storage; Fabrication 2 | Craft; workshop/warehouse storage stock |

The bar's identity represents selected suitable steel, including recovered stock
where its condition/grade is appropriate. It is not manufactured from generic
`steel_scrap` in this slice. A separate small blank avoids consuming a whole
blade-length bar for every small chisel; section 9 records yields and waste.

Charcoal is found fuel in W3A. Its later Survival-2 production route must bootstrap
independently of a charcoal-fueled forge. Firebrick and suitable steel manufacture
also remain later work; useful prepared stock is a scavenging reward.

### Recipes and bootstrap

All new recipes below are explicitly known-by-default; learned skill gates still
apply. F means Fabrication. Tool tiers are operation requirements, not output
quality or speed multipliers.

| Candidate recipe ID | Gate | Product and operation |
| --- | --- | --- |
| `assemble_crude_anvil` | F1 | Mount the recovered block using timber and fasteners; hand tools, no anvil/forge/welder |
| `assemble_charcoal_forge` | F2 | Build hearth and simple bellows from firebrick, sheet, pipe, timber, leather and fasteners; no heat-dependent construction |
| `forge_tongs` | F2 | Rod and pivot hardware; hammering 1, anvil 1, forge heat 1, metal holding 1 |
| `cut_tool_steel_blanks` | F2 | Prepare and cut suitable bar stock; forge heat 1, metal holding 2, metal saw 1 |
| `make_wooden_workshop_crate` | F2 | Timber, dowels and screws; ordinary hand-tool joinery, with real container capacity |
| `forge_pry_bar` | F2 | Produce existing `pry_bar` from rod stock; hammering 1, anvil 1, forge heat 1, metal holding 2; retained finishing tools |
| `forge_smithing_hammer` | F3 | Produce the first hammering-2 tool from suitable blanks and a wooden grip using hammering 1, anvil 1, forge heat 1 and metal holding 2 |
| `assemble_fabricated_anvil` | F3 | Fit a working face/forming features and a robust stand around recovered mass; hammering 2, anvil 1, forge heat 1, metal holding 2; retained fitting tools |
| `forge_wood_chisel` | F3 | Produce existing `wood_chisel` from suitable steel and handle stock; hammering 2, anvil 1, forge heat 1, metal holding 2; retained finishing/handle tools |

The chisel's actual consumers include existing `shape_timber_dowels`,
`make_quarterstaff` and the crate. The pry bar already has a functional
melee profile; this brief does not claim a new prying action.

Revise both existing `arming_sword` producers:

- `make_current_arming_sword` in `data/crafting/workshop_forging.json5`.
- `make_salvaged_arming_sword` in `data/crafting/low_tech.json5`.

Both require F4, **anvil 2, hammering 2, metal holding 2 and forge heat 1**.
Both consume a continuous `spring_steel_bar` plus appropriate grip/binding stock,
with retained cutting/finishing tools. Keep both recipe IDs; their current versus
legacy grip/binding inputs can remain different. Arbitrary current/obsolete scrap
does not remain an alternative blade-stock bypass. Revise names and descriptions
alongside inputs. Existing machete, quarterstaff, coil-spear and steel-spear routes
retain their earlier roles.

The first hammer needs only hammering 1; the fabricated anvil needs only anvil 1.
Finding either upgraded provider skips that construction task. Finding all the
equipment still leaves the product's learned skill, stock and fuel requirements.

### Placed objects

Proposed owner: `data/objects/workshop_forging.json5`.

| Candidate object ID | Capability | Backing/recovery |
| --- | --- | --- |
| `crude_anvil` | Anvil 1 | `crude_anvil_kit`; ordinary W1 recovery |
| `charcoal_forge` | Forge heat 1; W2 fuel/readiness | `charcoal_forge_kit`; cold, empty and idle to pack |
| `fabricated_anvil` | Anvil 2 | `fabricated_anvil_kit`; ordinary W1 recovery |
| `workshop_anvil` | Anvil 3, including lower-tier work | Fixed professional find; no early manufacture or packing item |

Professional tier 3 provides the accepted found-equipment ladder and covers
tier-2 jobs; W3A does not require tier 3. Generated forges start cold and empty.
Packing items supply no carried station traits.

## 4. W3B: electrical assemblies and useful welding

### New-item tracker

Owner: `data/items/workshop_electrical.json5`.

| Candidate ID | Represented unit | Practical role / construction band | Proposed acquisition |
| --- | --- | --- | --- |
| `sunglasses` | One pair | Ordinary eyewear; input to improvised protection, no welding trait | Clothing/everyday supplies; outdoor-shop supplies |
| `improvised_welding_goggles` | One assembled pair | Welding protection 1; Fabrication 1 from three sunglasses plus frame/bindings | Craft |
| `welding_filter` | One suitable filter insert | Critical prepared component for a proper mask | Hardware work-protection stock; workshop/auto-repair spares |
| `welding_mask` | One complete mask | Welding protection 2; Fabrication 2 using the filter, shell and straps | Craft; workshop/auto-repair protection stock |
| `salvaged_welder_power_module` | One working recovered power-stage assembly, including its heavy output connections | Enables assembly without manufacturing high-current electronics | Workshop/auto-repair equipment spares |
| `electrical_lead_set` | One set of prepared insulated connections | Useful assembly input for the pack and makeshift welder; Electronics 1 | Craft; electrical parts stock |
| `makeshift_arc_welder` | One assembled corded welder | Welding 1; Electronics 3 / Mechanics 2 | Craft from the working power module |
| `arc_welder` | One complete regulated corded welder and leads | Welding 2, also covers 1; better found equipment | Workshop/auto-repair ground equipment |
| `welding_electrode_packet` | One packet of compatible electrodes | Consumed operating input for welded products | Hardware supplies; workshop/auto-repair supplies |
| `welded_workshop_rack_kit` | One set of welded rack subassemblies and assembly hardware | Place a useful storage rack; Fabrication 2 | Craft |
| `workshop_battery_pack` | One housed recovered-cell pack | Useful rechargeable D1 storage; Electronics 2 | Craft; electrical/workshop bulk equipment |

The recovered power module represents a working assembly rather than generic
`transformer`, small-signal components or arbitrary copper becoming a welder.
Ordinary salvaged fan, casing and input/control connections complete the device.
Manufacturing the regulated found welder remains the accepted Electronics-5 /
Mechanics-3 later band.

The initial masks/goggles are passive trait providers. Existing clear
`safety_goggles` and `welding_gloves` do not substitute for the optical protection
trait. All crafting protection uses the accepted accessible/shared-tool rules.

### Recipes and useful learning

E means Electronics; M means Mechanics; F means Fabrication.

| Candidate recipe ID | Gate | Product and operation |
| --- | --- | --- |
| `assemble_electrical_lead_set` | E1 | Adapt recovered cable/connectors with ordinary tools and a multimeter; solderless, enabling progress before the first constructed iron |
| `assemble_soldering_iron` | E2 | Existing `soldering_iron` from a recovered heater, cable, copper tip stock and handle/fasteners; ordinary tools/testing, no soldering prerequisite |
| `assemble_workshop_battery_pack` | E2 | Compatible recovered rechargeable cells, leads, casing and solder; soldering 1 and electrical testing 1 |
| `assemble_makeshift_arc_welder` | E3 / M2 | Recovered power module, prepared leads, cooling fan, casing, fasteners and solder; basic soldering/testing and mechanical assembly |
| `assemble_improvised_welding_goggles` | F1 | Three sunglasses plus frame/bindings; no welding operation |
| `assemble_welding_mask` | F2 | Recovered suitable filter with shell and straps; no welding operation |
| `weld_workshop_rack` | F2 | Pipe, sheet, fasteners and electrodes; welding 1 and protection 1, plus stock preparation |
| `weld_fabricated_anvil` | F3 | Alternative producer of W3A's anvil-2 kit using recovered mass and suitable prepared steel; welding 2 and protection 2, plus fitting tools |

Two **existing-recycler revisions** supply useful practice without adding
duplicate recycling targets:

| Existing recipe | Current rule | Proposed rule and reason |
| --- | --- | --- |
| `dismantle_computer_power_supply` | Ungated, no explicit skill training | E1 with matching training: deliberate recovery of usable electrical components |
| `salvage_water_pump` | Ungated M0 training | M1 with matching training: fitted mechanical disassembly; existing motor/tool salvage still provides M0 learning |

These are accepted gameplay-gate changes. Their stable IDs, actual salvage
outputs and work are preserved. `make_steel_spear` already supplies a second M1
activity with its existing F1/M1 gates.

Keep `assemble_wiring_kit` and `solder_electrical_connectors` at their current E0
training, so the starter-power content retains its entry activities.

### Functional differences and resources

- Propose welder starting rates of `energy_per_work: 30000` for makeshift and
  `20000` for regulated equipment: 3 versus 2 energy per base work. Both use
  `store:'grid'`. The makeshift tool has a real operating disadvantage; its
  normal-speed demand also fits the current generator's rate of 4 before other
  loads. Section 9 records work durations and final energy budgets.
- Welding-1 products still require protection 1 when a welding-2 tool is chosen.
  Welding-2 products require protection 2. A found mask can cover either job.
- The battery pack is the small energy-content addition serving E2 learning:
  four existing rechargeable AA cells supply a 4,000-capacity rechargeable
  class-neutral internal store, with `placed_object:'grid_battery_station'`.
  Class-neutral internal storage uses the current crafting compatibility rule.
  Empty cells produce an empty pack; charged inputs transfer only compatible
  existing charge, bounded by capacity. Installation retains the original pack.
  It does not supply the grid while merely carried.
- The pack's final recycler must conserve both material and charge using existing
  accounting. Cell chemistry/manufacture and broader battery classes remain later
  energy-content work.
- `welded_workshop_rack` is a passive packing-backed object with real public
  storage at 120,000 ml. It supplies no hidden crafting capability or
  electricity. Its kit holds bolted-together welded subassemblies; packing
  requires the rack to be empty. Crate capacity is tuned separately and must
  justify its rigid external volume against existing bags/boxes.

## 5. Recipe-bound check

The current eight distinct input types include retained tools; ordinary traits
are capped at four. The authored operation shapes fit without a schema change;
section 9 records counts and physical yields.

| Representative job | Consumed types / retained types | Trait budget |
| --- | --- | --- |
| Forge assembly | Brick, sheet, pipe, timber, leather, fasteners: 6; up to 2 retained finishing tools | Hammering 1, metal saw 1, metal drilling 1, wrenching 1: 4 |
| Forged fabricated anvil | Block, suitable steel blanks, timber, fasteners: 4; hacksaw, file, drill, wrench: 4 | Hammering 2, anvil 1, metal holding 2, forge heat 1: 4 |
| Either sword | Suitable bar, grip stock, binding stock: 3; hacksaw and file: 2 | Anvil 2, hammering 2, metal holding 2, forge heat 1: 4 |
| Makeshift welder | Working module, lead set, fan, sheet casing, fasteners, solder: 6; pliers: 1 | Soldering 1, electrical testing 1, screwdriving 1, wrenching 1: 4 |
| Welded rack | Pipe, sheet, fasteners, electrodes: 4; wrench and file: 2 | Welding 1, protection 1, metal saw 1, metal drilling 1: 4 |
| Welded fabricated anvil | Block, suitable steel blanks, timber, fasteners, electrodes: 5; wrench and file: 2 | Welding 2, protection 2, metal saw 1, metal drilling 1: 4 |

Retained tools express specific operations where the four trait slots are already
occupied. Keep ingredient quantities/material suitability coherent with those
operations; do not hide a missing stage inside an omnibus workshop trait.

## 6. Learning and knowledge paths

| Advancement | Available useful work |
| --- | --- |
| Fabrication 0 -> 1 | Existing timber preparation and wooden clubs |
| Fabrication 1 -> 2 | Existing quarterstaff/machete/steel-spear work; new crude-anvil mounting; W3B goggles |
| Fabrication 2 -> 3 | Tongs, tool blanks, crates, pry bars and forge assembly; W3B masks/racks |
| Fabrication 3 -> 4 | Smithing hammer, fabricated anvil and chisel manufacture; W3B welded-anvil route |
| Electronics 0 -> 1 | Existing wiring-kit assembly; soldered connectors with a found iron |
| Electronics 1 -> 2 | Solderless lead sets; revised power-supply dismantling |
| Electronics 2 -> 3 | Iron assembly; recovered-cell pack assembly |
| Mechanics 0 -> 1 | Existing appliance-motor and simple tool dismantling |
| Mechanics 1 -> 2 | Existing fitted steel spear; revised water-pump dismantling |

Each row has a real source at the required training level: an r-level source
teaches only through r+1. Found stations/tools grant no construction XP, but the
stock-preparation, storage and tool/product recipes remain available for practice.
Repeated work is acceptable; later passes can widen useful choices.

At current default learning rates, without study:

| Goal | Total ordinary XP from level 0 | Eligible base work at 0.01 XP/work |
| --- | ---: | ---: |
| Fabrication 4 | 1,500 | 150,000 |
| Electronics 3 | 700 | 70,000 |
| Mechanics 2 | 300 | 30,000 |

Work must be distributed among eligible sources, not accumulated indefinitely on
an early recipe. A single copy of every new station/tool is not promised to grant
these levels. Section 9 quantifies repetition/material/fuel costs for both found
and crafted routes; comfortable pacing is not yet claimed.
Use existing book benefits as optional acceleration under their once-per-character
rules; this brief proposes no global XP or automatic construction-level grants.

Recommend explicit known-by-default knowledge for the initial W3 recipes, including
the revised swords. Equipment, stock, skill and operating resources provide their
gates. Existing coil-spear reference/autolearn behavior remains its own contract.
Advanced information-gated production is still owned by later content/D3 passes.

## 7. Acquisition coverage

Proposed placements build on inspected current contexts:

| Context and current route | Proposed addition / coverage target |
| --- | --- |
| `hardware_store`: `loot_tools`, `loot_hardware`, ground `loot_workshop_bulk_ground`; bulk-tool basement | Dedicated ground stock for blocks/firebrick/charcoal; ordinary mounting tools and fasteners; filters/electrodes where container bounds permit |
| `workshop`: fabrication benches, electrical bench, protection locker, parts basement | One fixed professional anvil and one cold/empty forge per qualifying workshop; tongs/hammer opportunities; suitable steel, electrical spares, electrodes and regulated welder opportunities |
| `auto_repair`: service benches, protection locker, ground equipment | Suitable recovered spring-steel bars; working welder modules or complete welders; filters/masks/electrodes; existing pump/motor stock for learning |
| `electronics_store`: tool/electronics bench and spares basement; `loot_electronics` -> `loot_electrical_parts` / `loot_computer_parts` | Leads, cells and pack opportunities; reuse existing heaters, cable, connectors, solder and power-supply salvage |
| `clothing_store` / outdoor-shopping contexts | Sunglasses through actual everyday/outdoor supply placements; charcoal in suitable ground-only camping stock |
| Residential/furnished-house workshop and storage pools | Existing early tools/materials; bounded optional shared workshop additions |

A workshop's fixed stations do not guarantee all stock, fuel, portable tools,
knowledge or skills for a sword. No full per-hardware-store bootstrap bundle is
proposed here. Risky found equipment and component finds provide distinct rewards.

Keep bulky equipment and stock in ground-only pools. The current broad tool,
hardware and electrical pools have furniture-capacity contracts; nested count
maxima matter even for low-probability entries. A pool draw selects an alternative,
so any later guaranteed bundle needs separate draws/placements.

Town and FirstHouse load shared definitions, but their actual placement coverage
differs. FirstHouse keeps its guaranteed D1 starter cache and optional household
workshop supplies. It does not acquire a guaranteed complete forging/welding
tutorial merely by loading these items. Town adds the named specialist buildings;
not every generated town contains every building type.

Proposed quantitative follow-up: trace at least one crafted and one found forging
route and one assembled and one found welding route from actual generated stock.
Record seed/site coordinates and all practice supplies when that validation runs.
The present inspection is evidence of source structure, not completed playthroughs.

## 8. Integration, conservation and compatibility

Proposed semantic files:

- W3A: `items/workshop_forging.json5`, `objects/workshop_forging.json5`,
  `crafting/workshop_forging.json5`, `worldgen/loot/workshop_forging.json5`.
- W3B: matching `workshop_electrical.json5` files under those directories.
- Upgrade existing item definitions and recipes in place. Include the shared
  sources in `data/content.json5` and `data/first_house.json5`.
- Town currently loads both sword producers; FirstHouse loads the current producer
  but not `crafting/low_tech.json5`. Validate both packs without introducing a
  duplicate recipe or an unnecessary new dependency on the legacy source.

Audit every producer/recycler of a selected output. In particular:

- The two anvil producers share one recycler; bound its returns against both
  production routes. Check current recycling coverage for `pry_bar`/`wood_chisel`
  before adding their manufacturing recipes.
- Suitable bar/blank salvage must not let arbitrary scrap become blade-quality
  stock. Returned material quantities cannot exceed the consumed material budget.
- Completed-product recycling does not return used electrodes, solder or forge
  fuel. Cancellation retains existing refunds of the actual reserved ingredients;
  spent heat/electricity stays spent.
- Manufactured storage/power equipment starts from the actual recipe inputs:
  no free cell charge, duplicate backing item or initially fueled generated forge.

Generation policy: retain version 7. Authoring changes content and uses existing
placement APIs. Generated results depend on the revised catalog as well as
seed/version; saved chunks retain their actual contents. No runtime schema,
save-envelope, LAN-protocol or generation-algorithm changes are needed.

**Content-upgrade policy: finish or cancel affected sword jobs with the prior
catalog before upgrading an existing save, or start a new world.** Snapshot
validation in `src/items/item_snapshot.cpp` requires exact reserved input types
and counts. Replacing the swords' scrap inputs with bars is therefore incompatible
with an old unfinished sword under the new catalog, even with a stable recipe ID.
Reserved scrap is not silently transmuted into suitable steel. Unaffected saves
and materialized equipment use the normal save contract. A data-only roster does not itself
require new runtime payload fields; content fingerprints change normally.

## 9. Authoring and integration record

The two slices use the eight semantic sources listed in section 8. Both Town and
FirstHouse include them; FirstHouse does not add the legacy sword recipe source.
All new recipes explicitly use default-known knowledge, with ordinary skill
training at their required levels. No global learning rate changes are made.

All 22 new items use canonical external RGB text art at
`data/art/items/<id>/art.txt`. Monochrome masters are kept locally under `/out`.
Reused IDs keep their existing illustrations. The original voice combined familiar
workshop objects, patient craftsmanship and useful salvage; **no deliberate
humor IDs** were selected. Future authoring and revisions follow the
[plain-default description policy](item_authoring.md#description-voice-plain-by-default),
not that earlier voice brief. Category/material flags remain descriptive metadata.
Protection is a crafting trait, not an added wearable armor effect or diagnostic
action.

### Physical and salvage tracker

Every row is integrated with its recipe/loot route from sections 3–7 and its
canonical illustration. Mass/volume describe one represented unit, not loose
members of a packet. Recycler returns below are complete; a dash is a deliberate
no-recycler decision.

| Item ID | g / external ml | Completed-product recycling |
| --- | ---: | --- |
| `solid_steel_block` | 20000 / 3000 | 150 steel scraps |
| `refractory_brick` | 2500 / 1400 | —; useful intact stock |
| `charcoal_charge` | 1000 / 4000 | —; fuel, recovery disabled |
| `crude_anvil_kit` | 24000 / 28000 | Block + 10 wood scraps + 1 steel scrap |
| `charcoal_forge_kit` | 27000 / 45000 | 6 bricks + 30 steel scraps + 5 wood scraps |
| `forging_tongs` | 1500 / 1200 | 12 steel scraps |
| `smithing_hammer` | 1300 / 850 | 10 steel scraps |
| `fabricated_anvil_kit` | 25000 / 30000 | Block + 8 steel scraps + 10 wood scraps |
| `spring_steel_bar` | 1800 / 400 | 12 ordinary steel scraps; irreversible quality downgrade |
| `tool_steel_blank` | 300 / 80 | 2 ordinary steel scraps |
| `wooden_workshop_crate` | 6000 / 32000 | 20 wood scraps |
| `sunglasses` | 40 / 150 | — |
| `improvised_welding_goggles` | 180 / 500 | 1 plastic scrap |
| `welding_filter` | 100 / 80 | —; prepared optical material |
| `welding_mask` | 650 / 3500 | Filter + webbing + 2 plastic scraps |
| `salvaged_welder_power_module` | 8000 / 5000 | 2 copper wire + 20 steel scraps + transformer |
| `electrical_lead_set` | 400 / 600 | 1 insulated power cable |
| `makeshift_arc_welder` | 10000 / 11000 | Power module + leads + fan |
| `arc_welder` | 12000 / 14000 | Power module + leads + fan + 10 steel scraps |
| `welding_electrode_packet` | 1000 / 700 | —; consumed, recovery disabled |
| `welded_workshop_rack_kit` | 21000 / 36000 | 10 pipes + 2 sheets + 1 bolt packet |
| `workshop_battery_pack` | 750 / 1000 | 4 AA cells + leads + 1 plastic scrap; actual remaining charge only |

There are **19 new explicit recyclers**: ten in forging (including the reused pry
bar) and nine in electrical (including the reused iron). The existing wood-chisel
recycler remains its single recycler, returning one steel scrap. The sword
retains its existing no-recycler policy. Both anvil producers share one recycler;
neither can regenerate suitable stock, fuel, electrodes or solder. Battery
salvage has 4000 total cell capacity, bounded by actual pack charge. Empty input
cells produce an empty pack; found packs are also initially empty.

The block is deliberately heavy working mass, rather than a small ordinary
offcut. The crate's 24000 ml storage fits within a rigid 32000 ml envelope: it
costs more empty carrying volume than the 24000 ml collapsible canvas duffel.
The rack supplies 120000 ml only after placement. The smithing hammer's
14 blunt damage / 130 attack cost trades speed against the claw hammer's
12 / 110, while its real advantage is hammering 2. Other new items use ordinary
improvised melee defaults; sword, pry-bar and chisel combat profiles are preserved.

### Authored work and resource budgets

| Recipe | Consumed g → product g | Base work | Operating resource |
| --- | ---: | ---: | --- |
| `assemble_crude_anvil` | 24200 → 24000 | 2400 | None |
| `assemble_charcoal_forge` | 27880 → 27000 | 5000 | None |
| `forge_tongs` | 1800 → 1500 | 3200 | 3200 heat |
| `cut_tool_steel_blanks` | 1800 → 1500 (five blanks) | 1800 | 1800 heat |
| `make_wooden_workshop_crate` | 6800 → 6000 | 2400 | None |
| `forge_pry_bar` | 1600 → 1200 | 3000 | 3000 heat |
| `forge_smithing_hammer` | 1400 → 1300 | 4800 | 4800 heat |
| `assemble_fabricated_anvil` | 25600 → 25000 | 6000 | 6000 heat |
| `forge_wood_chisel` | 500 → 220 | 3000 | 3000 heat |
| Both sword recipes | 2400 → 1800 | 6000 | 6000 heat |
| `assemble_electrical_lead_set` | 410 → 400 | 1800 | None |
| `assemble_soldering_iron` | 1100 → 250 | 4500 | None |
| `assemble_workshop_battery_pack` | 820 → 750 | 4000 | 4000 grid energy |
| `assemble_makeshift_arc_welder` | 10820 → 10000 | 9000 | 9000 grid energy |
| `assemble_improvised_welding_goggles` | 520 → 180 | 1200 | None |
| `assemble_welding_mask` | 700 → 650 | 2400 | None |
| `weld_workshop_rack` | 21800 → 21000 | 6000 | 18000 makeshift / 12000 regulated grid energy |
| `weld_fabricated_anvil` | 27600 → 25000 | 12000 | 24000 regulated grid energy |

The welded anvil uses the same block, four blanks, two planks and two bolt packets
as the forged anvil, plus two electrode packets. Large recovered heater/cable
units are trimmed for the small existing iron; discarded mass is not a second
product. The rack uses twelve pipes, three sheets, two bolt packets and one
electrode packet. Counts remain below the eight-input/four-trait bounds.

One charcoal bag yields 6000 heat; capacity is 24000 and ignition costs 1000.
A single bag supports tongs plus one blank-cutting batch exactly after ignition
(3200 + 1800), exhausting it. A sword needs two fresh bags and leaves 5000 heat.
Keep fuel positive and replenish before exhausting it to avoid another ignition.
There is no idle burn. At the generator's four-energy rate, either welder fits
ordinary one-work-per-tick demand, before other loads. One rack costs three
6000-energy generator packets with the makeshift tool, two with the regulated
tool; the higher-tier anvil costs four. Cancelled work does not refund spent
heat/electricity.

### Unaided practice accounting

These explicit no-study schedules quantify the investment, not a claim of a
comfortable single-town playthrough. Eligible work stops teaching at its source
ceiling; small overshoots cannot substitute for higher-level practice.

| Path | Work at each learned level | Material/fuel implications |
| --- | --- | --- |
| Crafted forging | F0: 20 clubs (10000); F1: crude anvil + 8 machetes (21600); F2: forge + tongs + 8 blank batches + 6 pry bars (40600); F3: hammer + fabricated anvil + 24 chisels (82800) | 8 bars yield 40 blanks; hammer/anvil/chisels consume 32. Add one blade bar for the sword. Heated work through the sword is 124400; one uninterrupted ignition needs 21 charcoal bags, leaving 600 heat |
| Found forging | F0: 20 clubs (10000); F1: 9 machetes (21600); F2: 6 blank batches + 10 pry bars (40800); F3: 27 chisels (81000) | Found anvil/forge/tongs/hammer skip their construction. Six bars yield 30 blanks, 27 used by chisels; another bar supplies the sword. Heated work through the sword is 127800; one ignition needs 22 bags, leaving 3200 heat |
| Electrical assembly | E0: 25 wiring kits (10000); E1: 12 lead sets (21600); E2: first iron + 9 battery packs (40500) | Leads need 12 cables/48 connectors. Packs need 36 cells, 9 leads, 18 plastic scraps and 9 solder; pack work costs 36000 energy. Welder assembly then needs another lead, working module, fan, sheet, bolts and solder, plus 9000 energy |
| Mechanical assembly | M0: 13 appliance motors dismantled (10400); M1: 7 fitted steel spears (22400) | 13 motors; spears need 7 each of rod, pipe, wire and wood scrap, plus reusable tools. Spear work also trains F1; the forging-only examples above do not assume this saving |

The crafted forging schedule additionally consumes two blocks, eight firebricks,
two sheets, one pipe, five planks for stations, four leather patches, fourteen
rods, five bolt packets, 80 steel scraps, eight steel-wire units and 35 wood
scraps through the sword (including its grip). The initial 20 club planks are
separate; scrap wood can come from additional timber preparation or salvage.
The found schedule consumes 20 club planks, 90 steel scraps, nine steel-wire
units, twenty rods and 38 wood scraps through the sword, plus its copper binding.
The crafted schedule also needs that binding. These are gross inputs without
recycling credits. Repeated output, found tools, study and alternate practice
remain legitimate choices; broader acquisition/pacing playtesting remains useful.

The generated workshop places `workshop_anvil` at local `(3,7,0)` and
`charcoal_forge` at `(5,7,0)`, with a common working tile `(4,7,0)`. New loot pools
are ground-only, including compact components, keeping existing furniture draw
bounds independent of the new stock. Hardware, workshop, auto repair, electronics,
clothing, sporting goods and warehouse placements use the dedicated pools.
Shared workshop/camping ground pools also provide optional FirstHouse finds.
These are weighted opportunities, not complete starter bundles.

### External SDL artwork handoff — Ready for artwork

The inventory illustrations are separate from map sprites. The five new objects
currently use the existing generic-object presentation: terminal `*`/`&` and SDL
`sprite.object.enabled`/`sprite.object.disabled` fallbacks. Inspection supplies
their actual names, capabilities, storage and forge state.

| Stable object ID / name | Materials and silhouette | Facing, states and reference |
| --- | --- | --- |
| `crude_anvil` / crude anvil | Dark square recovered steel block strapped to a rough timber stand; emphatically rectangular, no horn | Four ordinary facing orientations, no connections. Reference `crude_anvil_kit` text art; passive fixed appearance |
| `charcoal_forge` / charcoal forge | Low pale firebrick hearth, dark steel rim, side pipe and folded leather hand bellows on timber supports | Four orientations, no connections. Reference `charcoal_forge_kit`; base sprite depicts a cold empty hearth. Cold/Ready/fuel remain inspectable; a dedicated hot-state mapping is not part of the current renderer contract |
| `fabricated_anvil` / fabricated anvil | Fitted steel face and short forming projection above a braced timber stand, visible bolts and seams | Four orientations; reference `fabricated_anvil_kit`. More finished than the block, less monolithic than the professional find |
| `workshop_anvil` / workshop anvil | Heavy professional horned anvil, broad feet, smooth worn face; pre-collapse permanent fixture | Four orientations; fixed, no packing item. Visually strongest anvil tier; no powered or animated state |
| `welded_workshop_rack` / welded workshop rack | Open steel-tube uprights and three sheet shelves with bolted corners; empty space visible between shelves | Four orientations; reference rack-kit frames plus existing store-shelf proportions. Storage contents do not require individual sprite variants |

Map sprites should follow `assets/tiles/gen1_32x32`'s scale, lighting and ground
anchor. Integrate supplied assets through stable object IDs in `tileset.json5`;
optional mappings keep smaller packs loadable. Representative palette entries are
`A:{terrain:'floor_concrete',object:'workshop_anvil',facing:'north'}` and
`F:{terrain:'floor_concrete',object:'charcoal_forge',facing:'north'}` in the workshop.
All five occupy one blocking tile without sight occlusion. Item map sprites can
follow their canonical text-art silhouettes and retain generic item fallback until
the external artwork pass. New dedicated SDL sprites are not supplied by W3.

## 10. Verification

Verified on Windows with the Debug and SDL presets:

```powershell
./scripts/build.ps1 -Preset win-msvc-debug -SkipTests
./scripts/build.ps1 -Preset win-msvc-sdl -SkipTests
./scripts/test.ps1 -Preset win-msvc-debug -R '^(W3 |Workshop acquisition|Workshop stations have|Generated town workshop)'
./scripts/test.ps1 -Preset win-msvc-debug -CTestArgs @('-R', '(Content|content|Town|town|urban|Urban|[Ff]orge|[Cc]rafting|[Rr]ecycl|[Pp]ower|[Gg]rid|[Ss]tation|[Ww]orkshop|[Ss]ave|[Ss]kill|[Ii]tem expansion|[Ii]tem pilot|[Ii]tem slice|Current cloth|Current coil)', '-E', '^(W3 |Workshop acquisition|Workshop stations have|Generated town workshop|headless_lan_roundtrip|sdl_lan_roundtrip|save_client_roundtrip)')
./scripts/test.ps1 -Preset win-msvc-debug -R '^Town slice publishes sixty complete current definitions and retires the old catalog$'
$env:CATACLYSMX_WORKSHOP_PREVIEW = "$PWD/out/workshop"
./scripts/test.ps1 -Preset win-msvc-sdl -R '^SDL item slice expansion and workshop'
./scripts/test.ps1 -Preset win-msvc-sdl -R '^(sdl_frontend_smoke|sdl_town_default_smoke|headless_lan_roundtrip|sdl_lan_roundtrip)$'
git -c core.safecrlf=false diff --check
```

- Both builds passed. **16/16 workshop tests passed**: thirteen content/gameplay
  cases and three acquisition cases. Initial fixtures needed an ungated E0
  assertion and ground storage for bulky practice outputs; both were corrected.
- Regression selection: **310/311 passed initially**. Its sole failure expected
  only the eleven forging additions. The corrected **203-item** Town count
  passed the focused rerun (**1/1**); no unresolved selected failures remain.
  The entire suite was not run.
- The SDL browser test passed (**1/1**) and rendered all 22 new items at 480×360
  and 1120×900, with pixel/scroll checks. Forty-four previews remain under `/out`.
  Both full-size contact sheets and representative compact views were inspected;
  compact panes use ordinary vertical scrolling.
- SDL FirstHouse/Town smoke and SDL/headless LAN roundtrips passed (**4/4**).
  Tracked whitespace checks passed. No tooling tests were added or run.

`tests/workshop_content_tests.cpp` demonstrates authored construction and
found-equipment routes, actual F0→4/E0→3/M0→2 learning without administrative
level grants in the learning cases, both sword gates, accessible protection,
paid heat across pause/disk reload/cancellation, grid refusal/cancellation, rack
storage/recovery and battery charge/identity across crafting, installation,
removal and recycling. These controlled fixtures provision raw stock and grid
supply; they establish functionality, not a complete scavenging playthrough.
The separate acquisition cases validate named placements, ground-only pool
bounds, four rotated station approaches and generated stock.

### Generated-stock evidence

`Generated town workshop stock and cold forge have traceable sites` scanned
region `(0,0)` at seeds 0–31 with the authored catalog and generation 7.
Representative first observations (coordinates are X,Z,Y):

| Find | Seed | Site / world coordinate |
| --- | ---: | --- |
| Cold, empty forge | 0 | Workshop `(118,174,0)`; materialized runtime fuel 0, Ready false |
| Block and refractory brick | 1 | Workshop `(142,139,0)` |
| Charcoal and suitable steel bar | 0 | Workshop `(113,176,0)` |
| Tongs | 7 | Workshop `(113,83,0)` |
| Smithing hammer | 1 | Auto repair `(118,75,0)` through shared optional bulk stock |
| Filter | 3 | Hardware store `(108,140,0)` |
| Electrodes | 2 | Workshop `(107,142,0)` |
| Mask | 0 | Workshop `(113,176,0)` |
| Recovered power module and regulated welder | 1 | Auto repair `(69,133,0)` |
| Leads and battery pack | 0 | Electronics store `(199,115,0)` |
| Sunglasses | 0 | Clothing store `(194,136,0)` |

The sample contains 18 blocks, 63 bricks, 37 charcoal bags, 31 bars, six tongs,
three smithing hammers, 21 filters, 34 electrode packets, eleven masks,
18 modules, 42 leads, five regulated welders, 42 sunglasses and twelve packs.
These totals span **different worlds**; they do not form one player's recipe kit.
Detailed successful assertions are local at `out/workshop-acquisition-evidence.txt`.
Single-world scavenging routes with all repeated-practice supplies remain a
playtesting follow-up, alongside acquisition weights and learning pace.
