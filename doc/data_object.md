# Owned structured values and item state

`core/data_object.h` defines `DataObject`, an owned value tree with six explicit
types: boolean, signed int64, unsigned uint64, string, list, and named object.
Default construction creates an empty object. Storage uses EASTL-backed `String`
and `Vector`; copying a tree creates an independent value. No JSON handles or
borrowed content definitions are stored in it.

Use `SetBoolean`, `SetSigned`, `SetUnsigned`, `SetString`, or `Set(name, value)`
to replace or insert an object member. Replacement preserves its position;
insertion appends. `List()` and `Append` build ordered lists. `Find` returns a
nullable pointer; `Get(name, DataType)` and the scalar `Get...` functions return
`DataResult` and distinguish `Absent` from `WrongType`. Numeric getters never
coerce signedness, truncate, or convert through floating point. Returned views
and pointers borrow the tree and must not survive structural mutations.
`At`/`Name` enumerate values and object names in insertion order. Direct scalar
accessors, invalid indexing, and mutation of the wrong node type are broken
invariants and abort in all builds. Expected input errors use typed results.

## JSON boundary

`EncodeDataObject` / `DecodeDataObject` are implemented in `save_database.cpp`,
alongside its private yyjson codec. The interface contains only owned values,
strings, typed errors and optional diagnostics. It supports standard JSON only;
null and floating-point values are outside the value model and are rejected.
Decoding rejects duplicate object keys at every level and semantic value nesting
deeper than 64 edges below the root. Failure never returns a partially built tree.
Field output is deterministic insertion order. Item readers resolve stable
capability names independently of JSON key order.

Unsigned integers are exact JSON integer tokens, including `UINT64_MAX`.
Signed values use `{"$i64":"canonical signed decimal"}` even when nonnegative,
preserving signed zero and positive signed integers distinctly from unsigned
integers. Ordinary objects containing `$i64` or `$object` members are escaped as
`{"$object":{...}}`; that inner object is decoded literally. This makes reserved
names round-trip too. There are no reserved names in the owned tree itself.
Strings and member names retain their byte lengths, including embedded NULs.

```cpp
DataObject state;
state.SetUnsigned("energy", 42);
state.SetBoolean("enabled", false);
auto energy = state.GetUnsigned("energy");
auto text = EncodeDataObject(state);
auto restored = DecodeDataObject(text);
```

## Item capability state

`MakeItemState` is for creating a new runtime item. Energy starts at the
definition's configured initial value; chargers and controllers start disabled
and disarmed. Containers have no mutable capability entry.

`ItemRuntime` privately owns typed common energy state and polymorphic behaviors.
`WriteItemState` captures these into a complete plain `DataObject`, calling each
behavior's `SaveState`. `ReadItemState` returns a move-only runtime value or a
typed failure with optional diagnostics; behavior definitions implement their own
parsing through `Create(capability, &payload, diagnostics)`. A null payload is
reserved for new-item initialization. Definitions are shared immutable objects;
`ItemBehaviorConfig` resolves a definition-local behavior capability explicitly.
Archives contain owned `DataObject` state, never runtime variants or behavior
instances. This keeps archive copies independent of runtime ownership and lets a
new behavior own its schema without extending persistence dispatch.

Each object member is the stable capability name. Common energy has unsigned
`version` 1 and `energy`; existing behaviors retain these exact version 1 fields:

| Capability | Fields besides `version` |
| --- | --- |
| energy store | unsigned `energy` |
| charger | boolean `enabled`, unsigned `remainder` |
| door controller | boolean `armed`, unsigned `remaining_ticks`, object `target` with signed `x`, `z` |

Definitions (capacity, recharge policy, rates, costs, source capability and delay
configuration) are never serialized as state. The explicit schemas keep
config-dependent validation local to the reader without reflection or hidden
registration. Capability names survive ordinal changes. Adding a new stateful
capability requires saved state or an explicit future migration: readers never
initialize missing saved state. Current energy-reader policy rejects energy above
the current configured capacity; it does not clamp it automatically. Unsupported
per-capability versions return `StateError::UnsupportedVersion` (mapped to
`SaveError::UnsupportedVersion` at the save boundary). Future state migrations
must be explicit and specific to that capability and version.

## Shared snapshot validation

`items/item_snapshot.h` exports `ValidateItemSnapshot(content, chunk, diagnostics)`
for both RAM archives and durable saves. It requires exactly the common energy
and behavior entries (no container state or extra entries), validates energy
versions and capacity, and delegates local behavior parsing to the definition's
factory. After checking the complete ownership tree, installation compatibility
and occupancy, direct container volume, actor descendant mass and depth limit of
64 items, it dispatches every behavior's `ValidateSnapshot` hook. Active charger
relationships and armed-controller adjacency belong to those hooks.

`ItemSnapshotContext` borrows only a const catalog and archive. Its `FindItem`,
`Definition`, `Installed`, `State`, and `Energy` queries use persistent IDs and
definition-local capability IDs; no `World` or runtime handles are involved.
`Definition` requires an existing item with a valid definition; the validator
checks all definitions before invoking hooks. Missing item/state/energy lookups
return null or empty. Invalid types, missing parents, cycles and non-parent-first
archives fail validation before relational hooks run. This two-phase boundary
lets behavior code inspect related state safely without duplicating tree checks
in the world and database. Unsupported versions retain
`StateError::UnsupportedVersion` through the shared helper.
