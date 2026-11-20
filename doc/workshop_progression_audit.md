# Basic workshop progression: static audit

## Scope and evidence

This is the fresh-world D5 prerequisite and budget audit, not a scavenging
playthrough, an optimal route, or evidence that a particular world supplies the
listed stock. It uses the accepted
[D5 direction](urban_workshop_progression.md#d5--integration-contract-and-content-roster),
[delivery plan](crafting_delivery_plan.md), [workshop roster](workshop_rosters.md),
[renewable power](renewable_power_design.md), and
[home printing](home_printing_design.md), checked against the data and learning
implementation listed below. The [integration results](workshop_integration.md)
report generated acquisition and the resulting ground-stock tuning. No agent
playthrough was run; focused runtime regression tests are recorded there.
Use new worlds.

The accepted pass-5 screwdriver fallback is included as an integration dependency:
`makeshift_screwdriver`, 90 g / 100 ml, `screwdriving` 1; consume one
`steel_scrap`, 1,000 work, default-known, ungated Fabrication-0 training.
The shipped content replaces all thirteen exact retained `flathead_screwdriver`
recycler requirements with `screwdriving` 1.
When a schedule uses it, add one steel scrap and 1,000 work, or deliberately
replace two F0 club jobs with that eligible work and recompute the plank bill.
The independent baseline tables below retain the older club-only F0 schedule.

### Reproduction rules

- Start each independent schedule with learned level 0, zero XP and no study
  boosts unless stated otherwise. Supply the named reusable tools and raw stock;
  obtaining, transporting and defending them is **not** included in work totals.
- Ordinary matching training gives `work / 100` XP. Successive thresholds through
  level 4 are 100, 200, 400, 800 XP: eligible work requirements are respectively
  10,000, 20,000, 40,000, 80,000. Finish every listed job; surplus at its teaching
  ceiling is discarded. A level-r source teaches only through r+1.
- Counts below are **gross external inputs**: no cancellation, dismantling,
  recycling, incidental earlier loot or byproduct credit. Prepared blanks/leads
  made within a schedule are not counted again as external inputs.
- Manual work is base actor labor. At normal speed, 100 work/ticks is one game
  second; other speeds change duration, not the authored work budget. Machine
  processing is elapsed simulation ticks, independent of actor speed. These are
  compressed game units, not real-world working times or watts.
- Forge heat costs one per base work in the selected recipes. One
  `charcoal_charge` gives 6,000 heat; cold ignition costs 1,000, capacity 24,000.
  Budget `ceil((heated work + 1,000) / 6,000)` bags for one uninterrupted
  ignition. Refill **before** zero without exceeding capacity; loading all bags
  simultaneously is impossible. No idle burn is charged.
- `soldering_iron` costs 1 grid EU/work; `makeshift_arc_welder` costs 3 and
  `arc_welder` 2. Construction involving no powered provider is electrically free.
  Material solder/electrodes are separate from electricity. Failed/cancelled work
  does not refund spent power or heat.
- Wiring is additional site-dependent stock: reusable `wiring_kit`, one
  `copper_wire` per 16 newly covered tiles rounded up, plus one per installed
  battery. Tables do not silently include a particular workshop footprint.

Authoritative inputs:
[`workshop_forging.json5`](../data/crafting/workshop_forging.json5),
[`workshop_electrical.json5`](../data/crafting/workshop_electrical.json5),
[`weapons.json5`](../data/crafting/weapons.json5),
[`power.json5`](../data/crafting/power.json5),
[`mechanical_components.json5`](../data/crafting/mechanical_components.json5),
[`materials.json5`](../data/crafting/materials.json5),
[`electronics.json5`](../data/crafting/electronics.json5),
[`digital_fabrication.json5`](../data/crafting/digital_fabrication.json5),
[`starter.json5`](../data/books/starter.json5), and
[`skill_state.cpp`](../src/skills/skill_state.cpp)
(`TrainSkill`, `AwardSkillXp`, `CompleteBookStudy`, `LevelUp`).
Machine quantity/learning capture is in
[`machine_system.cpp`](../src/digital/machine_system.cpp); completion awards use
`AwardSkillXp` in [`world.cpp`](../src/core/world.cpp).
Both actual sword producers require Fabrication 4; the introduction of
`skills.md` now reflects that gate.

## Connected capability graph

F = Fabrication, E = Electronics, M = Mechanics, C = Computers. Found equipment
skips **its construction**, not a product's skills, stock, protection or operating
costs. All selected workshop/energy/manual printer-consumer techniques are
default-known; printing uses supplied `home_printer_software` and supplied designs.
No protected information or hacking is needed for these endpoints.

```text
homes / public shops / ordinary specialist premises
  -> hand tools + recoverable stock + optional public manuals
  -> steel_scrap -> makeshift_screwdriver [no tool prerequisite]
  -> screwdriving-1 salvage + pliers -> electrical parts / heaters / cable

cutting + timber -> F0 practice -> F1
  -> crude anvil OR found workshop_anvil
  -> F1 useful work -> F2
  -> cold forge assembly OR found cold/empty charcoal_forge
  -> charcoal + ignition + hammer + pliers -> first tongs
  -> suitable bars -> blanks / pry bars (F2 practice) -> F3
  -> first smithing hammer + fabricated anvil OR found forming tools/anvil
  -> chisels / useful F3 work -> F4 -> forged arming_sword
                                  \-> chisel -> dowels/crates/quarterstaffs

pliers + scrap/wire/tape -> E0 wiring-kit practice -> E1
  -> solderless leads / power-supply salvage -> E2
  -> first iron [no soldering prerequisite] OR found iron
  -> powered battery-pack assembly / iron assembly -> E3
  + M2 + recovered power module -> makeshift welder
  OR found arc_welder -------------------------------------------+
  + protection + electrodes + grid ------------------------------+
  -> rack storage (F2) / gasifier (F3,M2) / welded anvil (F3,tier2)

motor/tool salvage -> M1 -> fitted spears/pump salvage -> M2
  -> gasifier work (+ welder assembly where chosen) -> M3
  -> generator drives -> M4
  + E3 + recovered engine + alternator + drive + gasifier + leads
  -> integrated woodgas generator -> prepared wood -> night electricity

E2 + M1 + panel/controller/cable/mounting -> solar kit
  OR found complete solar/woodgas kit -> install/start without construction skill
  -> wiring -> direct supply / installed batteries -> irons, welders, printer

found home printer + supplied designs + filament + grid
  -> C0 cups -> C1 parts cases / boxes / buckles / grips
  -> C2 electronics cases -> C3 practice ceiling
  -> E2 + cells + leads + solder + printed case -> battery pack
  -> F1 + steel + printed grip -> machete
  -> Tailoring1 + cloth/webbing/thread + printed buckles -> duffel
```

### Stable providers, substitutions and remaining find dependencies

| Capability | Actual providers / operation boundary |
| --- | --- |
| `house_cutting` 1 | `hacksaw`, `retractable_utility_knife`, `chef_knife`, `tin_snips`; clubs, timber and wood-fuel preparation accept a trait |
| `hammering` 1 / 2 | `claw_hammer` / `smithing_hammer`; the first smithing hammer uses tier 1 |
| `metal_holding` 1 / 2 | `combination_pliers` / `forging_tongs`; first tongs accept pliers |
| `metal_saw` 1 | `hacksaw` |
| `metal_drilling` 1 | `hand_drill` |
| `wrenching` 1 / 2 | `adjustable_wrench` / `socket_wrench_set`; tier 2 covers tier 1 traits |
| `screwdriving` 1 | `flathead_screwdriver` or accepted `makeshift_screwdriver`; also the thirteen converted recycler operations |
| `electrical_testing` 1 | `multimeter`; a reusable found dependency, not manufactured by this slice |
| `soldering` 1 | Found or assembled `soldering_iron`, funded from worker grid |
| `welding` 1 / 2 | `makeshift_arc_welder` / found `arc_welder`; higher tier covers lower jobs |
| `welding_protection` 1 / 2 | `improvised_welding_goggles` / `welding_mask`; three sunglasses + plastic + tape make the first, a suitable filter + plastic/webbing/bolts makes the second |
| `anvil` 1 / 2 / 3 | Placed `crude_anvil` / `fabricated_anvil` / fixed found `workshop_anvil` |
| `forge_heat` 1 | Placed, fueled and ignited `charcoal_forge`; packed kit has no carried station capability |
| Explicit retained operations | `metal_file`, `hacksaw`, `hand_drill`, `adjustable_wrench`, `retractable_utility_knife`, `claw_hammer`, `wood_chisel`, `combination_pliers` remain exact IDs where recipes say so |

Trait substitution does **not** replace an exact retained ingredient. For example,
`socket_wrench_set` can satisfy a wrenching trait but cannot replace the retained
`adjustable_wrench` in `forge_tongs` or `assemble_fabricated_anvil`. Protection is
ordinary accessible/shared tool capability, not a worn-equipment prerequisite.
Clear `safety_goggles`, `sunglasses` alone and `welding_gloves` do not supply
`welding_protection`. The accepted goggles are a game recipe, not real-world
protective-equipment advice.

### Acquisition edges (opportunities, not guarantees)

| Needed edge | Actual pool and placement contexts |
| --- | --- |
| Ordinary hand tools / raw stock | `loot_tools` -> `house_tools` / `loot_hand_tools`; `loot_hardware` -> `house_workshop`, `loot_metal_stock`, `loot_fasteners`; hardware shops, workshops, repair premises and household workshop/storage contexts |
| Blocks, brick, charcoal | `loot_forging_hardware_ground` in hardware shops; `loot_forging_workshop_ground` in workshops and shared bulk stock; `loot_forging_camping_ground` through camping/outdoor stock |
| Bars, blanks, tongs, hammer, anvil kits | `loot_forging_workshop_ground`; `loot_forging_auto_ground` adds suitable bars/blanks at auto repair; city foundry stock supplies additional opportunities |
| Found stations | `workshop.json5` places professional anvil and charcoal forge; forge is cold/empty. It is not a stocked, fueled tutorial bundle |
| Welder/module/filter/electrodes | `loot_welding_workshop_ground` in workshop/auto repair; `loot_welding_hardware_ground` in hardware; sunglasses have separate clothing/outdoor opportunities |
| Cable/connectors/heater/cells/iron | `loot_electronics` -> `loot_electrical_parts` / `loot_computer_parts`, electronics retail and household/repair stock; toaster salvage yields two heaters and one cable; `loot_workshop_electrical_ground` supplies leads/cells/packs/controller |
| Early M practice | `loot_auto_repair` includes `appliance_electric_motor` and `water_pump`; city fabrication stock adds both. Fitted spear stock comes through hardware/metal-stock pools |
| Renewable assemblies / complete kits | `loot_workshop_bulk_ground` includes panel, engine, alternator, gasifier, complete solar/woodgas kits and prepared wood; hardware/workshop/auto repair/warehouse ground and optional furnished-house workshop stock. Controller also in electrical ground stock |
| Public information | `loot_reading_room_supplies` includes both workshop books; library, bookstore, school, apartments/houses and offices. `loot_city_workshop_references` adds ordinary industrial/service references. Ebook reader also occurs in `loot_electronics` |
| Printer / filament | Electronics store: installed printer plus two 100–200-portion draws; school: printer plus one 100–200 draw; selected duplex hobby pantry: printer plus 50–100; selected furnished-house workshop cellar: independent optional packed printer and small filament draw |

Pool sources are
[`urban`](../data/worldgen/loot/urban.json5),
[`retail`](../data/worldgen/loot/retail.json5),
[`forging`](../data/worldgen/loot/workshop_forging.json5),
[`electrical`](../data/worldgen/loot/workshop_electrical.json5),
[`city industry`](../data/worldgen/loot/city_industry.json5), and
[`printing`](../data/worldgen/loot/digital_fabrication.json5).
Building files under `data/worldgen/buildings/` attach these pools to actual
placements. Nested weighted draws choose alternatives; a pool containing every
ingredient is not a bundle. FirstHouse's curated D1 cache is not Town's starting
inventory. Bulky stock is ground-only; carrying capacity and station approach
geometry still matter.

## Recalculated no-study schedules

These deliberately simple examples preserve useful outputs but often produce many
more than one survivor needs. They prove an authored training chain, not desirable
pacing. Independent branches can share XP and inputs when deliberately sequenced;
do not add every independent table and call it a minimum integrated budget.

### Forging: constructed versus recovered equipment

Use the current copper-bound sword producer `make_current_arming_sword`; the
legacy `make_salvaged_arming_sword` has the same F4/station/heat gates but different
grip/binding stock.

| Learned band | Constructed equipment schedule | Recovered equipment schedule |
| --- | --- | --- |
| F0 -> 1 | 20 `make_wooden_club`: 10,000 work | Same |
| F1 -> 2 | `assemble_crude_anvil` + 8 `make_salvaged_machete`: 21,600 | 9 machetes: 21,600 |
| F2 -> 3 | `assemble_charcoal_forge` + `forge_tongs` + 8 `cut_tool_steel_blanks` + 6 `forge_pry_bar`: 40,600 | 6 blank batches + 10 pry bars: 40,800 |
| F3 -> 4 | `forge_smithing_hammer` + `assemble_fabricated_anvil` + 24 `forge_wood_chisel`: 82,800 | 27 chisels: 81,000 |
| Useful final output | One sword: 6,000 | One sword: 6,000 |
| Total manual work | **161,000** | **159,400** |
| Heated work | **124,400** | **127,800** |
| Fuel including one ignition | **21 bags**, 600 heat left | **22 bags**, 3,200 heat left |
| Electricity | 0 | 0 |

Constructed route gross stock through sword:

| Input | Count | Input | Count |
| --- | ---: | --- | ---: |
| `timber_plank` | 25 (20 clubs + 5 station timber) | `solid_steel_block` | 2 |
| `refractory_brick` | 8 | `steel_sheet` / `steel_pipe` | 2 / 1 |
| `leather_patch` | 4 | `nuts_and_bolts` | 5 |
| `steel_rod` | 14 | `steel_scrap` | 80 |
| `steel_wire` | 8 | `wood_scrap` | 35 |
| `spring_steel_bar` | 9 (8 blank batches + blade) | `copper_wire` | 1 |
| `charcoal_charge` | 21 | Reusable tools | See provider table |

Eight batches yield 40 blanks; hammer + anvil + chisels consume 4 + 4 + 24 =
32, leaving eight. The two blocks are the crude and fabricated anvils, not a
duplicate charge for one station.

Recovered route gross stock is 20 planks, 90 steel scraps, 9 steel-wire units,
20 rods, 38 wood scraps, 7 suitable bars (six batches + blade), 1 copper wire
and 22 charcoal bags. It assumes found forge, anvil of at least tier 2, tongs
and smithing hammer; 30 produced blanks minus 27 chisels leaves three.
Finding stations saves construction stock but **does not** solve F2/F3 practice.
The found schedule uses slightly more fuel because its chosen practice differs.

Neither schedule includes making its wood scraps. If obtained using
`cut_timber_scrap`, add four planks and 800 work for either route, leaving five
or two scraps respectively. Doing this during F0 can replace some club practice;
the figures above intentionally do not claim that saving.

### Electronics through a makeshift welder

| Band | Completed work | Work / grid EU |
| --- | --- | ---: |
| E0 -> 1 | 25 `assemble_wiring_kit` | 10,000 / 0 |
| E1 -> 2 | 12 `assemble_electrical_lead_set` | 21,600 / 0 |
| E2 -> 3 | First `assemble_soldering_iron` + 9 `assemble_workshop_battery_pack` | 40,500 / 36,000 |
| Endpoint, also requires M2 | `assemble_makeshift_arc_welder` | 9,000 / 9,000 |
| Total | Excludes separately acquired M2 and site wiring | **81,100 / 45,000** |

Gross inputs: 150 steel scraps, 26 copper wire, 25 cloth-repair tapes,
13 insulated cables, 48 electrical connectors, 1 heating element, 19 rigid
plastic scraps, 2 bolt packets, 36 rechargeable AA cells, 10 solder-wire units,
1 recovered welder power module, 1 cooling fan and 1 steel sheet. Twelve leads
are produced; nine packs and the welder use ten, leaving two.
Nine packs represent 36,000 capacity, **not** 36,000 created charge: actual
compatible cell charge transfers, and normal found rechargeable cells are empty.

The endpoint uses another 9,000 EU **through the iron**, not through the welder
being constructed. Its first useful `weld_workshop_rack` additionally needs F2,
protection 1, 12 pipes, 3 sheets, 2 bolt packets, one electrode packet and the
retained fitting tools; add 6,000 labor and **18,000 EU**. A found regulated
welder skips E3/M2 construction and pays **12,000 EU** for the same rack. The
placed rack actually provides 120 L public storage.

Found iron alternative: skip constructing an iron, but still need E2 -> 3
practice; ten 4,000-work packs suffice without study (40,000 EU). Found regulated
welder alternative: no need to reach E3/M2 merely to operate it; the chosen
welded product retains its own F/M gates and appropriate protection.
At E1, 29 `dismantle_computer_power_supply` jobs give 20,300 work and useful
parts instead of twelve leads; that is 29 recovered power supplies, not free XP.

### Mechanics through integrated wood gas

This connected example credits the makeshift welder's M2 construction work.
It assumes the independent F route has reached F3 and the E route E3. A found
welder variant lacks that 90 M XP and therefore uses **seven**, not six,
gasifier assemblies at M2.

| Band | Schedule / recipe | Work |
| --- | --- | ---: |
| M0 -> 1 | 13 `salvage_appliance_motor` | 10,400 |
| M1 -> 2 | 7 `make_steel_spear` | 22,400 |
| M2 -> 3 | Makeshift welder assembly (9,000) + 6 `weld_woodgas_processing_assembly` (36,000) | 45,000 |
| M3 -> 4 | 16 `assemble_generator_drive` | 80,000 |
| Final kit | `assemble_woodgas_generator_kit` | 9,000 |
| Total M-path labor | Includes the welder's already-counted labor once | **166,800** |

Gross M-path inputs, excluding the welder's separately listed bill: 13 appliance
motors; 39 rods (7 spear + 32 drive), 31 pipes (7 + 24), 7 steel wire,
7 wood scraps, 64 steel scraps, 25 sheets (24 gasifier + 1 final casing),
6 each mesh/hose/electrode packet, 17 bolt packets (16 drives + kit),
one recovered gasoline engine, one generator alternator and one lead set.
The lead can be one of the electrical schedule's two surplus leads.
One of six gasifiers and one of sixteen drives enter the final kit; five
gasifiers and fifteen drives remain. Spear work also trains F1, but its credit
cannot substitute for F2/F3 sources.

Six gasifiers cost **108,000 EU** with the makeshift welder or 72,000 with a
regulated one; the found-welder seven-gasifier variant costs **84,000 EU**.
Drive and final generator assembly have no powered trait and no grid cost.
Combined with the electrical schedule, the constructed-welder example therefore
needs **153,000 EU before wood gas exists**, excluding other outputs and wiring.
Its first construction cannot fund itself. Mains before permanent day-7–14
shutdown, recovered solar, a found generator, or previously charged installed
batteries are legitimate external starts.

The last kit consumes a **working recovered engine and suitable alternator**;
neither generic `appliance_electric_motor` nor arbitrary small motors substitute.
Finding a complete wood-gas kit skips all construction skills. Finding just a
gasifier/drive bypasses those manufacture steps, but if M4 is still needed,
another eligible training source must replace their lost practice.

For operation, `prepare_wood_from_plank` yields two 1 kg portions for 400 work;
`prepare_wood_from_scrap` yields one for five wood scraps and 250 work.
Each portion delivers 4,000 EU, maximum loaded fuel 24,000 EU. A 12,000-EU
regulated rack costs three portions; two plank jobs produce four portions,
800 labor, leaving one. A makeshift rack costs five portions rounded up,
20,000 available EU, leaving 2,000 fuel EU after the 18,000-EU job.
No standalone gas-container/charcoal-manufacture chain is assumed.

### Solar and a practical pre-woodgas supply

At E2/M1, `assemble_solar_generator_kit` consumes one panel, controller,
insulated cable, bolt packet and pipe: **4,000 labor, zero electricity/heat**.
Retain pliers and provide testing/screwdriving/wrenching 1. A found complete
kit skips these gates. Install outdoors, extend wiring, start, and install an
actual battery if storage is desired. Both renewable installations are fixed,
non-packable after placement, and start stopped; wood gas additionally starts empty.

One explicit novice skill preparation is 25 wiring kits (10,000 work), twelve
leads (21,600), thirteen motor dismantles (10,400), then solar assembly (4,000):
**46,000 labor**, zero electrical construction cost, reaching E2/M1 and producing
the solar kit. Its raw bill is the E0/E1 bills above plus thirteen motors and the
solar recipe's five inputs. Recovering those motors yields useful scrap/wire,
but this gross schedule does not credit it.

Peak output is 1 EU/tick, zero at night, with triangular 06:00–18:00 daylight
and direct-sky gating. Under continuous exposure/run intent with enough demand
or storage, a whole day's potential is
`0.5 * 12 * 3,600 * 100 = 2,160,000 EU`. Day-average rate is 0.25 EU/tick.
This large potential does not imply a small battery retains a day of output.
A 4,000-EU pack needs **at least** 4,000 peak-equivalent ticks (40 game seconds);
actual filling takes longer away from peak or with competing loads.

The 153,000-EU electrical/woodgas construction bill is 7.08% of that ideal daily
potential and 153,000 peak-equivalent ticks (25.5 game minutes). It is not an
elapsed-time promise: use the actual daylight integral, startup phase, demand,
paid slices and storage. The makeshift welder's nominal 3-EU/work demand exceeds
one panel's peak at normal actor speed; storage, additional panels, another
source or waiting for funded work is needed. Nine full packs hold 36,000 EU,
not the whole construction bill; they can be recharged and reused.

### Useful printing and a real consumer

The printer is found working equipment; no printer construction, acquired
schematic, separate laptop or Computers skill is required for the first cup.
All six recipes require supplied `home_printer_software`; the machine supplies
the relevant instructions. Copy/import actions are not practice sources.

| C advancement / endpoint | Batches | Filament portions | Processing EU/ticks | Manual setup |
| --- | --- | ---: | ---: | ---: |
| C0 -> 1 | 10 `print_plastic_cup` | 50 | 12,000 | 400 |
| C1 -> 2 | 10 `print_small_parts_case` | 140 | 24,000 | 400 |
| Useful C2 component | 1 `print_electronics_case` | 21 | 3,600 | 220 |
| Through useful component | 3 jobs, bin emptied between them | **211 (2,110 g)** | **39,600** | **1,020** |
| Optional C2 -> 3 completion | 13 more electronics cases, batches 12 + 1 | 273 | 46,800 | 660 |
| Full C0 -> 3 total | 14 electronics cases overall | **484 (4,840 g)** | **86,400** | **1,680** |

The full schedule deliberately prints the first electronics case separately to
use it immediately: its extra job costs 200 setup work versus grouping all
fourteen as 12 + 2. The latter reproduces the design's **1,480** setup total.
Fourteen cases give nominal 420 XP at C2; 20 are capped. Ten cups give 100 XP,
ten parts cases 200. Setup work itself does not give those rewards; successful
batch completion awards the initiator, including offscreen completion.

Consume the first electronics case using the actual
`assemble_workshop_battery_pack_in_printed_case`: E2, four rechargeable AA
cells, one lead set, one solder wire, soldering/testing 1, **3,500 labor and
3,500 EU**. If the lead is not already available, add
`assemble_electrical_lead_set`: cable + four connectors, 1,800 labor, no power.
Thus from a supplied printer and already qualified trade worker, C0 through the
printed-case battery endpoint costs **43,100 EU** and **6,320 manual work**
(1,020 setup + 3,500 assembly + 1,800 lead preparation), plus the listed filament,
cells/cable/connectors/solder. Its pack starts with actual input charge only.
The cups and cases are functional storage, not pretend progression tokens.

Other actual consumers are `fit_machete_with_printed_grip` (F1, ten steel
scraps + steel wire + grip, hammer/saw/file, 2,200 work) and
`stitch_canvas_duffel_with_printed_buckles` (Tailoring 1, five canvas patches +
webbing + thread + buckles, knife/sewing supplies, 1,800 work). The durable
recipe source is authoritative for stable consumer IDs.

The 12 L bin and expanded 255-input-count bound limit a case batch to twelve
(`21 * 12 = 252` filament). Other empty-bin maxima are cups 30, parts cases 14,
boxes 4, buckle sets 51, grips 25. Clear outputs between batches.
One electronics store's two authored filament draws total **200–400 portions**:
enough only at some draws for the 211-portion battery example, and never enough
for the 484-portion full C3 schedule on those two draws alone. School draws
100–200; household draws 50–100. This is a concrete quantity mismatch with
“one site funds all printer learning,” not proof that such a promise was made.

## Public study alternatives and ceiling semantics

[`starter.json5`](../data/books/starter.json5) contains the actual noncombat books:

| Information identity / carriers | Benefit on the next level earned | Study work |
| --- | --- | ---: |
| `workshop_primer_text`: `workshop_primer_print`, `workshop_ebook_reader` | F next 1–3: 20 direct XP, 2x; E next 1–3: 20 direct, 1.5x; M next 1–4: 20 direct, 1.5x | 100 |
| `salvage_notes_text`: `salvage_notes_print` | F next 1–10: 15 direct XP, 1.5x in ordinary band; 100 theory at advanced eligible levels | 100 |

The primer's F `theory_xp:100` cannot help its authored next-1–3 range: an
advanced F5 character is ineligible. Below learned level 5 it uses its direct
grant/multiplier, not theory. Neither book teaches Computers; public study
currently cannot reduce the printing schedule.

Books apply eligible benefits **once per character and book identity**, not once
per physical copy or once per eligible level. Print and ebook primer are not two
boosts. All eligible skills are processed at that one completion: their benefits
cannot be separately saved for different later levels. The strongest multiplier
wins rather than multiplying; it resets at level-up. Ordinary direct grants share
a 30%-of-current-level cap. Requirements still use learned skill, never boosted
effective skill. Studying is not recipe memorization; both books also remain
references for `make_coil_spear`, whose own F1/reference or F2-autolearn rule is
separate from the default-known routes audited here.
Electronic reading requires positive charge in the reader's own store; reading
currently does not drain it. A printed copy avoids that access condition.

| Explicit alternative (independent timing examples) | Recomputed consequence |
| --- | --- |
| Constructed forging: primer at F2; notes at F3 | Keep F0/F1 above. At F2: forge + tongs + **6** blank batches = 19,000 work; `20 + 190 * 2 = 400`. At F3: hammer + anvil + **14** chisels = 52,800; `15 + 528 * 1.5 = 807`, capped at 800. Through sword: **109,600 labor including 200 study**, **72,800 heat**, **13 charcoal bags**, 4,200 heat left. Seven bars overall, 22 of 30 blanks used; 25 wood scraps rather than 35. Other constructed-route stock unchanged |
| Electrical: primer at E2 | E0/E1 unchanged. Iron + **6 packs** = 28,500 work; `20 + 285 * 1.5 = 447.5`, capped at 400. Through welder: **69,200 labor including study**, **33,000 EU**; 24 cells, 13 plastic scraps, 7 solder units. Other gross inputs unchanged; five of twelve leads remain |
| Mechanics: primer at M3 | Keep M0/M1/M2 connected schedule above. **11 drives** give `20 + 550 * 1.5 = 845`, capped at 800, replacing sixteen. Through kit: **141,900 labor including study**, same gasifier power, ten spare drives; saves ten rods, twenty scraps and five bolt packets |

These are alternatives, **not three independently reusable primer benefits**.
The F2/E2/M3 timings may not coexist: reaching M3 via gasifiers already requires
F3. A joint route must choose when to complete its single primer and recompute
remaining practice. This is why the gross schedules above omit study rather than
pretending a permanent book multiplier applies across every tier.
At learned noncombat 5+, practice and theory must pair; none of the endpoints
here needs that advanced system. Repeating capped low-tier work never substitutes
for the missing higher-tier source.

## Findings and follow-up

### Finite assembled-route acquisition reports

`tools/workshop_report.py` supplements raw single-recipe baskets with these
explicit ordered schedules. The schedules read live exported inputs, outputs,
work, power-provider rates and forge ignition/fuel values; they do not recursively
search for ways to manufacture missing items. The following audit-reference
budgets are independent, already-qualified construction routes except for the
printer's explicitly illustrated Computers sequence:

| Report route | Ordered operations | Reference work / operating budget |
| --- | --- | --- |
| `skilled_found_forging_one_sword` | One timber-scrap preparation, one sword | 6,200 manual work; 6,000 work heat + 1,000 ignition; two charcoal bags |
| `skilled_manufactured_forging_one_sword` | Timber scrap, crude anvil, forge, tongs, two blank batches, hammer, fabricated anvil, sword | 31,200 manual work; 23,600 work heat + 1,000 ignition; five charcoal bags |
| `skilled_assembled_woodgas` | One lead set, drive, gasifier and final generator kit | 21,800 manual work; 18,000 EU with selected makeshift-welder accounting |
| `static_printer_c0_to_c2_battery_consumer` | Ten cups, ten parts cases, one electronics case, one lead set, printed-case battery assembly | 6,320 manual work including setup; 39,600 machine ticks; 43,100 total EU |

The report also exports `static_nostudy_manufactured_forging`,
`static_nostudy_found_forging` and `static_nostudy_electronics_assuming_m2`,
reproducing the practice schedules above. Both forging routes prepend four
timber-scrap preparations, adding four planks and 800 work without reducing
the club count. Their totals are therefore 161,800 and 160,200 work; electronics
remains 81,100 work / 45,000 EU and assumes the separately acquired M2 gate.
They start from the audit's novice trade levels, not the already-skilled
assumption of the first three one-off routes. The report does not simulate XP;
the audit supplies the teaching-band reasoning.

Earlier outputs enter an intermediate inventory and satisfy later consumed
inputs. Thus a sword route can prepare its wood scraps, wood gas can assemble
its drive/gasifier, and the battery route can print its case rather than needing
those craft-only components as direct generated finds. Exact outside retained
tools use maximum shared counts. Constructed items and their installable placed
objects supply traits only after their construction: the fabricated anvil cannot
retroactively satisfy its own bootstrap. Missing screwdriving may prepend the
actual exported screwdriver recipe once, with its live work and input bill.

`derived_routes.csv` and `summary.json` retain per-observation missing material,
exact-tool and external-provider-trait information, gross outside mass/volume,
work, processing time, electricity and heat/fuel budgets. Accounting selects
`soldering_iron` and `makeshift_arc_welder`; externally required provider traits
are reported separately, so a stronger find can satisfy a capability without
silently changing the selected conservative energy budget. Trait-only equipment
mass, site wiring, transport and actual grid availability are not included.
These already-skilled bills are **not** substitutes for the larger no-study
practice schedules above. Route coverage still checks no character's learned
skills, information, daylight, charged storage or survival path.

### Established by the static graph and arithmetic

1. **No mandatory forge/anvil/iron/welder construction cycle was found.** Cold
   station assembly needs no forge, first hammer/tongs accept lower providers,
   first iron needs no iron, and first welder needs no welding. Renewable
   construction has external-power starts. The accepted screwdriver fallback
   removes the exact-tool acquisition dependency for thirteen recyclers;
   integration checks both screwdriver providers and the preserved other gates.
2. **Practice, not one-off station construction, dominates gross stock.** The
   examples make 25 wiring kits, 9 packs, 24–27 chisels, six gasifiers and sixteen
   drives. These are mathematically valid but implausibly repetitive relative to
   one survivor workshop's demand. The concern is demonstrated output repetition,
   not a proven world-acquisition failure.
3. **Found equipment does not automatically make learning cheap.** It skips
   construction XP as well as stock; the chosen found-forging route still costs
   22 kg charcoal and seven suitable bars. A found regulated welder, however,
   really can bypass E3/M2 if the desired product does not require those skills.
4. **Finite-stock dependencies are intentional but important.** There is no
   selected charcoal, firebrick, suitable steel, filament, engine or photovoltaic
   manufacture route. Ordinary scrap cannot become spring steel or printable
   filament. These are not broken paths while scavenging sources exist; quantity
   sufficiency remains a separate question.
5. **One printer find and one filament draw are not a complete C3 progression
   kit.** Even the 211-portion useful battery example can exceed an electronics
   shop's minimum 200 portions; 484 portions always exceeds its two-draw maximum.
   Conversely, C3 is not required to obtain a useful print or printed battery case.
6. **Public information is optional acceleration, not an absent lock.** Both
   public workshop books have real placements; all chosen techniques are already
   known or printer-supplied. There is no missing advanced-book gate to fix for
   the basic loop. Computers has no current public-study alternative.
7. **Energy quantity and instantaneous rate are different bottlenecks.** One
   continuously exposed panel has substantial ideal daily potential but cannot
   sustain 3-EU/work welding directly at normal speed. Stored charge/source
   capacity, nighttime and wiring matter; an empty constructed pack fixes none
   of them by itself.

### Disposition after measurement

| Priority | Actionable proposal | Reason / validation condition |
| --- | --- | --- |
| Complete | One-scrap screwdriver and all thirteen trait conversions | Closes the agreed hand-tool fallback; other retained-tool and skill requirements are preserved |
| 2 | Add or retune useful M2/M3 repair/conversion work before increasing bulk engine stock merely to fund practice | Six gasifiers and sixteen drives are an output-demand mismatch. Keep M4 assembly and default curve unless a separately reviewed change is justified |
| 3 | Broaden E0/E2 and F3 useful learning choices, or raise selected per-work training rates with explicit recalculated budgets | Twenty-five wiring kits and dozens of chisels reward repetitive surplus manufacture. Do not globally lower gates or declare every long recycler higher-tier |
| Defer | Public Computers-1/2 study or extra filament contexts | Every measured small/medium-world union already exceeds the 484-portion C3 bill. Local site insufficiency is not evidence for a global feedstock increase |
| Documented / later UI | Teaching ceilings, one-use book timing and external-power needs | This audit records the rules; stale sword prose is corrected. Player-facing progression/help improvements remain a separate usability task |
| Complete | Ground-only charcoal, brick, pipe/rod and suitable-bar bundles, with recovered-tool weights adjusted for the expanded pool | [Before/after results](workshop_integration.md) measure quantity and joint route coverage without promising a complete workshop in one settlement |

No required identity was absent from all authored acquisition contexts. The
measurement pass now records same-world and per-settlement quantities, joint
construction bills, missing capabilities, geometric distances and initial site
population. It does not establish a survivable collection path, transport
feasibility or enjoyable pacing. In particular, abundant filament and a feasible
one-off generator bill do not validate the repeated-output learning schedules.
Useful M2/M3, E0/E2 and F3 practice diversity remains the main follow-up before
expanding progression. This pass preserves the accepted teaching rates and skill
anchors rather than disguising those learning concerns with more rare engines or
universal loot guarantees.
