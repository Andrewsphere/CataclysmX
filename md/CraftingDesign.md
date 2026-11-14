# Crafting System Design

## Contents

- [Status and scope](#status-and-scope)
- [Glossary](#glossary)
- [Agreed rules](#agreed-rules)
- [Recipes and materials](#recipes-and-materials)
- [Tool traits and requirements](#tool-traits-and-requirements)
- [Jobs](#jobs)
- [Completed items and recycled components](#completed-items-and-recycled-components)
- [Recycling recipe](#recycling-recipe)
- [Content and runtime](#content-and-runtime)
- [Illustrative content](#illustrative-content)
- [Deferred decisions](#deferred-decisions)
- [Acceptance scenarios](#acceptance-scenarios)

## Status and scope

This document records the agreed crafting architecture. It describes intended
behavior, not an implemented feature or a complete parser specification. Exact
field names, numerical balancing, and the choices under
[deferred decisions](#deferred-decisions) remain open.

Recycling, disassembling, and uncrafting mean the same operation. This document
uses recycling. Implemented APIs belong under `doc/` when the features exist.

## Glossary

| Term | Meaning |
| --- | --- |
| Tool trait | A named tool capability, such as hammering, available at an integer tier. |
| Item quality | TBD. |
| Recycling / disassembling / uncrafting recipe | A separate definition referencing the crafting recipe it reverses and specifying requirements and an exhaustive list of returnable components. All three names mean the same thing. |
| Tier | A tool trait's level. Requirements accept the stated tier and higher. |
| Job | Persistent work state recording the recipe, progress, and reserved materials. Unfinished crafting stores this in a physical item. |
| Consumable input | Material spent or incorporated into the result when crafting completes, such as glue or nails. Recycling eligibility is separate. |
| Nonconsumable input | An item required but retained after crafting, such as a hammer. Its energy may still be spent. |

## Agreed rules

- Each item has at most one crafting recipe and at most one recycling recipe.
  Crafting inputs are exact; alternatives are excluded initially.
- A crafting recipe may disable crafting while describing components for
  recycling otherwise uncraftable loot.
- Recycling is a separate action and recipe definition. Its definition references
  the crafting recipe's ID.
- Both recipe types are entries in the shared JSON5 `definitions` array:
  `type: 'recipe'` and `type: 'recycling_recipe'`, each with a stable `id`.
- Completed items never store their original component items. Crafting consumes
  the reserved materials on completion; recycling creates new component items.
- Items that hold or use operating resources may retain the corresponding fuel,
  charge, or energy data. This does not preserve original component items.
- Recycling transfers remaining operating resources to compatible returned
  components without duplication or restoration of resources already spent.
- A recycling recipe lists every component it may return. Omitted components
  return nothing, even if their item definitions otherwise allow recovery.
- An unfinished craft is a physical item carrying a job, reserved materials, and
  saved progress. It can be carried or dropped.
- Pausing preserves the job. Cancelling unfinished crafting returns all reserved
  materials and discards progress; spent tool energy is never refunded.
- Recycling losses are predictable rather than random.
- Work uses simulation ticks and explicit simulation inputs, independently of
  rendering and wall-clock pacing. Skills come later; item quality is TBD.

## Recipes and materials

A crafting recipe describes fixed consumable inputs, nonconsumable inputs,
tool-trait requirements, outputs, required work in simulation ticks, and whether
recycling is allowed. A separate recycling recipe describes taking that output
apart. Their stable IDs differ from each other and from item IDs.

The one-recipe restriction applies separately to each operation: one crafting
definition may have one linked recycling definition. These are not competing
ways to craft the same item.

An item may have no crafting recipe. Without a definition describing its
crafting inputs, it cannot be recycled through this system. Found items may have
a crafting definition with crafting disabled and recycling allowed, plus a linked
recycling recipe. This allows recycling without making those items craftable.

Whether recycling is allowed must be explicit. A recipe disabling recycling
cannot have a linked recycling definition. A recipe allowing recycling requires
that definition; enabled recycling is never inferred solely from ingredients.

If recycling is enabled, crafting once produces exactly one output item.
If recycling is disabled, crafting may produce multiple items. Making several
recyclable items means repeating the craft, without an implied speed discount.
This avoids ambiguity about which output corresponds to the crafting input counts.

All consumable inputs are consumed when crafting completes, regardless of
whether recycling can later create items of those types. Nonconsumable inputs
are retained. Recycling eligibility is an item or recipe property, not a
separate material subclass.

An item definition can ban recovery everywhere. A recycling recipe can further
restrict which crafting inputs are returned in that particular use. Explicitly
listing a component does not override an item-wide ban or guarantee its full
return.

## Tool traits and requirements

A tool trait has a stable ID, a readable name, and integer tiers. An item can
have several tool traits. A rock and a hammer can both have hammering at
different tiers. A requirement accepts its stated tier and higher tiers of that
same tool trait.

Recipes may require a particular tool when its identity matters. Tool-trait
requirements allow interchangeable tools.

Initially, materials and tools come from accessible carried inventory, including
accessible carried containers. Nearby ground items, workstation storage, and
configurable work areas are future extensions. Carrying an unfinished item does
not satisfy its other requirements.

A powered tool can consume energy as work advances. Having the right tool trait
does not make powered operation free. Energy use belongs to the actual selected
tool, and insufficient power pauses the job. Energy already spent remains spent
when the job is paused or cancelled. Exact billing and tool selection are deferred.

Skills are excluded initially. Higher-tier speed bonuses and requirements for
simultaneous tool use are not implied by the minimum-tier rule and remain open.
Item quality remains TBD and has no defined effect on crafting or recycling.

## Jobs

An unfinished craft is a physical item holding a job. The job records its recipe,
reserved materials, and completed work. The actor works on the job; progress is
not stored solely on the actor.

| Operation | Required behavior |
| --- | --- |
| Start | Validate requirements and reserve consumable inputs atomically; refusal leaves them untouched. |
| Advance | Record completed work and spend applicable tool energy. |
| Pause | Preserve the unfinished item, reserved materials, and progress. |
| Resume | Recheck access and requirements without reserving the same materials again. |
| Save/load | Preserve the unfinished item, reserved material states, and job progress. |
| Complete | Consume the reserved materials, create the output without embedded component items, and finish the job exactly once. |
| Cancel unfinished crafting | Return all reserved materials, discard progress, and keep spent tool energy spent. |

Reserved consumable inputs cannot simultaneously be used by another craft, as
tools, or as freely accessible inventory. Moving or dropping the unfinished item
moves its reservation with it, without duplicating or refunding materials.

Consumable inputs remain reserved until completion so cancellation can return
them. Reserving actual inputs while a job is unfinished does not mean completed
items retain those inputs. Intermediate chemical and physical transformations
are deliberately not simulated. A recipe that disables recycling can still be
paused or cancelled before crafting completes.

Cancelling unfinished crafting differs from recycling a completed item. A
reserved bottle of glue can be returned; glue spent in a completed item cannot.
Refunded materials keep their actual state; cancellation does not restore
separately spent energy. Nonconsumable inputs remain tools, not components owned
by the finished item; tool access and concurrent-use details remain deferred.

Required and completed work are measured in simulation ticks. Progress must not
depend on wall-clock time. Actor-speed adjustments and interruption checkpoints
need a later decision consistent with the existing timing model.

## Completed items and recycled components

Completed items never contain the original component items for later recovery.
There is no hidden collection of part items to persist, assign between multiple
outputs, or count toward an output's mass. Ordinary container inventory and
explicitly installed items remain separate systems, not crafting history.

Recycling uses the linked crafting definition, the exhaustive return list, and
relevant target state to determine the types and quantities of newly created
components. It does not depend on the identities of the inputs originally used.
Found and crafted items follow the same recipe rules.

Items that contain fuel, charges, or another resource they run on may persist
that resource data. This exception is limited to operating resources; it does
not introduce component identities or general copies of consumed inputs' state.
Other consumed inputs do not require saved state for later reconstruction.

Compatible recycled components receive the target's remaining operating
resources, bounded by the amount still available and the returned components'
capacities. Across all returned components, the total transferred amount of each
resource cannot exceed the amount remaining in the target. Resources already
spent cannot be restored from historical input state or normal new-item defaults.

For example, a device with 40 remaining charges can transfer at most those 40
charges to compatible returned batteries in total, not 40 to each battery. A
device with no remaining charge cannot produce charged batteries through
recycling. Resource compatibility, distribution among multiple returned parts,
and handling of resources that cannot fit remain deferred.

## Recycling recipe

A recycling recipe is a separate `type: 'recycling_recipe'` definition referencing
the `id` of the `type: 'recipe'` it reverses. The referenced recipe identifies
the item being recycled and its fixed crafting inputs. Recycling is not nested
inside that crafting definition.

| Setting | Meaning |
| --- | --- |
| Crafting recipe reference | The recipe whose single output item is taken apart. |
| Duration | Work required for recycling, independently of crafting time. |
| Nonconsumable inputs and tool traits | Independently declared tools and requirements for taking the item apart. |
| Consumable inputs | Additional materials needed during recycling, such as solvent. |
| Returned components | An exhaustive list of crafting inputs that may be created by recycling, with their maximum amounts. |
| Conditions and modifiers | Predictable restrictions and losses; supported factors remain to be designed. |

The return list does not inherit unlisted crafting inputs. Omitting a component
means none is returned. Explicit entries can allow fewer components than the
crafting recipe consumed, and applicable losses may reduce those amounts further.

Recycling does not automatically inherit crafting tools or duration when fields
are omitted. For example, hammering may be needed to make an item while prying
is needed to take it apart.

A recycling recipe can return only selected fasteners and require solvent to
separate bonded parts. It must respect these rules:

1. Only consumable components listed in the referenced crafting recipe may be returned.
2. Every returnable component must also be explicitly listed in the recycling recipe.
3. Maximum returned counts cannot exceed the corresponding crafting input counts;
   losses can reduce these amounts further.
4. An item-wide recovery ban always applies; the recipe can only restrict it further.
5. Nonconsumable crafting tools cannot become recycling outputs.
6. Additional recycling materials never add to the types or counts returnable
   from the crafting inputs.
7. Recycling may create component items of allowed types, but cannot create
   unrelated scrap or substitute materials.
8. Completing recycling consumes the target exactly once; it cannot remain
   available alongside the returned components.
9. Transferred operating resources are bounded by the target's remaining amount
   and the returned components' capacities. New-item defaults cannot add extra
   fuel, charge, or energy to the recovered components.

Losses are predictable from the relevant state and recipe. There are no random
recovery rolls. Exact arithmetic, rounding, and factors remain open.

Use explicit fields and references to crafting inputs to describe the differences.
The design does not require a generic JSON patch engine or scripting language.
Recycling-job interruption, material reservation, and cancellation still need
their own rules; crafting's full-refund behavior does not silently apply to them.

## Content and runtime

Extend the existing [content pipeline](../doc/content_format.md) with JSON5 recipes
and tool-trait definitions loaded at startup. Follow the envelope used by
[`data/terrain.json5`](../data/terrain.json5): both recipe types are objects in the
shared `definitions` array alongside item and terrain definitions.

- `type: 'recipe'` defines crafting inputs and outputs.
- `type: 'recycling_recipe'` references a crafting recipe and defines recycling.
- Each definition has its own stable `id`. Recycling is not an embedded object.
- There is no separate top-level recipe array.

A recycling-only loot item still has a `type: 'recipe'` definition describing its
components, with crafting disabled, and a separate linked recycling definition.

Files may be separate, but reference resolution considers all sources. Items,
crafting recipes, and recycling recipes may reference definitions in other files.
Loading produces immutable typed content or diagnostics, without a partially
usable catalog.

Use stable string IDs for recipes, items, and tool traits in saved data.
Catalog-local numeric IDs and runtime handles are not durable identities.
Validate reference types, recipe uniqueness per operation, recycling permission,
return-list restrictions, and structural constraints before activating content.
Stable IDs remain unique across sources and definition types. The remaining
schema details and format-version changes remain open.

| Responsibility | Boundary |
| --- | --- |
| Crafting recipes | Immutable inputs, requirements, work, outputs, and recycling permission |
| Recycling recipes | Crafting-recipe reference, independent requirements, and exhaustive component returns |
| Requirement checks | Check availability and select actual materials and tools |
| Jobs | Persist reserved materials and completed work; actors refer to the job they are working on |
| Recycling | Consume the target and create only the components permitted by its recipe |
| World | Own items, enforce access, and perform authoritative changes |

These responsibilities do not require one class per row. Simulation code reads
typed definitions, not JSON.

The existing [items API](../doc/items_api.md) provides ownership and mutable energy
state. The [crafting machine](../doc/objects_api.md) provides examples of atomic
input changes, paused jobs, and completion events. Its current inline recipe is
not already the general crafting system. Its start-time consumption rules must
not replace the cancellation refunds agreed here. Sharing recipes with machines
remains future work.

## Illustrative content

The `definitions` envelope, recipe type names, separate linked definitions,
and stable IDs follow the agreed content layout. The other fields and numbers
illustrate meaning rather than a complete parser contract. The current loader
does not accept the new recipe types. Item-wide recovery bans are defined
separately; `format_version: 1` shows the current file envelope, not a decision
about future format-version changes.

```json5
{
  format_version: 1,
  definitions: [
    {
      type: 'recipe',
      id: 'assemble_bonded_frame',
      crafting_enabled: true,
      recycling_enabled: true,
      inputs: [
        {item: 'wood_plank', count: 4},
        {item: 'nail', count: 8},
        {item: 'wood_glue', count: 1},
      ],
      tools: [],
      tool_traits: [{id: 'hammering', tier: 1}],
      result: {item: 'bonded_frame', count: 1},
      duration_ticks: 1200,
    },
    {
      type: 'recycling_recipe',
      id: 'recycle_bonded_frame',
      reverses: 'assemble_bonded_frame',
      duration_ticks: 600,
      tools: [],
      tool_traits: [{id: 'prying', tier: 1}],
      consumed_materials: [{item: 'solvent', count: 1}],
      returned_components: [
        {input: 'nail', maximum_count: 6},
      ],
    },
    // Recycling disabled: one craft may produce several items.
    {
      type: 'recipe',
      id: 'make_adhesive_patches',
      crafting_enabled: true,
      recycling_enabled: false,
      inputs: [
        {item: 'cloth_sheet', count: 1},
        {item: 'wood_glue', count: 1},
      ],
      tools: [],
      tool_traits: [{id: 'cutting', tier: 1}],
      result: {item: 'adhesive_patch', count: 4},
      duration_ticks: 300,
    },
  ],
}
```

In the frame example, only nails can be returned: planks and glue are omitted.
Six nails is an upper limit, not a guaranteed return. Other restrictions may
reduce that amount. Solvent is consumed during recycling and is not returned.
Adding glue to the return list would still not override an item-wide ban on its
recovery.

## Deferred decisions

These are open design work, not permission to select undocumented behavior during
implementation:

- Item quality: TBD.
- Resource compatibility, distribution and rounding across multiple returned
  components, and handling of resources with no compatible destination or
  insufficient capacity. Transfer without duplication is settled; component
  items and general input-state copies are not stored inside a completed item.
- Remaining recipe fields, defaults, validation details, and content-format
  versioning. Both recipe types, their reference relationship, and the exhaustive
  meaning of the return list are settled.
- Formulas for returned amounts, integer rounding, condition factors, and balance
  values. Skills remain future work.
- Powered-tool energy billing, shared tools, switching tools, and pause timing;
  actor speed and any higher-tier speed modifiers.
- Recycling-job progress, interruption/resume, consumption timing for additional
  materials, cancellation, and when the target item changes.
- Destruction of unfinished items, transfer between workers, concurrent access,
  filled container inputs, installed parts, and reservation storage while unfinished.
- Output placement and capacity failures, repeating crafts, associations for
  multiple outputs, recipe knowledge, and machine/workstation integration.
- Save-format changes and handling of content changes between saves. This document
  does not change existing persistence contracts.

## Acceptance scenarios

These describe future implementation checks, not tests added or run for this
documentation change.

| Scenario | Expected outcome |
| --- | --- |
| Linked crafting and recycling recipes appear in `definitions` | Their different types select the appropriate loading, and the recycling reference resolves across sources. |
| A recipe reuses another definition's stable ID | Loading rejects the duplicate ID, including across files and types. |
| An item has two crafting recipes or two recycling recipes | Loading rejects the conflict; one linked recipe of each type is allowed. |
| A recycling recipe references an unknown or non-crafting definition | Loading rejects the invalid reference. |
| A recipe requires the hammering tool trait at tier 2 | Tools at tier 2 or higher qualify; tier 1 does not. |
| Starting fails a material or tool requirement | No materials are reserved or lost. |
| An unfinished item is paused, dropped, saved, and reloaded | Its identity, reserved material states, and job progress survive. |
| Unfinished crafting is cancelled | All reserved materials return; progress is discarded and spent tool energy stays spent. |
| Crafting completes | Consumable inputs are consumed; the output contains no saved component items. |
| A completed item holds fuel, charges, or another operating resource | It can persist that resource data without retaining component items. |
| A device with 40 remaining charges is recycled into compatible batteries | At most 40 charges are transferred in total, within the batteries' capacities. |
| A device with no remaining charge is recycled into batteries | The batteries receive no charge, regardless of normal new-item defaults. |
| Job completion is processed twice | Outputs are not duplicated. |
| A component is omitted from `returned_components` | None of that component is returned, even if its item type permits recovery. |
| The recycling recipe lists a globally forbidden material | The item-wide ban still prevents its return. |
| A recycling return names a nonconsumable crafting tool or unrelated material | Loading rejects that return. |
| Recycling consumes solvent | It does not increase the permitted returned quantities. |
| Recycling allows more of a component than the crafting recipe consumed | The definition cannot authorize that excess. |
| Recycling completes | The target is consumed once and the allowed component items are created once. |
| Making an item requires hammering and recycling requires prying | Recycling uses its independent prying requirement. |
| A powered tool runs out of energy | The job pauses without inventing energy or progress. |
| A crafting definition disables crafting but has recycling enabled and a linked recycling definition | The loot item can be recycled without being player-craftable. |
| A crafting recipe disables recycling | No linked recycling definition is allowed; its completed output cannot be recycled. |
| Crafting produces several items while recycling is enabled | Loading rejects the configuration. |

