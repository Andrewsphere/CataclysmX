# Save format

CataclysmX saves are SQLite database files. The strict v9 format is governed by
one authoritative `metadata.format_version = 9` for both SQL and JSON. The
`metadata` columns are `singleton INTEGER PRIMARY KEY CHECK(singleton=1)`,
`format_version INTEGER NOT NULL`, and `game_version TEXT NOT NULL`; its single
row is `(1, 9, "0.1.0")`. The other tables are
`world_state(singleton INTEGER PRIMARY KEY CHECK(singleton=1), payload TEXT NOT
NULL)` and `chunks(x INTEGER NOT NULL, z INTEGER NOT NULL, y INTEGER NOT NULL,
payload TEXT NOT NULL, PRIMARY KEY(x,z,y))`, and
`player_memories(player_id TEXT NOT NULL, x INTEGER NOT NULL, z INTEGER NOT NULL,
y INTEGER NOT NULL, payload TEXT NOT NULL, PRIMARY KEY(player_id,x,z,y))`.
Extra application tables/schema objects are rejected.

## JSON payloads

The single `world_state.payload` object has exactly `seed`,
`generation_version`, `next_id`, `sky_light`, and `session`. `sky_light` is a
required integer in 0–255: the fixed environment illumination (new-world default
192), independent of wall-clock time. The session object has exactly
`tick`, `rng_state`, `player`, `position` (`x`, `z`, `y`), `active_radius`,
`resident_radius`, `autosave_interval`, `pacing`, `players`, and `circle_residency`.
`circle_residency` is required in v8/v9 and is either JSON `null` (legacy square
residency) or an exact object `{ "radius_tiles": integer }` with radius in
0–128 inclusive. Zero is a valid circle radius; the default `CircleResidency`
value is 78 tiles. Missing, duplicate, unknown, noninteger or out-of-range fields
are invalid. `SessionState::circleResidency` stores this as an optional value;
absence selects square residency. `active_radius` and `resident_radius` retain
their existing chunk units and numeric validation in both modes:
`0 <= active_radius <= resident_radius <= 8`. In square mode the resident
square must fit the saved host's chunk-coordinate bounds. In circle mode only
the active square must fit those chunk bounds, and the actual tile circle must
fit the world X/Z bounds around the saved host position. The retained legacy
resident radius does not impose an additional spatial restriction in circle
mode. These spatial checks match the World/Server residency policy.
Keeping the optional circle separate preserves
the exact legacy square policy rather than inferring a circle from chunk radii.

`players` is an
array of at most 32 exact objects `{ "actor": decimal-string,
"position": {"x": int, "z": int, "y": int}, "name": string,
"reconnect_token": decimal-string }`. Names contain 1–32 bytes and no NUL;
actor IDs and nonzero guest tokens are unique. Every entry refers to a
player-marked actor at its recorded position, including unloaded chunks. The
host entry, when present, matches `player`/`position` and uses token zero;
guests require nonzero tokens. Empty rosters preserve generic world saves.
For sessions with a host or nonempty roster, every player-marked actor across
all saved chunks must appear in the roster; an empty roster with a nonzero host
acts as an implicit singleton roster and permits no other player actors. The
host must never be parked. Sessions with both host ID zero and empty roster
remain generic and need not enumerate player-marked actors.
The nested `pacing` object
has exactly `ticks_per_second` (positive uint32 JSON integer), `rate_limited`
(boolean), and `pause_when_idle` (boolean). Seed, generation version and RNG
state are unsigned 32-bit JSON integers; coordinates and radii are JSON
integers. `tick`, `next_id`, player ID and autosave interval are canonical
unsigned 64-bit **decimal strings** (digits only, no leading zeroes except
`"0"`). JSON objects require exact field sets; unknown and duplicate keys are
invalid.

Every row in `chunks` stores a complete snapshot of one single-layer 16×16 X/Z
chunk, not a delta. Chunk Y is in `[-10,10]`.
The root object has exactly `tiles`, `actors`, `objects`, and `ground_items`:

- `tiles`: exactly 256 row-major `{ "terrain": string, "damage": integer }`
  entries. Terrain is the stable string `TerrainDefinition::id` from the supplied
  content catalog (including custom terrain); damage is 0–255. Runtime
  `TerrainId` numeric values are never persisted. Every tile must resolve in that
  catalog; unknown strings, embedded NULs and numeric terrain values are invalid.
