# Skills, books and learning content

## Shipped starter content

`data/skills/core.json5` defines the noncombat skills Cooking (`cooking`, maximum 5),
Chemistry (`chemistry`, maximum 10), Fabrication, Mechanics, Electronics, Computers, Tailoring,
Survival and Medicine, plus nine combat skills: Melee, Unarmed, Knives, Swords,
Clubs & Hammers, Axes, Polearms, Shields and Dodge. The main
and FirstHouse packs include these definitions; activity coverage is a starter
slice, not a complete ten-tier progression for each skill.

In `data/crafting/materials.json5`, `cut_timber_scrap` is ungated level-0 Fabrication
training toward level 1. In `data/crafting/weapons.json5`, `make_coil_spear`
requires Fabrication 1 and a reference
or knowledge, and explicitly autolearns at Fabrication 2. Its independent,
ungated `disassemble_coil_spear` recycler trains Fabrication at level 0 and
teaches the spear recipe through disassembly. Both arming-sword producers require
Fabrication 4, suitable bar stock, anvil 2, hammering 2, metal holding 2 and a
fueled forge; both are known by default. The [workshop roster](workshop_rosters.md)
supplies level-2/3 useful practice and Electronics-1/2 learning activities.
Power-supply dismantling now requires/trains Electronics 1; water-pump dismantling
requires/trains Mechanics 1. Their work and salvage outputs are preserved.

`data/books/starter.json5` defines `workshop_primer_text` and
`salvage_notes_text`; both reference `make_coil_spear` and have distinct study
benefits. Their printed carriers are `workshop_primer_print` and
`salvage_notes_print`. `workshop_ebook_reader` carries `workshop_primer_text`,
sharing its once-per-character benefit with the printed primer; its own `cell`
must have charge. FirstHouse offers the manuals through debug spawning.

