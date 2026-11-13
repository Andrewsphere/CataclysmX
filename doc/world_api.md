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

- `TileCoord{x, z, y}` uses X/Z horizontally and Y for elevation. Fields are
  ordered `{x, z, y}` so `{x, z}` remains a surface (`y=0`) aggregate. X/Z are
  valid in `[-1'000'000, 1'000'000]`; Y is `[-10, 10]`.
- Chunks are single-layer `16 × 16` X/Z tiles, with Y as the chunk layer.
  `ChunkOf` uses floor division horizontally, so surface tile `{-1, -1}` belongs to
  chunk `{-1, -1, 0}`. `TileIndex` is row-major (`local_z * 16 + local_x`) and
  gives `255` for that tile.
- `ChunkRegion{center, radius}` is an inclusive square in **chunk units**, with
  radius `0..8`. Every request covers full-height columns, and `center.y` is
  ignored. The entire square must fit horizontal chunk coordinates `[-62'500, 62'500]`.
  Edge chunks still contain slots outside valid tile coordinates; use
  `ValidPosition`/`GetTile` for tile access.
- `ColumnCoord{int32_t x, int32_t z}` identifies a horizontal full-height column.
  `TileCircleRegion{center, radiusTiles}` selects columns by an integer circle in
  **tile units**, ignoring `center.y`. Radius is `0..maxCircleResidencyRadius`
  (`128`). For each column, the nearest integer tile in its inclusive 16×16
  rectangle must have squared horizontal distance at most `radiusTiles²`.
  Tangencies are included; radius zero selects exactly the containing column.
  The entire circle's horizontal footprint must fit valid world tile bounds;
  invalid circles are rejected rather than clipped. There is no implicit halo.
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
`CollectActiveActors`, `ElapseTicks`, `ScheduleActorAction`, and `RecordActorTurn` exclude
parked actors even in active chunks. Live actors may share their position.
Explicit inventory operations remain available. `MoveActor` can relocate a
parked actor to a loaded passable occupied tile without occupying it. Unparking
requires a passable unoccupied tile (`blocked`/`occupied` otherwise). Unload and
reload preserve parked state and possessions but retire runtime handles.

`ParkActors(eastl::span<const ActorHandle>)` first validates every handle as a
resident player, returning `invalid_handle` or `invalid_request` without changing
any actor on failure. It then parks the whole set before settling gravity once.
Empty sets, duplicate handles, and already parked players are accepted. This
narrow batch operation preserves stacked players' positions regardless of input
order: sequential parking would remove a lower player's support and drop an upper
player before it could be parked. Other unsupported actors and items settle after
the batch; this is a parking operation rather than a general transaction API.

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
This private keyed map is kept separately because a connection's cached
position can become stale after administrative movement of a parked actor.

## Lighting and player observations

