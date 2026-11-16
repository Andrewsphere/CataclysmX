# Broad item slice — batch plan and integration

## Agreed scope

First milestone: **60 new definitions**, in three 20-item authoring batches,
plus preservation of the five approved pilots (`wood_scrap`, `claw_hammer`,
`glass_bottle`, `work_shirt`, `arming_sword`). The complete category roadmap is
larger than this milestone. Books, newspapers, manuals and similar reading
material are deferred until the skill system is established.

The remaining agreed families and candidate batches are recorded in the
[item content backlog](item_backlog.md).

Use one general transistor, servo and water pump, with distinct tiny, appliance
and vehicle-traction motors. Avoid cosmetic model proliferation. Each entry has
an explicit represented unit, physical values, concise description, descriptive
flags and canonical external colored text art. Unsupported effects remain
metadata-only; this pass adds no new simulation subsystem.

## Coverage and file ownership

| Batch | New IDs | Definition source | Art/review record |
| --- | ---: | --- | --- |
| Materials and electronics | 20 | `data/items/slice_materials.json5` | `doc/item_slice_materials.md` |
| Tools, clothing and weapons | 20 | `data/items/slice_equipment.json5` | `doc/item_slice_equipment.md` |
| Food, medical and survival | 20 | `data/items/slice_supplies.json5` | `doc/item_slice_supplies.md` |

Each author owns their definition source, their 20 art directories, and their
review record. The coordinator owns shared flags, legacy retirement, manifests,
loot/recipe integration and aggregate verification. Item batches are work rosters,
not loot pools; buildings reuse appropriately themed subsets.

### Materials and electronics roster

`steel_scrap`, `copper_wire`, `copper_tube`, `steel_sheet`, `aluminum_scrap`,
`rigid_plastic_scrap`, `corrugated_cardboard`, `glass_shard`, `rubber_hose`,
`cloth_patch`, `timber_plank`, `transistor`, `diode`, `microcontroller_board`,
`desktop_motherboard`, `tiny_electric_motor`, `appliance_electric_motor`,
`traction_electric_motor`, `servo`, `water_pump`.

### Tools, clothing and weapons roster

`adjustable_wrench`, `combination_pliers`, `hacksaw`, `flathead_screwdriver`,
`soldering_iron`, `metal_file`, `chef_knife`, `pry_bar`, `cotton_tshirt`,
`denim_jeans`, `rain_jacket`, `leather_work_gloves`, `safety_goggles`,
`fire_axe`, `police_baton`, `pump_shotgun`, `shotgun_shells`, `coil_spear`,
`cloth_repair_tape`, `camping_backpack`.

### Food, medical and survival roster

`canned_beans`, `dill_pickles`, `potato_chips`, `chocolate_bar`,
`instant_noodles`, `long_grain_rice`, `peanut_butter`, `bottled_water`,
`antiseptic`, `sterile_gauze`, `cloth_bandage`, `bar_soap`, `sewing_supplies`,
`nylon_rope`, `camping_tarp`, `steel_water_bottle`, `plastic_storage_box`,
`aa_alkaline_battery`, `aa_rechargeable_battery`, `battery_charger`.

## Legacy policy

The user explicitly requests retirement of the old non-pilot catalog. Concrete
replacement identities above are deliberate replacements, not accidental aliases.
Retired **44 legacy definitions** and retained **22 recipe/demo dependencies**
with flag ID `obsolete`, displayed as **Obsolete**.
The flag is editorial metadata, not a hidden-item or automatic migration mechanic.
The pilot definitions remain current. Every shipped pack loads independently.
The [integration inventory](item_slice_integration.md) names every removal and
remaining dependency, including six deliberately retained legacy loot identities.
The old hand saw was removable because its last non-loot references were tests;
the current hacksaw satisfies the original pilot's cutting requirement.

## Completed integration

All **60 new items, 60 colored illustrations, four recipes and five recyclers**
are integrated. There are no upgraded legacy entries in this milestone: the five
pilots are preserved, replacements are new concrete identities, and retained old
definitions are explicitly obsolete. Town has 65 current authored items,
22 obsolete definitions, and the internal unfinished-craft wrapper (88 total).

