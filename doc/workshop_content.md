# W3 playable metalworking content

**Status: Historical bounded proposal; superseded by the [W3A/W3B implementation](workshop_rosters.md).**
The [shared crafting progression framework](crafting_progression.md) now records
the reviewed domain outlines. The low station/sword skill gates below are
superseded by the accepted metalworking direction: tiered anvils,
Fabrication 2 forge/tongs, Fabrication 3 forming equipment and Fabrication 4 proper
swords. The makeshift welder now has an accepted Electronics-3/Mechanics-2
construction anchor, and welding protection is an additional requirement. Retain
this draft as source material; the [revised W3A/W3B briefs](workshop_rosters.md)
now propose the new roster. The
[dependency/delivery plan](crafting_delivery_plan.md#revised-w3-boundary)
records current revision inputs. Current authored content and verification belong
to the W3A/W3B implementation record, not the historical budgets below.

**Current prerequisite distinction:** W3A forging can use W1/W2.
W3B electrical/welding uses D1 and ordinary accessible/shared tool traits for
[crafting protection](crafting_progression.md#welding-protection), including for
found welders. Powered placed manual providers additionally need M-Bench.
Runtime dependencies must be checked against the revised roster's actual providers.
The old item counts, quantities, training path and acquisition/version proposals
must be reviewed against the new roster rather than treated as current targets.

This is the bounded content milestone following the verified W1/W2 mechanics in
[workshop design](workshop_design.md), under the
[urban progression plan](urban_workshop_progression.md).

## 1. Historical recommendation and review boundary

Deliver two useful routes:

```text
ordinary hand tools + salvaged stock
  -> mounted block anvil + hand-blown charcoal forge
  -> forging tongs
  -> either existing arming-sword recipe

electrical salvage + ordinary hand tools
  -> existing soldering iron -> existing soldered connectors
found arc welder + electrodes + D1 supply
  -> packable workshop storage rack
```

W1/W2 already support these manual recipes, placed equipment, whole-item fuel,
powered tools, reservations and recovery. A passive packing-backed storage object
can implement the rack with the existing station placement path. No new runtime
interface or per-entity storage is proposed.

The accepted D2 contract already requires both sword routes to receive workshop
gates. This review selects the equipment, exact gates, starting operating costs
and acquisition coverage before item authoring.

Recommended decisions:

| Decision | Proposed W3 rule | Reason |
| --- | --- | --- |
| Scope | Nine new items, four upgraded items, four new objects, five new crafting recipes and two revised sword recipes | Each addition has an immediate use on a complete route |
| Forge balance | 1 kg charcoal charge = 6,000 heat; capacity 24,000; ignition 1,000 | Adopt W2's worked example as the first shipped balance |
| Forging gates | Tongs require ordinary pliers; swords require tongs; both use anvil 1, forge heat 1 and hammering 1 | Bootstrap remains reachable; tongs have a downstream purpose |
| Sword parity | Both recipes require Fabrication 2, 6,000 work and 6,000 heat; preserve their existing input identities | Close the legacy skill/workshop bypass with stable recipe IDs |
| Recipe knowledge | New bootstrap recipes are known by default; skills still apply | Avoid adding a manual hunt before the first usable workshop |
| Welding | Found corded welder; electrodes are reserved ingredients; rack costs 6,000 grid energy | Exercise D1 supply and give electrical salvage an immediate useful result |
| Acquisition | Guaranteed core supplies at appropriate sites, randomized extra stock; found stations in the town workshop | Avoid a chain dependent on many individually rare rolls |
| Generation policy | Retain generation v7; ship a content revision | Existing placement algorithms already express the additions |

These are proposals, not user-approved changes or measured balance results.

## 2. Bounded roster

All nine new items belong in `data/items/workshop.json5`. Proposed IDs were checked
against `data/` and are unused. Units and values below are starting balance targets.
`g / ml` describes the complete item, not an individual member of a packet.

| New item ID | Unit; g / ml | Role and progression | Acquisition | Recovery proposal |
| --- | --- | --- | --- | --- |
| `solid_steel_block` | One salvaged block; 10,000 / 1,400 | Solid anvil face; pre-forge stock | Hardware and workshop ground stock | Saw into 90 `steel_scrap` |
| `refractory_brick` | One firebrick; 2,000 / 1,000 | Forge lining; pre-forge stock | Hardware ground stock | No recycler |
| `charcoal_charge` | One 1 kg bag; 1,000 / 3,000 | Six thousand heat units | Hardware, workshop and outdoor ground stock | Fuel only |
| `crude_anvil_kit` | One mounted block/stand; 14,000 / 16,000 | Place `crude_anvil`; no carried trait | Craft; occasional workshop bulk loot | One `solid_steel_block` + 10 `wood_scrap` |
| `charcoal_forge_kit` | One hearth with hand bellows; 17,000 / 28,000 | Place `charcoal_forge`; no carried trait | Craft; found workshop station or bulk loot | Four bricks + 25 `steel_scrap` + four `wood_scrap` |
| `forging_tongs` | One pair; 1,500 / 1,600 | `metal_holding` 2; unlock substantial forging | Forge; found workshop | 12 `steel_scrap` |
| `arc_welder` | One corded inverter welder and leads; 6,000 / 7,000 | Powered `welding` 1; electrical branch | Workshop and auto-repair ground equipment | Four `copper_wire` + 20 `steel_scrap` |
| `welding_electrode_packet` | One small electrode packet; 500 / 400 | Operating ingredient; one per rack | Workshop, auto repair, compact hardware supplies | No completed-product recovery |
| `welded_workshop_rack_kit` | One dismantled rack; 11,000 / 16,000 | Place 120,000 ml workshop storage | Craft | Three `steel_pipe` + 40 `steel_scrap` |

The rack consists of welded subassemblies joined with bolts, so packing an empty
rack is consistent with its item form. Its capacity is an explicit storage
benefit; it supplies no additional crafting trait.

Upgrade **four existing item definitions**, preserving IDs, artwork and unrelated
stats:

| Existing item | Owning file | Change |
| --- | --- | --- |
| `combination_pliers` | `data/items/slice_equipment.json5` | Add `metal_holding` 1 |
| `hacksaw` | `data/items/slice_equipment.json5` | Add `metal_saw` 1 alongside existing cutting |
| `hand_drill` | `data/items/expansion_equipment.json5` | Add `metal_drilling` 1 |
| `arming_sword` | `data/items/low_tech.json5` | Replace cold-shaped-blade prose to match hot forging |

The existing `heating_element`, `insulated_power_cable`, `soldering_iron`, stock,
hand tools and solder are reused. In particular, no second soldering-iron identity
or new heating-element item is needed. The iron remains 250 g / 400 ml and retains
its D1 operating rate. The proposed explicit iron recycler returns two `steel_scrap`
(200 g), with no reconstructed heating element or cable.

Four object definitions in `data/objects/workshop.json5`:

| Object ID | Behavior / capabilities | Placement and recovery |
| --- | --- | --- |
| `crude_anvil` | Passive; `anvil` 1 | Original `crude_anvil_kit` backing |
| `charcoal_forge` | Forge; `forge_heat` 1 | Original `charcoal_forge_kit` backing; cold/empty recovery |
| `workshop_anvil` | Passive; `anvil` 1 | Found fixed fixture; no packing item |
| `welded_workshop_rack` | Passive; 120,000 ml public storage | Original `welded_workshop_rack_kit` backing; empty-only recovery |

All four block movement but not sight. The proper fixed anvil grants the same
currently useful capability as the crude one; an extra tier would have no W3
consumer. Its benefit is ready access without building a stand.

### Coverage and presentation targets

- Nine new items: three raw stock/fuel units, two station items, two tools, one
  welding consumable and one useful storage result. Four existing-item upgrades
  are counted separately.
- Five new crafting recipes, two revised producers of the same sword, and seven
  explicit new recyclers: block, anvil kit, forge kit, tongs, welder, rack and iron.
  Charcoal, electrodes and refractory brick get no standalone recycler.
- Six new traits: `anvil`, `forge_heat`, `metal_holding`, `welding`, `metal_saw`,
  `metal_drilling`; retain existing `hammering` and `soldering`.
- Every new item needs canonical external text art. Plan distinctive silhouettes:
  solid offcut, pale brick, charcoal sack, strapped block/wood stand, low hearth
  with bellows, long-jawed tongs, compact inverter/looped leads, electrode sleeve,
  stacked rack subassemblies. Status for all: **Brief pending**.
- Four new object SDL briefs: block on stand, low hearth/bellows, fixed horned
  anvil, open shelving. Use readable distinct terminal glyphs during functional
  implementation; forge readiness remains inspectable through existing UI.
  Dedicated state-aware sprite behavior is not assumed.
- Description voice should be plain and factual: identify the stock, equipment
  and supported workshop use. Follow the shared [description policy](item_authoring.md#description-voice-plain-by-default);
  this small roster has no required atmosphere or humor. Existing art stays
  associated with its stable IDs.

## 3. Recipes and skill pacing

All new recipes are explicitly known by default. Counts below mean whole stock
units/packets. Tool names are stable item IDs; retained tools are not consumed.

| Recipe ID | Consumed inputs | Retained items; required traits | Base work; skill |
| --- | --- | --- | --- |
| `assemble_crude_anvil` | Block ×1, `timber_plank` ×2, `nuts_and_bolts` ×1 | `adjustable_wrench`; hammering 1, metal saw 1, metal drilling 1 | 4,000; no gate, Fabrication-0 training |
| `assemble_charcoal_forge` | Brick ×6, `steel_sheet` ×1, `steel_pipe` ×1, `timber_plank` ×1, `leather_patch` ×4, `nuts_and_bolts` ×1 | `adjustable_wrench`; hammering 1, metal saw 1, metal drilling 1 | 6,000; no gate, Fabrication-0 training |
| `forge_tongs` | `steel_rod` ×2, `nuts_and_bolts` ×1 | Hammering 1, anvil 1, forge heat 1, metal holding 1 | 2,000; Fabrication 1 |
| `assemble_soldering_iron` | `heating_element` ×1, `insulated_power_cable` ×1, `copper_wire` ×1, `wood_scrap` ×1, `wood_screws` ×1 | `combination_pliers`, `flathead_screwdriver`; no powered trait | 1,000; no gate, Electronics-0 training |
| `weld_workshop_rack` | `steel_pipe` ×4, `steel_sheet` ×3, `nuts_and_bolts` ×1, electrode packet ×1 | `adjustable_wrench`, `metal_file`; welding 1, metal saw 1, metal drilling 1 | 3,000; Fabrication 1 |
| Both existing sword IDs | Preserve their three existing consumed input types/counts | Add retained `hacksaw` and `metal_file`; hammering 1, anvil 1, forge heat 1, metal holding 2 | 6,000; Fabrication 2 |

The largest new recipe has seven input types, including retained tools. Forge,
tongs and swords demonstrate no need to increase the eight-input/four-trait bounds.
Manual bellows are part of the forge equipment and construction inputs; tending
is included in the accepted ignition/work abstraction.

Consumed mass → product mass is respectively **14,200 → 14,000**, **17,880 →
17,000**, **1,800 → 1,500**, **1,100 → 250**, **11,500 → 11,000** grams for the five
new recipes. The soldering-iron route trims small usable components from larger
salvaged units; the high waste deliberately preserves the existing item's scale.
No extra byproduct output is assumed. Both sword routes remain 2,000 → 1,800 g.
Proposed recycler returns above stay below target mass and cannot reconstruct the
spent operating consumables. Author their hand-tool/work requirements explicitly
and audit cycles before shipping.

### Skill progression is a real cost

Current defaults require 100 XP for Fabrication 0→1 and 200 XP for 1→2, at one XP
per 100 eligible work. The proposed two station constructions therefore provide
**100 XP in total**, taking an unaided novice to Fabrication 1 through useful
construction. Tongs then give 20 XP toward level 2.

Preserve the current global XP curve and default recipe training rate. A sword
remains a later trained result: after tongs, eight existing level-1 machete crafts
give another 192 XP, enough for level 2. This is a concrete reachable example,
not a claim that eight spare machetes is ideal pacing. Existing workshop books
accelerate training; other level-1 work, including the rack, also contributes.
Finding stations skips construction, so it does not grant that construction XP;
existing level-0 woodworking and study remain the found-equipment skill route.

**Review point:** approve this deliberate training investment, or request faster
local W3 training before authoring. A faster sword path should be an explicit
balance choice rather than silently granting a level for placing found equipment.

## 4. Common operating rules

- The mounted anvil supplies `anvil` tier 1 only while placed. Ordinary
  `combination_pliers` gain `metal_holding` tier 1; forging tongs supply tier 2.
  The hand-blown forge supplies `forge_heat` tier 1 only while Ready.
- Tongs and both swords use exactly four traits: `hammering`, `anvil`,
  `forge_heat`, `metal_holding`. A retained finishing/cutting tool can occupy an
  input slot where needed; broad `house_cutting` is not a metal-saw capability.
- Forging uses `heat_per_work: 10000`. The forge starts cold and empty whether
  crafted or generated; finding equipment does not provide fuel or skill.
- The welder supplies `welding` tier 1 and
  `crafting_power: {store:'grid', energy_per_work:20000}`. Every provider of the
  new welding trait must pay power. It has no local battery store.
- Welding electrodes and solder are nonrecoverable ingredients in completed
  product recycling. Cancellation still returns the actual reserved ingredients,
  as required by D2; already spent electricity and heat stay spent.
- Packing equipment keeps its original item. A rack must be empty to pack; a
  forge must additionally be cold and have no fuel. Fixed workshop fixtures have
  no packing action.

### Resource examples

Two charcoal charges supply 12,000 heat. One ignition, 2,000 work of tongs and
6,000 work of sword forging leave **3,000 heat**. A fresh single charge can make
tongs and leave 3,000; it cannot finish a sword without another charge.
Pausing does not itself consume more fuel. Relighting costs another 1,000.

A 3,000-work rack at the proposed welder rate costs **6,000 energy**: one current
`generator_fuel_packet`, or six fully charged 1,000-charge installed batteries,
with no competing loads. The existing generator's four-energy-per-tick allowance
supports the two-energy-per-base-work demand at normal worker speed. Faster work
and competing loads still obey D1's actual throughput/funding rules.

These are exact authored-cost targets, not measurements. Failed payment, paid
slices, interruptions and cancellation retain W1/W2 semantics.

## 5. Acquisition targets

Use existing named places and actual palette/ground placements. A generated town
is not guaranteed to contain every building type; guarantees below are **per
qualifying site**, not per region.

| Context | Proposed minimum acquisition | Purpose |
| --- | --- | --- |
| `hardware_store` rear stock | One block, six bricks, two charcoal charges; three planks, one sheet, one pipe, four leather patches, three fastener packets and two rods | Enough stock for both crude stations and first tongs |
| `hardware_store` tool stock | One hammer, hacksaw, hand drill, wrench and combination pliers | Makes that site's bootstrap materials usable without five rare tool rolls |
| `workshop` fabrication room | Fixed `workshop_anvil`, cold/empty `charcoal_forge`, tongs, two charcoal charges | Found-equipment alternative; skills and operating costs remain |
| `workshop` and `auto_repair` | One welder, two electrode packets; four pipes, three sheets and a fastener packet; saw/drill/wrench/file access | Each qualifying site can supply one useful welded rack |
| Existing electrical-parts/tool contexts | Heating elements, cable, copper, handle stock and screw packets, plus screwdriver/pliers; existing found irons/solder | Preserve found soldering; verify assembled route from generated stock |
| Existing ground-only workshop/camping pools | Random extra charcoal; workshop bulk also admits block, bricks and packed station equipment | Repeatable alternative locations and FirstHouse opportunities |

Guaranteed bundles require separate single-entry pool draws/placements for their
members. One weighted pool selects an alternative and cannot implement an
all-members bundle. Keep bulky stock, stations and welders on ground destinations.
Add electrodes only to compact pools whose worst-case container volumes remain
valid. Reuse the existing `loot_workshop_bulk_ground` and
`loot_camping_bulk_ground` placement coverage for randomized extras.

The full found forging/welding routes belong in Town's specialized buildings.
FirstHouse includes the shared definitions and ground-pool opportunities; its
existing guaranteed D1 starter cache remains the dependable electrical tutorial.
Do not claim a seed-independent complete FirstHouse metalworking tutorial from
random workshop loot.

Implementation must record seed/site coordinates for a real crafted route and a
found route, verify rotated access to each station, and check the source pools for
all soldering-assembly ingredients. Exact optional-extra weights can be tuned
against these coverage targets; essential bootstrap availability is not delegated
to those weights.

This is a **multi-site scavenging progression**. The guaranteed hardware bundle
ends at stations and tongs, not a sword: sword stock, a finishing file and further
practice materials still need acquisition. For scale, the eight-machete training
example alone consumes 80 `steel_scrap`, eight `wood_scrap` and eight `steel_wire`.
Found stations likewise do not guarantee every sword ingredient/tool or skill.
Soldering assembly uses distributed existing loot rather than a guaranteed kit.
The generated acceptance routes must demonstrate these additional dependencies.

## 6. Integration and compatibility

Use small semantic additions rather than moving the earlier catalogs:

- `data/items/workshop.json5`: new equipment, stock and operating consumables.
- `data/objects/workshop.json5`: packing-backed stations/rack and found fixtures.
- `data/crafting/workshop.json5`: new traits, recipes and explicit recyclers.
- `data/worldgen/loot/workshop.json5`: dedicated acquisition pools.
- Existing item/recipe files: upgrade reused tools and both sword recipes in
  place, preserving their stable IDs.
- `data/content.json5` and `data/first_house.json5`: include the shared sources;
  validate both independently. The standalone demo has no sword producer and
  does not need the workshop content.

The final item-authoring pass supplies canonical external item text art and
stable-ID SDL briefs. New map artwork remains an external-workstation handoff;
temporary glyph presentation does not count as completed sprite artwork.

### Generation-version decision

Retain `world_generation_version == 7`. W3 changes authored pools, furniture and
recipe content, not placement, terrain or electrical-registration algorithms.
The deterministic input contract already includes the catalog.

Saved chunks retain their actual items, backing equipment, forge state and
depleted loot. Unmaterialized chunks can use revised content, including in older
worlds; generation version alone does not freeze their catalog. The existing
save contract does not pin a content fingerprint. Reproducing an earlier loot
layout therefore requires the earlier content as well as seed/version.

No save-envelope or LAN-schema bump is proposed for data-only W3. The ordinary
content fingerprint changes. A later engine/schema requirement would reopen this
decision rather than silently expanding the milestone.

## 7. Acceptance and implementation handoff

After roster approval, use the item-create/text-art-create workflow and deliver:

1. Independently load full Town and FirstHouse packs. Check exact tool/trait
   limits, backing mappings, all new art references and the affected demo load.
2. Obtain pre-forge stock and tools through actual generated placements; construct
   and place both stations, then forge tongs and a sword. Show the skill path from
   a fresh character rather than supplying skills only through test setup.
3. Repeat with found equipment. Verify both sword producer IDs refuse missing
   heat/tongs/skill and retain useful early machete and spear alternatives.
4. Assemble a soldering iron without soldering, then make the existing connectors.
   Use a found welder on mains and finite supply to build/place a rack and store
   items in it. Test empty-only packing with the actual authored rack.
5. Exercise authored tongs with pause/save/reload and predicted fuel balance;
   exercise authored welding with cancellation and supply refusal. Retain the
   existing engine-level contention and atomic-funding regression coverage.
6. Audit every producer and recycler of the selected outputs, including legacy
   recipes. Recycled mass must not exceed the target plus consumed inputs;
   completed products must not regenerate electrodes, solder, charcoal or power.
7. Check legal accessible placements and worst-case container volumes. Compare
   acquisition against the approved quotas, not merely catalog membership.

Use the narrow applicable Debug build/test selection from [testing](testing.md);
add content-level acceptance cases rather than duplicating W1/W2 synthetic
mechanics tests. Run affected worldgen/persistence coverage if placements change,
and verify shared presentation of the new equipment. Record actual commands and
results here when implemented.

### Review evidence

Historical read-only content and engine audits accompanied this draft. A separate draft review
checked recipe bounds, mass arithmetic, XP and supply budgets, and identified the
multi-site acquisition qualification recorded above. The reviewed engine supports
manual forging/welding and a packing-backed storage rack under the then-proposed
requirements. The revised protection rule uses ordinary traits; check its
providers and trait-slot budget with the new roster. Powered placed manual
equipment still needs M-Bench. W3 has not been implemented or tested; the prior
W1/W2 verification remains in
[workshop design](workshop_design.md).
