# World API

[`World`](../src/core/world.h) owns terrain, actors, items, interactive objects
and load requests. Constructing `World(content, seed)` (default seed `42`) loads no chunks.
The immutable `ContentCatalog` must outlive the world and must not be moved from
or replaced while borrowed. The generator requires `floor` and `wall` definitions;
applications call `ValidateWorldgenContent` to check generation-role passability.
`GetContent()` exposes this read-only catalog. Terrain properties are indexed by
compact, catalog-local `TerrainId` values; zero is invalid, including in `Tile{}`.
Supply an explicit terrain when calling `SetTile`.
`GetSeed()` and `GetGenerationVersion()` expose the world's immutable generation
metadata as read-only values. In particular, these report the actual seed and
version loaded from a save, rather than an application's defaults; the in-game
world-generation debug menu uses them for its generated-baseline preview.
Mutations return `WorldResult<T>`, an alias for `eastl::expected<T, WorldError>`.
Expected failures are values; broken invariants and allocation failure are fatal.
Engine code runs without exceptions.

## Identity and borrows

`ActorHandle`, `ItemHandle`, `ObjectHandle` and `LoadRequest` are distinct typed,
64-bit generational, non-owning handles. Use them only with the issuing `World`;
raw values are not globally unique and must not be exchanged between worlds.
Zero is the empty handle. Deletion, request release, and entity unloading retire
the corresponding runtime handles. Moving a live entity between loaded chunks
preserves its handle, including handles to its carried item tree.

`PersistentId` is a separate 64-bit identity, issued monotonically across actors,
items and objects within one world. IDs survive archive/reload; runtime handles
do not. `FindActor`, `FindItem` and `FindObject` search **live entities only**,
returning an empty handle for an archived or absent ID. After reloading a chunk,
resolve its IDs again to obtain fresh handles. Persistent IDs do not imply a
disk-save format or identity shared by different worlds.

`GetActor`, `GetItem`, `GetObject`, `GetChunk` and `GetTile` return read-only pointer borrows;
missing entities/chunks/tiles return `nullptr`. Copy needed values before a
mutation, then reacquire pointers. Never retain borrows across deletion,
unloading, or uncontrolled mutations. Returned handle vectors are snapshots,
not ownership or lifetime guarantees.

## Coordinates and availability

- `TileCoord{x, z}` is a two-dimensional tile position; Z is the map's second
  horizontal axis, not elevation. Each axis is valid in `[-1'000'000, 1'000'000]`.
- Chunks are `16 × 16` tiles. `ChunkOf` uses floor division, so tile `{-1, -1}`
  belongs to chunk `{-1, -1}`. `TileIndex` is row-major (`local_z * 16 + local_x`)
  and gives `255` for that tile.
- `ChunkRegion{center, radius}` is an inclusive square in **chunk units**, with
  radius `0..8`. The entire square must fit chunk coordinates `[-62'500, 62'500]`.
  Edge chunks still contain slots outside valid tile coordinates; use
  `ValidPosition`/`GetTile` for tile access.
- Spatial mutations require loaded destination chunks. A valid but unloaded
  destination returns `chunk_unavailable`; an out-of-bounds tile returns
  `invalid_position`. Queries and mutations do not implicitly load chunks.

Passability comes from each terrain definition (in bundled content, floor and
open doors are passable; walls and closed doors are not). Actor creation
and movement reject impassable or actor-occupied destinations. World-level
`MoveActor` accepts any valid loaded destination, without enforcing adjacency
or spending a turn. Interactive objects have a separate occupancy index and do
not themselves block actors.

## Parked players

