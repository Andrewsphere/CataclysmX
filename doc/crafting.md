# Runtime crafting and recycling

`World` owns crafting mutations. `StartCrafting(actor, recipe)` and
`StartRecycling(actor, target, recipe)` return `WorldResult<CraftingResult>`.
Invalid handles/IDs/settings are errors; missing ingredients, missing tools,
insufficient energy, unavailable actors and disabled recipes are ordinary typed
`CraftingOutcome` values. `Started` means reservation and first funding committed.
Start refusal moves nothing, creates nothing and spends nothing.

`ResumeCrafting`, `PauseCrafting` and `CancelCrafting` operate on the unfinished
item handle. An actor can work on one job; a job has at most one worker. Different
jobs can use the same tool. Pause finishes its paid slice. Cancellation discards
all work, including pending work, without refunding tool energy. Actual reserved
items return to the cancelling actor, falling back to ground when too heavy.

## Inspection and ownership

`InspectCrafting` returns an optional value snapshot. `CollectCraftingJobs` clears
and fills caller-owned reusable storage with reachable jobs in persistent-ID
order. `CraftingJobView::completedWork` includes credited whole work only.
`fundedTicksRemaining` describes the paid slice; no work is credited early.

The physical unfinished item owns its consumed ingredients as ordinary item-tree
children, but these children are reserved: no public access, transfer, independent
item timer progression, or crafting selection. `IsReservedItem` is a pure query
for presentation filtering. Frontends must filter reserved children when walking
raw `Item::contents`. `GetItemVolume` computes the wrapper's external volume as
the sum of its children. Wrapper mass is zero plus ordinary recursive child mass.
Moving/dropping a wrapper moves its actual children. Inputs and recycling targets
must initially be leaves. Retained tools remain in place.

Only unfinished item runtime states allocate a `CraftingJobState`; ordinary items
carry one additional owning pointer. World slots are never assumed pointer-stable.
Jobs persist worker identity, not borrowed actor pointers. Destroying a worker
leaves funded work available for explicit reassignment.

## Selection and funding

Sources are the actor's accessible inventory tree and ground trees within a
same-layer Chebyshev radius. A bounded passable-tile flood fill prevents sourcing
through walls or blocked diagonal corners. Other actors, object storage, installed
children and reserved trees are excluded. Selection uses persistent-ID order and
reusable scratch vectors, with no persistent inventory cache. Exact retained tools
and trait tools may overlap; consumables and tools do not. Funding bills every
distinct selected powered tool: every retained instance (including counts greater
than one) and every trait tool. One multitool can serve several traits and an exact
entry, and pays once per slice. The reusable funding batch is bounded by 8*255
retained instances plus 4 trait tools; recipe definitions remain packed fixed lists.

Selection currently uses the first eligible tools in that stable order; it does
not search for an alternative funded combination after an energy refusal.
Refuelling or moving the selected tool out of the source area permits another
selection on resume.

Each slice rechecks tools, computes every charge, preflights them all, and then
pays and captures its work and timing. Losing a tool after funding does not undo
that slice. A worker must afford the whole next slice; insufficient power pauses
the job. Funded slices are processed in wrapper persistent-ID order, so shared
tools pay sequentially and deterministically.

Work is fixed-point in 1/10000 base-work units. Slice timing also retains its
integer-division residue; a speed change rescales that sub-fixed-point residue
with integer flooring. Energy rates use `ratioScale` (10000 is one energy per
base work). One durable job-owned balance, with denominator 100000000, carries
fractional energy through distinct powered tools in persistent-ID order. Each tool
contributes its rate times the funded work, pays the whole units reached at that
point, and leaves the fractional balance for the next tool/slice. Thus total billed
energy is the floor of cumulative tool costs, independently of slicing. Integer
rounding can charge a previous tool's carried fraction to a later tool. This is a
job accounting policy, not a transfer of stored energy between tools. Separate jobs
sharing a tool retain separate balances and still fund in job-ID order.

