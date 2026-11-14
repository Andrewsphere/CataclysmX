# Crafting system design

## Status and scope

Crafting and recycling are implemented as world-owned, persistent jobs. This
document summarizes the current concept and scope; see [runtime crafting](../doc/crafting.md),
[crafting content](../doc/crafting_content.md), [commands and transport](../doc/crafting_commands.md),
and [frontend controls](../doc/crafting_ui.md) for detailed contracts. Recycling,
disassembling, and uncrafting refer to the same operation here.

## Recipes and content

Crafting recipes specify exact consumed inputs, retained item requirements,
minimum-tier tool traits, output and base work. Several recipes may produce the
same item; each can be enabled or disabled independently. Requirements are static,
fixed-capacity lists: at most eight distinct packed input item types, including
retained tools, and four distinct tool traits. Counts and tiers are 1–255.
Alternatives within an input requirement are not supported.

An item has at most one recycler, either explicitly authored for that target or
generated from one crafting recipe's optional `recycling` section. Generation
requires a single-item crafting result; crafting recipes without generated
recyclers may produce multiple items. An explicit recycler can exist without a
crafting recipe and declares its own returns and requirements. Neither kind has
a runtime reference to a crafting recipe. Multiple crafting alternatives do not
choose a recycler automatically; conflicting recyclers reject the content load.

Generated returns are `floor(input count * efficiency / 10000)` for consumed,
recoverable inputs whose item definitions allow recovery; zero counts are
omitted. `10000` means 100%, and `1000` means 10%. Retained tools and additional
recycling requirements are not generated returns. Explicit returns are independently
authored and cannot name an item that bans recovery. Recycling requirements and
work are independent of crafting requirements and work.

Definitions use the existing JSON5 `definitions` array and stable IDs. All content
sources are loaded and references resolved together, including forward and
cross-file references; invalid content rejects the entire catalog transaction.
Runtime definitions use typed IDs and canonicalized static lists. See the
[schema](../doc/crafting_content.md) for fields, limits and the injected
unfinished-craft item.

## Jobs and tools

Starting a craft or recycling job atomically checks requirements, reserves actual
inputs and funds the first work slice. Refusal moves and spends nothing. Recycling
also reserves its target. Inputs and targets must initially be leaves; retained
tools stay in place. Sources include accessible inventory trees and nearby ground
trees on the same layer, selected in persistent-ID order. A bounded passable-tile
flood fill prevents sourcing through walls and blocked diagonal corners. Other
actors, object storage, installed children and reserved trees are excluded.

The physical unfinished item owns its reserved items as hidden item-tree children.
They cannot be accessed, transferred, selected or independently timed. Its
external volume is the sum of its children's volumes; its zero base mass plus
recursive child mass gives its mass. Moving or dropping it moves the actual
children. Frontends walking raw contents must filter reserved items.

One actor can work on one job, and each job has at most one worker; different
jobs may share tools. Exact retained tools and trait tools can overlap, but tools
and consumables cannot. Each distinct selected powered tool pays once per slice,
including every required retained instance. Slices are funded atomically after
rechecking tools and energy. A tool moved or lost after funding does not undo the
paid slice; the next slice rechecks requirements. Shared tools are billed in
wrapper persistent-ID order.

Work is fixed-point base work, with timing based on actor speed: lower speed
values mean faster work, consistent with `ActionTicks`. Final slices are shortened.
Power rates use the same 10000 scale: 10000 is one energy per base work. A
job-owned fractional remainder carries energy costs across tools and slices,
including tool changes; whole energy is prepaid for each slice. Insufficient
energy pauses work without partially paying that slice. See the runtime document
for ordering and rounding details.

Pause finishes the paid slice and preserves progress and reservation. Resume
rechecks requirements. Cancellation immediately discards all progress, including
pending work, without refunding tool energy; actual reserved items return to the
cancelling actor's inventory, falling back to the ground when too heavy. These
rules apply to recycling jobs too. Jobs and their reservations survive save/load;
inactive or archived time does not advance paid slices.

Runtime settings default to a nearby radius of 1 and a maximum slice of 100
elapsed ticks. Allowed ranges are radius 0–8 and slice length 1–1000 ticks. Jobs
snapshot settings on start/resume; changing defaults does not alter an already
funded or continuing job.

## Completion and recycling

Completion destroys the reserved inputs and wrapper exactly once and creates
canonical outputs, without preserving original input identities or general input
state as crafting history. Found and crafted items of the same type use the same
unique recycler. Ordinary storage and installations are separate from that history.

Output energy stores are first zeroed, then compatible source energy is transferred
in deterministic source and output order, bounded by remaining energy and
destination capacity; excess is discarded. Crafting draws from consumed inputs;
recycling draws only from its original target, never additional consumables.
Default charges cannot create energy. Equal named battery classes
are compatible, and a class-neutral internal store is compatible with any class;
different nonempty classes cannot transfer through an output intermediary within
one operation. Other input
state is not reconstructed.

## Deferred design

Skills, richer item quality and condition effects, higher-tier speed bonuses,
stations or machine integration, recipe knowledge, and richer requirement
alternatives remain future design choices. The existing runtime rules above do
not imply those features.

## Acceptance scenarios

These scenarios describe expected behavior; they do not claim tests were run for
this documentation update.

| Scenario | Expected outcome |
| --- | --- |
| References span content files or point forward | All sources resolve together; invalid content or duplicate IDs reject the entire load. |
| Two crafting recipes produce one item | Both are allowed and independently selectable when enabled. |
| Generated and authored recyclers target the same item | The conflict rejects the load; each target has at most one recycler. |
| Loot has only an explicit recycling recipe | It can be recycled without an associated crafting recipe. |
| Generated recycling uses 11 eligible inputs at efficiency 1000 | One is returned; retained, banned, nonrecoverable and zero-count returns are omitted. |
| A generated recycler accompanies a multi-item result | Loading rejects it; a multi-item crafting result without generated recycling is allowed. |
| An explicit return names a recovery-banned item | Loading rejects the definition. Explicit returns otherwise need not match crafting inputs. |
| Recycling needs solvent and prying while crafting needs hammering | Recycling uses its independent inputs and traits; solvent does not add generated returns. |
| A requirement asks for tier 2 | A matching tier 2 or higher tool qualifies; tier 1 does not. |
| Starting lacks material, a tool, or sufficient energy | No reservation, creation or energy expenditure occurs. |
| An input is behind a wall or blocked diagonal corner | Nearby selection cannot reach it through the obstruction. |
| An unfinished item is paused, dropped, saved and reloaded | Its job, progress and actual reserved children survive; children remain hidden and inaccessible. |
| Crafting or recycling is cancelled | Reserved items return to inventory or the ground if too heavy; work is discarded and paid energy remains spent. |
| A funded tool moves away | The paid slice finishes; the next slice must find suitable tools and power. |
| Two jobs share a powered tool | Each job funds separately in persistent-ID order; one tool serving multiple requirements pays once per slice. |
| Crafting or recycling completes | The wrapper and original inputs or target are consumed once; newly created outputs contain no original component history. |
| A device with 40 energy is recycled into compatible batteries | At most 40 energy is transferred in total, subject to capacity; zero source energy yields zero output energy. |
| Completion is processed again | Outputs are not duplicated. |