- 59 new items have actual Town loot placements, using existing themed pools.
- The coil spear is craft-only; every required material and retained tool is
  obtainable through current loot without obsolete inputs.
- Current recipes turn cloth patches into bandages, timber into pilot offcuts,
  typed stock into the pilot sword, and timber/steel/wire/tape into a coil spear.
- Recycling shirts, jeans, sheet steel, appliance motors and pumps supplies
  current stock with mass-bounded returns.
- Three motor scales are 20 g, 4 kg and 60 kg. The 45-liter traction motor has a
  dedicated auto-repair ground placement; it is not nested in container loot.
- FirstHouse shares the updated household items/loot and current crafting sources;
  its guaranteed cutting tool is the chef knife. Demo/showcase packs stay narrow.
- Books, notebooks, newspapers and manuals remain deferred; reading rooms use
  ordinary visitor supplies. No replacement reading content was authored.

The [integration table](item_slice_integration.md) records exact recipe IDs,
inputs, yields, tools, work and acquisition contexts. The per-batch authoring
records above supply unit/role/progression tables, physical reasoning and complete
monochrome masters. New art uses the generic map-icon fallback.

### Description tone follow-up

The initial 60 descriptions were mostly factual. A follow-up adds three deliberate
pieces of dry apocalypse humor: `corrugated_cardboard` (the scope of a fragile
warning), `cloth_repair_tape` (temporary repairs becoming infrastructure), and
`canned_beans` (a serving suggestion assuming civilization). These are three
items across the whole 60-item generation, not three per sub-batch.

The authoring skills and guide now make atmosphere, implied history and personality
the primary purpose of descriptions, with visual/practical detail secondary.
This follow-up changes those three descriptions; it is not a full prose rewrite
of the other 57 entries.

The description-only follow-up passed **2/2 existing content checks** using
`./scripts/test.ps1 -Preset win-msvc-sdl -R '(Town slice publishes|All shipped manifests)'`.
The skills were reviewed as Markdown; no tooling tests were added or run.

### Implemented versus descriptive

Storage, battery charging, cutting traits, crafting/recycling and authored melee
use existing gameplay systems. Food/drink consumption, healing, clothing/weather
protection, firearm firing/ammunition, shelter, repair, motor/pump operation and
electrical spear effects are **metadata-only or future mechanics**. The coil spear
currently deals ordinary piercing melee damage. Packaging does not create free
empty containers or liquid behavior.

## Verification

Initial slice integration build and focused test command, from the repository root:

```powershell
./scripts/build.ps1 -Preset win-msvc-sdl -SkipTests
./scripts/test.ps1 -Preset win-msvc-sdl -R '([Ss]lice|item pilot|[Cc]ontent|[Cc]rafting|[Rr]ecycl|[Hh]ouse|[Uu]rban)'
```

Build succeeded; **126/126 selected CTest cases passed** in the final integration run
(43.34 seconds). This includes the eight new content/gameplay cases and the new
SDL rendering case, independent manifest loading, original pilot progression,
and relevant existing content/crafting/recycling/house/urban cases. This is a
focused run, not a claim that the entire suite was run. No tooling tests ran.

All 60 assets were checked against their monochrome masters for decoded glyphs
and spacing, allowed glyphs, declared colors and dimensions. Renderer validation
produced 120 previews (60 items at two sizes). The coordinator inspected all 60
wide-size illustrations in contact sheets and individual previews, and sampled
compact previews. Eight silhouettes were redrawn after that review: wrench,
pliers, axe, baton, shotgun, spear and both AA cells. Their updated wide previews
were inspected individually; the corrected art passed the renderer case and the
final focused test run. Tall assets intentionally scroll at compact size.

To regenerate the individual previews (ensure `out/` exists):

```powershell
$env:CATACLYSMX_ITEM_SLICE_PREVIEW = "$PWD/out/item-slice"
./scripts/test.ps1 -Preset win-msvc-sdl -R 'SDL item slice'
Remove-Item Env:CATACLYSMX_ITEM_SLICE_PREVIEW
```

Files are `out/item-slice_<id>_480.png` and `_1120.png`. The review also produced
local contact sheets at `out/item-slice_materials_contact.png`,
`out/item-slice_equipment_contact.png`, and `out/item-slice_supplies_contact.png`.
These are generated review artifacts, not canonical assets.
