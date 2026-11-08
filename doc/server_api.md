# Server and simulation API

[`Server`](../src/core/server.h) is the authoritative local or LAN listen-server
endpoint: construct it, submit commands, consume value snapshots. It owns a
[`World`](world_api.md), player, bubble requests, tick counter and RNG state.
The application supplies elapsed time explicitly; the endpoint reads no clock.
One server owns one simulation timeline for the host and up to three connected guests.

## Authoritative multiplayer session

`JoinPlayer(name, reconnectToken, newToken)` returns a typed `SessionResult<PlayerJoin>`.
Names contain 1–32 bytes. A zero reconnect token creates a character using the
caller-generated, nonzero unique `newToken`; a nonzero reconnect token resumes a
parked character. The result is `Joining`: it cannot hold the input barrier until
`ActivatePlayer(id)` stages a personal active/resident bubble and finds a free,
passable spawn tile. Candidate tiles are tried in deterministic square rings,
starting at the saved position and searching beyond the original chunk even
with zero-radius bubbles. A destination's entire resident bubble is staged
before relocating or unparking; a failed storage read leaves the guest Joining.
A `DisconnectPlayer(id)` parks a guest and releases its bubble;
host disconnect is rejected. Up to four connected players (including host) and
32 persisted characters are supported. Parked characters retain position,
inventory and cooldown, but neither occupy tiles nor receive timer updates.

`Submit(id, PlayerInput{sequence, opportunity, command})` admits a structurally
valid latest intent without executing it. The per-player `opportunity` is an opaque
action-slot token; submit the current value from `ViewForPlayer` and advance the
sequence for each accepted replacement. Invalid shape leaves the slot intact;
reused sequences return `duplicate_input`, and an old opportunity returns
`stale_input`. Input arriving after its grace expires is also stale, even if
another player's longer grace still holds the round. Disconnect, including cancellation while joining, invalidates the
old opportunity. Reconnection resets the sequence baseline to zero; opportunities
continue increasing. Each round collects one command or an expired normal wait
from **every currently ready active player**, then resolves in persistent-player-ID
order. A failed command consumes its slot and gets a new opportunity at the same
tick without renewing its grace. Expired players remain eligible while other
ready players' deadlines run. Due objects precede the barrier, and NPCs run only
after all ready players have committed positive-cost actions.

`Update(elapsedMicroseconds).commandResults` contains each consumed input result,
including failures, exactly once in that update. Automatic waits increment
`skippedActions` instead. The legacy optional `commandResult` describes the host's
buffered result. `GetPlayers()` returns roster status and grace; `ViewForPlayer`
provides personal LOS/inventory, roster, revision, opportunity and last accepted
sequence. `View()` delegates to the host. The revision increases on admissions,
connection and world/pacing changes even if the simulation tick does not advance.
Network token generation and elapsed wall time live outside this deterministic API.

`Server(content, seed)` defaults to seed `42`, starts at tick `0`, and builds a small demo:
player at `{0, 0}`, a backpack containing a bandage there, a rock at `{1, 0}`,
an initially disabled object at `{2, 0}`, a closed door at `{3, 0}`, a wall at
`{3, 1}`, and an NPC at `{5, 5}`. World terrain generation itself creates no actors.
The immutable catalog must outlive the server. At the application boundary, call
`ValidateServerContent(content, diagnostics)` before construction: the demo
requires `floor`, `wall`, and `closed_door`, and its floor must be passable.
Construction treats these requirements as invariants. Terrain interactions use
resolved `opens_to`/`closes_to` IDs, not special terrain names.

## Commands and tick cost

Legacy host `Send(Command)` returns a `CommandResult` with `outcome`, `advanced`, `tick`,
optional `storageError`, and `cost`. Gameplay outcomes are distinct from the
world's `eastl::expected` errors.

