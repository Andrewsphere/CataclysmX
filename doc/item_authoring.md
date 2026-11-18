# Item authoring

This guide is an editorial workflow. The current loadable schema is authoritative in
[content format](content_format.md), [item presentation](item_presentation.md),
[crafting content](crafting_content.md), and [combat content](combat_content.md);
[content packs](content_pack.md) describes manifest integration. Check the actual
authored data and implementation when those
sources disagree. Unknown fields are errors.

## Identity, physical scale, and setting

1. Search all definitions and references before naming anything. IDs are stable,
   globally unique across ordinary definition kinds, and match `[a-z][a-z0-9_]*`; reuse or
   improve an existing item instead of adding a duplicate. Do not casually rename
   IDs referenced by saves, recipes, or loot.
2. Say what one item means: one object, one packet, a handful, a roll, etc. Counts
   in recipes and loot count those units, not individual nails inside a handful.
   Set integer `mass_g` and positive external `volume_ml` for that entire unit,
   including packaging where appropriate. Container capacity is separate from its
   external volume; compare like units and existing items before changing stats.
3. Write one to three concise sentences whose primary purpose is **atmosphere
   and vibes**. Give the object a place in the world: implied history, human
   habits, a lost everyday comfort, a survivor's new use for it, or a promise
   that outlived its maker. Visual
   details and practical use support that purpose; the description should offer
   more than a verbal version of the illustration or stats. Keep its identity
   clear and avoid implying unsupported mechanics.
4. The setting is a futuristic apocalypse with plentiful advanced pre-collapse
   salvage. Historical or simple equipment is primarily accessible through
   plausible low-tech crafting without an operating civilization, rather than
   primarily museum loot. Make crafting inputs and tools obtainable along the path.

## Description voice and restrained humor

Prefer a specific, evocative detail over a list of materials, shapes and features.
An ordinary object can feel familiar, comforting, neglected or absurd without a
full backstory. Do not make every item tragic or append the same observation about
the end of the world. Keep essential distinctions clear (such as full versus
empty packaging or nonsterile cloth), while leaving numeric specifications to
their fields and the inspection UI.

Sprinkle in understated humor or satire about the world's end: roughly **2–3
deliberately humorous descriptions in a 60-item generation** is the desired
scale, not a punchline on every item. This is a shared editorial target across
subagents and sub-batches, not a per-agent quota or loader rule. Smaller batches
need not force a joke. Use object-specific irony, failed marketing promises or
mundane bureaucracy; avoid repetitive quips and fourth-wall references.

For example, the cloth repair tape's description is:

> A roll of cloth repair tape, sold for temporary fixes. The temporary fixes are
> now the infrastructure.

Record the selected humor IDs in the batch report so the final tone pass can
check the whole collection. Atmosphere remains the purpose of the non-humorous
descriptions too; humor is only one available tone.

### A survivor's perspective

**Survival ingenuity** is another way to create atmosphere: suggest why an
ordinary object suddenly matters, why it deserves space in a pack, or what a
resourceful survivor might see in something others discarded. Ask: *What would
someone living among the dead notice or save this for?*

Use one concrete possibility or need rather than a feature list. Scarcity,
preparation, improvised repairs, shelter, recovering useful parts and preserving
small comforts can all supply a hook. An item need not become a weapon to be
worth finding. This perspective can replace a forced joke while retaining
personality and the sense of a changed world.

Illustrative voice examples:

- **Cloth patch:** "A clean scrap of cotton. Worth keeping for the day the
  bandages run out."
- **Appliance motor:** "The appliance is gone, but the copper inside still gives
  you a reason to carry this home."
- **Steel scrap:** "Someone once swept this off a workshop floor. You see a blade
  that has not happened yet."

These examples connect to existing bandage crafting, motor recycling and
salvaged-blade crafting. Keep actionable suggestions grounded in supported
behavior and obtainable recipes. An imagined use must not read as a promise of
an available action, protection value or other unimplemented effect. The goal
is to spark the player's imagination through the object's place in survival,
not turn the description into a tutorial.

## Classification and actual behavior

Classify each item using [definition flags](definition_flags.md) and
`data/flags/core.json5`, which supplies 37 shared entries. A shared definition is
`{type:'flag', id:'...', name:'...', description:'...', domains:['item', ...], kind:'property'|'category'}`;
items use `flags:['stable_name', ...]`. Reuse applicable definitions, check their
meaning and item domain, and define missing vocabulary deliberately. Unknown,
duplicate, and domain-invalid references fail loading. Flags occupy a separate
namespace from ordinary definition IDs. They are descriptive metadata, not
automatic mechanics; include the vocabulary source in each pack that needs it.

Only author implemented mechanics: the documented `container`, `energy_store`,
`charger`, and `door_controller` capabilities; existing `tool_traits` and their
tiers; `crafting_power` where its documented store exists; documented melee/grip
and armor profiles; and supported book/manual definitions. See
[combat content](combat_content.md) and [skill content](skills_content.md) for
equipment and learning fields. Check exact composition and bounds in the
authoritative documents. Material names, firearm/food/medicine flags and prose
do not create material simulation, firing, consumption or healing mechanics.
Record intended but unsupported properties and behaviors as metadata-only in the
delivery report; prose and classifications do not implement them.

