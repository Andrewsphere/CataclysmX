# Item authoring

This guide is an editorial workflow. The current loadable schema is authoritative in
[content format](content_format.md), [item presentation](item_presentation.md), and
[crafting content](crafting_content.md); [content packs](content_pack.md) describes
manifest integration. Check the actual authored data and implementation when those
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
3. Write one to three concise sentences about appearance and practical use. A
   little grim or dry humor is welcome occasionally, never at the expense of
   clarity or an implied mechanic that does not exist.
4. The setting is a futuristic apocalypse with plentiful advanced pre-collapse
   salvage. Historical or simple equipment is primarily accessible through
   plausible low-tech crafting without an operating civilization, rather than
   primarily museum loot. Make crafting inputs and tools obtainable along the path.

## Classification and actual behavior

Classify each item using [definition flags](definition_flags.md) and
`data/flags/core.json5`, which supplies 36 shared entries. A shared definition is
`{type:'flag', id:'...', name:'...', description:'...', domains:['item', ...], kind:'property'|'category'}`;
items use `flags:['stable_name', ...]`. Reuse applicable definitions, check their
meaning and item domain, and define missing vocabulary deliberately. Unknown,
duplicate, and domain-invalid references fail loading. Flags occupy a separate
namespace from ordinary definition IDs. They are descriptive metadata, not
automatic mechanics; include the vocabulary source in each pack that needs it.

Only author implemented mechanics: the documented `container`, `energy_store`,
`charger`, and `door_controller` capabilities; existing `tool_traits` and their
tiers; `crafting_power` where its documented store exists; and documented melee
profiles. Check exact composition and bounds in the authoritative documents.
Do not invent material, armor, ranged, food effects, or other unsupported fields.
Record intended but unsupported properties and behaviors as metadata-only in the
delivery report; prose and classifications do not implement them.

## Balance and progression

Use shipped values as canonical anchors, not an invented universal stat table:
`claw_hammer` is 600 g/500 ml, `glass_bottle` 200 g/600 ml with 500 ml capacity,
and `work_shirt` 300 g/1200 ml. The existing `scrap_knife` and its crafting chain
provide an early improvised progression anchor; compare new items to their peers,
their role, acquisition cost, and the current combat data. Existing items without
`melee` use the default 4 HP blunt, 100 ticks, one hand. Explicit melee damage is
in hundredths of HP, not whole HP; verify damage types, penetration, hands and
attack cost against [item presentation](item_presentation.md). Avoid silently
rebalancing existing definitions. Give reasons for chosen values and tradeoffs.

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
