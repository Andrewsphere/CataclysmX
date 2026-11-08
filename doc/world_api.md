# World API

[`World`](../src/core/world.h) owns terrain, actors, items, interactive objects
and load requests. Constructing `World(seed)` (default seed `42`) loads nothing.
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

Floor and open doors are passable; walls and closed doors are not. Actor creation
and movement reject impassable or actor-occupied destinations. World-level
`MoveActor` accepts any valid loaded destination, without enforcing adjacency
or spending a turn. Interactive objects have a separate occupancy index and do
not themselves block actors.

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

## Generation, archives and brief implementation notes

New terrain is a deterministic function of seed and tile coordinates, independent
of load order or simulation RNG. The central `[-32, 32]` square on both tile axes
is floor; outside it generation mixes floor and walls. Generation creates no
actors, items or objects and consumes no persistent IDs. The server separately
constructs the demo scene.

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
