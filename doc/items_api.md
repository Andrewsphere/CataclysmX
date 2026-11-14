# Typed world items

`World` owns item trees. `Item::type` is a catalog-local `ItemTypeId`, and
its private, move-only `ItemRuntime` owns typed energy values and independent
`unique_ptr<ItemBehavior>` instances. `Item` itself is move-only; its identity,
type, location and contents remain public read-only inspection values through
`World::GetItem`. Resolve
types with `ContentCatalog::FindItem`, and capability IDs with
`FindItemCapability(definition, name)`. Zero IDs are invalid. Capability IDs are
one-based and local to a definition; durable storage uses stable names at the
serialization boundary. `CreateItem(type, location)` initializes each behavior
through its definition's `Create` factory.
Read-only borrows follow the [World lifetime rules](world_api.md).

## Ownership, volume and mass

`ItemLocation::Inside(parent)` is ordinary storage. The parent's
`ContainerDefinition::capacityVolumeMl` bounds the sum of immediate ordinary
children's external volumes. An ordinary child's external volume is fixed,
regardless of its own contents. Containers have no separate mass limit.

An unfinished crafting wrapper is a special reserved container: its external
volume is the sum of its reserved children's volumes, and its zero base mass
plus recursive child mass gives its mass. The actual children move with the
wrapper but cannot be publicly accessed, transferred, selected for crafting, or
independently timed. Frontends traversing raw `Item::contents` must filter them
using `IsReservedItem`. See [runtime crafting](crafting.md) for reservation,
cancellation and completion rules.

`ItemLocation::Inside(parent, capability)` uses the behavior definition's
`AcceptsInstallation(childDefinition)` compatibility rule. Each mount holds one
item. For chargers this installs a compatible battery. Installed children
remain in `Item::contents`, but consume no ordinary storage volume. A compatible
battery has exactly one energy store, is rechargeable, matches the charger's
`batteryClass`, and has no charger capabilities. Runtime validation repeats
these checks even though the content loader rejects ambiguous definitions.

`GetItemMass(handle)` includes all descendants. `GetCarriedMass(actor)` sums
complete direct inventory trees. Both use checked uint64 arithmetic.
`GetContainerUsedVolume(handle)` exposes ordinary immediate volume. Invalid
handles return zero. `SetActorCarryCapacity(actor, grams)` cannot set a limit
below current mass; the default is 100000 grams. Create/transfer into carried
containers checks their root actor, and moving within one actor's tree adds no
extra mass. All trees have maximum depth 64, including the root item.

Validation precedes ownership changes. Capacity, compatibility, cycles, invalid
owners and occupied mounts leave both trees and item states untouched.
Same-location transfers succeed without effects, including cancellation.
Successful movement of an armed device or its ancestor cancels the door attempt.
Removing/transferring/destroying a mounted battery immediately stops its old
charger and clears its fractional remainder. Destroying a parent destroys its
descendants; destroying actors/objects drops their intact item trees.

## Access and contextual actions

```cpp
bool World::CanAccessItem(ActorHandle actor, ItemHandle item) const;
Vector<ItemActionInfo> World::QueryItemActions(ActorHandle actor, ItemHandle item) const;
ItemUseResult World::UseItem(ActorHandle actor, ItemHandle item,
    const ItemUseRequest& request);
WorldResult<void> World::SetItemEnergy(ItemHandle item,
    ItemCapabilityId capability, std::uint32_t energy);
```

Access requires a live, active, unparked actor and either its own nested
inventory or a ground-rooted tree on its tile. Object contents and another
actor's inventory are inaccessible. Low-level ownership APIs remain useful for
administration without access checks.

Queries return capability IDs, local string action IDs, typed argument requirements,
availability outcomes, and configured base costs in definition capability
order, then lexical local action ID order. `ItemSystem` verifies emitted action
descriptors against their declared schema and rejects duplicate identities as
broken invariants. Chargers display start/stop and install/remove according to current
state; door controllers display arm/disarm. Install requests supply `otherItem`;
arm requests supply one of six axial `dx,dz,dy` directions. Query results are snapshots. `UseItem`
revalidates access, arguments and world state immediately before execution.
`None` arguments require an empty `otherItem` and zero direction; `Item` requires
a nonzero `otherItem` and zero direction; `Direction` requires an empty
`otherItem` and a cardinal unit direction. Malformed shapes return `InvalidAction`
before behavior execution. Query availability remains advisory. Successful
behavior results must return their declared positive base cost, and failed
results must return zero; a behavior violating that contract fails fast.
Installing requires an empty mount and an independently accessible compatible
battery. Removal returns the battery to direct actor inventory and checks mass.

