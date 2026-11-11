# Typed objects and crafting machines

## Content

`ContentCatalog` exposes `FindObject`, `TryGetObject`, `GetObject`, and
`ObjectCount`, with the same contracts as item/terrain lookup. `ObjectTypeId`
wraps `uint32_t`; zero means an untyped compatibility object. Nonzero IDs are
assigned in lexical definition-ID order, independently of source order. IDs are
globally unique across objects, items, terrain, and building patterns. Persist
the string definition ID rather than the catalog-local number.

Content format version 1 accepts an optional root `objects` array. Existing
`definitions` arrays are unchanged. An objects-only source may omit
`definitions`; a source with neither array is invalid. Objects are not entries
with `type:'object'` in `definitions`.

```json5
{
  format_version: 1,
  objects: [
    {
      id: 'scrap_press', name: 'scrap press', behavior: 'crafting_machine',
      machine: {
        input: 'scrap', input_count: 3,
        output: 'metal_pipe', output_count: 1,
        duration_ticks: 300, energy_capacity: 3000, energy_per_tick: 2,
      },
    },
    {id: 'display_stand', name: 'display stand', behavior: 'none'},
  ],
}
```

All machine fields are required. Counts are integers in `[1,1024]`; duration
and energy per tick are in `[1,UINT32_MAX]`. Capacity is an integer in
`[energy_per_tick,UINT64_MAX/2]`. The capacity bound reserves arithmetic
headroom; one job need not fit in one charge. Fractional/negative numbers,
unknown/duplicate fields, unknown behavior kinds, and unresolved item IDs are
rejected. `none` forbids a machine config. References resolve only after all
item sources have been parsed and sorted. Structural validation is also
available through `ValidObjectDefinition`; it cannot check catalog membership.

`data/terrain.json5` includes the `scrap_press` demonstration alongside its input
and output item definitions.

## Ownership and API shape

`objects/interactive_object.h` defines the concrete, move-only
`InteractiveObject`. Its default constructor and `(PersistentId, TileCoord,
bool enabled)` constructor initialize the retained public fields, including
the legacy timer counters, and leave `type` zero and `behavior_` null. Only
`World`, a friend, installs or invokes its private owned behavior.

`ObjectDefinition` is a value with a closed `ObjectBehaviorKind` and a typed
`CraftingMachineDefinition`. `MakeObjectBehavior(definition, state,
diagnostics)` uses explicit dispatch to create at most one behavior; `None`
returns a successful null pointer. This avoids a registry, capability lists,
or action-vector allocations for this small closed set. The behavior borrows
the **entire immutable definition**: the catalog must outlive it and must not
be moved, replaced, or structurally mutated while objects exist.

`ObjectBehavior` exposes:

| Method | Contract |
| --- | --- |
| `NextEventIn()` | Relative ticks to completion or power boundary; absent when idle/paused; zero when output is due. |
| `Elapse(Tick)` | Mutates only local work/energy, clamped to the first boundary. Never produces items. |
| `ProcessEvent(ObjectMutationContext&)` | Materializes one completed job and clears it. Other states are a no-op. |
| `Use(context, actor, ObjectAction)` | Start, Stop, or Collect; returns a typed gameplay outcome/reason. |
| `SaveState()` | Owned strict `DataObject` state. |
| `Describe()` | Typed value snapshot of energy/capacity, job/running/work/duration, recipe/counts; no disposable action vector. |
| `SetEnergy(uint64_t)` | Rejects over-capacity values without mutation; may pause a running job. |

Successful uses return `Succeeded`, reason `None`, base cost 100. All other
outcomes have base cost zero. `NoInputs`, `NoPower`, `NoJob`, `Busy`,
`NoCapacity`, and `NoContents` are ordinary refusals; invalid actor/access
failures are `Inaccessible`. Unknown action enum values are `InvalidAction`.
World validates actor/object handles and interaction access before every Use,
including Stop and resume, which may need no context transaction.

## Machine rules

* New machines start fully charged, idle, with no reserved inputs.
* Start on an idle machine first checks that at least one full tick is powered,
  then atomically consumes the recipe input count from the actor's direct
  inventory. Leaf items only: never consume an item with contents or installed
  children. Selection is by lowest persistent item ID, not vector or handle
  order. Failure consumes nothing and creates no job.