## Balance and progression

Use [item balance anchors](item_balance.md) as the shared design reference. It
defines current comparison items, intended progression relationships, acquisition
checks, clothing controls and battle conditions. Start with the item's role,
nearest current peers, expected advantage and meaningful tradeoff. Shipped values
are comparison points; measured win rates are not approved universal targets.

`claw_hammer`, `chef_knife` and `arming_sword` provide utility and dedicated-weapon
references. `scrap_knife` is obsolete and should not anchor new progression.
Compare all reachable acquisition routes, supported utility, equipment costs and
combat results. Verify units and fields against [combat content](combat_content.md)
and [item presentation](item_presentation.md). Explain chosen values and any
deliberate retuning of existing definitions. Keep generated reports under `/out`.

## Art direction and external format

Use `text-art-create` for item illustrations. First draw a complete uncolored glyph
grid, review its silhouette, then add color without changing glyphs or spacing.
Target roughly 24–32 cells along the dominant dimension, choosing a shape-specific
rectangle rather than forcing a square; account for terminal cells being taller
than wide. Use the single-cell Unicode allowlist in [item presentation](item_presentation.md),
including approved box and block glyphs. Preview in the intended renderer when
available, checking font, aspect ratio, clipping, contrast, and silhouette.

The canonical external file is `data/art/items/<id>/art.txt`, referenced
by `art: '/art/items/<id>/art.txt'`: the leading slash is relative to the **data
root**, the manifest directory, not the OS filesystem root. One file owns the
finished colored art; compare it against the uncolored grid by removing markup,
preserving escaped literal `<` and all grid whitespace. The pilot's real-renderer
preview command is documented in [item pilot](item_pilot.md).

```text
@text-art 1
@color steel #AAB4BD
@color shadow #39434B
@default steel
---
<shadow>╔══╗
<steel>║██║<reset>
<shadow>╚══╝
```

In this format `@color name #RRGGBB` declares a file-local color;
`@default name` is optional, and `---` starts the art. `<name>` switches color,
`<reset>` returns to the default, and `<<` emits a literal `<`. Color state resets
at every line. Choose approximately 6–12 colors as an artistic guideline, not a
validation requirement; fewer may suit simple art. Preserve whitespace and use
only declared color names. The legacy inline array remains supported; use external
assets for new illustrations. See [item presentation](item_presentation.md) for
exact header rules, limits, and both formats.

## Integration and review

- Add definitions to an appropriate source already included by the intended pack,
  or add a new source to each relevant manifest; never include the same definition
  twice in one pack. Confirm cross-file IDs resolve. Art files are referenced by
  items, not listed in `files`; deploy the whole data tree with the manifest.
- Where requested, connect items to plausible loot pools and their actual placement
  contexts; an item definition alone does not spawn it. Follow
  [house content](house_content.md) and [urban content](urban_content.md).
- Add crafting or recycling only when appropriate. Respect distinct inputs and
  trait limits, available tools, retained versus consumed inputs, output counts,
  work, and the one-recycler-per-target rule in [crafting content](crafting_content.md).
  Avoid circular or unavailable prerequisites.
- Validate the affected pack with available project content-loading checks and
  inspect diagnostics; preview supported art in the actual frontend if feasible.
  Report precisely what was checked and what could not be checked. Report new
  versus upgraded IDs, integration, balance rationale, and metadata-only or pending
  mechanics. Do not claim an unimplemented feature works.

## Pilot roster

The five pilot definitions are `wood_scrap`, `claw_hammer`, `glass_bottle`,
`work_shirt`, and `arming_sword`. The first and last are in
`data/items/low_tech.json5`; the others are in the household and urban item files.
See [item pilot](item_pilot.md) for the crafted sword progression and balance
rationale; consult the actual manifests for current pack inclusion.

The [first broad slice](item_slice.md) adds 60 current definitions and explicitly
retires the older non-pilot catalog. Remaining dependency definitions carry the
descriptive `obsolete` category; new recipes should use current alternatives.
Books and manuals use the implemented [skill content](skills_content.md) and
[combat content](combat_content.md) schemas; a book flag alone grants no learning.

## Larger batch briefs

Use `item-batch` with explicit counts and a coverage matrix. Location, role, and
progression are independent axes: workshop/garage/clinic; salvage/tool/clothing;
improvised/civilian/advanced. A useful first expansion request is:

> Complete a 20-item garage batch: 6 salvage materials, 6 hand tools,
> 3 consumables, 3 storage items, and 2 workwear items. Count enriched existing
> definitions separately from new IDs. Reuse the approved flag vocabulary and
> supported capabilities. Plan the roster with each item's distinct purpose,
> obtainable inputs/tools, and intended spawn context; then complete descriptions,
> monochrome-first artwork, definitions, and integration. Record missing mechanics
> as such rather than inventing unsupported fields.

Keep a per-batch table of IDs, roles, source files, acquisition, art, integration,
validation status, and outstanding mechanics. Reuse finished pilot entries as
references. Advanced equipment can be salvaged while accessible craft chains
cover simple tools, melee weapons and historical equipment.