`SetActorParked(actor, true)` parks a resident player actor without deleting its
identity, last position, inventory, turns, or remaining delay. Invalid handles
return `invalid_handle`; non-players return `invalid_request`. Repeating the
same state succeeds. `GetActor` and `FindActor` still work, while `ActorAt`,
`ActiveActors`, `ElapseTicks`, `ScheduleActorAction`, and `RecordActorTurn` exclude
parked actors even in active chunks. Live actors may share their position.
Explicit inventory operations remain available. `MoveActor` can relocate a
parked actor to a loaded passable occupied tile without occupying it. Unparking
requires a passable unoccupied tile (`blocked`/`occupied` otherwise). Unload and
reload preserve parked state and possessions but retire runtime handles.

`SessionState::players` is a connection-independent roster of at most 32
`SavedPlayer` entries: player actor ID, last tile, 1–32 byte name and reconnect
token. The host entry matches `player`/`playerPosition` and uses token zero;
guests use unique nonzero tokens. Empty rosters preserve generic world saves.
Session code updates roster positions before checkpoints and parks offline
guests. Network connections are transient and are not saved.

`FindPlayerPosition(id)` returns an `eastl::optional<TileCoord>` without loading
chunks or accessing disk. It tracks created player actors across movement,
parking, unload, archive eviction and reload; destroyed players return empty.
`LoadSave` seeds it from the validated roster (or the implicit singleton host
when the roster is empty), so server roster players are immediately queryable
before chunk materialization. Generic saves with neither host nor roster may
contain additional players; these become known when their chunks are loaded.
This small ID-sorted index is kept separately because a connection's cached
position can become stale after administrative movement of a parked actor.

## Chunk demand: request, reconcile, release

`RequestChunks` acquires demand; `UpdateRequest` replaces that request's region
and mode; `ReleaseRequest` retires it. These operations change demand only.
Call `ReconcileChunks()` explicitly at a synchronous boundary to apply it. It
returns `SaveResult<void>` because storage-backed loads can fail; see the
[save format and checkpoint contract](save_format.md).
Invalid updates leave the previous request intact. Copying or dropping a
`LoadRequest` value does not acquire or release demand.

Reconciliation takes the union of all requests:

| Demand for a chunk | Result |
| --- | --- |
| Any `active` request | Loaded and eligible for simulation |
| Only `resident` requests | Loaded and queryable, but frozen by simulation |
| No requests | Unloaded into an in-memory value archive |

Active demand includes residency and wins overlaps. Releasing one request
cannot cancel another request's demand. Residency does not prohibit explicit
administrative mutations. Reconciliation itself never advances turns.
`LoadedChunks()` includes both modes in lexicographic `(x, z)` order.

```cpp
#include "core/world.h"

cataclysmx::WorldResult<void> load_origin(cataclysmx::World& world) {
    auto request = world.RequestChunks({{0, 0}, 0}, cataclysmx::LoadMode::active);
    if (!request) {
        return eastl::unexpected(request.error());
    }
    world.ReconcileChunks();
    // Perform work here; retain *request instead if demand should outlive this call.
    auto released = world.ReleaseRequest(*request);
    if (!released) {
        return eastl::unexpected(released.error());
    }
    world.ReconcileChunks();
    return {};
}
```

## Item ownership and destruction

Every item has exactly one location: ground, an actor's direct inventory,
another item's contents, or an object's contents. Use `ItemLocation::Ground`,
`Carried`, `Inside` and `Stored` to construct destinations. Only backpacks are
item containers. Nested backpacks are supported; self-containment and ancestor
cycles return `containment_cycle`, and non-container parents return
`not_container`.

`TransferItem` validates before changing ownership and spatial indexes. Moving
a container carries its descendants without changing their direct parent.
This is an administrative operation: it does not enforce reach, capacity, or
turn cost. All ownership changes go through `World`, never borrowed collections.

```cpp
cataclysmx::WorldResult<void> give_rock(
    cataclysmx::World& world, cataclysmx::ActorHandle actor) {
    auto rock = world.CreateItem(cataclysmx::ItemKind::rock,
                                 cataclysmx::ItemLocation::Carried(actor));
    if (!rock) {
        return eastl::unexpected(rock.error());
    }
    return {}; // World owns the item; the local handle owns nothing.
}
```