The balance is independent of tool identities and requirement slots: replacement,
multitool split/merge, pause, and save do not orphan fractions. Unpowered tools do
not settle debt. Failed preflight changes neither the balance nor any tool energy.
Successful cancellation/completion discards the job's final sub-unit balance along
with its work state. Fixed-point work and energy balances survive slices and saves. Lower
actor speed means faster work, matching `ActionTicks`; final slices are shortened.

`SetCraftingSettings` accepts radius 0..8 and maximum elapsed slice ticks 1..1000.
Defaults are radius 1 and 100 ticks. Defaults are administrative runtime policy,
not session persistence. Jobs snapshot these settings on start/resume and persist
them, so changing defaults does not change an already funded or continuing job.

## Scheduling and persistence

The actor's existing action deadline schedules funded work. `ProcessCraftingEvents`
runs before the input barrier in `AdvanceUntilInput`. Callers should follow a
successful start/resume with their usual simulation advancement. A sparse assigned
job registry avoids scans of all items and idle jobs. Parked/inactive/archive time
does not advance paid slices. Save payloads store remaining duration, not offline
absolute deadlines. Attachment reconstructs the sparse registry.
If the worker is active but its wrapper becomes ineligible (for example, moved
into a resident-inactive chunk or another parked actor's inventory), the event
boundary releases the actor's timer and assignment as `WorkerUnavailable`.
Credited work, paid pending work, remaining duration, and fractional balances are
preserved. The actor can act again; later explicit resume continues the funded
slice without repayment. A parked/inactive worker itself still freezes normally.
Unavailable transitions clear any pause request, including worker destruction and
conflicting claims on attachment. Attachment conflicts do not clear the actor's
unrelated action timer.

The existing item `DataObject` envelope has a versioned `crafting_job` entry for
unfinished definitions only; the outer save format is unchanged. Recipe stable
names resolve to current numeric IDs during loading. Snapshot validation checks
recipe/work consistency, bounded residues and timers, exact reserved child counts,
leaf ownership, and duplicate worker claims within an archive.
The initial job payload is version 1 and stores scalar `energy_remainder`, strictly
less than 100000000. Active statuses require a worker; pause status and flag must
agree. Paused/refused jobs cannot retain pending work or timers. Worker-unavailable
jobs may retain already funded work for explicit reassignment.
Recycling also persists the original target's persistent ID in `target` (zero for
crafting jobs). Snapshot validation requires it to identify exactly one direct
reserved child with the recipe's target type. Additional inputs may have that same
type, so identity is never inferred from child ordering or type alone.

Funded work still finishes when the wrapper moves out of reach. Completion creates
outputs on the ground at the wrapper's actual spatial root: its ground position,
or its current actor/object owner's position, through any containing items. It
automatically collects them into the worker only if the wrapper was accessible to
that worker immediately before completion, using the job's sourcing radius. A
wrapper in another actor's inventory is inaccessible even at a nearby position.
This prevents paid components from teleporting back to their former worker.
Cancellation separately retains its actor-inventory/ground-fallback policy.

Completion zeroes all product energy stores before distributing compatible source
energy. Crafting uses all consumed inputs in child persistent-ID/store order.
Recycling uses **only the actual target's remaining energy**; additional recycling
consumables cannot add to recovered energy, even if their type matches the target.
Both use declared output/store order. Cancellation releases all reserved originals,
including recycling consumables, preserving their remaining energy. Store
compatibility is local to crafting/recycling: equal named `batteryClass` values
are compatible, and an empty class denotes a class-neutral internal store, which
can transfer to/from any battery class. Different nonempty classes are incompatible.
This allows ordinary batteries to contribute energy to a device's internal stores
and those stores to supply recovered batteries, including devices with multiple
stores. Rechargeability is not required for manufacturing/recovery transfers.
Charging and battery-installation rules are unchanged; no content schema change is
needed. Compatibility is checked per original source/output pair, so an output
internal store is not used as an intermediary between mismatched battery classes.
Capacity limits apply and excess is discarded. This applies in both crafting and
recycling directions. Original components and wrapper are destroyed exactly once;
products contain no original components. Content owns bans and output validation.
