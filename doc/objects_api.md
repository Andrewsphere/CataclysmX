# Typed objects, crafting machines, and power behaviors

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

The demo manifest includes `data/objects/demo.json5` and `data/items/demo.json5`.
The full pack adds furniture in `data/objects/furniture.json5`; see
[house content](house_content.md) and [content packs](content_pack.md).

Optional definition properties include `kind`, `blocks_movement`, `blocks_sight`,
`movable`, `capacity_volume_ml`, and `light`. Blocking and movability default to
false; kind defaults to generic. Light defaults to zero and accepts
`{intensity, range}` with the same validation as terrain emission: both zero or
both positive, with intensity at most 255 and range at most `maxLightRange`.
`CreateObject` copies the initial emission into runtime light; enabled state gates
emission. These properties live in shared immutable definitions, while existing
runtime fields hold appearance, light, contents and behavior. Behavior-none
furniture is passive and has no per-furniture tick behavior; dismantling is not
implemented.

Explicit `capacity_volume_ml` is a positive, finite external-volume budget for
direct contents, including dynamic unfinished-craft volume. `GetObjectUsedVolume`
returns the used volume, or zero for an invalid handle. Insertions and restored
contents must fit. No capacity means no player storage interface, not unlimited
player storage; legacy generic and machine internal contents remain supported.
`CanAccessObjectStorage` checks active same-layer immediate reach and the explicit
capacity. Behavior-none storage supports `Collect` through the usual carry checks.
Crafting can also access reachable storage as described in [crafting](crafting.md).

## Ownership and API shape

`objects/interactive_object.h` defines the concrete, move-only
`InteractiveObject`. Its default constructor and `(PersistentId, TileCoord,
bool enabled)` constructor initialize the retained public fields, including
the suspended legacy `updateDeadline`, and leave `type` zero and `behavior_` null. Only
`World`, a friend, installs or invokes its private owned behavior.

`ObjectDefinition` is a value with a closed `ObjectBehaviorKind` and typed
`CraftingMachineDefinition` / `GeneratorDefinition` configurations. The enum
appends `Generator` and `GridBattery`, preserving the values of `None` and
`CraftingMachine`. `MakeObjectBehavior(definition, state,
diagnostics)` uses explicit dispatch to create at most one behavior; `None`
and `GridBattery` return a successful null pointer. This avoids a registry, capability lists,
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
| `PrepareGeneratorInterval(Tick now)` | Claims the remaining allowance in an absolute 100-tick generator interval; never burns fuel. |
| `SpendGeneratorEnergy(uint64_t)` | Atomically subtracts generator fuel and allowance; rejects insufficient fuel/allowance or absent intent. |
| `FundGridWork(Tick)` | Grants prepaid grid-machine work; requires an intended job, no outstanding paid ticks, and a positive amount no greater than remaining work. |

The three power methods default to `WorldError::invalid_request` for unrelated
behaviors. World owns access and scheduling; power-system integration invokes
these methods through World's private wrappers.

Successful uses return `Succeeded`, reason `None`, base cost 100. All other
outcomes have base cost zero. `NoInputs`, `NoPower`, `NoJob`, `Busy`,
`NoCapacity`, and `NoContents` are ordinary refusals; invalid actor/access
failures are `Inaccessible`. Unknown action enum values are `InvalidAction`.
World validates actor/object handles and interaction access before every Use,
including Stop and resume, which may need no context transaction.

The [callback context](object_callbacks.md) carries explicit object identity,
performs atomic item transactions immediately, and defers self-destruction and
item notifications until the behavior returns.

## Internal-power machine rules

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

## Grid machines and generators

`CraftingMachineDefinition::gridPowered` defaults to false. A grid machine has
positive `energyPerTick` but zero `energyCapacity`; it has no private electrical
store and starts with zero energy. Recipe/count/duration validation is shared
with internal-power machines. Start atomically reserves the initial recipe
inputs and records `runRequested` even when there is no supply. Resume consumes
no additional inputs. A waiting intended job still makes Start return Busy.

