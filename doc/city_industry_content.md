# City commercial and industrial sites

Authored in `data/worldgen/buildings/city_industry.json5`; new pools live in
`data/worldgen/loot/city_industry.json5`. These are opportunities, **not
per-settlement guarantees**. Minimum size is an eligibility gate, not a promise.

## Roster and pre-D4 population anchors

Ground counts as an occupied floor; roofs do not. Numbers below sum **all**
authored indoor markers across all occupied floors, before the generator's single
5×/6×/7× settlement multiplier. No yard markers add another indoor budget.

| Stable ID | Era / minimum size | Footprint / floors | Exact baseline |
| --- | --- | --- | --- |
| `covered_market_arcade` | Older / small | 26×15 / 1 | Ground: 1 + 1 + 0.5 = **2.5** |
| `electronics_service_center` | Newer / small | 24×14 / 1 | Ground: 0.8 + 0.7 = **1.5** |
| `retrofitted_foundry` | Older / small | 24×13 / 1 | Ground: 1 + 1 + 0.5 = **2.5** |
| `automated_fabrication_plant` | Newer / large | 32×18 / 2 | Ground 1 + 1 + 1 + 0.5; upper 0.75 + 0.75 = **5.0** |
| `clean_assembly_laboratory` | Newer / large | 24×15 / 2 | Ground 1 + 0.75; upper 1 + 0.75 = **3.5** |
| `neighborhood_compute_house` | Older / small | 16×11 / 1 | Ground: **0.8** |
| `device_reclamation_depot` | Older / small | 24×14 / 1 | Ground: 0.8 + 0.7 = **1.5** |

Market shoppers span the food/hardware passage. Electronics and reclamation split
customers from repair staff; reclamation shares the electronics-service family
and acquisition role. Foundry workers occupy the hot shop and annex stock store.
The larger plant supports production, loading and upper controls; the lab splits
assembly staff and technical offices across both floors. The house-sized compute
site has only a possible maintenance worker: it is industrial **infrastructure**,
never housing or a residential-loot exception.

Only existing `monsters_shoppers`, `monsters_workshop`, `monsters_warehouse` and
`monsters_office` groups are used (defined in `data/monsters/specialists.json5`).
Their selections each represent one actor per successful baseline marker.

## Routes and useful rewards

- **Arcade:** walk-through public passage, four distinct food/clothing/hardware/
  electrical-tool units and two rear loading stores. Public front/back openings
  and independent rear staff doors let players bypass individual shop floors.
- **Electronics service:** glazed display frontage, connected repair benches,
  component room and rear service door. One `home_polymer_printer_station`;
  two independent, guaranteed ground draws of `loot_printer_filament_large`
  supply **2–4 kg total**, without a settlement-size quantity multiplier.
- **Reclamation:** intake counter and waiting rows lead through a passive
  checkpoint to returned-device sorting and repair rooms. Separate rear loading
  doors; one supplied-design home printer and the same two independent filament
  draws. Notice boards/checkpoint communicate service dependency, not a hacking
  action or real access restriction.
- **Foundry:** soot-brick hot shop contains functional `workshop_anvil`,
  `charcoal_forge` and `grid_scrap_press`. Riveted service annex supplies welding,
  forging stock, repair tools, protection and workshop references. Rear concrete
  loading yard offers another entrance; machines still require their normal setup,
  power and feedstock.
- **Plant:** ribbed-steel production hall, rear asphalt loading yard, stores,
  maintenance/locker rooms and matched stairs to controls and precision benches.
  Existing motors, pumps, welding supplies, metal stock, controls and backup-power
  parts have current uses. `fabrication_cell` and `precision_assembly_bench`
  are passive fixtures; advanced production/software recovery remains **pending**.
- **Laboratory:** reception and seating, controlled assembly rooms, parts stores,
  lockers/washroom and upstairs technical offices. Rear service access and matched
  stairs offer ordinary walking routes. Precision electronics, soldering supplies,
  sheet materials and references are usable now; advanced manufacture and protected
  information acquisition remain **pending**, without token unlock items.
- **Compute house:** pale-brick service shell and block partitions separate racks,
  cooling, repair and backup-power stores. Wiring, fans, power supplies, memory,
  storage and control parts are salvage opportunities. Racks/cooling/terminals are
  passive, not free powered services or new computing capabilities.

Every footprint has a secondary ordinary ground door. All rooms connect without
moving furniture; stairs match between occupied floors and no roof has stairs.
Yards are explicitly exterior; roof surfaces and air above yards are `space:none`.

## Specialist pools and storage

New compact pools are `loot_city_repair_tools`, `loot_city_control_components`,
`loot_city_compute_spares`, `loot_city_returned_devices`,
`loot_city_precision_stock`, `loot_city_maintenance_stock` and
`loot_city_workshop_references`. Their existing-item themes distinguish tools,
controls, compute salvage, appliance returns, assembly materials, service stock
and supported basic references. Draws are weighted alternatives, not every listed
item at every site.

New `loot_city_foundry_stock_ground`, `loot_city_fabrication_stock_ground` and
`loot_city_backup_power_ground` pools are **ground-only**. Existing bulky forging,
welding, electrical and filament pools also stay on the ground. Compact furniture
draws include count/roll bounds and cumulative pools on the same fixture; the
largest authored fixture bound is **12,500 ml**, below the 100,000 ml specialist
storage limit. No new items or dependency changes are required.

## Palette and artwork status

Weathered brick/aged plaster and fresh rear infill identify the arcade; repaired
brick identifies reclamation; soot brick and riveted steel distinguish the foundry
retrofit. Pale brick/block identify compute infrastructure. Composite/glazed
electronics frontage, ribbed plant walls, insulated lab walls, steel/sealed/epoxy/
clean-room floors and concrete/asphalt yards form coherent material zones.

Terminal names/glyphs provide the functional presentation. New terrain/object SDL
art status is **Brief pending**: retain connected masonry/panel edges, glazed versus
opaque door states and rotated entrances; distinguish racks, cooling equipment,
service notices/checkpoints, fabrication cells and precision benches. Artwork must
not imply unsupported production, hacking, permanent power or clean-room effects.

## Integration

Register both new JSON5 paths in the parent content manifests. Preserve family
membership (`device_reclamation_depot` belongs to `electronics_service_center`),
large-only plant/lab eligibility, occupied-floor counts and single application of
the population multiplier. This content does not alter manifests, existing data,
source code or items. No builds or tooling tests are part of this authoring task;
engine/content-loader and generated-world verification belong to integration.
