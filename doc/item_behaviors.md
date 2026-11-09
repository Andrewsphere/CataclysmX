# Adding item behaviors

Item behavior is an open C++ extension point. `World` owns items and structural
invariants; `ItemSystem` coordinates runtime behavior objects. Concrete charging
and door implementations live in `src/items/charger_behavior.cpp` and
`src/items/door_controller_behavior.cpp`. World, persistence, networking and UI
contain no dispatcher listing those behavior types.

## Definition, instance and registration

Implement `ItemBehaviorDefinition` and `ItemBehavior` from
[`items/item_behavior.h`](../src/items/item_behavior.h), plus a
`ParseItemBehavior` factory from
[`items/item_registry.h`](../src/items/item_registry.h).

The definition owns immutable configuration and an `ItemBehaviorSchema`:

- `Create(capability, archive, diagnostics)` constructs an independent runtime
  instance. A null archive initializes new-item defaults; otherwise it validates
  and restores that behavior's complete versioned payload.
- `ValidateComposition` checks dependencies between capabilities after all
  definitions and shared resources have resolved.
- `AcceptsInstallation` defaults to false; override it for a typed installation
  relationship. World checks occupancy, ownership, capacity and cycles.
- `ValidateSnapshot` checks relational persisted-state rules using a read-only
  `ItemSnapshotContext`. Local parsing can be shared with `Create`.

`BehaviorDefinitionPtr` is `eastl::shared_ptr<const ItemBehaviorDefinition>`.
Sharing is deliberate: catalog copies retain immutable configuration without
copying runtime state or requiring virtual cloning. Allocate with the project's
allocator, for example:

```cpp
auto definition = eastl::allocate_shared<MyBehaviorDefinition>(
    FatalAllocator{}, configuration);
```

Instances may borrow their definition's configuration. The catalog must outlive
the world and remain unchanged. They must not borrow the temporary parsing input,
the registry, or a partially assembled `ItemDefinition`.

Registration is explicit and has no global mutable state or static constructors:

```cpp
ItemBehaviorRegistry registry;
RegisterBuiltinItemBehaviors(registry);
auto registered = registry.Add("counter", ParseCounterBehavior);
if (!registered) { /* duplicate, reserved, or invalid registration */ }
auto content = LoadContent(sources, registry, diagnostics);
```

The registry can be destroyed after loading. Existing `LoadContent` and file-load
overloads construct a local built-in registry for convenience. An explicitly
supplied registry defines the available behavior types, so register built-ins
when the content needs them. `container` and `energy_store` are reserved typed
shared services rather than polymorphic behaviors.

The parser receives an owned, typed `DataObject`, the resolved shared services
and capability names, and its local capability ID. It returns the immutable
definition and a normalized configuration object including effective defaults.
It is responsible for validating its own field set and values. The content
boundary adds source/path diagnostics and canonicalizes object field order.
Canonical configuration and schema values participate in the LAN fingerprint.

## Runtime interface

Each item privately owns independent `unique_ptr<ItemBehavior>` instances.
One item can contain several instances, including repeated behavior types under
different capability names. No variant or central runtime state enumeration is
involved. Inspect state through `World::CaptureItemState`, `GetItemEnergy` and
`DescribeItem`; these return values rather than mutable behavior pointers.

The runtime interface has eight hooks:

| Hook | Contract |
| --- | --- |
| `QueryActions` | Pure query returning currently relevant declared actions |
| `Use` | Revalidate and execute a local action; positive declared cost on success, zero on refusal |
| `NextEventIn` | Pure query; no value means no scheduled work, zero means due |
| `Elapse` | Advance an enabled activity by explicitly supplied simulation ticks |
| `ProcessEvent` | Resolve a due event and make progress |
| `OnChange` | React to queued movement, containment or energy changes |
| `SaveState` | Return the complete owned versioned state payload |
| `Describe` | Return generic status/activity/countdown presentation |

The timing and change hooks have inert defaults. Ordinary items have no runtime
behaviors. Only behaviors returning a deadline enter the sparse scheduling
registry; stored item count does not determine per-tick work. Relative time and
fractional state freeze with inactive roots, including parked players.

`ItemReadContext` exposes the const world, shared energy queries, installed
children and transfer validation. `ItemMutationContext` exposes checked energy
assignment/transfer, item transfer, tile mutation and `RequestDestroy`. Shared
energy has one authoritative value; consumers do not maintain duplicate charge.

Mutation callbacks do not invoke one another recursively. Notifications and
scheduler refresh are queued. Destruction is applied after the callback returns,
before later callbacks, and dispatch resolves handles afresh. Reacquire entity
borrows after structural mutation and never retain callback contexts.

Due work drains before the player-input barrier. Each selection takes the lowest
currently due persistent-item-ID/capability pair, including newly awakened work.
A callback must not leave itself due at zero; the same behavior firing repeatedly
in one drain is also a broken invariant. Use a positive delay for subsequent
work. Large elapsed spans and subdivisions must produce equivalent state.

## Local actions and presentation

Action identity is `(item, capability, local action string)`. Names such as
`start` may be reused by unrelated behaviors. Declare each action's argument
(`None`, `Item`, `Direction`), category (`Operate`, `ManageContents`), positive
base cost and `ItemText{localizationKey, fallback}` label in the schema.

The loader checks schema order, uniqueness, enum values, costs, activity bounds
and transport-compatible metadata limits. Action IDs use ASCII
`[a-z][a-z0-9_]*`, at most 64 bytes. An item declares at most 64 actions in total.
Text keys/fallbacks contain at most 192 bytes without C0 or DEL bytes. Generic
validators and limits are shared with transport. Query results must match their
declarations; incorrect C++ implementations fail fast.

`ItemUseOutcome` provides only generic outcomes. Domain refusals carry a stable
reason code and localization-key/fallback pair, so a new reason requires no
frontend enum or switch. Clients use metadata to select an item/direction, show
single-use or multi-use UI, and submit the explicit local action ID. The server
revalidates arguments and access at execution, including queued commands.

## Persistence and verification

Live runtime ownership is move-only. RAM archives and SQLite snapshots retain
plain `DataObject` state keyed by stable capability names. Shared snapshot
validation dispatches behavior-local and relational checks through definition
interfaces. No runtime pointer or local numeric capability ID is persisted.

The existing built-in version-1 payloads and save format V5 are unchanged; a
literal pre-refactor payload is covered by the persistence tests. LAN protocol
5 represents local action strings and generic metadata.

`tests/item_behavior_tests.cpp` supplies a test-only counter behavior through the
same registry API. It composes with charging and a door timer, persists and
restores, runs through LAN commands, and exercises deferred self/sibling
destruction and same-tick notifications. Adding that behavior required no
production dispatch, UI, serializer or networking cases.