Power allocation prepays work using `FundGridWork`. Only intended, funded work
advances: `Elapse` spends at most `fundedTicks`, subtracting the same number from
remaining work. Exhausting a paid slice leaves `runRequested` true and waits for
another allocation. `Describe().running` is true exactly when intent and paid
ticks are both present. There is no energy balance to refill through `SetEnergy`;
that operation is rejected for grid machines. `NextEventIn` reports the paid
slice boundary, or zero for pending completion.

**Manual Stop preserves and freezes unused paid ticks**, together with progress,
but clears intent. Neither elapsed time nor funding can resume a manually
paused job. Explicit Start resumes its existing paid slice. This matches the
pause semantics of internal machines and avoids charging twice for the same
work. Completion produces output once through `ProcessEvent`, then clears job
and intent; completion never automatically starts another recipe.

`GeneratorDefinition` contains `fuelItem`, `fuelCapacity` (chemical-energy
charges), `energyPerFuel`, and `outputPerTick`. Fuel item IDs must be nonzero;
energy per fuel and output per tick must be positive. Capacity must hold at
least one fuel item. Capacity checks use subtraction before refueling so even
`UINT64_MAX` capacity is safe. A generator cannot have a
machine config; other behavior kinds cannot have a generator config.

Generators start with **zero fuel and no run intent**. Refuel checks room for
the complete `energyPerFuel`, atomically consumes one `fuelItem` using the
callback context, and adds its chemical energy. Failure consumes nothing.
Refueling never starts a generator. Start records intent and refuses empty
fuel with NoPower; Stop clears intent and discards unused interval allowance.
`Describe` reports fuel as `energy`, fuel capacity as `energyCapacity`, output
rate as `energyPerTick`, and `running = runRequested && energy > 0`.

Generator quanta are aligned to absolute multiples of `ticks_per_turn` (100).
When an intended generator first prepares a new interval at `now`, its allowance
is `outputPerTick * (100 - now % 100)`. Repeated preparation in the same interval
never reissues allowance. Preparing a later interval expires the old allowance;
skipped intervals do not accumulate. Stop/restart in an already claimed interval
cannot replenish its discarded allowance. An idle, never-started generator does
not claim an interval merely by being prepared. Preparation rejects an interval
older than its persisted anchor. Power integration must prepare the current
interval before spending; spending has no time argument.

Only `SpendGeneratorEnergy` consumes fuel, subtracting the requested amount from
both fuel and allowance after checking both. Generators have no local scheduled
event, elapsed-time work, or output callback work. The power system prepares
their allocation boundaries. Allowance is a rate limit, not stored electricity;
it may exceed the fuel currently available.

`GridBattery` has empty behavior state and a null behavior. It forbids object
storage capacity and both behavior configs. Its original installed battery
child is the sole authority for electrical charge; no behavior-local charge is
created or persisted.

## Saved state format and safe defaults

```text
{version: unsigned 1, energy: unsigned uint64,
 has_job: bool, running: bool, remaining_work: unsigned Tick}
```

`SaveState()` writes all five fields. Saved-state reading intentionally accepts
omission where there is an unambiguous safe default:

| Field | Saved-state requirement / default |
| --- | --- |
| `version` | Required unsigned 1. |
| `energy` | Optional; defaults to 0, never initial full capacity. |
| `has_job` | Optional; defaults to false. |
| `running` | Optional; defaults to false. |
| `remaining_work` | Required when `has_job` is true; otherwise defaults to 0. |