- `actors`: `{ "id": decimal-string, "position": {"x": int, "z": int, "y": int},
  "player": boolean, "parked": boolean, "turns": decimal-string, "inventory": [...],
  "speed": positive-uint32, "remaining_ticks": decimal-string,
  "carry_capacity_g": decimal-string, "light": {"intensity": integer, "range": integer},
  "controller": {"type": string, "state": structured-value} }`.
  Carry capacity preserves the full uint64 range.
  Controller envelopes have exactly `type` and `state`. Stable controller names
  are `none` (empty object state) and `wander` (exact `{ "version": 1 }` state,
  with an unsigned version). Players, including parked players, require `none`;
  NPCs may use either controller. Missing/empty envelopes, unknown controller
  names, malformed state, duplicate keys and unsupported state versions fail.
  Controller read/factory helpers own the state schema and its validation.
- `objects`: `{ "id": decimal-string, "position": {"x": int, "z": int, "y": int},
  "enabled": boolean, "updates": decimal-string, "contents": [...],
  "update_interval": positive-uint32, "remaining_ticks": decimal-string,
  "kind": integer, "facing": integer, "broken": boolean,
  "light": {"intensity": integer, "range": integer}, "type": string,
  "state": structured-value }`.
  `type` is the stable `ObjectDefinition::id`, never the runtime `ObjectTypeId`
  ordinal. The empty string identifies a generic object and requires `{}` state.
  Typed objects require an existing catalog definition and saved state accepted
  by `ValidateObjectBehaviorState`; an empty or missing state never requests
  factory defaults. Semantic appearance remains independent of behavior type.
  Semantic kinds are Generic=1, Chair=2, Table=3, Lamp=4; None=0 is invalid for
  a stored object. Facing is North=0, East=1, South=2, West=3. These closed semantic
  enum values are part of v7, not content ordinals or renderer sprite indices.
  Source intensity is 0–255, source range is 0–12, and intensity is zero if and
  only if range is zero. `enabled` controls object emission; disabled or broken
  objects still retain validated source parameters. Light objects have exactly
  `intensity` and `range`; actor and object sources follow the same validation.
- `ground_items` and nested `inventory`/`contents` arrays use exact item objects
  with `id` (decimal string), `type` (stable content item ID), `state` (structured
  capability object), `installation` (stable **parent** behavior capability name,
  or empty string), and recursive `contents`. Ground items also have `position`
  (`x`, `z`, `y`);
  contained items do not. Runtime type/capability ordinals are never persisted.
  Maximum nesting depth is 64 items including the root. Ordinary children require
  a container; their direct external volumes must fit its configured capacity.
  Installed children require a parent behavior that accepts their definition,
  with at most one child per installation capability. Existing chargers accept one
  rechargeable battery of the matching battery class per charger. A battery has
  exactly one energy store and no charger capabilities. Installed children do not
  consume ordinary container volume. Actor/object/ground roots cannot specify an
  installation. Every descendant's mass counts against actor carrying capacity.

State entries use stable capability names and capability-local schemas (energy
and chargers version 1, door controllers version 2); see
[data_object.md](data_object.md) for exact fields and integer encoding. Rock,
bandage, and backpack have empty state objects. Missing required saved state is
invalid, rather than initialized from defaults. Unknown item/capability names,
duplicate fields and unsupported state versions fail with diagnostic context.
Parents are decoded before children so installation names resolve in the parent's
definition. Enabled chargers require an installed compatible nonfull battery and
positive source energy. Armed door controllers must be directly on the ground,
target one of the six axially adjacent coordinates and retain a delay no greater than the
configured delay (including zero). Disarmed controllers have zero delay/target.

RAM archives store item state as a plain owned `DataObject` (defaulting to an empty
root object). Runtime capture/restore uses common typed energy state and each
behavior's factory/`SaveState`; the database writes and decodes the generic value
tree directly. The controller target is a world position and includes its Y
coordinate in the v6 state representation. `ValidateItemSnapshot` is the shared item-state and ownership-tree
validator for both archive paths, dispatching behavior-owned local and relational
validation. Installation names resolve against the parent definition and its
`AcceptsInstallation` policy. Metadata is version 9. Energy and charger state
remain version 1; door state version 2 adds required signed Y. These schemas use
unsigned JSON numbers and signed `$i64` target
coordinates. No capacity clamping or implicit state migration is introduced.

