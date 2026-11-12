# Actors and AI controllers

## Ownership and inspection

`actors/actor.h` defines the concrete, move-only `Actor`, owned by `World` in its
actor slot map. `Actor(PersistentId, TileCoord, bool player)` preserves the previous
public inspection fields and defaults. Players have no controller; non-players
start with Wander. Destruction and move operations are defined out of line so the
controller can remain forward-declared in the actor header.

Each actor owns one private `eastl::unique_ptr<AiController> controller_`. Only
`World` is its friend. Controller ownership and mutable access never escape through
`World::GetActor`. This avoids the mutable-pointee loophole in const smart-pointer
inspection. No Actor inheritance is involved. `AiController` has one implementation,
the private final `WanderController` and `MeleeController`, with explicit enum-based construction.

## Decision callback

`ActorReadContext(const World&, ActorHandle)` provides `GetWorld()` (const) and
`Self()` (a typed non-owning handle). The context and any world pointers, references,
or ranges obtained from it are callback-scoped borrows. Controllers must not retain
them. World mutations, deletion, unloading, and subsequent callbacks require fresh
lookups. The caller supplies a live self handle and keeps the world stable for the
callback. The context itself does not acquire residency or validate the handle.

`ChooseAction(context, uint32_t& rng)` returns a value `ActorIntent`:

- `Wait`: spend `baseCost`, with no movement.
- `Move`: attempt the relative `direction`, spending `baseCost` even if blocked.
- `Attack`: request combat against the selected actor.

The executor must require positive `baseCost`, schedule the actor once, and apply
movement validation/mutation through World. Choosing an action does not mutate the
world, schedule time, move actors, or update observations. The default intent is a
100-cost Wait. `direction` uses the existing X/Z/Y coordinate order.

Wander consumes exactly one unsigned modulo-2^32 LCG draw:
`rng = rng * 1664525U + 1013904223U`. Its top two bits select, in order,
`+X, +Z, -X, -Z`. It always returns a horizontal Move with base cost 100,
including when the destination is blocked or unavailable. It does not inspect
passability or draw again to find another destination.

Melee chooses an adjacent reachable hostile when available, otherwise uses the
same Wander decision. It is deliberately minimal and does not pursue a target.
NPC movement scales authored base cost by `LocomotionBaseCost` before actor speed;
body model, controller and faction are independent. See [combat API](combat_api.md).

The same logical world/self, saved controller state, and explicit RNG state must
produce the same intent, next controller state, and next RNG state. RNG belongs to
the simulation/session and must be restored separately. Wander has no mutable
controller state. No wall-clock time, global RNG, or pointer identity affects it.

## State and persistence

`MakeAiController(kind, state = nullptr, diagnostics = nullptr)` returns
`StateResult<eastl::unique_ptr<AiController>>`. A null state requests initial defaults.
An explicit payload is strictly validated, never defaulted or repaired. Successful
None construction contains a null pointer; check the expected result before the
pointer. `ValidateAiControllerState` validates payloads without allocating a
controller. Validation does not construct diagnostic strings when diagnostics is
null. `StateResult`/`StateError` are shared vocabulary in `core/state_result.h`;
`items/item_types.h` continues to include them for existing callers.

`CaptureAiController(const AiController*)` creates an owned envelope;
`ReadAiController(envelope, diagnostics)` validates and constructs it:

| Kind | Stable envelope |
| --- | --- |
| None / null pointer | `{"type":"none","state":{}}` |
| Wander | `{"type":"wander","state":{"version":1}}` |
| Melee | `{"type":"melee","state":{"version":1}}` |

The Wander and Melee version is an **unsigned** DataObject integer. Missing fields, unknown
fields, unknown/case-mismatched names, wrong types (including signed version 1),
and invalid enum values return `StateError::InvalidData`. An unsigned version other
than 1 returns `StateError::UnsupportedVersion`. None requires an empty object,
not an empty list. Envelopes require exactly `type` and `state`, in either order.
`AiControllerKindName` and `ReadAiControllerKind` convert stable names explicitly.
Runtime controllers must not report None; None is exclusively a null pointer.

The parser uses the borrowed [StateReader](data_object.md#shared-state-reader):
typed reads retain their first error, followed by one explicit `Result()` check
before semantic validation or construction. Wander has no optional saved fields:
its sole `version` field remains required. An empty saved Wander state is not a
request for new-controller defaults. Diagnostic paths retain the full
`actors.controller.state.version` (or envelope `actors.controller.type` / `state`)
qualification. Validation and construction use the same payload parser.

## World API and scheduling

`world.CreateActor(position, player)` returns a `WorldResult<ActorHandle>`.
`world.SetActorController(actor, kind)` replaces the controller with its default
state; it rejects stale handles, invalid kinds, and non-None controllers on players.
`world.CaptureActorController(actor)` returns the owned envelope, or an empty
object for an invalid handle. For example, given a live NPC handle:

```cpp
auto changed = world.SetActorController(npc, AiControllerKind::None);
if (changed) {
    auto snapshot = world.CaptureActorController(npc);
}
```

An NPC with None is idle: it is excluded from NPC decisions and actor deadlines,
and its remaining delay freezes until a controller is restored. Players also
have None, but their scheduled time advances normally. `ProcessNpcActions(rng)`
executes ready active NPCs in persistent-ID order, schedules positive action cost,
and uses `TryActorStep` for moves; blocked attempts still cost time. World read
pointers are borrows: reacquire them after any mutation or unloading.

`Actor::actionDeadline` records an absolute due tick while eligible and a frozen
duration while suspended. Read the current delay with
`World::GetActorRemainingTicks(actor)`. The [entity scheduling API](entity_scheduling.md)
describes the authoritative World clock, activation transitions, and persistence.

Save v10 stores controller and combat state; pending combat targets use persistent identities.
See [save format: v7/v8/v9 compatibility and checkpoint upgrade](save_format.md#version-789-compatibility-and-checkpoint-upgrade)
for legacy defaults and upgrade behavior.
