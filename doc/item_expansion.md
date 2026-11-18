# Second broad item expansion

## Approved scope and coverage

The approved scope is **80 new items**, authored in four separate 20-item batches.
Reading and learning items remain deferred by explicit choice for this expansion,
even though the skill system now supports them. Existing stable definitions and
the earlier retirement inventory remain intact.

| Batch | Count | Coverage target | Author-owned files |
| --- | ---: | --- | --- |
| Stock and hardware | 20 | 8 metal stock, 7 wood/textile/polymer stock, 5 hardware | `data/items/expansion_stock.json5`, `data/crafting/expansion_stock.json5`, [stock record](item_expansion_stock.md) |
| Electronics | 20 | 18 components/assemblies, 2 household donor devices | `data/items/expansion_electronics.json5`, `data/crafting/expansion_electronics.json5`, [electronics record](item_expansion_electronics.md) |
| Equipment | 20 | 6 tools, 10 wearables including storage, 4 craft-only melee weapons | `data/items/expansion_equipment.json5`, `data/crafting/expansion_equipment.json5`, [equipment record](item_expansion_equipment.md) |
| Household and outdoors | 20 | 4 containers, 4 sleeping/shelter, 3 field supplies, 3 repair supplies, 3 foods, 2 hygiene, 1 belonging | `data/items/expansion_supplies.json5`, `data/crafting/expansion_supplies.json5`, [supplies record](item_expansion_supplies.md) |

Every item needs a represented unit, physical values, atmosphere-first description,
shared flags, canonical external RGB text art, and an actual acquisition route.
Target **76 loot-accessible items and four craft-only weapons**. Stock and tools
must support reachable crafting; donor devices must have mass-bounded dismantling.
Existing tools and stock remain useful. Advanced components are ordinary electronics
salvage, not unique late-game trophies. Historical/simple weapons are made from
obtainable stock with retained hand tools.

Location targets are independent of authoring groups: hardware/workshop, auto shop,
electronics/office, homes, clothing/laundromat, sporting goods, grocery and medical
or cleaning storage should all receive appropriate subsets. Bulky items require
capacity-safe placement. Batch membership never defines a loot pool.

The shared deliberate-humor budget is **three descriptions**: `wood_screws`,
`toaster`, and `toilet_paper`. Other prose should vary familiarity, history and
survivor resourcefulness without promising unimplemented actions.

## Reserved roster

These IDs were checked against current and retired catalogs. Retired generic
`nails`, `utility_knife`, and `plastic_bucket` are not reclaimed.

### Stock and hardware

`steel_pipe`, `steel_rod`, `steel_wire`, `steel_chain`, `wire_mesh`,
`aluminum_sheet`, `aluminum_tube`, `copper_sheet`, `plywood_panel`, `wooden_dowel`,
`rubber_sheet`, `foam_padding`, `canvas_patch`, `leather_patch`, `nylon_webbing`,
`framing_nails`, `wood_screws`, `nuts_and_bolts`, `door_hinge`, `coil_spring`.

### Electronics

`resistor`, `capacitor`, `light_emitting_diode`, `toggle_switch`, `power_relay`,
`cartridge_fuse`, `electrical_connector`, `insulated_power_cable`, `transformer`,
`induction_coil`, `heating_element`, `processor_chip`, `memory_module`,
`solid_state_drive`, `computer_power_supply`, `cooling_fan`, `display_module`,
`distance_sensor`, `toaster`, `desk_radio`.

### Equipment

`retractable_utility_knife`, `tin_snips`, `wood_chisel`, `hand_drill`, `multimeter`,
`socket_wrench_set`, `cotton_socks`, `hooded_sweatshirt`, `sneakers`, `winter_coat`,
`work_coveralls`, `hard_hat`, `welding_gloves`, `tool_belt`, `canvas_duffel`,
`padded_jacket`, `wooden_club`, `quarterstaff`, `salvaged_machete`, `steel_spear`.

### Household and outdoors

`utility_bucket`, `water_jerrycan`, `metal_toolbox`, `glass_jar`, `sleeping_bag`,
`wool_blanket`, `foam_sleeping_mat`, `camping_tent`, `candle`, `matchbox`, `compass`,
`sewing_thread`, `epoxy_adhesive`, `solder_wire`, `rolled_oats`, `canned_tuna`,
`trail_mix`, `toilet_paper`, `toothpaste`, `toy_robot`.

## Balance and integration review

Use [item balance](item_balance.md) for current anchors, actual costs and the
three-enemy reference panel. Equipment briefs must distinguish crafted early
weapons, specialized two-handed options, normal clothing, protective workwear,
and storage encumbrance. No item name grants reach, lighting, electrical attacks,
weather protection, food consumption or appliance operation.

The coordinator owns manifests, themed loot integration, shared tests, backlog
updates and aggregate checks. Each author owns only their two data files, their
20 art directories and their batch record. Batch records track unit, role,
location, stage, recipes/recyclers, art, validation and pending mechanics, and
preserve the complete reviewed monochrome grids before colorization.

## Completed coverage and acquisition

All **80 new definitions and 80 colored illustrations are complete**, with no
upgraded or retired IDs. Both Town and FirstHouse include the four item sources,
four crafting sources and shared `worldgen/loot/expansion.json5`. Town now loads
178 items: 154 current authored items, 22 obsolete compatibility definitions and
two internal containers. All four batch quotas above were met.

