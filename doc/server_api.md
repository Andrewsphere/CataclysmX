# Server and simulation API

[`Server`](../src/core/server.h) is the authoritative local single-player
endpoint: construct it, send commands, consume value snapshots. It owns a
[`World`](world_api.md), player, bubble requests, tick counter and RNG state.
There is no transport, wall-clock loop, networking or multiplayer in V1.

`Server(seed)` defaults to `42`, starts at tick `0`, and builds a small demo:
player at `{0, 0}`, a backpack containing a bandage there, a rock at `{1, 0}`,
an initially disabled object at `{2, 0}`, a closed door at `{3, 0}`, a wall at
`{3, 1}`, and an NPC at `{5, 5}`. World terrain generation itself creates no actors.

## Commands and turn cost

`Send(Command)` returns `CommandResult{outcome, advanced, tick}`. Gameplay
outcomes are distinct from the world's `eastl::expected` errors.

| Command kind | Semantics |
| --- | --- |
| `wait` | Succeeds without moving the player |
| `move` | One cardinal tile; requires passable, unoccupied terrain |
| `pick_up` | Move a ground item on the player's tile to direct inventory |
| `drop` | Move a directly carried item to the player's tile |
| `interact` | Open/close an adjacent door, otherwise toggle an adjacent object |

Move and interact require `(dx, dz)` equal to `(±1, 0)` or `(0, ±1)`.
Pickup/drop use `Command::item`, a **persistent ID**, not a handle. Zero selects
the lowest-ID eligible item. Nested contents cannot be selected directly;
moving a backpack preserves its contents. Fields irrelevant to a command are
ignored. Closing a door occupied by an actor fails with `occupied`.

Each success, including wait, applies the command, calls `advance` once, then
increments the server tick once. `advanced` is true and `tick` is the resulting
tick. Failures (`blocked`, `occupied`, `nothing_here`, `invalid_command`,
`not_carried`, `unavailable`) leave the tick and simulation RNG unchanged and
pause all simulation. A failed move may temporarily load a destination for
validation, then release that demand; failure does not promise zero archive
bookkeeping. Use `outcome_message` for a short display message.

```cpp
#include "core/server.h"

cataclysmx::Server server(42);
const auto result = server.Send({cataclysmx::CommandKind::move, 1, 0});
const char* message = cataclysmx::outcome_message(result.outcome);
const auto snapshot = server.View(); // Safe to keep after later commands.
```

## Player bubble and administrative loading

The bubble is centered on the player's **chunk**. Defaults are active radius
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
An external active request can advance remote actors on each successful player
command; a resident request only keeps them loaded. Call `ReconcileChunks()`
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

Visibility uses circular range and integer Bresenham line of sight. Walls and
closed doors are visible themselves but block cells beyond them; unavailable
tiles stop sight without loading. Hidden cells are default/empty and convey no
world information: always check `visible` before drawing terrain. There is no
remembered exploration map in the snapshot.

Visible cells contain terrain, actor/object persistent IDs and flags, and a
ground-item count. Inventory entries describe direct possessions in persistent-ID
order, including each item's kind and immediate contained-item count. The view
contains no borrowed entity pointers and does not enumerate nested inventories
or ground-item IDs; administrative inspection can obtain those from `World`.

## Explicit simulation and brief decisions

[`cataclysmx::advance(World&, ActorHandle, std::uint32_t&)`](../src/core/simulation.h)
advances one current active snapshot. The supplied player must be live,
player-marked and active. Direct callers own RNG state and decide when to call;
`advance` does not increment a server tick or reconcile chunk requests.

Active actors are processed once in persistent-ID order, independent of storage
or reload order. Every active actor records one turn, including the player.
Each non-player actor consumes exactly one explicit RNG draw even if blocked,
then attempts a cardinal step into an active, passable, unoccupied tile. Active
objects are processed in persistent-ID order after actors; only enabled objects
increment their update counters. Resident-only and archived entities freeze with
no catch-up. NPC wandering and object counters are **demo behavior**, not a
general AI, item-update system or scheduler.

The server gates simulation on successful commands so input handling and UI
refreshes cannot change timing. Simulation takes world/player/RNG explicitly and
has no UI, networking or hidden random source. Replay requires the same seed,
commands and any administrative mutations/load changes.

## Client integration

The client interface uses `cataclysmx_game` for the FTXUI game.
`cataclysmx_headless` runs a game demo by default; `--script` reads one newline-
delimited command per line from standard input. Its grammar is:

```text
wait
move <n|s|e|w>
pickup [persistent-id]
drop [persistent-id]
interact <n|s|e|w>
view
inspect
bubble <active-radius> <resident-radius>
force <chunk-x> <chunk-z> <radius> <resident|active>
release <request-key>
quit
```

Directions map to north `-Z`, south `+Z`, east `+X`, west `-X`. Optional item IDs
use the zero/lowest-ID selection rule when omitted. `view` consumes a snapshot;
`inspect` is administrative inspection. `bubble`, `force` and `release` change
loading demand without spending a gameplay turn. `force` acquires an additional
request; pass its returned request key to `release`. Request keys are runtime
load handles, distinct from item persistent IDs.

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

`cataclysmx_headless --probe` retains the earlier checksum-probe invocation;
the default invocation is the game demo. Build/run setup belongs in the
[project README](../README.md).
