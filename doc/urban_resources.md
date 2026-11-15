# Urban resources: building author checklist

These are shared definitions for building patterns. `data/content.json5` includes
the five urban/specialist sources together with the shared demo items, household
items, crafting trait, household loot, furniture, cellar monsters, and AI profiles.
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
volume of at most 6,000 ml per roll, including nested household pools. Capacity
validation sums the actual worst-case volumes of all nonzero-chance markers and
rolls targeting a furniture tile: 100,000 ml is ample for 16 rolls at this
conservative bound, but is not an unlimited budget. For ground loot use
`destination:'ground'`; furniture loot requires capacity on that exact tile.

| ID | Theme |
| --- | --- |
| `loot_food` | packaged food |
| `loot_kitchen` | utensils and kitchen supplies |
| `loot_cleaning` | soap, detergent, linens and bucket |
| `loot_medical` | bandages, gauze, scissors and pouch |
| `loot_tools` | hand tools |
| `loot_hardware` | fasteners, materials and workshop supplies |
| `loot_electronics` | batteries, cords and components |
| `loot_clothing` | work clothing and sewing supplies |
| `loot_books` | books and notebooks |
| `loot_office` | stationery, books and electronics |
| `loot_fire_station` | emergency tools, medical supplies and rope |
| `loot_police_station` | office and emergency supplies |
| `loot_auto_repair` | tools, hardware and parts storage |
| `loot_outdoors` | containers, rope, pack and provisions |
| `loot_warehouse` | hardware, packing and provisions |
| `loot_school` | stationery, books, toys and snacks |
| `loot_laundry` | cleaning, clothing and linens |
| `loot_pantry` | food and pantry staples |
| `loot_utility` | tools, cleaning and electrical supplies |
| `loot_preparedness` | medical, outdoor and food supplies |
| `loot_hobby` | books, sewing, toys and hardware |

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

Food, clothing, books, and medical supplies are presently props or crafting
inputs; their names do not confer eating, wearing, reading, or healing mechanics.
The listed containers expose ordinary item storage, and cutting tools supply the
existing `house_cutting` trait. Existing item and battery behavior is unchanged.

| ID | Name | ID | Name |
| --- | --- | --- | --- |
| `dry_pasta` | dry pasta | `rice_packet` | packet of rice |
| `cereal_box` | box of cereal | `snack_packet` | packet of snacks |
| `coffee_tin` | tin of coffee | `tea_box` | box of tea |
| `cooking_oil` | bottle of cooking oil | `flour_bag` | bag of flour |
| `frying_pan` | frying pan | `serving_spoon` | serving spoon |
| `dish_towel` | dish towel | `detergent_box` | box of detergent |
| `scrub_brush` | scrub brush | `plastic_bucket` | plastic bucket (container) |
| `gauze_roll` | roll of gauze | `medical_gloves` | box of medical gloves |
| `medical_scissors` | medical scissors (cutting) | `wrench` | wrench |
| `pliers` | pliers | `utility_knife` | utility knife (cutting) |
| `wood_handle` | wooden handle | `wire_bundle` | bundle of wire |
| `scrap_knife` | scrap knife (cutting) | `bolts` | handful of bolts |
| `duct_tape` | roll of duct tape | `electrical_cord` | electrical cord |
| `circuit_board` | circuit board | `work_shirt` | work shirt |
| `work_trousers` | work trousers | `apron` | apron |
| `work_gloves` | work gloves | `paperback_book` | paperback book |
| `reference_book` | reference book | `notebook` | notebook |
| `pencil` | pencil | `paper_stack` | stack of paper |
| `plastic_bottle` | empty plastic bottle (container) | `canteen` | empty canteen (container) |
| `rope_coil` | coil of rope | `sewing_kit` | sewing kit |
| `toy_blocks` | box of toy blocks | `first_aid_pouch` | first aid pouch (container) |
| `parts_box` | empty parts box (container) | | |

## Crafting and recycling

| ID | Name | Effect |
| --- | --- | --- |
| `carve_wood_handle` | Carve a wooden handle | plank to handle with cutting |
| `salvage_wire_bundle` | Salvage a bundle of wire | cord to wire with cutting |
| `make_scrap_knife` | Make a scrap knife | handle, wire and scrap to a cutting tool |
| `cut_up_work_shirt` | Cut up a work shirt | recycle shirt into three clean rags with cutting |
| `cut_up_work_trousers` | Cut up work trousers | recycle trousers into four clean rags with cutting |