Destroying an actor drops its direct possessions at its last position; destroying
an object drops its contents there. Nested item trees remain intact. Destroying
an item instead destroys its entire descendant tree. Reusing retired handles
returns `invalid_handle` from mutations and `nullptr` from entity lookups.

## Relative timing

`core/timing.h` defines `Tick` as unsigned 64-bit simulation time, with 100 ticks
per standard turn. `Actor` appends `speed` (positive uint32, default 100) and
`remainingTicks` (default zero, ready). `InteractiveObject` appends
`updateInterval` (positive uint32, default 100) and `remainingTicks` (default 100).
These are world-owned values exposed only through read-only borrows.

- `SetActorSpeed(handle, speed)` rejects zero with `invalid_request`; it does not
  change an already committed delay. Lower speed values mean shorter actions.
- `ScheduleActorAction(handle, baseCost = 100)` requires a live, active, ready
  actor and positive cost. It sets the delay to `ActionTicks(baseCost, speed)`
  (ceiling of cost × speed / 100), increments `turns` exactly once, and marks the
  chunk dirty. Inactive actors return `chunk_unavailable`; a non-ready actor or
  zero cost returns `invalid_request`. Counter overflow is a fatal invariant.
  `RecordActorTurn` remains an explicit administrative counter operation; it
  neither checks readiness nor schedules a delay.
- `ElapseTicks(elapsed)` saturates delays at zero for active actors and active,
  enabled objects, marking changed chunks dirty. It does not record turns,
  trigger updates, or modify session time. Resident, archived, and disabled
  object timers remain frozen, including across unload/reload.
- `SetObjectUpdateInterval(handle, interval)` rejects zero and explicitly
  restarts the timer at that interval, even when disabled or resident.
  `SetObjectEnabled` preserves the current delay. `RecordObjectUpdate` requires
  an active object; if enabled it increments `updates` and resets the delay to
  `updateInterval`, otherwise it changes neither. The scheduler calls it only
  when due; World permits explicit administrative updates before then.

Invalid entity handles return `invalid_handle`. Relative delays avoid absolute
deadline rebasing and inactive-chunk catch-up, and actor delays may exceed uint32.
All these mutations preserve the existing borrow rules.

`SessionState::tick` is global simulation ticks. Its appended `pacing` settings
(`ticksPerSecond = 100`, `rateLimited = false`, `pauseWhenIdle = true`) control
real-time pacing rather than simulation units. The rate must be positive. Session
settings, speeds, intervals, and relative delays all survive checkpoints.

## Generation, archives and brief implementation notes

New terrain uses the deterministic region planner documented in the
[world-generation API](worldgen_api.md), independent of load order or simulation
RNG. Regions are 256×256 tiles; their plans provide roads, parcels, and eligible
pattern buildings as well as integer environment samples. Terrain-only catalogs
are supported: they produce no buildings and use the documented terrain
fallbacks. The central `[-32, 32]` square on both tile axes remains floor.
Generation creates no actors, items or objects and consumes no persistent IDs.
The server separately constructs the demo scene.

Unloading captures tiles (including damage), entity values/counters and complete
item ownership trees as values linked by persistent IDs, then retires runtime
handles. Reload restores those values and collection order with fresh handles.
Resident-only and archived entities do not advance or catch up on reactivation.
Archives exist only for currently unloaded chunks in this `World`, and disappear
with it. See the [save format](save_format.md) for disk checkpointing and archive
eviction; validation scans all durable chunks and is not constant-cost streaming.

Internally, central slot maps own live entities, while chunks hold spatial
indexes. Keeping these separate preserves live handles across chunk movement.
`ActiveActors()` and `ActiveObjects()` return snapshots sorted by persistent
ID; `ItemsAt()` also uses persistent-ID order. Storage/load order is not a turn
order. `CheckInvariants()` checks ownership and spatial consistency for
administrative/headless scenarios; it does not advance or repair the world.