| Batch | New items / art | Loot-accessible | Craft recipes | Recyclers |
| --- | ---: | ---: | ---: | ---: |
| Stock and hardware | 20 / 20 | 20 | 1 | 8 |
| Electronics | 20 / 20 | 20 | 0 | 9 |
| Equipment | 20 / 20 | 16 | 7 | 20 |
| Household and outdoors | 20 / 20 | 20 | 0 | 4 |
| **Total** | **80 / 80** | **76** | **8** | **41** |

The four weapons are craft-only. The other craft results are dowels, a padded
jacket, a tool belt and a canvas duffel. All material and retained-tool prerequisites
are obtainable through current loot; new routes do not depend on obsolete items.
Recycling supplies existing scrap as well as current components, textiles and
padding. Whole-unit mass bounds also hold through nested donor dismantling.

Fourteen semantic pools serve existing household and urban collections. Electronics
parts are ordinary shop/office/workshop salvage; fasteners and stock feed hardware,
auto and warehouse loot; garments serve clothing, household and work contexts;
food and hygiene use pantry/bathroom collections. Five ground-only pools handle
workshop bulk, camping bulk, outerwear, toasters and buckets. Their placements in
eleven building sources avoid increasing shared furniture-pool volume bounds.
Oats have a separate capacity-checked dry-staples route. Existing guaranteed pilot
and household tutorial supplies remain obtainable.

## Starting balance decisions

The initial 96-seed weapon/armor screen motivated focused tuning rather than
copying weak existing family results. Final equipment reasoning is in the
[equipment record](item_expansion_equipment.md#starting-balance-decisions):

- **Club:** one plank and any Cutting 1 provider make an inexpensive fallback;
  a found hammer remains better equipment with tool utility.
- **Quarterstaff:** improved modest offense and lower shaping work accompany its
  measured two-handed guarding advantage. It is not a demonstrated universal
  hammer upgrade; defensive exposure and trained style comparisons explain its role.
- **Machete:** 15 HP slashing, cost 115 and 2400 work give dedicated steelwork a
  confirmed advantage over hammer while retaining cutting utility and a free hand.
- **Steel spear:** a confirmed upgrade over the coil spear, below the sword;
  fabrication 1/mechanics 1 and 3200 work better fit that intermediate role.
  Its four fitting tools remain a real acquisition cost. No extra reach is implied.
- **Wearables:** everyday garments, regional work protection and storage remain
  distinct from deliberate armor. The padded jacket reduces passive damage but
  slows active fights and increases exhaustion; improved survival can coexist
  with worse final injuries. Loaded-storage combat and piercing-focused armor
  encounters were not measured.

Important weapon relationships were confirmed on 384 separate seeds at skills
0/3/6, with normal recipe AI and the shopper/cellar/police panel. Jacket comparisons
also cover individual garments and the intended cotton/gloves/boots layering.
The balance passes comprise 19,584 initial-screen fights, 7,776 focused-screen
fights and 62,208 confirmation/exposure fights. These are scenario-specific design
evidence, not universal win-rate targets. Raw reports stay under
`out/item-expansion-balance/` (initial tuning) and
`out/item-expansion-followup/` (final decisions).

## Implemented and descriptive behavior

Implemented additions use existing armor, grips/melee, cutting traits, container
storage, crafting and recycling. There are six functional containers, including
the two wearable storage items, and ten authored wearable profiles. Electrical
parts, appliances, field supplies and many hardware types also provide setting
and future feedstock. Flags do not implement electrical operation, radio/sensing,
fire/light, food consumption, hygiene, warmth/weather, shelter/sleeping, liquid
handling, general repair chemistry or fastening. No new reading content was added.

## Verification and art review

Built `cataclysmx_tests` with `win-msvc-sdl` and the combat experiment target with
`win-msvc-release`, after initializing the Visual Studio developer environment.
The integrated focused run passed **162/162 CTest cases** (82.27 seconds):

```powershell
$env:CATACLYSMX_ITEM_EXPANSION_PREVIEW = "$PWD/out/item-expansion"
./scripts/test.ps1 -Preset win-msvc-sdl -R '(Item expansion|[Ss]lice|item pilot|[Cc]ontent|[Cc]rafting|[Rr]ecycl|[Hh]ouse|[Uu]rban|All shipped manifests)'
```

This includes independent shipped-pack loading, worldgen capacity validation,
76 placed routes, craft-only reachability, mass bounds, real storage/spear crafting
with retained tools, donor dismantling, worn-belt storage and the SDL item browser.
Three overwide illustrations were corrected before that passing run. Following
the final nine silhouette refinements, the affected checks passed **6/6** again
(61.93 seconds):

```powershell
./scripts/test.ps1 -Preset win-msvc-sdl -R '(Item expansion|SDL item slice and expansion|All shipped manifests)'
```

All 80 colored assets were compared exactly with their complete monochrome masters.
The SDL case generated **160 previews**, at 480×360 and 1120×900. The coordinator
inspected all 80 large previews in eight contact sheets, re-inspected the nine
refined silhouettes, and sampled compact views. Long art/details scroll at compact
size. Preview files are `out/item-expansion_<id>_480.png` and `_1120.png`;
contact sheets use `out/item-expansion_<batch>_contact_<1|2>.png`.
No tooling tests ran. There are no incomplete roster entries or integration blockers.
