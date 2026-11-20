# Item content backlog

This living list records remaining groups after the [first 60-item slice](item_slice.md),
the five approved pilot items, and the completed [80-item expansion](item_expansion.md).
Most families have only partial coverage; their remaining candidates are listed below.
Examples are candidate content, not finalized IDs, counts or generation order.

The expansion added 80 new IDs and upgraded no existing IDs:

- [Stock and hardware](item_expansion_stock.md): 20 items, including metal and
  textile stock, fasteners, a hinge and a spring.
- [Electronics](item_expansion_electronics.md): 20 items, including basic
  components, computer assemblies and two household donor devices.
- [Equipment](item_expansion_equipment.md): six tools, ten wearables including
  storage, and four craft-only melee weapons.
- [Household and outdoors](item_expansion_supplies.md): 20 supplies spanning
  containers, shelter, field and repair supplies, food, hygiene and a belonging.

The slice and expansion rosters are references for completed items. Legacy
definitions marked `obsolete` do not count as current coverage; see the
[retirement inventory](item_slice_integration.md).
Their authoring batches no longer define source ownership; use the
[semantic source map](content_pack.md#source-organization) when adding or finding
definitions.

## Authoring decisions to preserve

- Work in coherent batches of roughly 15–25 items, agreeing the exact roster and
  coverage before generation. Count new definitions separately from upgrades.
- Prefer distinct practical types over cosmetic variants or every possible
  material/shape combination. State whether one item is an object, packet,
  handful, roll or other represented unit.
- Keep one general transistor, one servo and one pump. These already exist.
  Motors have deliberately different size classes; tiny, appliance and vehicle
  traction motors already exist. Add further motor classes only for a distinct
  practical role, rather than minor rating/model differences.
- Reuse the general catalog across buildings; add signature equipment and themed
  loot instead of separate police/fire-station versions of ordinary items.
- Advanced pre-collapse salvage should be plentiful in the setting. Historical
  and simple equipment should primarily have obtainable low-tech crafting paths.
- Follow the existing batch skill for descriptions, units, flags, art, supported
  behavior, acquisition, recipes and validation. Future mechanics must be recorded
  as such rather than represented by invented content fields.
- **Reading content was deliberately deferred for the physical-item expansion.**
  The learning system is implemented; plan further reading content in a dedicated
  learning batch. Existing manuals and books remain. Plain paper/cardboard salvage
  is a separate material family.

## A. Material stock, scrap and mechanical hardware

Initial coverage includes steel scrap/sheet, copper wire/tube, aluminum scrap,
rigid plastic scrap, corrugated cardboard, glass shard, rubber hose, cloth patch,
timber plank and the pilot wood scrap.

- [ ] **Copper stock:** foil, winding wire and other useful offcuts/forms.
  Distinguish raw winding stock from a functional coil assembly.
- [ ] **Steel and iron stock:** angle stock, netting and cast-iron fragments.
  New pipe, rod, wire, mesh and chain exist; the retained old metal pipe is obsolete.
- [ ] **Aluminum stock:** angle extrusion, foil and heat sinks.
- [ ] **Wood and engineered wood:** particleboard and veneer.
- [ ] **Plastics, rubber and foam:** plastic sheet/tubing, gaskets and other
  recognizable furniture/appliance fragments.
- [ ] **Paper and packaging stock:** paper sheets, paperboard, cardboard tubes
  and packing paper. Paperboard packaging differs from corrugated shipping boxes.
- [ ] **Glass and ceramics:** intact flat panes, ceramic fragments and insulators.
- [ ] **Textiles and leather:** denim stock, felt and stuffing, where distinct
  from existing cotton, canvas, leather and webbing stock.
- [ ] **Other useful metals:** brass fittings/scrap, lead weights and zinc castings.
  Prefer brass or steel for ordinary gears rather than copper by default.
- [ ] **Mechanical hardware:** washers, brackets, bearings, gears, pulleys and latches.
- [ ] **Machining waste:** metal chips/shavings (swarf), where they have a useful
  role distinct from ordinary scrap.

These groups can span several batches. Favor recognizable furniture/device
sources and meaningful crafting roles over exhaustive material permutations.

## B. Electrical, electronic and composite components

Initial coverage includes a diode, transistor, microcontroller board, desktop
motherboard, three motor sizes, servo and water pump.

- [x] **Basic electronics:** resistor, capacitor and LED added alongside the
  existing diode and transistor.
- [x] **Electrical switching and connections:** switch, relay, fuse, connector
  and complete power cable added.
- [x] **Power and heat components:** transformer, induction coil and heating
  element added. More advanced power-control modules remain below.
- [ ] **Computer parts:** graphics cards and additional distinct storage or cooling assemblies.
- [ ] **Control and interfaces:** other integrated circuits, camera modules,
  speakers, microphones and sensors beyond the display and distance sensor.
- [ ] **Appliance mechanisms:** compressors, solenoids, thermostats,
  valves, drive belts, filters and appliance control boards.
- [ ] **Advanced pre-collapse parts:** compact actuators, sophisticated sensors
  and power-control modules for later futuristic equipment.
- [ ] **Household donor devices/assemblies:** recognizable electronics and
  appliances that provide components through appropriate dismantling routes.

Use general component identities instead of catalogs of chip models or electrical
ratings. Complete donor devices, working assemblies and raw material stock are
different salvage stages.

## C. Clothing, workwear and wearable storage

Initial coverage includes the pilot work shirt, cotton T-shirt, jeans, rain
jacket, leather work gloves, safety goggles and camping backpack.

- [ ] **Everyday clothing:** underwear and other common
  garments with distinct coverage/use.
- [ ] **Weather and outdoor clothing:** thermal layers, boots,
  weather-appropriate gloves and hats.
- [ ] **Workwear and protective equipment:** high-visibility vests and other
  distinct protective gear.
- [ ] **Uniforms and specialist clothing:** police uniforms, firefighter turnout
  clothing, medical scrubs and kitchen clothing.
- [ ] **Wearable storage:** pouches and other distinct carrying equipment.

Material, coverage, warmth, protection and storage are useful design distinctions;
only supported mechanics should be encoded as functional behavior.

## D. Building-specific equipment and loot

The first slice supplies initial police/fire-station gear and reuses its general
catalog across existing buildings. The following location collections remain
incomplete; they are content themes, not a claim that their buildings are absent.

| Collection | Remaining focus |
| --- | --- |
| Police station | Handcuffs, duty equipment, evidence bags and radios |
| Fire station | Broader rescue equipment, protective clothing and emergency supplies |
| School | Distinct science-classroom, gym and general school supplies; additional reading content deferred |
| Restaurant / diner | Commercial kitchen equipment, cookware, utensils, serving supplies and kitchen clothing |
| Hardware store | Broader fasteners, construction stock, repair consumables and tools |
| Clinic / pharmacy | Medicines, diagnostic instruments and medical disposables |
| Auto shop / gas station | Vehicle tools, fluids, replacement parts and roadside supplies |
| Warehouse / logistics depot | Packaging, straps, pallets, labels and handling equipment |
| Garden center / farm supply | Seeds, fertilizer, irrigation parts and gardening tools |
| Electronics / appliance store | Consumer devices, repair supplies and additional salvage assemblies |
| Office / library | Office equipment, storage media and non-reading supplies; additional books/manuals deferred |
| Sporting-goods / camping store | Outdoor clothing, shelter, fishing and camping supplies |
| Laundromat / maintenance room | Cleaning products, plumbing supplies and appliance parts |

Author shared items once and place appropriate subsets in these collections.

## E. Firearms and other weapons

Coverage includes a pump shotgun and shell box, police baton, fire axe,
the pilot arming sword, crafted coil spear and four expansion craft-only weapons.
Firearm use is not implemented; the coil spear currently has ordinary piercing
melee behavior.

- [ ] **Civilian-market firearms:** broader hunting, sporting and common
  personal-defense families, with modest model variety.
- [ ] **Ammunition and magazines:** coherent supporting supplies for the selected
  firearm families, plus maintenance supplies and accessories.
- [ ] **Practical melee equipment:** additional dedicated weapons and useful
  dual-purpose implements with distinct handling/tradeoffs.
- [ ] **Historical and low-tech weapons:** primarily craftable progression using
  obtainable stock and tools rather than reliance on museum loot.
- [ ] **Makeshift sci-fi weapons:** further improvised designs connecting advanced
  components, insulation, switching and power supplies to crafting progression.

Coordinate weapons with their acquisition and supporting parts; avoid isolated
variants or descriptions that imply unsupported attacks.

## F. Survival and outdoor supplies

Initial coverage includes packaged water, empty bottles, rope, a tarp, backpack,
basic medical supplies, batteries and a charger.

- [ ] **Water collection and treatment:** collection equipment,
  filters and purification supplies.
- [x] **Shelter and sleeping:** packed tent, sleeping bag, wool blanket and foam
  mat added; shelter and sleeping actions remain future mechanics.
- [ ] **Fire and lighting:** lighters, flashlights and lanterns.
- [ ] **Navigation and communication:** additional radios and signaling equipment;
  maps belong to the information-content planning discussed below.
- [ ] **Food acquisition:** fishing, trapping and gardening supplies.
- [ ] **Field repair:** broaden cordage, sewing/repair consumables and spare
  fasteners using the shared tool/material families.

## G. Tools and workshop consumables

Initial coverage includes hammer, adjustable wrench, combination pliers, hacksaw,
screwdriver, soldering iron, file, chef knife, pry bar and cloth repair tape.

- [ ] **Garage and automotive tools:** broader vehicle service/repair equipment,
  including useful catalog entries ahead of vehicle gameplay.
- [ ] **Construction tools:** measuring, fastening, cutting and building equipment.
- [ ] **Electronics tools:** measuring and repair tools beyond the soldering iron.
- [ ] **Metalworking tools:** broader cutting, shaping, joining and bench equipment.
- [ ] **Plumbing tools** and associated repair supplies.
- [ ] **Sewing and leatherworking tools** beyond the small sewing kit.
- [ ] **Gardening and agricultural tools.**
- [ ] **Measuring tools:** tape measure, calipers and level.
- [ ] **Sharpening and maintenance:** whetstones, abrasives and brushes.
- [ ] **Consumables:** glue, lubricants, solvents, sandpaper,
  cutting discs, welding rods and sealants.

## H. Food, drinks and cooking supplies

Initial coverage includes chips, chocolate, instant noodles, rice, canned beans,
dill pickles, peanut butter and bottled water.

- [ ] **American snacks and convenience food:** additional candy, snack cakes,
  boxed meals and other recognizable junk food.
- [ ] **Pantry staples:** pasta, flour, dried beans, sugar, salt and oil.
- [ ] **Canned and preserved food:** meat, additional fish, vegetables, soup, fruit, jam and
  meaningful additional pickle/preserve types.
- [ ] **Fresh and refrigerated food:** eggs, milk, cheese, meat and produce.
- [ ] **Drinks:** soda, juice, coffee, tea and energy drinks.
- [ ] **Cooking ingredients:** spices, sauces and baking supplies.
- [ ] **Emergency and outdoor food:** ration bars and dehydrated meals.

Fresh-food placement should account for the intended time since the collapse.
Fictional brands may add personality without proliferating equivalent variants.

## Additional agreed families

- [ ] **Medical and hygiene:** medicines, diagnostic instruments, additional
  dressings/disposables and sanitary supplies beyond toothpaste and toilet paper.
- [ ] **Containers and packaging:** empty cans, crates, bags and other distinct
  containers beyond the jar, bucket, jerrycan and toolbox. Full packaged food is
  not automatically an available empty container.
- [ ] **Power equipment:** power banks, adapters, extension cords, lamps and
  additional useful battery/charging formats beyond the current AA set.
- [ ] **Agriculture and gardening supplies:** seeds, fertilizer and irrigation
  equipment, coordinated with outdoor tools and location collections.
- [ ] **Ordinary belongings:** toys, photographs, jewelry, wallets, keys,
  decorations and hobby supplies. These provide lived-in locations even when
  their immediate survival utility is limited.

## Intentionally deferred: reading and information

The skill and learning system is implemented, and existing manuals and books remain
in the catalog. The user deliberately chose to **keep further reading content
deferred** for this physical-item expansion; the next dedicated learning batch can
plan additional books, newspapers, manuals, reference works, cookbooks, notebooks
and magazines. The information family also includes maps, written records and
recorded data; plan their gameplay role alongside that work. Physical electronics,
storage hardware and plain paper/cardboard stock remain separate categories.

## Content coverage versus mechanics

This backlog tracks missing item content. Existing definitions may already
represent food, clothing, medical supplies, guns, motors or pumps while their
intended behavior is still metadata-only. Their future mechanics are documented
in [the slice record](item_slice.md#implemented-versus-descriptive); do not create
duplicate item definitions merely because those systems are unfinished.

When taking a group into production, check the current catalog and this document,
choose a concrete roster, and link its completed batch record here. Cross-listed
themes such as medical supplies, agriculture and repair should share definitions.