`ItemUseOutcome` distinguishes `Succeeded`, `InvalidAction`, `Inaccessible`, and
`Refused`. A refusal carries `ItemUseReason{code, ItemText{key, fallback}}`, so
additional behaviors need no global outcome or localization mappings. Built-in
reason codes are `missing_battery`, `incompatible_battery`, `occupied`, `no_power`,
`full`, `not_deployed`, `invalid_target`, and `no_capacity`. Failures do not mutate world
state and return zero cost. Success returns the capability's configured
`actionCost` as `ItemUseResult::baseCost`. Item actions never schedule an actor,
advance time, or consume RNG; the caller schedules the successful action's cost
through the existing command/barrier policy. `SetItemEnergy` validates store
type and capacity and stops affected chargers on empty-source/full-target
boundaries; invalid requests fail before modifying state.

## Charging and frozen clocks

```cpp
eastl::optional<Tick> World::NextItemEventIn() const;
void World::ProcessItemEvents();
std::size_t World::ActiveItemCount() const;
```

Only behaviors whose `NextEventIn` returns a value enter the sparse runtime
registry of `{handle, capability}` pairs. An item can stay registered while frozen; `ActiveItemCount` reports
registered item handles, including frozen entries, rather than capability
count. No passive battery or ordinary inventory scan runs per tick.

Eligibility follows the complete root chain: ground/object roots need an active
chunk; actor roots additionally require an unparked actor. Resident and archived
items, including fractional charging remainders and remaining door delays,
freeze without catch-up. Restore recreates registry entries from saved state;
unload and destruction retire them. Handles are resolved afresh for activity
work and ordering is by persistent item ID, then capability ID.

`ProcessItemEvents` drains all eligible, currently due work before returning.
After each callback completes and deferred destruction, notifications and
scheduler refresh drain, it selects the lowest currently due persistent item
ID/capability pair again. Newly awakened behaviors participate immediately,
including keys lower than the one just processed; the whole dispatch sequence
therefore need not be monotonically increasing. Each individual selection uses
the stable key order. Only the sparse scheduled registry is searched, never all
items or inventories, and no simulation time is advanced during this drain.

Charging transfers energy from the configured store on the charger item into
the child installed at that capability's mount. The battery's energy state is
the sole target charge value. Content permits at most one charger per source
store; target batteries have no charger capabilities. This restriction avoids
cross-charger energy chains and order-dependent sharing. Charging conserves
integer source-plus-target energy. Rates are energy per 100 ticks, with an exact
integer fractional remainder; partitioning elapsed time preserves results.
Remainders reset on stop, disconnect/replacement, source empty or target full.
Terminal boundaries automatically stop charging.

`NextItemEventIn` includes eligible source-empty/target-full boundaries and
door deadlines; frozen items contribute nothing. `ElapseTicks` advances only
eligible sparse item activities and marks affected chunks dirty even when only
a remainder changes. Multiplication is bounded by the terminal boundary before
arithmetic, so very large administrative elapsed values safely saturate charging
and timers. Due door attempts are resolved by `ProcessItemEvents`; repeated
processing is harmless and cannot leave a zero-delay retry loop.

## Delayed door attempts and simulation integration

Arming requires the device directly on the acting actor's ground tile and an
active axial neighbor whose terrain has `opensTo`. Vertical targets additionally
require an open shared face (open space above, or paired stairs). The definition supplies
the delay. Disarm clears target and remaining delay. At the deadline, the world
rechecks deployment, adjacency, target activity and the target's current terrain;
it applies that terrain's `opensTo` when available, then always disarms the
attempt. Invalidated targets produce no terrain change.

`AdvanceUntilInput` processes due objects, then due items, then the existing
player input barrier and NPC phase. Item deadlines participate in the next-event
minimum. Existing input barriers and `limitTick` pacing still bound time
advancement. Archives preserve installation, full capability state and actor
carry limits, and reject invalid definitions, state, timers, mounts, volume,
depth and carried mass before restoring runtime ownership.