Defaults apply only to absent members. Explicit wrong types remain errors,
including signed integers in place of unsigned values, integer flags, or string
numbers. Valid false and zero are preserved. Unknown
fields are rejected; unsupported versions return `StateError::UnsupportedVersion`.
Other malformed states return `StateError::InvalidData`, optionally with a
field diagnostic. Energy must not exceed capacity; work must not exceed
duration. No job implies not running and zero work. Running incomplete work
requires at least one tick of energy. A pending job with zero work is valid,
whether running or paused and regardless of energy. Paused jobs preserve
their paused state on reload. `ValidateObjectBehaviorState` and the factory
share the same private parser returning a complete `StateResult<MachineState>`;
neither mutates World or exposes partially parsed state. Typed reads use
[StateReader](data_object.md#shared-state-reader) validates the version before
reading its payload, then checks the payload result before applying semantic
constraints. Diagnostics retain field-relative paths such
as `remaining_work`; framing belongs to the caller.

For example, `{version: unsigned 1}` restores an idle, empty-energy machine.
`{version: unsigned 1, has_job: true, remaining_work: unsigned 5}` restores
unpowered, paused work. `{version: unsigned 1, has_job: true}` is invalid:
defaulting its work to zero would grant a free completion. An explicit
`remaining_work: unsigned 0` with a job is still a valid pending completion.
An omitted job marker does not excuse nonzero work or a true running flag.

New-machine creation is deliberately separate: `MakeObjectBehavior(definition,
nullptr)` starts fully charged and idle. Passing an explicit saved payload never
requests that initial charge. These defaults allow concise safe saved states
without repairing malformed input or inventing energy or completed jobs. The
version remains required so omission cannot bypass format compatibility checks.

A null behavior (both type-zero compatibility objects and typed `None`
definitions) saves an empty `DataObject{}` through the owning World. A generic
state must be an empty object. Do not fabricate a machine version envelope
for it.

### Strict power state version 2

Internal-power machines retain version 1 and its defaults. Grid machines and
generators require version 2, with every listed field required and correctly
typed; they never interpret a legacy version-1 machine charge as grid power or
fuel. Version 2 is not accepted by internal-power machines. Unknown fields are
errors in both formats.

```text
grid machine:
{version: unsigned 2, has_job: bool, run_requested: bool,
 remaining_work: unsigned Tick, funded_ticks: unsigned Tick}

generator:
{version: unsigned 2, energy: unsigned uint64, run_requested: bool,
 generator_output_remaining: unsigned uint64,
 generator_interval_start: unsigned Tick,
 generator_interval_initialized: bool}
```

Grid funded ticks cannot exceed remaining work, which cannot exceed duration.
No job implies zero work, zero paid ticks, and false intent. False intent with
nonzero paid ticks is a valid manual pause. True intent with zero paid ticks
is a valid supply wait; reload preserves this distinction.

Generator fuel cannot exceed capacity. The interval start must be aligned to
100 ticks and must be zero when uninitialized. Allowance is at most one full
quantum's output and must be zero without intent or without an initialized
interval. Intent with empty fuel is valid after exhaustion and remains distinct
from manual Stop. Saving both remaining allowance and the absolute claimed
interval prevents reload or same-interval restart from granting another budget.
`GridBattery`, like `None`, accepts only an empty object state.

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

`MoveObject(object, destination)` is administrative: it ignores movability,
reach and action costs, but validates a resident, passable, supported destination
without another object, and excludes unparked actors for blocking objects. A
same-position move is a no-op; contents remain attached. In contrast, legacy
administrative `CreateObject` permits fixtures on wall or air terrain. Generated
placements require supported passable terrain, and blocking objects cannot overlap
unparked actors.

`QueryObjectMove(actor, object, kind)` is side-effect-free and reports a prospective
base cost when push or pull is executable. `TryMoveObject` returns typed errors for
invalid handles and gameplay outcomes for refusals; it atomically moves actor and
object after preflight. Both use orthogonal same-layer adjacency and neither
schedules an actor. This legacy cardinal API remains for direct callers.
Player grab mode uses `QueryObjectStep`/`TryObjectStep` with an explicit horizontal
direction, preserving any eight-neighbor offset and using object-defined movement
costs. See [grabbing](grabbing.md) for lifetime, timing and collision contracts.

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