`World::GetLightLevel` and `World::GetLightBand` expose the shared derived resident
light field; `LightBand` classifies levels as Dark, Dim, Ordinary, or Bright. Edits
that affect sources, geometry, sky, or residency invalidate the field; its next
query refreshes it. Sky and source contributions can be reused across source-only
changes. See [surface lighting](surface_lighting.md) for incident faces and
incremental recombination, [perception](perception.md) for source rules, and
[the view contract](server_api.md#views-and-line-of-sight).

The first dirty light query calls `LightingField::Prepare`, publishes invalidation
tokens, and invalidates affected observer filters before any filter-cache decision.
Output is then computed lazily with `EnsureChunk` for the requested chunk;
chunk-contiguous and incident queries ensure their own target as well. Perception
ensures each sampled chunk before reading its field. Published chunk revisions are
invalidation tokens even when output has not yet been materialized. Full unload
and reload at the same coordinates obtains fresh source-input tokens, including
when no light query occurred between the two boundaries.

`CanPlayerSee` provides residency-clipped authoritative terrain visibility without
loading or learning. `SetVisionProfile` validates session-local sight policy;
default maximum range is 64. `GetRevision` tracks presentation-relevant changes,
while `GetMapRevision` separately tracks map-relevant changes. These are conservative
equality tokens, allowing status changes to reuse map work. `GetPerceptionStats`
and `ResetPerceptionStats` expose diagnostic counters without flushing caches.
See [bulk perception](bulk_perception.md) for the API, exact 3D algorithm, and
invalidation rules.

`GetPlayerMemories()` returns a by-value, owned canonical snapshot: players are
sorted by persistent ID and each player's chunks by `(x,z,y)`. Runtime memories
are private maps keyed by player and chunk; ordering is applied when capturing a
snapshot or saving, rather than maintained during observation. Call
`UpdatePlayerObservations()` explicitly at observation boundaries: construction
and player activation establish initial observation; successful player actions,
same-tick completed effects, and each NPC action refresh it. Queries and arbitrary
edits do not teach. Administrative edit batches must call it themselves when
intended to teach resident active players. Offline and Joining players do not
observe live map changes. Saves serialize stored observations without refreshing
them. Memory records terrain and limited object appearance, not machine state;
current views can be fresh without updating memory.

## Chunk demand: request, reconcile, release

`RequestChunks` acquires demand; `UpdateRequest` replaces that request's region
and mode; `ReleaseRequest` retires it. These operations change demand only.
`RequestCircle(TileCircleRegion, LoadMode)` returns `WorldResult<LoadRequest>`;
`UpdateCircleRequest(LoadRequest, TileCircleRegion, LoadMode)` returns
`WorldResult<void>`. These use the same handles and lifecycle as square demand.
Either update API can replace the other shape. Invalid regions return
`invalid_region`; invalid handles or modes return `invalid_request`.
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
`LoadedChunks()` includes both modes in unspecified column order, with each
complete column contiguous and its 21 layers in ascending Y. Request shapes merge
into keyed column demand with active demand winning. Every selected column has
uniform activity. Reconciliation traverses the column maps directly, without a
separate committed demand index; keyed ownership avoids shifting large terrain
arrays when demand slides. Chunk queries use a private column hash and a Y-array
index. Dirty and optical-change chunks are keyed sets; per-chunk revisions and
player positions use keyed maps. Ordering is imposed at behavioral and persistence
boundaries rather than on these internal traversals.

`GetPerceptionStats()` also counts `columnsLoaded`/`columnsUnloaded` and actual lazy
lighting work: `lightingOutputChunks`, `lightingSkyDirectChunks`,
`lightingSkyFirstChunks`, and `lightingSourcePatches`. These cumulative diagnostic
counters reset through `ResetPerceptionStats()` without invalidating caches;
`lightingRebuilds` counts input synchronization, not eager whole-field rebuilds.

Storage reads and incoming archive validation (including cross-source ID aliases)
complete before live ownership, activity, archives, or runtime keys change. A failed
reconciliation leaves the live world intact for retry. All new columns and their
terrain are installed before restoration; complete residency and item ownership
are established before item callbacks are attached.
Gravity settles at the completed boundary.

`SessionState::circleResidency` is an appended optional `CircleResidency` policy.
Its default radius is `playerSightRange + maxLightRange + lightSpillDistance`
(78 tiles); absence retains legacy square residency. Checkpoint/load validation
checks the active square plus the selected resident footprint. Retained legacy
radii still satisfy `0 <= activeRadius <= residentRadius <= 8`, but an unused
resident square does not constrain a circle session's position. The policy does not itself acquire demand;
session/server code issues the corresponding circle request.

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
`Carried`, `Inside` and `Stored` to construct destinations. Items use catalog-local
`ItemTypeId` values in `Item::type`; `CreateItem(type, location)` initializes shared
resources and independent virtual behaviors through their immutable definitions.
Runtime behavior ownership is private. `CaptureItemState`, `GetItemEnergy` and
`DescribeItem` provide value inspection. Ordinary item storage requires a container
capability. `Inside(parent, installation)` with a nonzero capability ID instead
selects that behavior's installation mount; compatibility is defined by its
`AcceptsInstallation` hook. Nested trees are limited to 64 items along
any root-to-leaf path. Self-containment and ancestor cycles return
`containment_cycle`; non-container ordinary parents return `not_container`.

`TransferItem` validates before changing ownership and spatial indexes. Moving
a container carries its descendants without changing their direct parent.
It enforces storage volume, mount compatibility/occupancy, depth and the
destination root actor's carry mass before committing. Same-location transfers
are no-ops; same-root transfers do not count moving mass twice. This is an
administrative operation and does not enforce reach or spend simulation time.
All ownership changes go through `World`, never borrowed collections.

```cpp
cataclysmx::WorldResult<void> give_rock(
    cataclysmx::World& world, cataclysmx::ActorHandle actor) {
    auto rock = world.CreateItem(world.GetContent().FindItem("rock"),
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

`Actor::carryCapacityGrams` defaults to 100000. `SetActorCarryCapacity(handle,
grams)` rejects a limit below current carried mass. `GetItemMass` includes every
descendant, including installed batteries; `GetCarriedMass` sums full inventory
trees. `GetContainerUsedVolume` sums only immediate ordinary children's fixed
external volume, excluding installed batteries. These read-only uint64 queries
return zero for invalid handles and fail fast on arithmetic overflow. There is
no container-specific mass limit. Capacity failures return `capacity_exceeded`,
incompatible installations return `incompatible_item`, and an occupied
mount returns `occupied`. World delegates item action dispatch and time processing
to `ItemSystem`; individual behavior implementations apply effects through checked
mutation contexts. See [items API](items_api.md) for actions and activities, and
[adding item behaviors](item_behaviors.md) for the extension contracts.

## Support and gravity

Terrain with `open_space` provides no floor support; all other terrain supports
a floor. `SettleGravity()` settles unsupported actors and complete ground-item
trees deterministically, bottom-up by Y and persistent ID, without RNG, damage, or elapsed time. It
stops above a supporting tile, actor, or the world bottom. Parked actors remain
frozen. Spatial terrain edits also settle affected resident columns. See
[Y layers](y_layers.md) for terrain flag constraints, stairs, and sight rules.

## Simulation clock and deadlines

`core/timing.h` defines `Tick` as unsigned 64-bit simulation time, with 100 ticks
per standard turn. `World::GetTick()` is the authoritative clock (initially zero).
Actors own `actionDeadline`; legacy periodic objects own `updateDeadline`.
Read relative durations with `GetActorRemainingTicks(handle)` and
`GetObjectRemainingTicks(handle)`; these require valid live handles and return
frozen durations during suspension. See [entity scheduling](entity_scheduling.md).

- `SetActorSpeed(handle, speed)` rejects zero with `invalid_request`; it does not
  change an already committed delay. Lower speed values mean shorter actions.
- `ScheduleActorAction(handle, baseCost = 100)` requires a live, active, ready
  actor and positive cost. It sets the delay to `ActionTicks(baseCost, speed)`
  (ceiling of cost × speed / 100), increments `turns` exactly once, and marks the
  chunk dirty. Inactive actors return `chunk_unavailable`; a non-ready actor or
  zero cost returns `invalid_request`. Counter overflow is a fatal invariant.
  `RecordActorTurn` remains an explicit administrative counter operation; it
  neither checks readiness nor schedules a delay.
- `ElapseTicks(elapsed)` advances the World clock with checked arithmetic;
  ordinary elapsed time never rewrites actor or legacy object deadlines.
  Active chunks containing actors/objects are conservatively marked dirty so
  incremental checkpoints capture changed relative durations. It does not
  record turns or trigger updates. Inactive, archived, parked, and disabled
  timers remain frozen, including across unload/reload.
  Controllerless NPCs are idle and excluded from clock advancement. Typed object
  behaviors advance their own work/energy; typed `None` objects have no timer.
- `SetObjectUpdateInterval(handle, interval)` rejects zero and explicitly
  restarts the timer at that interval, even when disabled or resident.
  `SetObjectEnabled` preserves the current delay. `RecordObjectUpdate` requires
  an active object; if enabled it increments `updates` and resets the delay to
  `updateInterval`, otherwise it changes neither. The scheduler calls it only
  when due; World permits explicit administrative updates before then.

Invalid mutation handles return `invalid_handle`. Suspension captures remaining
duration once; resumption rebases only after all eligibility conditions hold.
Actor delays may exceed uint32. Global clock/deadline overflow fails fast.
Checkpoints persist the World clock as `SessionState::tick`, overriding the
caller's copy, and retain relative delays in the existing archive format.
Loading installs the saved clock before any chunk can materialize.
All these mutations preserve the existing borrow rules.

`CollectReadyPlayers(Vector<ActorHandle>&)` clears the caller's buffer and appends
all active, unparked, ready players sorted by persistent ID. `ProcessNpcActions(uint32_t& rng)`
captures due NPC `{id, handle}` records in a reusable World-owned buffer, sorts by
ID, and revalidates each record before invoking its controller. A positive action
deadline is committed before attempting movement. Wander retains the legacy one-draw
horizontal policy, including blocked attempts, and each NPC action is followed by
`UpdatePlayerObservations()`. `NextActorEventIn()` directly scans eligible central
slots for the minimum relative deadline, returning empty when none can act.

`ProcessObjectEvents()` returns whether any due object event was processed. It
captures and orders one due batch by persistent ID, revalidates handles, and
dispatches behaviors or generic timer updates. New entities wait for a subsequent
batch; callback reentry is forbidden. A processed behavior must clear its zero
deadline. `NextObjectEventIn()` is a direct minimum scan, without snapshots or
sorting. Disabled objects freeze even pending completion; enabled machine jobs
at zero work remain due regardless of running intent or remaining power.

Actors now live in `actors/actor.h`; controllers are privately owned by World.
`CaptureActorController(handle)` captures the stable `{type,state}` envelope;
invalid handles return an empty object. `SetActorController(handle, kind)` accepts
`None` (idle) or `Wander` for NPCs. Players must use `None`; assigning player AI
returns `invalid_request`. Archive validation and restoration use the same strict
envelope helpers, preserving idle NPCs rather than installing a default policy.

`TryActorStep(actor, TileCoord direction, bool activeOnly = false)` shares cardinal
movement validation and execution between command and controller callers. The
source must be active and unparked; the target must be loaded, passable, and
unoccupied. Vertical steps require stairs at both endpoints. `activeOnly` also
requires an active target (NPC wandering). This operation spends no time; callers
schedule their action separately. `MoveActor` remains administrative relocation.

### Typed objects and callback transactions

`CreateObject(ObjectTypeId, position, enabled = true)` creates a content-defined
object; the existing `CreateObject(position, enabled = false)` creates a generic
timer. `DescribeObjectState(handle)` returns an optional value view (empty for
invalid/generic objects); `CaptureObjectState(handle)` captures behavior state,
or an empty object when absent. `SetObjectEnergy(handle, uint64_t)` validates
capacity and dirties the owning chunk on success. Adding energy does not restart
paused work: use `Start` explicitly. Enabled state is an independent clock gate,
so disabling/re-enabling never discards progress or running intent.

`UseObject(actor, object, ObjectAction::{Start,Stop,Collect})` rechecks access for
every action: active unparked actor, active object, same Y level, and Chebyshev
distance at most one. Cross-layer interaction is rejected even through open space
or matched stairs. Success reports a positive base cost without
scheduling the actor; refusals report zero cost and preserve state.

World implements callback-scoped `ObjectMutationContext` transactions. Starting
a job preflights sufficient direct-inventory leaf inputs, then consumes the
lowest persistent IDs. Nested items and nonempty containers are ineligible.
Output creation targets `Stored(self)`. Collection preflights every transfer and
the entire aggregate carried mass before moving any contents. Item notifications
remain deferred through the object callback and gravity settles at the enclosing
boundary, preventing intermediate notification side effects. Self-destruction is
requested and applied only after the callback returns. Contexts and entity borrows
must never escape callbacks. Identity and the destruction request live in the
context; World retains only the callback guard and scoped notification/gravity
deferral. See [object callbacks](object_callbacks.md) for ordering and lifetime.

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
Archives include actor carry limits, item types, installation IDs and all
capability state. Runtime owner handles and the sparse activity registry are
rebuilt on materialization; no runtime handles are persisted. Archive validation
checks definitions, capability state, mounts, volume, depth, root mass and
deployed timer targets before creating live keys.
Resident-only and archived entities do not advance or catch up on reactivation.
Pending archives exist only for currently unloaded chunks in this `World`, and
disappear with it. `ArchivedChunkCount()` counts these authoritative pending values
only. They are never evicted before a successful checkpoint, and failed checkpoints
retain them. After commit, a bounded subset transfers to a separate clean FIFO warm
cache before pending storage is released; the pending count
is then zero. Load precedence is live, pending, warm, disk, then generation.
Warm hits consume their cached value only after successful preflight, restore fresh
handles, and retain no runtime light fields. New authoritative captures/checkpoint
replacements invalidate stale same-coordinate cache values.

The warm cache holds at most 256 layer archives with an 8 MiB conservative owned
storage budget (including its outer capacity and actor/object vector capacities).
Only archives with zero item-vector capacity, no actors, and no typed objects
are eligible. `DataObject` does not expose recursive allocation capacities, so
controller envelopes and typed behavior state use disk after checkpoint instead
of an inaccurate heap-size estimate. Actor-free generic-object caching remains
available; authoritative pending archives are never excluded.
Oversized eligible values are skipped. Pending write and warm-cache admission order
follow unspecified pending-map traversal; FIFO eviction follows actual admission
order, with no wall-clock policy. Eviction changes performance only; the committed
database remains authoritative. See the [save format](save_format.md) for disk
checkpointing; validation scans all durable chunks and is not constant-cost streaming.

Internally, central slot maps own live entities, while chunks hold spatial
indexes. Keeping these separate preserves live handles across chunk movement.
`CollectActiveActors(Vector<ActorHandle>&)` and `CollectActiveObjects(Vector<ObjectHandle>&)`
clear caller-owned buffers while retaining capacity, then scan central slots to
append unordered snapshots. Parked actors are excluded; objects are included
regardless of enabled state. Ordering belongs at the behavior/presentation boundary,
not in generic collection. `ItemsAt(position)` returns an allocation-free `ItemRange` filtering the
containing chunk's ground-item index, yielding `ItemHandle` values in unspecified
order. Invalid or unloaded positions yield an empty range. Use range-for traversal,
`begin()`/`end()`, or `Empty()` for inspection; the range has no size or random-access
interface. The range and its iterators borrow World storage and are invalidated by
any World mutation (and by World destruction); reacquire them after mutation.
`CountItemsAt(position)` counts directly without building a snapshot.
`CollectItemsAt(position, output)` clears a caller-owned `Vector<ItemHandle>` while
retaining its capacity, then appends an explicit snapshot in unspecified order.
Use this snapshot when traversal must cross a mutation. Its handle values survive
changes to the query/index, but deletion or unloading can make them stale; use
`GetItem()` to check liveness. These separate APIs keep routine inspection free of
disposable allocations and make snapshot ownership explicit.
Default item selection chooses
the minimum persistent ID, and presentation orders items at its boundary. Gravity
and scheduled event ordering remain explicit; storage/load order is not a turn
order. `CheckInvariants()` checks ownership and spatial consistency for
administrative/headless scenarios; it does not advance or repair the world.
