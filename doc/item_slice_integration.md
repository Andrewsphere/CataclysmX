# Completed slice: integration and retirement inventory

## Status and ownership

Data integration, retirement, content/gameplay checks and renderer review are
complete. The coordinator built `win-msvc-sdl` and the final focused CTest run
passed **126/126**. Exact commands and visual-review details are in
[the batch record](item_slice.md#verification).

No runtime code, pilot item definition, or original recipe work value changed.
The original pilot server test now supplies the current hacksaw for its existing
cutting trait; all original crafting quantities, timing and retention assertions
remain. The new tests are registered in `CMakeLists.txt`.

## Catalog audit

The pre-slice authored catalog contained **71 items**: demo 15, household 11,
urban 43, and low-tech 2. Integration removes **44**, retains **22 obsolete**
dependency definitions and the **5 unchanged current pilots**, and includes the
**60 new definitions**. Town therefore has **87 authored items**, plus the injected
`unfinished_craft`, for a verified `ItemCount()` of **88**.

### Retained obsolete definitions (22)

| Source | IDs | Reason |
| --- | --- | --- |
| `items/demo.json5` (15) | `rock`, `stone_marker`, `backpack`, `bandage`, `scrap`, `metal_pipe`, `wood_plank`, `blinking_lightbulb`, `toggle_lightbulb`, `single_blink_lightbulb`, `disposable_battery`, `rechargeable_battery`, `field_charger`, `door_controller`, `utility_controller` | Engine/server startup requires rock/backpack/bandage; optional controllers, bulbs and charger preserve the demo; the AA pair preserves the demo battery contract; stone marker has a recipe/recycler; scrap and pipe are the unchanged `scrap_press` machine input/output; plank and scrap feed original crafting routes. |
| `items/household.json5` (2) | `clean_rag`, `house_bandage` | Rags/bandage preserve `cut_house_bandage` and existing clothing recyclers. |
| `items/urban.json5` (5) | `wood_handle`, `wire_bundle`, `scrap_knife`, `electrical_cord`, `work_trousers` | Preserve every original urban recipe/recycler and the original pilot sword's wire input. |

All have `flags:['obsolete']`, preserving their other properties. Shared flag
`obsolete` has display name **Obsolete**, item-only domain, category kind, and a
dependency-retention description. It has no automatic filtering/hiding behavior.
The internal unfinished craft is not a legacy authored definition.

### Removed definitions (44)

- Household (7): `kitchen_knife`, `canned_food`, `soap`, `nails`, `screwdriver`,
  `cooking_pot`, `hand_saw`.
- Urban (37): `dry_pasta`, `rice_packet`, `cereal_box`, `snack_packet`, `coffee_tin`,
  `tea_box`, `cooking_oil`, `flour_bag`, `frying_pan`, `serving_spoon`, `dish_towel`,
  `detergent_box`, `scrub_brush`, `plastic_bucket`, `gauze_roll`, `medical_gloves`,
  `medical_scissors`, `wrench`, `pliers`, `utility_knife`, `bolts`, `duct_tape`,
  `circuit_board`, `apron`, `work_gloves`, `paperback_book`, `reference_book`,
  `notebook`, `pencil`, `paper_stack`, `plastic_bottle`, `canteen`, `rope_coil`,
  `sewing_kit`, `toy_blocks`, `first_aid_pouch`, `parts_box`.

Their production loot references were replaced. These definitions had no external
item art references, so there was no newly orphaned legacy item art to delete.
No book/newspaper/manual replacement was invented. `loot_books` was removed;
all 29 building links now point to `loot_reading_room_supplies` (water, chocolate,
batteries and fabric). Existing reading-room building/furniture identities remain.

The final audit also removed `kitchen_knife`: the legacy bandage recipe uses a
cutting trait rather than that identity. Guaranteed `house_kitchen` loot now yields
the current `chef_knife`, preserving the tutorial's obtainable cutting tool.
The coordinator also removed `hand_saw`: it was referenced by loot and tests,
not by a recipe identity or runtime dependency. The current `hacksaw` supplies
the same cutting tier to the original pilot route.

### Deliberate obsolete loot exceptions

Only **6 obsolete identities** remain in production loot:

- `clean_rag`: guaranteed linen source for the unchanged household bandage recipe.
- `wood_plank`, `scrap`: low-weight compatibility draws for the original
  pilot/urban chains and unchanged pilot progression contract.
- `electrical_cord`, `wire_bundle`: low-weight `loot_legacy_recipe`, reachable from
  hardware; preserves wire salvage, scrap knife and original sword routes.
- `work_trousers`: low-weight clothing draw keeps the original recycler usable.

The main food, medical, electrical and outdoor pools use current items. Existing
server starting backpack/bandage remain obsolete because startup is unchanged;
they are not newly added loot exceptions. All new chains have current tools and
materials obtainable without any of these obsolete sources.

## Acquisition: all 60 new IDs

Pools are themed and reused by the already populated Town buildings, rather than
one loot pool per authoring batch. Every listed loot route has positive weight,
positive rolls/chance in existing building placements; acquisition tests inspect
the expanded placement graphs, and the existing urban layout suite checks physical
approach access to the placements on each floor/basement.

| New IDs | Acquisition pools / actual contexts |
| --- | --- |
| `steel_scrap`, `copper_wire` | `house_workshop`, `loot_hardware`; home workbench, hardware shop, workshop, garage |
| `copper_tube`, `steel_sheet`, `aluminum_scrap`, `rigid_plastic_scrap`, `glass_shard`, `rubber_hose`, `timber_plank` | `loot_hardware`; hardware shop/warehouse/workshop/garage; timber also `house_storage` |
| `corrugated_cardboard` | `loot_office`, `loot_warehouse`; office storage and warehouse crates |
| `cloth_patch` | `house_household_supplies`, kitchen/cleaning/laundry pools; cabinets and sewing/cleaning storage |
| `transistor`, `diode`, `microcontroller_board`, `desktop_motherboard`, `tiny_electric_motor`, `servo` | `loot_electronics`; electronics shop/workshop/office electronics; hobby pool also offers tiny motor/servo |
| `appliance_electric_motor`, `water_pump` | `loot_auto_repair`; 100-liter garage workbenches/shelves |
| `traction_electric_motor` | dedicated `loot_traction_motor_ground`, guaranteed garage floor marker at auto repair local `(5,2,0)`; never nested in ordinary pools |
| `adjustable_wrench`, `metal_file`, `pry_bar`, `safety_goggles` | `loot_tools`; hardware shop, workshop, garage |
| `combination_pliers`, `hacksaw`, `flathead_screwdriver` | `house_tools`, also nested under `loot_tools`; household tool storage and commercial workshops |
| `soldering_iron` | `loot_electronics`; electronics repair stock |
| `chef_knife` | guaranteed `house_kitchen`, plus `loot_kitchen` and `house_tools`; kitchens/tool storage |
| `cotton_tshirt`, `denim_jeans`, `rain_jacket`, `leather_work_gloves` | `loot_clothing`; clothing shop/lockers/laundry; jacket also outdoors, gloves also industrial/emergency stock |
| `fire_axe` | `loot_fire_station`; fire-station equipment storage |
| `police_baton`, `pump_shotgun`, `shotgun_shells` | `loot_police_station`; police-station storage |
| `coil_spear` | crafting only: `make_coil_spear` |
| `cloth_repair_tape` | `house_workshop`, `loot_warehouse`, `loot_utility`; repair benches/crates/cabinets |
| `camping_backpack` | `house_storage`, `loot_outdoors`, `loot_preparedness`; storage chest/outdoor shop/preparedness storage |
| `canned_beans`, `dill_pickles`, `instant_noodles`, `long_grain_rice`, `peanut_butter` | `house_pantry`, nested food/pantry pools; homes, groceries, food shops |
| `potato_chips`, `chocolate_bar`, `bottled_water` | `loot_food`; convenience/grocery stores, outdoor food stock; visitor supplies also offer chocolate/water |
| `antiseptic`, `sterile_gauze`, `cloth_bandage` | `house_bathroom`, `loot_medical`; bathrooms, pharmacy, clinic/hospital, emergency stations; bandage also crafts |
| `bar_soap` | `house_bathroom`, `loot_cleaning`; bathroom/cleaning/laundry storage |
| `sewing_supplies` | `house_household_supplies`, clothing/laundry/hobby pools; domestic and clothing storage |
| `nylon_rope`, `camping_tarp`, `steel_water_bottle` | `loot_outdoors`; sporting goods/preparedness supplies; rope/bottle also fire station |
| `plastic_storage_box` | household supplies, garage, warehouse, cleaning storage |
| `aa_alkaline_battery`, `aa_rechargeable_battery`, `battery_charger` | `loot_electronics`; electronics shop and electrical stock; alkaline also household/visitor/preparedness stock, charger also house tools |

Physical sizes are unchanged: motors are **20 g/30 ml**, **4000 g/3500 ml**, and
**60000 g/45000 ml**. The sole pump is 2500 g/3000 ml; the sole servo is 60 g/100 ml.
The traction motor is not discounted to fit furniture. The shotgun is 4500 ml and
belongs to the police pool in large storage; the auto repair pool's 3500 ml maximum
fits its workbenches/shelves. Loader capacity validation and the existing worldgen
tests passed in the coordinator's final focused run.

## Current-only recipes and recycling

`data/crafting/slice.json5` adds four recipes and five explicit recyclers:

| ID | Consumed input / result | Tools; work |
| --- | --- | --- |
| `cut_cloth_bandage` | 1 cloth patch (40 g) -> 2 cloth bandages (40 g) | cutting tier 1; 100 |
| `cut_timber_scrap` | 1 timber plank (2000 g) -> 10 pilot wood scraps (2000 g) | cutting tier 1; 200 |
| `make_current_arming_sword` | 14 steel scraps + 2 wood scraps + 1 copper wire (2000 g) -> pilot arming sword (1800 g) | hammering + cutting tier 1; 6000 |
| `make_coil_spear` | 1 timber plank + 1 steel scrap + 1 copper wire + 1 repair-tape roll (2600 g) -> coil spear (2400 g) | explicitly retained hacksaw and combination pliers, cutting tier 1; 1200 |
| `cut_up_cotton_tshirt` | 180 g shirt -> 4 cloth patches (160 g) | cutting tier 1; 100 |
| `cut_up_denim_jeans` | 650 g jeans -> 12 cloth patches (480 g) | cutting tier 1; 180 |
| `cut_steel_sheet` | 2000 g sheet -> 15 steel scraps (1500 g) | retained hacksaw; 600 |
| `salvage_appliance_motor` | 4000 g motor -> 20 steel scraps + 4 copper wires (2800 g) | retained adjustable wrench + combination pliers; 800 |
| `salvage_water_pump` | 2500 g pump -> 12 steel scraps + 2 copper wires (1600 g) | retained adjustable wrench; 600 |

No generated recycler was added to alternate sword recipes, and no second recycler
targets `work_shirt`. Stock returns feed the current bandage/sword/spear routes;
there is no new circular crafting/recycling chain. Retained tools do not inflate
recoverable mass. The original seven crafting routes and three recyclers (including
the generated marker recycler) remain unchanged in their original files.

## Manifest boundaries

- Town includes all three slice item sources and `crafting/slice.json5`.
- FirstHouse shares changed household loot, so it includes the same three sources,
  the low-tech pilot item source required by the current scrap/sword recipes, and
  `crafting/slice.json5`. Cross-file traits resolve from existing household files.
- Demo and zombie showcase manifests stay narrow and independently load their
  original sources plus the shared vocabulary they already included.

## Test coverage

`tests/item_slice_tests.cpp` adds eight focused cases under `[item-slice]`:

1. Complete new definitions (description, colored art, flags, no obsolete), exact
   catalog/retirement inventory, unchanged current pilot classification, one servo
   and pump, and an explicit six-ID obsolete-loot exception whitelist.
2. Actual authored building acquisition for all 59 loot items and a fixed-point
   current-only material/tool-trait path to all 60 plus wood scraps and the sword;
   no obsolete inputs in new recipes and mass-balanced current crafting.
3. Motor scale, exclusive garage ground marker, and actual generated traction-motor
   acquisition in a bounded sample of production region plans.
4. Current bandage/timber/sword crafting: missing tools, exact output/consumption
   counts, cutting/hammering requirements, retained tools.
5. Spear crafting: required saw/pliers, no substitution by generic cutting alone,
   atomic missing-tool refusals and retention of both tools after success.
6. Five new recyclers: current-only targets/inputs/returns, recoverable mass <=
   target mass, actual missing-tools/success paths, exact yields and retained tools.
7. Current AA charger operation, alkaline refusal, energy transfer, authored
   container capacities, pump fitting only the larger pack and traction motor
   exceeding that pack's capacity.
8. Independent demo/FirstHouse/zombie manifest and server-content validation.

The final selected CTest cases include these checks, original pilot progression,
content/house/urban and crafting/recycling coverage, and the SDL slice/pilot render
checks. No existing generic assertion was changed simply to accommodate a mismatch.
The slice SDL case renders all 60 via the production item-browser controller at
480x360 and 1120x900, checks that art reaches pixels, and checks scroll restoration.

## Files touched by this integration workstream

- `CMakeLists.txt`
- `data/content.json5`, `data/first_house.json5`
- `data/flags/core.json5`
- `data/items/demo.json5`, `data/items/household.json5`, `data/items/urban.json5`
- `data/crafting/slice.json5` (new)
- `data/worldgen/loot/household.json5`, `data/worldgen/loot/urban.json5`
- Under `data/worldgen/buildings/`: `apartment.json5`, `apartment_walkup.json5`,
  `auto_repair.json5`, `bookstore.json5`, `courtyard_house.json5`, `duplex.json5`,
  `farmhouse.json5`, `hospital.json5`, `library.json5`, `office.json5`,
  `office_annex.json5`, `park.json5`, `police_station.json5`, `row_house.json5`,
  `school.json5`, `tall_row_house.json5`
- `tests/item_slice_tests.cpp` (new), `tests/item_pilot_tests.cpp`
- `doc/item_slice_integration.md` (new)

## Remaining gameplay scope

- Food/healing/armor/ranged/shelter/repair effects remain descriptive metadata;
  only already supported melee, storage, crafting and charging are exercised.
- Catalog IDs/fingerprints change as expected with content retirement. This pass
  does not provide save migration or alter engine persistence contracts.