* Start on a paused, incomplete job resumes without consuming more inputs.
  Start while running or with completed output still pending returns Busy.
  Insufficient energy for one tick refuses initial start/resume with NoPower.
* Stop pauses running incomplete work, preserving the job and remaining work.
  Stopping an idle/already paused/completed job returns NoJob.
* Each tick costs exactly `energyPerTick`; no fractional work is performed.
  Energy below that amount (including a nonzero remainder) pauses incomplete
  work. `Elapse` discards any elapsed amount beyond the first boundary.
* Work reaching zero retains its job and reports a due event even if stopped
  or out of power. `ProcessEvent` creates all output items in the object's
  contents atomically, clears the job, and does not start another job. Failure
  to produce is a broken World invariant and fails fast.
* Collect transfers **all current contents**, including previously accumulated
  output, to the interacting actor. It checks total carry capacity before any
  transfer. Empty contents return NoContents. Collect does not alter a job.
* Refilling a paused machine does not resume it automatically. Explicit Start
  is required. Idle energy changes do not create a job.

The product subtracted by Elapse cannot overflow: elapsed work is bounded by
`energy / energyPerTick` before multiplication. At a power boundary with work
remaining, the machine pauses immediately and has no event left to process.
At completion, output remains due until ProcessEvent, including when energy
has simultaneously reached zero.

## Strict state format

```text
{version: unsigned 1, energy: unsigned uint64,
 has_job: bool, running: bool, remaining_work: unsigned Tick}
```

Every field is required, including the version. Signed integers are not
accepted in place of unsigned values in this typed archive object. Unknown
fields are rejected; unsupported versions return `StateError::UnsupportedVersion`.
Other malformed states return `StateError::InvalidData`, optionally with a
field diagnostic. Energy must not exceed capacity; work must not exceed
duration. No job implies not running and zero work. Running incomplete work
requires at least one tick of energy. A pending job with zero work is valid,
whether running or paused and regardless of energy. Paused jobs preserve
their paused state on reload. `ValidateObjectBehaviorState` and the factory
share validation; neither mutates World.

A null behavior (both type-zero compatibility objects and typed `None`
definitions) saves an empty `DataObject{}` through the owning World. A generic
state must be an empty object. Do not fabricate a machine version envelope
for it.

## World API and scheduling

`CreateObject(type, position, enabled = true)` returns a
`WorldResult<ObjectHandle>`; the untyped overload takes `(position, enabled =
false)`. `DescribeObjectState` returns an optional value snapshot: absent for an
invalid or untyped object, and a None snapshot for a typed None object.
`CaptureObjectState` returns owned state, and `SetObjectEnergy` validates the
handle, behavior, and capacity. `UseObject(actor, object, action)` checks active
residency and reach, including vertical access, and returns `ObjectUseResult`.
It does not schedule the actor: a direct caller must schedule the positive
`baseCost` on success, with a ready active actor.

For example, with a loaded position and a ready, nearby actor carrying inputs:

```cpp
auto created = world.CreateObject(content.FindObject("scrap_press"), position);
if (created) {
    ObjectHandle press = *created;
    auto used = world.UseObject(actor, press, ObjectAction::Start);
    if (used.outcome == ObjectUseOutcome::Succeeded) {
        auto scheduled = world.ScheduleActorAction(actor, used.baseCost);
        // Handle a scheduling failure in the caller.
    }
    auto state = world.DescribeObjectState(press);
}
```

Handles are non-owning; reacquire World pointers and ranges after mutations or
unloading. `SetObjectEnabled` is a global advancement/event freeze independent of
the machine's `running` state. Disabling does not stop or reset a job; enabling
does not resume a stopped job. Use remains access-checked even while disabled.
Only active, enabled objects advance or process events. `NextObjectEventIn()`
reports their next boundary; `ElapseTicks()` advances work but does not create
output. A completed job reports a zero-time event until an explicit
`ProcessObjectEvents()` call materializes it. Drain due events before advancing
positive time and limit advancement to the next simulation boundary.

Save v9 records stable object definition IDs, entity identities, and behavior
state rather than runtime handles or catalog-local IDs. See [save format:
v7/v8 compatibility and checkpoint upgrade](save_format.md#version-78-compatibility-and-checkpoint-upgrade).