| Command kind | Semantics |
| --- | --- |
| `wait` | Succeeds without moving; positive `waitCost` base duration, default `100` |
| `move` | One cardinal tile; requires passable, unoccupied terrain |
| `pick_up` | Move a ground item on the player's tile to direct inventory |
| `drop` | Move a directly carried item to the player's tile |
| `interact` | Open/close an adjacent door, otherwise toggle an adjacent object |

Move and interact require `(dx, dz)` equal to `(±1, 0)` or `(0, ±1)`.
Pickup/drop use `Command::item`, a **persistent ID**, not a handle. Zero selects
the lowest-ID eligible item. Nested contents cannot be selected directly;
moving a backpack preserves its contents. Fields irrelevant to a command are
ignored. Closing a door occupied by an actor fails with `occupied`.

The simulation unit is fixed: `ticks_per_turn == 100`. Actor `speed` is a
**duration factor**, default `100`: lower values finish faster. A successful
action costs `ceil(baseCost * speed / 100)` ticks. Wait accepts a positive
`uint32` base duration (for example `150` or `250`); every other command uses
base cost `100`, ignoring `waitCost`. Zero wait cost is `invalid_command`.
Positive durations always round upward to at least one tick.

In single-player host play, `Send` admits the input and processes due object
updates before resolving its action. A success applies immediately, commits a
cooldown through `World::ScheduleActorAction`, then calls `Update(0)`.
`advanced` means **an action
was committed**, rather than that the tick necessarily changed. `cost` records
its actual speed-scaled duration; `tick` is the resulting simulation time.
Default uncapped, idle-pausing play fast-forwards to the next player input.
Under a rate cap, the action can commit with an unchanged tick and a positive
`remainingTicks`; later elapsed updates discharge that cooldown.

A busy player can submit the next intent. `Send` retains **one pending command**
and returns `queued`, with `advanced == false` and `cost == 0`. The latest valid
command replaces that slot, keeping key repeat from building a movement backlog.
Queueing changes no world state, cooldown, simulation time, or RNG. Command
shape is validated immediately; malformed input leaves an existing intent intact.

At the next readiness boundary, after due object effects and before the NPC
phase, `Update` consumes the intent once. Movement directions are relative to
the player's position at execution. Destination availability, occupancy, item
ownership, and speed-scaled cost use the current authoritative state. A failed
intent is cleared and the ordinary input/idle policy resumes; it is never retried
automatically. The same buffer works if a cooldown survives switching the cap off.

Execution failures (`blocked`, `occupied`, `nothing_here`,
`invalid_command`, `not_carried`, `unavailable`) leave simulation time
and RNG unchanged, with `advanced == false` and `cost == 0`. Independent later
elapsed updates still follow the configured pacing policy.
A failed move may temporarily load a destination for
validation, then release that demand; failure does not promise zero archive
bookkeeping. Use `OutcomeMessage` for a short display message.

```cpp
#include "core/server.h"

auto content = cataclysmx::LoadContentFile("data/terrain.json5");
if (!content || !cataclysmx::ValidateServerContent(*content)) { return; }
cataclysmx::Server server(*content, 42);
const auto result = server.Send({cataclysmx::CommandKind::move, 1, 0});
const char* message = cataclysmx::OutcomeMessage(result.outcome);
const auto snapshot = server.View(); // Safe to keep after later commands.
```

## Runtime pacing and elapsed updates

`SetPacing(PacingSettings)` applies policy at runtime; `GetPacing()` returns it.
The settings are `ticksPerSecond = 100`, `rateLimited = false`, and
`pauseWhenIdle = true` by default. A zero rate is rejected (`false`) without
changing settings. Rate changes affect real-time conversion, not simulation
units or already committed action durations.

