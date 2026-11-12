# Entity scheduling

`World::GetTick()` owns authoritative simulation time. New worlds start at zero;
`ElapseTicks(duration)` advances this clock. Server tick reads, simulation limits,
and checkpoints use this same clock. The compatibility `AdvanceUntilInput(World&,
Tick&, ...)` treats its tick argument as an output mirror; it cannot install or
reset simulation time. Prefer the overload without that argument. `Advance`
continues from the existing World clock on every call.

## Deadlines and suspension

Actors expose `actionDeadline`, legacy untyped periodic objects expose
`updateDeadline`, both as read-only World inspection values. `ActionDeadline`
stores an absolute `dueTick`, a `suspendedTicks` duration, and `suspended` state.
Running timers are eligible when `dueTick <= World::GetTick()`. Ordinary elapsed
time does not rewrite these fields. Use `GetActorRemainingTicks(handle)` and
`GetObjectRemainingTicks(handle)` for presentation/relative durations; handles
must be live. Running overdue durations clamp to zero.

World recomputes combined eligibility at explicit mutation boundaries:

- Actors require an active chunk, an unparked state, and either player status
  or a controller. Controllerless NPCs retain their committed delay.
- Legacy periodic objects require an active chunk and enabled state.
- An eligible-to-ineligible transition captures remaining duration once.
  Only the transition back to fully eligible rebases `dueTick = now + remaining`.
  Overlapping reasons therefore cannot resume a timer early or capture it twice.
- Movement between active/inactive chunks, controller replacement, parking,
  object enablement, reconciliation, and restoration refresh eligibility.
  Unloaded archives retain relative durations and cannot catch up while absent.

Scheduling commits the current action cost and speed before executing the action;
speed edits do not revise committed deadlines. Legacy interval edits explicitly
restart their timer, including while suspended. Due scans retain reusable batches
sorted by persistent ID, independent of handle allocation and dense storage order.
Objects/items complete before the player input barrier; NPCs execute after it.
Overdue periodic events execute once and schedule from the current tick.

Combat pending actions use the same actor deadline. Heavy windup and its subsequent
recovery capture speed at commitment; ordinary damaging hits or forced movement
preserve the windup deadline while applying the strongest bounded accuracy penalty.
Cancellation replaces a pending deadline with disruption recovery; further
disruptions cannot reset it. Combat events are processed in persistent-ID order.
Due combat transitions complete after object/item events and before the player
input barrier. A due windup is not a new input opportunity: it executes the strike
and enters its already committed recovery without counting another actor turn.
Active elapsed time advances combat cooldowns, stamina and body recovery; parked,
inactive and archived actors freeze these values without catch-up. Pending target
IDs may be stale when a windup resolves. See [combat API](combat_api.md).
Controllerless active NPCs retain frozen action deadlines while body recovery,
stamina and combat cooldowns advance with their active surroundings.

Typed object behaviors and item behaviors still receive elapsed work/energy
processing. Their progress is actual work state, not an entity scheduling timer.
Typed objects leave their unused legacy timer suspended; its saved value remains
compatible. `GetObjectRemainingTicks` reports this legacy timer, not behavior work;
use `DescribeObjectState`/`NextObjectEventIn` for typed behavior progress.

## Persistence and arithmetic

The archive and wire formats keep relative delays. Capture derives durations at
the current World clock; checkpoints overwrite the supplied `SessionState::tick`
copy with that clock in the same database transaction. Load installs the saved
clock before chunks materialize; restoration rebases relative delays only on
activation. Inactive and unloaded chunks need no elapsed-time rewrites.

Every positive elapse conservatively dirties active chunks containing actors or
objects. This chunk-level operation is essential: otherwise a clean incremental
checkpoint would retain old relative timers even though deadlines stayed fixed.
Behavior/item elapsed processing also retains its existing dirty tracking.

`CheckedTickAdd` fails fast if a global advance or deadline addition would exceed
`UINT64_MAX`. Time never wraps, resets, or silently saturates; callers must leave
room for future actions and resumed delays. Tests that exercise very large
elapsed work use fractions of the clock range, leaving room to resume and
schedule. Independent work-state arithmetic tests may still use maximum values.
Relative duration reads compare before subtracting, so equality and overdue
deadlines cannot underflow.

Reconciliation treats saved relative delays as untrusted input. Before changing
live chunks, activity flags, handles, or archive ownership, it checks that every
incoming actor delay and untyped-object delay fits `UINT64_MAX - GetTick()`.
For a pending heavy attack, that check includes its committed follow-up recovery
as well as its remaining windup, including on resident reactivation.
This includes resident-only loads and pending/warm RAM archives; unused legacy
timers on typed objects are excluded. It also preflights eligible suspended live
timers before reactivating resident chunks, including the same-columns fast path:
a duration that fitted when loaded may no longer fit after inactive time passes.
Failure returns `SaveError::InvalidData` with an optional diagnostic and retains
live state and pending demand for correction/retry. This boundary validation does
not change the fail-fast contract for ordinary internal clock/deadline additions.

This shape keeps streaming/persistence relative and scheduler execution absolute,
without adding catch-up queues or lazy evaluation machinery.
