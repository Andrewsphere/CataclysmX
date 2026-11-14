# Crafting commands and transport

Crafting commands use the authoritative server command path. All fields other
than those listed below must retain their `Command` defaults (including
`waitCost = ticks_per_turn`, balanced combat style, and an empty interaction).

| Kind | `recipe` | `item` |
| --- | --- | --- |
| `craft` | Nonzero catalog `CraftingRecipeId` | Zero |
| `recycle` | Zero | Persistent ID of the accessible target item |
| `resume_craft` | Zero | Persistent ID of the unfinished job wrapper |
| `pause_craft` | Zero | Persistent ID of the unfinished job wrapper |
| `cancel_craft` | Zero | Persistent ID of the unfinished job wrapper |

Recycling resolves the target's unique compiled recycling recipe. Reserved
children cannot be addressed as crafting targets. World validates reach,
ownership, recipe availability, tools, energy, and job assignment.

## Results and scheduling

Successful start and resume return `craft_started`, `recycling_started`, or
`craft_resumed`, with `advanced = true` and `cost` equal to the funded remaining
ticks. World already schedules the actor; the server does not schedule another
action. Later funded slices continue without another command.

Pause and cancel return `craft_paused` and `craft_cancelled`. These controls have
zero action cost and `advanced = false`. A working pause returns
`CraftingOutcome::PauseRequested`: the current paid slice finishes before work
pauses. Cancellation releases recoverable reserved inputs immediately.

Normal commands buffer one latest intent while the actor is busy. Because
crafting renews its own deadline, pause/cancel for the actor's exact assigned
active job execute at the submission's current simulation tick, after command
shape, opportunity, and sequence admission checks. This is an authoritative
explicit input, so its submission tick matters for replay. An accepted interrupt
clears buffered intent, consumes an opportunity, records its sequence, and
synchronizes pacing and revision. It returns its final receipt immediately;
there is no later duplicate resolution receipt. It cannot interrupt unrelated
busy work or control another worker's job through this fast path. Controls of
ready/paused jobs use ordinary queued execution and may succeed at zero cost.

Failures return `crafting_refused` with a typed `craftingOutcome` and a meaningful
`CommandResultMessage`, independently of item-use reasons. Malformed command
envelopes return `invalid_command` before admission.

## Snapshot and protocol layout

Protocol 13 adds the recipe ID to command records, the crafting outcome to result
records, and a bounded list of persistent `CraftingEntry` records to snapshots.
Each job includes its wrapper ID, kind, recipe ID, status, worker's persistent ID,
total/completed work, funded remaining ticks, and pause-request flag. Runtime
handles never cross the transport boundary. Counts, enums, recipe-kind exclusivity,
progress, and catalog recipe/work consistency are validated before publication.

Inventory trees omit reserved children. Unfinished wrappers report dynamic mass
and volume, zero visible child count, and zero storage capacity/usage. They are
not browseable containers or storage destinations. Ordinary item metadata still
must match its definition and visible descendants.

Numeric catalog IDs are valid within a connection because the content fingerprint
binds both endpoints to matching catalogs. The fingerprint includes tool traits,
item crafting/recovery properties, and every compiled crafting and recycling
recipe, including requirements, flags, outputs, work, and enabled state in stable
catalog order. Save identities continue to use stable content names.