`Update(uint64_t elapsedMicroseconds)` returns `SessionUpdate{tick,
skippedActions, needsInput, storageError, commandResult, commandResults}`. The optional
`commandResult` reports a buffered intent's execution outcome, action cost, and
execution tick; that tick may precede the final update tick. It is present only
in the update that consumes the intent, including a failed intent. Deferred world
loading failures appear in both that command result and the update's storage
error. `needsInput` means the controlled
player is ready, and `skippedActions` counts automatic waits committed during
this call. Elapsed time is split at simulation events and input deadlines;
fractional tick credit is retained while active. Partitioning an elapsed span
into smaller updates preserves the outcome.

- With `rateLimited`, ticks consume credit at `ticksPerSecond`; without it,
  simulation jumps directly to its next input barrier.
- With `pauseWhenIdle`, a ready player without a command pauses the local
  endpoint. Idle elapsed time is discarded, so it cannot accelerate a later
  action. Busy cooldowns still advance.
- A queued intent counts as activity and executes immediately when ready,
  preserving remaining elapsed credit across that boundary. No input grace or
  idle pause is inserted between the two actions.
- With idle pause disabled, a ready player without input receives one normal
  action's grace: `ActionTicks(100, speed) / ticksPerSecond` seconds. Expiry
  commits a normal wait (base cost `100`), then simulation continues. At
  100 ticks/s, speeds `50`, `70`, and `100` receive `0.5`, `0.7`, and `1` second.
  A large update can expire several readiness episodes.
- Existing readiness budgets survive runtime rate/speed changes; a new speed
  determines grace on the next readiness episode. Switching the cap clears
  tick credit while retaining input budgets. Entering idle clears tick credit.

The frontend owns the elapsed-time baseline and must keep it current while
paused. See [tick scheduling](tick_scheduling.md) for the standalone `TickPacer`,
multi-player activity/deadline rules, rounding, and transient-state details.

## Player bubble and administrative loading

Each connected player has a bubble centered on their **chunk**. Defaults are active radius
`1` (a `3 × 3` square) and resident radius `2` (`5 × 5`), with active chunks
included in the resident total. `SetBubbleRadii(active, resident)` requires
`0 <= active <= resident <= 8` and the full resident region to fit world limits.
Invalid settings return `invalid_region` without changing the bubble; valid
settings reconcile immediately without consuming a tick.

```cpp
cataclysmx::WorldResult<void> shrink_bubble(cataclysmx::Server& server) {
    auto changed = server.SetBubbleRadii(0, 1);
    if (!changed) {
        return eastl::unexpected(changed.error());
    }
    return {};
}
```

A successful move recenters/reconciles the bubble before simulation advances.
When necessary, the destination is loaded before moving and before old demand
is removed, preserving the player's runtime handle even with zero radii.
Moves whose future resident bubble would cross the coordinate limits fail as
`unavailable`. Interact does not load unavailable neighboring chunks.

