# Urban resources: building author checklist

These are shared definitions for building patterns. `data/content.json5` includes
urban/specialist sources and the shared semantic item/crafting catalog, including
power and workshop equipment, together with demo dependencies, household loot,
furniture, cellar monsters, and AI profiles. See
[source organization](content_pack.md#source-organization) for current ownership.
References resolve across sources.
IDs below are exact; names are display names. See `house_content.md` for placement
rules and `crafting_content.md` for recipe semantics.

## Furniture

All of these block movement. Refrigerator and metal locker also block sight;
only the counter and refrigerator are immovable. Storage furniture has 100,000 ml
capacity; office chair and examination bed have no storage. Beds use `generic`,
not a dedicated bed kind. The washing machine is storage only. For light, reuse
the existing `cellar_lamp`; `house_chair` is also available.

| ID | Name | Kind | Storage |
| --- | --- | --- | --- |
| `store_shelf` | store shelf | generic | yes |
| `store_counter` | store counter | table | yes |
| `refrigerator` | refrigerator | generic | yes |
| `metal_locker` | metal locker | generic | yes |
| `office_desk` | office desk | table | yes |
| `office_chair` | office chair | chair | no |
| `bookcase` | bookcase | generic | yes |
| `examination_bed` | examination bed | generic | no |
| `school_desk` | school desk | table | yes |
| `restaurant_table` | restaurant table | table | yes |
| `washing_machine` | washing machine | generic | yes |
| `industrial_workbench` | industrial workbench | table | yes |
| `warehouse_crate` | warehouse crate | generic | yes |
| `utility_cabinet` | utility cabinet | generic | yes |
| `display_rack` | display rack | generic | yes |

The detail pass also adds `house_sofa`, `house_toilet`, `kitchen_sink`,
`kitchen_stove`, and `house_bathtub` in shared furniture definitions. All five
block movement and have no storage; use actual storage furniture for loot.

## Loot pools

Each invocation selects one weighted alternative; nested pools make independent
draws where counts request them. Every pool below has a worst-case external
volume of at most 6,000 ml per roll, including nested household pools, **except
`loot_traction_motor_ground` (45,000 ml, ground-only)**. Capacity
validation sums the actual worst-case volumes of all nonzero-chance markers and
rolls targeting a furniture tile: 100,000 ml is ample for 16 rolls at this
conservative bound, but is not an unlimited budget. For ground loot use
`destination:'ground'`; furniture loot requires capacity on that exact tile.

| ID | Theme |
| --- | --- |
| `loot_food` | packaged food |
| `loot_kitchen` | utensils and kitchen supplies |
| `loot_cleaning` | soap, cloth patches and storage box |
| `loot_medical` | cloth bandages, sterile gauze and antiseptic |
| `loot_tools` | hand tools |
| `loot_hardware` | typed material stock and workshop supplies |
| `loot_electronics` | batteries, boards, small components and motors |
| `loot_clothing` | work clothing and sewing supplies |
| `loot_reading_room_supplies` | visitor supplies, workshop/salvage references and combat manuals |
| `loot_office` | visitor supplies, cardboard and electronics |
| `loot_fire_station` | emergency tools, medical supplies and rope |
| `loot_police_station` | baton, shotgun, shells, office and medical supplies |
| `loot_auto_repair` | tools, hardware, appliance motors, pumps and parts storage |
| `loot_traction_motor_ground` | one large motor; dedicated garage floor placement |
| `loot_outdoors` | containers, rope, pack and provisions |
| `loot_warehouse` | hardware, packing and provisions |
| `loot_school` | visitor supplies, electronics and snacks |
| `loot_laundry` | cleaning, clothing and linens |
| `loot_pantry` | food and pantry staples |
| `loot_utility` | tools, cleaning and electrical supplies |
| `loot_preparedness` | medical, outdoor and food supplies |
| `loot_hobby` | sewing, small actuators, electronics and hardware |
| `loot_legacy_recipe` | low-weight wire/cord sources for unchanged legacy recipes |

## Monster groups and inhabitants

All specialists use `sighted_zombie`, simple bodies and the undead faction.
Speed is the existing action timing value (smaller is faster). These definitions
do not add armor, healing, gear or extra health. Each group includes the existing
`cellar_zombie`; weighted specialties dominate matching professional sites.

| Group ID | Favored inhabitants |
| --- | --- |
| `monsters_residential` | cellar zombie, with shoppers and office workers |
| `monsters_shoppers` | shopper |
| `monsters_medical` | nurse, then doctor |
| `monsters_fire_station` | firefighter |
| `monsters_police_station` | police |
| `monsters_workshop` | mechanic and construction worker |
| `monsters_office` | office worker |
| `monsters_school` | office worker and shopper |
| `monsters_outdoors` | outdoors worker |
| `monsters_restaurant` | cook |
| `monsters_warehouse` | warehouse worker |

| Monster ID | Name | Speed | Damage | Accuracy |
| --- | --- | ---: | ---: | ---: |
| `zombie_firefighter` | firefighter zombie | 120 | 16 | 9000 |
| `zombie_nurse` | nurse zombie | 90 | 8 | 9000 |
| `zombie_doctor` | doctor zombie | 100 | 10 | 9000 |
| `zombie_police` | police zombie | 100 | 14 | 9000 |
| `zombie_mechanic` | mechanic zombie | 105 | 13 | 9000 |
| `zombie_construction_worker` | construction worker zombie | 110 | 15 | 9000 |
| `zombie_cook` | cook zombie | 100 | 11 | 9000 |
| `zombie_office_worker` | office worker zombie | 100 | 10 | 9000 |
| `zombie_shopper` | shopper zombie | 100 | 10 | 9000 |
| `zombie_warehouse_worker` | warehouse worker zombie | 105 | 14 | 9000 |
| `zombie_outdoors_worker` | outdoors worker zombie | 95 | 12 | 9000 |

## Items

Town loads 203 item definitions, including generated runtime items and retained
obsolete dependencies. The [catalog membership table](content_pack.md#catalog-membership)
compares the packs. The [broad slice](item_slice.md), [expansion](item_expansion.md)
and [workshop roster](workshop_rosters.md) record their respective additions;
the [retirement inventory](item_slice_integration.md) records dependency-only
`obsolete` entries.

Clothing supports wearing, authored protection and encumbrance. Containers expose
ordinary item storage; `hacksaw` and `chef_knife` provide `house_cutting`. AA batteries
and `battery_charger` use the energy-store/charger behaviors. Food and medical
classifications alone do not provide eating or healing actions.

Printed workshop/salvage references and combat manuals occur in reading-room and
hobby loot; `workshop_ebook_reader` occurs in electronics loot. Ordinary book
study, recipe references and shared printed/electronic learning identity are
implemented; see [skill content](skills_content.md). D3's transferable digital
packages and printer libraries follow their separate accepted design.

## Crafting and recycling

The following compatibility routes retain their obsolete dependency identities
in `data/crafting/urban.json5`. Current-material bandage, stock-preparation,
forging and spear routes live in the semantic crafting sources; see
[source organization](content_pack.md#source-organization) and the
[workshop roster](workshop_rosters.md) for current ownership and requirements.

| ID | Name | Effect |
| --- | --- | --- |
| `carve_wood_handle` | Carve a wooden handle | plank to handle with cutting |
| `salvage_wire_bundle` | Salvage a bundle of wire | cord to wire with cutting |
| `make_scrap_knife` | Make a scrap knife | handle, wire and scrap to a cutting tool |
| `cut_up_work_shirt` | Cut up a work shirt | recycle shirt into three clean rags with cutting |
| `cut_up_work_trousers` | Cut up work trousers | recycle trousers into four clean rags with cutting |