Parked actors must be player-marked. They retain position, inventory and delay
without occupying a tile. Parked actors may overlap live actors; two live
actors may not overlap. Unload, checkpoint and reload preserve parked state.
Session reconnect logic parks offline guests after loading; the durable roster
keeps their identities and tokens independently of transient connections.

IDs must be unique across all chunks and less than `next_id`; entity positions,
ownership, chunks and session player identity/position are validated. Generic
`World` saves allow session player ID zero; `Server` requires a nonzero player.
`generation_version` accepts 1 and 2 (legacy) and 3 (current); other values fail with
`SaveError::UnsupportedVersion`. New worlds use generation version 3. Versions 1
and 2 retain their prior surface generation; nonsurface slices use the layered
terrain rules. The SQL/JSON save schema is version 9; strict v7/v8 saves are also
accepted as described below. Save formats v1 through v6 and all other schema
versions are rejected. In particular, v2 command-count ticks are not silently reinterpreted as
simulation ticks. Content's own format version is independent of both versions.
Version 7 explicitly adds illumination sources, object semantic identity,
fixed skylight and independent per-player observations. Individual state migrations are a separate,
explicit capability-version policy; no generic auto-clamp or fallback applies.

### Version 7/8 compatibility and checkpoint upgrade

Versions 7 and 8 have the same SQL tables and memory encoding as v9. Their actor
records lack `controller`; object records lack `type` and `state`. Decoding
synthesizes default player `none` / NPC `wander` envelopes and generic objects
with empty state. These legacy exact field sets reject the v9 additions.
Version 7 additionally excludes `circle_residency` from the exact session fields;
opening v7 preserves its chunk radii and selects square residency. Versions 8/9
require the field, preserving any saved circle. Opening is read-only validation:
it does not rewrite payloads or metadata. Metadata selects the strict decoder;
relabeling modern chunks as legacy is invalid.

`SaveDatabase::Begin` starts the first checkpoint with `BEGIN IMMEDIATE`, checks
that metadata still matches the opened durable version, and validates the old
database. Before returning to the caller it rewrites **every** legacy chunk and
the snapshot, then sets metadata to 9, all inside that transaction. A coordinate
list allows decoding/writing one chunk at a time without modifying an active
chunk scan. Each old row is decoded exactly once during the rewrite; newly
written v9 rows are never decoded as legacy. Memory rows are unchanged by the
migration itself. Generation versions 1, 2 and 3 are preserved.

A separate transaction-local effective version switches to 9 only after the
full rewrite. Reads and commit validation use this effective version, including
new caller `WriteChunk` rows; the durable version advances only after SQLite
commits. Snapshot-only, chunk-only and empty checkpoints all migrate the full
database. An explicitly supplied snapshot may select a new residency policy.
Validation, SQL, busy-commit failures and explicit rollback restore the original
metadata, snapshot and every chunk. Rollback resets the effective decoder to the
durable version, permitting legacy reads and retry on the same connection.

RAM archive fields are appended for source aggregate compatibility:
`SavedActor::controller` is an owned `DataObject` envelope;
`SavedObject::type` and `state` are an `ObjectTypeId` and owned `DataObject`.
An empty object controller in a source-constructed archive is a compatibility
sentinel: validation/encoding normalizes it to the player/NPC default via the
controller factory. This convenience applies only to RAM construction; real v9
JSON must contain a complete envelope. Object state has no default sentinel for
typed objects. Shared helper/factory validation keeps persistence independent of
behavior-specific state internals.

`session.tick` counts global simulation ticks (100 per standard turn), while
`pacing.ticks_per_second` only maps simulation ticks to real time.
`autosave_interval` remains measured in turns of 100 ticks; zero disables it.
Actor and object `remaining_ticks` use canonical unsigned 64-bit decimal strings, including
zero when ready/due. Actor delays may exceed uint32; object delays must be no
greater than `update_interval`. Speeds, intervals, and pacing rates reject zero,
negative values, nonintegers, and uint32 overflow. Delay parsing rejects negative,
noncanonical, numeric JSON, and uint64-overflow values.

Timers are relative remaining delays, not absolute deadlines. Resident and
archived chunks and disabled objects retain frozen delays without catch-up on
reactivation, even when session time has advanced elsewhere. Persisting the
relative values keeps that rule identical across RAM archives and disk reloads.