`GetWorld()`, `GetPlayer()` and `GetTick()` provide administrative/headless access.
Additional `GetWorld().RequestChunks(...)` requests combine with the bubble using
the [world demand rules](world_api.md#chunk-demand-request-reconcile-release).
An external active request makes remote actors and object timers participate
in elapsed simulation; a resident request only keeps them loaded. Call `ReconcileChunks()`
after changing external demand and explicitly release owned requests.

Ordinary clients use `Send`/`View`. Administrative mutation bypasses command
validation and turn costs; callers must preserve the server's live, player-marked,
active player and bubble assumptions. It is not a second gameplay command API.

## Views and line of sight

`View(radius = 8)` returns an independent `WorldView` value and does not load
chunks, advance turns or consume RNG. Radius is clamped to `0..12`. The square
has width and height `2 * radius + 1`, with `origin` at its minimum world X/Z.
Cells are row-major: `cells[row_z * width + column_x]`. Display rows follow world
Z, not elevation.

Visibility uses circular range and integer Bresenham line of sight. Terrain with
`blocks_sight` is visible itself but blocks cells beyond it; unavailable
tiles stop sight without loading. Hidden cells are default/empty and convey no
world information: always check `visible` before drawing terrain. There is no
remembered exploration map in the snapshot.

Visible cells contain terrain, actor/object persistent IDs and flags, and a
ground-item count. Inventory entries describe direct possessions in persistent-ID
order, including each item's kind and immediate contained-item count. The view
also includes player `remainingTicks`, `speed`, current `pacing`, and a
`commandQueued` flag for the pending intent. It
contains no borrowed entity pointers and does not enumerate nested inventories
or ground-item IDs; administrative inspection can obtain those from `World`.

## Explicit simulation and brief decisions

[`AdvanceUntilInput(World&, Tick& tick, uint32_t& rng, Tick limitTick)`](../src/core/simulation.h)
uses an absolute limit at or after `tick`, returning `SimulationResult`:

- `InputRequired`: `readyPlayers` contains **all** active, ready player handles,
  in persistent-ID order. Due object effects have run; the NPC actor phase at
  this tick has not. Callers resolve input and commit each action using
  `World::ScheduleActorAction(handle, baseCost)`, then resume. Busy players do not
  join the barrier. An uncommitted ready player continues to hold it.
- `LimitReached`: elapsed time reached the limit; `nextEventIn` is the positive
  relative delay to the next event. Events due exactly at the limit are
  processed, including a player barrier if applicable.
- `NoEvents`: no active actor or enabled object can produce a future event;
  time stays at the last processed tick. `nextEventIn` is zero.

At each event tick, due enabled objects update first in persistent-ID order and
immediately schedule their next interval (default `100`), independent of player
speed. After the player barrier clears, due NPCs act in persistent-ID order,
commit positive-cost normal actions, and consume one explicit RNG draw each,
even when blocked. They attempt a cardinal step into an active, passable,
unoccupied tile. The simulator then elapses to the nearest event or the limit.
Resuming at the same tick repeats neither committed NPC actions nor object
effects. Actor `remainingTicks` and object timers are relative delays;
resident-only and archived entities freeze without catch-up on reactivation.

`Advance(World&, ActorHandle, uint32_t&)` is a single-player convenience: commit
a normal player wait, then simulate to the next input barrier. The player must
be live, active, player-marked, and ready. This helper owns no server tick and
does not reconcile chunk requests. One normal speed-100 action now spans 100
ticks, rather than incrementing time by one.

Explicit tick, RNG, input barriers, and a separate elapsed-time pacer keep
simulation deterministic and headless. NPC wandering and object counters remain
demo behavior. Replay requires the same seed, commands, pacing/elapsed inputs,
and administrative mutations/load changes.

## Client integration

The client interface uses `cataclysmx_game` for the FTXUI game.
`cataclysmx_headless` runs a game demo by default; `--script` reads one newline-
delimited command per line from standard input. Its grammar is:

```text
wait [positive-base-cost]
move <n|s|e|w>
pickup [persistent-id]
drop [persistent-id]
interact <n|s|e|w>
view
inspect
bubble <active-radius> <resident-radius>
force <chunk-x> <chunk-z> <radius> <resident|active>
release <request-key>
rate <positive-ticks-per-second>
limit <on|off>
idle-pause <on|off>
elapse <microseconds>
save [path]
load <path>
autosave <turns>
quit
```

Directions map to north `-Z`, south `+Z`, east `+X`, west `-X`. Optional item IDs
use the zero/lowest-ID selection rule when omitted. `view` consumes a snapshot;
`inspect` is administrative inspection. `bubble`, `force` and `release` change
loading demand without spending a gameplay turn. `force` acquires an additional
request; pass its returned request key to `release`. Request keys are runtime
load handles, distinct from item persistent IDs.
`rate`, `limit`, and `idle-pause` change runtime pacing without committing an
action. `elapse` explicitly supplies a `uint64` microsecond span to `Update`;
script execution does not infer elapsed wall time. `inspect` includes pacing,
speed, and remaining delay.

Blank lines and `#` comments are accepted. Malformed syntax or a rejected
administrative request exits with code `2`; rejected gameplay commands are valid
responses and do not terminate the script. `quit` ends input processing.
`inspect` is deliberately privileged and reports loaded/active/archive counts,
entity IDs and actor counters for headless scenarios.

The TUI uses arrows/hjkl to move, `.` to wait, `g` to pick up, `d` to drop,
`o` then a direction to interact, and `q`/Escape to quit. Escape cancels a pending
interaction first. `cataclysmx_game --render` prints a snapshot without terminal
initialization. Both clients render the same value-only view through
[`client/text.h`](../src/client/text.h).
During play, `r` toggles the rate cap, `p` toggles idle pause, and `[`/`]`
halve/double the tick rate within `1..1,000,000`. The terminal supplies elapsed
microseconds during its update loop; status shows rate, policy, speed, and delay.

During play, F11 opens the terminal client's world-generation debug menu. Its
arrows/hjkl cursor moves by scale; uppercase HJKL jump one region (256 tiles).
Keys 1–5 or Tab/Shift+Tab select Roads, Parcels, Ownership, Biomes, or Terrain;
plus/minus zoom by powers of two within 1–16. Home/c recenters on the menu-entry
position, PageUp/PageDown browses details, and F11/Escape/q closes it. The view
uses the world's actual seed and generation version and shows generated baseline
content, excluding live edits and entities. Generation version 1 saves display
an unavailable notice. Opening or using the menu does not load chunks, tick, or
mutate the world. Its data API is frontend-independent; the menu factory is
FTXUI-specific and confined to the terminal target (see
[world-generation debug inspection](worldgen_api.md)).

`cataclysmx_headless --probe` retains the earlier checksum-probe invocation;
the default invocation is the game demo. Build/run setup belongs in the
[project README](../README.md).

## Saves

`Server::CreateSave(path)` creates a new save and performs its first checkpoint;
it refuses an existing path. `Save()` checkpoints the attached database, and
`Server::LoadSave(content, path)` returns a new server only after successful validation
and restoration. These APIs return typed `SaveResult` values; optional
diagnostics provide detail. See the [save format](save_format.md). The generic
`World` save API also supports session player ID zero; `Server` requires a real
player. Administrative load-request handles are runtime-only; the host bubble is
recreated on load, while guests return parked until activated again. Checkpoints
read guest positions from live or archived world state, including administrative
relocations of a parked character; a parked host is an invalid session.

Save format **version 4** persists the player roster and reconnect tokens, actor speed and remaining cooldown, object
interval and remaining timer, simulation tick/RNG, and session pacing settings.
A checkpoint can therefore resume mid-action or partway through an object
interval. Pending input intents, tick credit, fractional credit, readiness snapshots, and partially
spent input deadlines are transient: load resets them and gives ready players
fresh grace. Frontends reset their elapsed-time baseline; offline time is never
simulated.

`SetAutosaveInterval(turns)` / `GetAutosaveInterval()` retain **turn** units:
one configured turn is 100 simulation ticks, independent of speed or command
count. The saved interval value is unchanged (for example `2` still means
200 ticks); `0` disables autosave. Successful manual saves reset cadence at the
current tick. `Send` after a committed action and `Update` check autosave;
storage failures appear separately without undoing gameplay and can be retried
on a subsequent update or successful action.

Headless commands: `save <path>` creates the first save (refusing an existing
path), `save` checkpoints, `load <path>` replaces the session only on success,
and `autosave <turns>` configures autosave (`0` disables it). Paths cannot
contain whitespace. The terminal frontend accepts `--save <path>` for a new
game (default autosave interval 1000 turns), `--load <path>` to restore the
saved interval, and capital `S` to save. With no arguments it remains in-memory;
`--render` is preserved.