The separate `boxing_manual` and `weapon_forms_manual` carriers appear in
ordinary loot; their repeatable art-acquisition attempts are distinct from ordinary
book benefits. See [combat content](combat_content.md#shipped-identities-and-starter-tuning)
and [combat learning](combat_learning.md). Town has 18 skills and 203 items;
the demo has 17 items, including the generated corpse container. These counts
describe the current packs, not fixed content limits. See
[catalog membership](content_pack.md#catalog-membership) for FirstHouse and
recipe/object totals.

## Authoring schema

Definitions use the existing JSON5 `format_version: 1` document and `definitions`
array. IDs are globally unique, references resolve across files, and invalid or
duplicate fields/references reject the load. See [crafting content](crafting_content.md)
for the underlying item and recipe schema. For example, the following definitions
can appear together in that array:

```json5
{type:'skill', id:'fabrication', name:'Fabrication'}
{type:'item', id:'panel', name:'Panel', mass_g:100, volume_ml:100}
{type:'recipe', id:'make_panel', inputs:[], result:{item:'panel',count:1}, work:100,
 skills:[{skill:'fabrication',level:1}],
 knowledge:{autolearn:[{skill:'fabrication',level:2}], crafts_to_learn:2,
            disassemblies_to_learn:3}}
{type:'book', id:'panel_manual', name:'Panel manual', study_work:1000,
 benefits:[{skill:'fabrication',first_level:1,last_level:5,
            direct_xp:20,multiplier:20000}], recipes:['make_panel']}
{type:'item', id:'panel_manual_print', name:'Printed manual', mass_g:100, volume_ml:100,
 books:['panel_manual']}
```

Add commas between entries in the actual array. The recipe requires level 1;
its autolearn threshold is level 2. The manual is a reference before and after
study, but studying it does not itself memorize the recipe.

## Skill and book definitions

`skill` requires `id` and `name`; `category` is `noncombat` (default) or `combat`.
The catalog accepts at most 4095 skill definitions (resolved IDs 1–4095).
Optional `max_level` is an integer in 1–10, default 10. Optional `xp_to_next`
contains exactly `max_level` integers, each 1–1000000000, indexed from level 0.
An omitted curve uses the default curve through that maximum. Field order does
not matter. The default curve is listed in [runtime skills](skills.md).

```json5
{type:'skill', id:'cooking', name:'Cooking', max_level:5,
 xp_to_next:[100,200,400,800,1200]}
{type:'skill', id:'chemistry', name:'Chemistry'}
```

Food preparation/preservation uses Cooking; chemical processing, reagents and
material production use Chemistry. Use both only when the operation needs both.
The removed combined identity has a bounded save migration, not a content alias
or shared XP pool.

`book` requires `id`, `name`, and `study_work` (1–UINT32_MAX). `benefits` and
`recipes` default to empty. Each benefit requires a unique `skill`, `first_level`
and `last_level` in 1–the referenced skill's maximum, inclusive and ordered. Optional `direct_xp` and
`theory_xp` default to zero and range through 1000000000; `multiplier` defaults
to 10000 and ranges from 10000 to 40000. Combat benefits require zero direct and
theory XP and multiplier at most 12000. Ranges refer to the next level earned.

Book `recipes` is a list of recipe IDs, not skill benefits or an automatic
memorization list. Duplicate recipe IDs within one book are invalid, but distinct
books may list the same recipe. Items may list multiple distinct book IDs in
`books`; different physical or electronic carriers of one book ID share that
information identity and its once-per-character benefit.

An electronic carrier can specify `reading_power` naming its own energy-store
capability; it must also list at least one book. For example:

```json5
{type:'item', id:'panel_reader', name:'Reader', mass_g:100, volume_ml:100,
 books:['panel_manual'], reading_power:'battery',
 capabilities:[{id:'battery',type:'energy_store',capacity_energy:10,rechargeable:false}]}
```

The store must have positive energy to use its references; no reading energy is
currently consumed.

## Recipe gates and training

Crafting recipes and both explicit and generated recyclers accept `skills` and
`training` arrays. At most four required skills and two training entries are
allowed. Each skill requirement is `{skill:'id', level:1}` with level
1–the referenced skill's maximum. When `training` is omitted, every required
skill trains at its required level with default `xp_per_work:100` (0.01 XP per
base work). More than two requirements therefore need an explicit training
selection; omission is rejected rather than silently choosing rewards.

A present `training` array is the **complete selection**, not a set of overrides.
`training:[]` grants no skill XP, even when requirements exist. An entry has
`skill`, `level` 0–the skill's maximum and optional `xp_per_work` 0–10000.
For a required skill its level must match that requirement.
An independent skill without a requirement must use training level 0; higher-tier
training needs the matching learned-skill gate.
Duplicates in an array are invalid. A zero rate disables that training reward
but still occupies one of the two selection slots.

Crafting `knowledge` accepts `known_by_default`, `autolearn`, `crafts_to_learn`
and `disassemblies_to_learn`. Omitting the whole object preserves legacy
`known_by_default:true`; supplying `knowledge:{}` defaults it to false. Autolearn
is enabled only when its array is present, even when empty; at most four thresholds
are allowed. Its skill thresholds
are 0–the skill's maximum and must not undercut the same recipe requirement. It requires both
recipe gates and all listed thresholds. Counts default to 2 and 3 respectively,
and each must be 1–65535.

Array counts are checked before resolving entries. Skill-specific level limits
are checked after resolving references, including forward references, generated
recyclers, inferred training and combat/manual prerequisites.
Over-cap references are errors, never silently lowered. Cooking-5 recipes remain
usable even though their default training cannot advance an already qualified cook;
recipe memorization and other eligible skill training still apply.

Recycler `teaches_recipes` explicitly lists distinct recipe IDs, including an
empty list to teach none. If omitted, a generated recycler teaches its generating
recipe; an explicit recycler teaches the single matching output recipe. With
multiple matches, an explicit list is required if any matching recipe is gated;
legacy all-known matches default to an empty list. With no matches it is empty.
This inference affects memorization, not the recycler's independent skill gates
or returned items.