The API keeps ownership separate from access and action scheduling so the same
validated primitives serve gameplay, headless replay and administrative tools.
Definition-local capability IDs make dispatch explicit without hidden
registration; the small sparse registry avoids per-tick whole-world scans.

## Runtime behavior boundary

```cpp
DataObject World::CaptureItemState(ItemHandle item) const;
eastl::optional<ItemEnergyView> World::GetItemEnergy(ItemHandle item,
    ItemCapabilityId capability) const;
ItemPresentation World::DescribeItem(ActorHandle actor, ItemHandle item) const;
```

These APIs return owned snapshots, never pointers to mutable runtime objects.
An invalid item captures an empty object/presentation; invalid item/store energy
queries return `nullopt`. `ItemPresentation` contains typed energy views, generic
activity/countdown views and contextual actions. Runtime state is intentionally
private: const smart-pointer access would still expose mutable behavior objects.

`ItemSystem` owns dispatch, notifications and scheduling. `World` retains root
eligibility, access, ownership, volume, mass and depth policy. Behaviors use
`ItemReadContext` for the const world, energy views, installed children and
transfer checks. Callback-scoped `ItemMutationContext` offers validated item and
tile changes, typed energy assignment/atomic transfer, and deferred destruction.
There is no mutable-world accessor. Callbacks must reacquire item borrows after
mutations; they must not retain context references beyond the callback.

Mutating callbacks run without reentrant `OnChange` dispatch. Actual energy or
ownership changes queue notifications to the affected item and its ancestors;
item moves also notify the moved subtree and old/new parents. Same-location
transfers and same-value energy assignments emit nothing. No sibling broadcast
is performed. `ItemChange::item` identifies the changed item/parent even when an
ancestor receives the notification; `capability` identifies the energy store or
affected installation mount (zero for ordinary contents/movement).
`RequestDestroy` takes effect after the current callback returns,
before later callbacks. Detaching clears scheduled work and queued changes for
retired handles. Scheduler refresh is queued during mutating callbacks and runs
after destruction and change delivery; it never queries pending-destruction
behaviors. After a due callback and notification drain, a surviving eligible
behavior must report no event or a positive delay; a remaining zero deadline is
a broken invariant. A behavior becoming due again after it has already fired in
the same `ProcessItemEvents` call is also a broken invariant, catching cross-
behavior zero-time cycles such as A waking B which immediately rearms A. Retired
handles are resolved again before further dispatch and do not receive callbacks.
Every mutating callback marks its surviving owning root dirty,
including fractional-only elapsed work. World reconciliation and checkpointing
remain outside callback execution.

Behavior definitions expose immutable schemas with lexically sorted local action
IDs, argument/category, labels and base costs, plus activity/countdown bounds.
Built-in runtimes borrow const references to their definition-owned config and
schema; definitions remain owned by the catalog for the world's lifetime. Queries
do not rebuild schemas or copy per-runtime configuration.
Action IDs match ASCII `[a-z][a-z0-9_]*` and contain at most 64 bytes. Built-ins
use `start`, `stop`, `install`, `remove`, `arm`, `disarm`; IDs are interpreted
only within the selected behavior. Built-in label keys preserve `item.action.0`
through `item.action.5` for existing localization files. `ItemUseMessage` supplies
generic outcome fallback text; detailed refusal text comes from the result.

`MakeItemState` initializes energy and calls each definition's factory.
`ReadItemState` is a pure constructor from a complete `DataObject`; it rejects
missing, unknown and malformed capability state. `WriteItemState` captures energy
plus each runtime behavior's `SaveState`, in stable capability-name order. SaveV6
energy stores use `{version:1, energy}`, chargers use
`{version:1, enabled, remainder}`, and door controllers use
`{version:2, armed, remaining_ticks, target:{x,z,y}}`. Target coordinates retain
their signed structured-value representation. Door state version 1 is explicitly
unsupported; its behavior implementation version is also 2 so content fingerprints
include the changed targeting contract. All targets validate full 3D bounds and adjacency.

`SavedItem::state` is plain `DataObject`. Shared `ValidateItemSnapshot` validates
ownership and capacity rules and calls behavior-specific `ValidateSnapshot`
hooks before materialization. Restore initializes runtime state before attaching
ownership, then schedules only after the entire chunk's item trees exist. This
allows factories and validation hooks to extend behavior without adding concrete
behavior types or state switches to `World` or persistence code.