## Checkpoints and API

### Per-player memory encoding

Each `player_memories` row stores a complete single-layer 16×16 observation chunk.
The player key is a canonical unsigned uint64 decimal **TEXT** value, preserving
IDs above SQLite's signed integer range. Zero, leading zeros, signs, whitespace,
overflow and embedded NULs are invalid. Chunk bounds match live chunk bounds,
including Y in `[-10,10]`. Memory chunks need not have any corresponding live
`chunks` row and survive unloading and eviction independently.

The exact payload is `{ "palette": [null, "floor", ...], "runs": [[index,count], ...],
"objects": [{"tile": integer, "kind": integer, "facing": integer,
"active": boolean, "broken": boolean}, ...] }`.

- Palette slot zero is always `null`, meaning unknown terrain. Other entries are
  unique stable terrain strings resolved against the supplied catalog. There are
  at most 257 entries, including unknown. Missing content fails with the stable
  name and `player_memories[player][x,z,y]` diagnostic context.
- Each run is exactly two integers: a valid palette index and a positive count.
  Runs cover exactly 256 row-major tiles without overflow. At most 256 runs are
  allowed. The writer emits maximal runs and first-use palette order; the reader
  accepts equivalent split runs and unused palette entries. Every allocated memory
  chunk must contain at least one observed terrain tile; all-unknown chunks are invalid.
- Objects are sparse, strictly increasing by tile index (0–255), with no
  duplicate tile, unknown enum value, None kind, or object on unknown terrain.
  Appearance stores only kind/facing/active/broken, with no entity ID, behavior,
  inventory, source parameters or ownership link to a live object.
- Memory owners must resolve to player-marked actors anywhere in the durable
  world, including parked actors and unloaded chunks. An empty server roster in
  a generic World save does not invalidate a legitimate memory owner.

`SaveSnapshot::memories` is decoded into owned `PlayerMemory` values, numerically
ordered by persistent player ID (not SQL text-key order). Input owners must be
strictly increasing; each owner's chunks must be strictly lexicographic `(x,z,y)`.
Duplicate owners/chunks/objects and unordered chunks/objects are errors. A player
with no allocated observation chunks needs no rows and is omitted on reload;
its owner is nevertheless validated when supplied to a checkpoint.
World's runtime memories are private maps keyed by player and chunk; capturing
`GetPlayerMemories()` by value or saving produces the canonical sorted snapshot.
Remembered objects remain small vectors sorted by tile index. Sorting at the
snapshot boundary preserves the existing format and validation without maintaining
player and chunk ordering on every observation.

`SaveDatabase::WriteSnapshot` rewrites all memory rows in V1, inside the same
transaction as changed live chunks and world/session state. This costs O(total
remembered terrain and objects) encoding/writes per checkpoint, including stale
observations. Saving serializes the supplied observation values without refreshing
them from live tiles or objects. Open and commit validate every memory row and
durable owner. Memory validation, SQL failures and commit failures roll back the
entire checkpoint, including prior chunk writes and memory-row deletion.

`World::Save` writes changed full chunk snapshots and session state in one
transaction. `World::CreateSave` exclusively creates a new path and attaches it
only after the initial checkpoint succeeds. `World::LoadSave` requires a
pristine world, validates the database, and leaves chunk materialization to
demand. Mutations mark chunks dirty. Unloaded chunks remain as RAM archives,
including pending changes, until a successful save; only then may saved archives
be evicted. A failed save rolls back, preserving the prior durable state and
retaining dirty memory for retry.

### Internal archive ownership and reconciliation

Unloaded pending snapshots are owned by a segmented hash map keyed by the full
`ChunkCoord` (X, Z and Y). They remain authoritative and may grow without bound
until a successful checkpoint; cache pressure never discards unsaved values.
Segmented storage avoids relocating a growing contiguous array of inline tile
payloads, and erasing a key moves at most one other payload. There is no separate
pending unload-order index; pending writes and admission use unspecified map order.

