# Tick scheduling: runtime pacing and input deadlines

An expired player's wait is decided even when another player's longer grace
still holds the round. Newly arriving input cannot replace that expired slot;
the next opportunity becomes available after the round resolves.

`core/pacing.h` provides the standalone `TickPacer`. It owns transient runtime
pacing state and receives elapsed microseconds explicitly from its frontend.
It has no clock access, sleeping, networking, command resolution, or world mutation.
The simulation tick unit remains fixed: `ticks_per_turn == 100`.

## Interface and integration

- `SetSettings(PacingSettings)` rejects a zero `ticksPerSecond` and leaves all
  state unchanged on rejection. `GetSettings()` returns the current value.
- `Synchronize(const Vector<PlayerReadiness>&)` takes a complete live player
  snapshot. Each entry supplies an `ActorHandle`, persistent ID, positive speed,
  readiness, and whether a command is queued (including a future command).
  Call it after readiness, commands, or player membership changes, before
  charging elapsed time for the resulting state. Supply each player once.
- `IsIdle()` means the pause policy is enabled and no player is busy or has
  a command. An empty snapshot is idle under this policy. A single busy player
  or queued command keeps pacing active, even when other players await input.
- `ElapseMicroseconds(uint64)` charges explicitly supplied elapsed time only
  when non-idle. Frontends must advance their elapsed-time baseline while
  paused as well, so paused wall time is never submitted again after resuming.
- `AvailableTicks()` returns credited whole ticks when rate-limited and the
  maximum `Tick` otherwise. `ConsumeTicks(Tick)` spends whole credit, preserving
  fractions; insufficient capped credit is a fatal invariant violation.
  Consumption is a no-op when uncapped.
- `MicrosecondsUntilTicks(Tick)` gives the ceiling of additional elapsed time
  needed for the requested credit, or zero if uncapped/already credited. This
  is a rate conversion; callers check `IsIdle()` before waiting for advancement.
- `NextDeadlineMicroseconds()` returns the ceiling of the nearest remaining
  input deadline, zero for an expired deadline, or an empty optional when idle
  or none remain. `NextUnexpiredDeadlineMicroseconds()` ignores already-expired
  budgets when choosing an elapsed step. Expired players stay eligible for a
  wait while another participant still has grace.
- `ExpiredPlayers()` returns expired handles in persistent-ID order only when
  non-idle. Expiration remains visible until a subsequent snapshot removes it;
  the caller owns command resolution. Handle value breaks equal-ID ties.
- `Reset()` clears deadlines, advancement credit, and activity, retaining settings.

The controller stores values rather than actor pointers and never dereferences
handles. Snapshot collection and validation of live world membership belong to
the caller.

## Units and preserved budgets

One simulation tick is **1,000,000 fixed-point units**. Each elapsed real
microsecond contributes `ticksPerSecond` units. Whole ticks and a sub-tick
remainder represent advancement credit, avoiding loss from repeated fractional
elapsed updates. Uncapped mode still charges input deadlines.

A ready player without a command receives
`ActionTicks(100, speed) * 1,000,000` units of input grace on first registration.
At the default 100 ticks per second, speeds 50/75/80/100 receive
500/750/800/1000 milliseconds. An existing readiness episode retains its partial
budget across speed changes and rate changes; a new rate changes only the real
time needed to exhaust the remaining units. Both persistent ID and generational
handle must match to retain a budget. Missing players and players no longer ready
lose their deadlines. Queued ready players retain their budget, which continues
to decrease while the session is active. A queued intent suppresses automatic
waits until it is validated; a failed intent exposes the remaining budget.
Returning players start fresh.

Idle synchronization clears whole and fractional advancement credit to prevent
catch-up. Idle elapsed calls preserve input budgets. Switching the cap on or off
clears advancement credit but preserves deadlines. Settings changes that enter
idle also clear advancement credit immediately.

Arithmetic divides before multiplying where a full-width product would overflow.
Credit saturates at the maximum `Tick`; unrepresentable microsecond waits saturate
at the maximum `uint64`. Deadline subtraction saturates at zero. All rounding of
positive waits is upward, so fractional budgets cannot expire early. Positive
player speed is a caller invariant; zero speed fails fast when registering a
ready player without a command.

## Save/load boundary

`PacingSettings` can be persisted as session policy. Tick credit, fractional
credit, input deadlines, pending command intents, and the activity snapshot are runtime-only and must not
be saved. After loading, apply the saved settings, call `Reset()`, reset the
frontend elapsed-time baseline, and synchronize current player readiness.
Offline wall time is never counted; ready players receive fresh input grace.

The server owns one pending intent slot per active player. A ready player with
queued input retains its existing deadline budget (admission does not renew grace);
the pacer excludes that deadline from expiration while the input is queued.
An all-ready barrier resolves only once every missing participant has input or
expired grace. All deadlines count down concurrently while another player is busy
or has a command. Idle pause freezes all-ready/no-input sessions. Successful
actions and automatic waits begin a fresh grace episode at the next readiness;
zero-cost failures keep their existing deadline even after it expires. Repeated
failed intents cannot extend a busy peer's cooldown. `SessionUpdate::commandResults`
reports each admitted input consumed in the
call; the legacy `commandResult` mirrors the host's buffered result.

`tests/pacing_tests.cpp` covers grace lengths, runtime policy/rate/speed changes,
fractional accumulation, idle behavior, command activity, deterministic ordering,
registration and identity replacement, pruning, reset, and extreme arithmetic.