After commit, eligible pending values are admitted in map traversal order into a
separate clean warm map. Warm values are disposable, item-free snapshots with a
256-entry and 8 MiB limit. FIFO age is stored in ordered keys, independently of
subsequent hash iteration and dense-value relocation. Consuming a middle value leaves the
relative age of every survivor intact; replacing a value appends a fresh age.
Oldest values are evicted first. The byte budget includes allocated map value
capacity (including keys and unused slots), index storage, a conservative bound
for segment pointers, ordered-key capacity and actor/object allocation capacities.
Item allocations, including reserved-but-empty item storage, are excluded by
rejecting those snapshots from the warm cache. Actor controller and object state
now also own `DataObject` storage; cache budgeting must account for their dynamic
allocations or exclude snapshots carrying them. Eviction falls back to validated
disk loading and does not change persistent identity or saved values.

`Capture` produces trusted immutable RAM values from invariant-preserving world
operations. Demand changes do not repeatedly validate their terrain or item trees.
`CheckInvariants` still fully validates live, pending and warm snapshots.
External disk values cross the full validation boundary in
`SaveDatabase::ReadChunk`: decoding invokes `ValidateItemSnapshot` and chunk
validation, checking content, ownership, state, positions, timers and local IDs.
Reconciliation uses those results directly and collects IDs without revalidating
payloads to reject conflicts against all live entities (including ones about to
unload), pending values, warm values and other staged disk inputs. Known incoming
RAM values are counted once. With no external incoming values, the existing
identity invariant makes this global preflight unnecessary.

All disk reads and cross-chunk identity checks finish before reconciliation
consumes any RAM snapshot, retires live keys, or changes activity/residency.
Failures retain all sources for retry. When the demanded column set is unchanged,
reconciliation skips archive work and updates activity and per-layer world/map
revisions before retaining the usual deferred gravity
boundary. Resident disk-restored actors and ground items may still be suspended:
activation must settle them using the newly committed activity set. As on the
structural path, gravity is queued before clearing demand-dirty bookkeeping and
flushed when the gravity batch exits. Movement uses the normal mutation helpers,
including spatial indexes, player tracking, item notifications and lighting
invalidation where needed. Handles and relative timers remain intact; settling
does not advance time. Activity changes themselves do not invalidate geometry or
lighting. These choices keep storage validation at its boundary while preserving
deterministic gameplay, callbacks and failure atomicity. Cache retention itself
depends on admission order and affects performance only.

`ReconcileChunks` returns `SaveResult<void>` because storage-backed loads can
fail. It stages chunk changes and leaves live world state unchanged on failure.
Save operations similarly return typed `SaveResult<T>` errors; optional
`Diagnostics*` appends structured diagnostic frames; `FormatDiagnostics` renders
them for display. Caller context is retained and outer boundary contexts are
appended as failures propagate. Unknown terrain diagnostics include the missing
string and a location path such as `chunks[-3,2,0].tiles[37].terrain`. SQLite and
JSON internals are not public simulation APIs.

`SaveDatabase(const ContentCatalog&)` retains a nonowning catalog reference;
the catalog must outlive the database and the world that owns it. `World` and
`Server` likewise receive the catalog explicitly. Both opening and committing
validate every saved tile against that catalog, including nonresident chunks;
chunk writes also reject invalid runtime IDs (zero included). Stable strings
allow a save to reload against a catalog with different numeric ID ordering.
Missing definitions cause an error rather than substituting a default tile.
Regeneration requires the current immutable content catalog; the save does not
pin a content fingerprint. Consequently, catalog changes can affect unexplored
chunks when they are generated, while already saved chunk snapshots retain their
stored terrain IDs.
This API keeps content ownership outside persistence while making resolution
explicit and preventing runtime numbering from becoming a persistence contract.

Opening and committing scan all saved chunks to validate global ID uniqueness
and other data. Validation decodes at most one payload at a time, plus retains
the ID set: world chunks need not all be resident, but validation work and ID
memory are O(total saved data).

```cpp
#include "core/server.h"
#include "content/content.h"

auto content = cataclysmx::LoadContentFile("data/terrain.json5");
if (!content || !cataclysmx::ValidateServerContent(*content)) { return; }
cataclysmx::Server server(*content, 42);
auto created = server.CreateSave("run.sqlite"); // first save; refuses existing path
if (!created) { /* inspect created.error() */ }
auto saved = server.Save();
auto loaded = cataclysmx::Server::LoadSave(*content, "run.sqlite");
if (loaded) {
    // *loaded is eastl::unique_ptr<cataclysmx::Server>.
}
```

Keep the existing server until `LoadSave` succeeds when replacing a running
session. Administrative load-request handles are not persisted; loading restores
the player's saved bubble requests.
