# Save format

CataclysmX saves are SQLite database files. The strict v2 format is governed by
one authoritative `metadata.format_version = 2` for both SQL and JSON. The
`metadata` columns are `singleton INTEGER PRIMARY KEY CHECK(singleton=1)`,
`format_version INTEGER NOT NULL`, and `game_version TEXT NOT NULL`; its single
row is `(1, 2, "0.1.0")`. The other tables are
`world_state(singleton INTEGER PRIMARY KEY CHECK(singleton=1), payload TEXT NOT
NULL)` and `chunks(x INTEGER NOT NULL, z INTEGER NOT NULL, payload TEXT NOT NULL,
PRIMARY KEY(x,z))`. Extra application tables/schema objects are rejected.

## JSON payloads

The single `world_state.payload` object has exactly `seed`,
`generation_version`, `next_id`, and `session`. The session object has exactly
`tick`, `rng_state`, `player`, `position` (`x`, `z`), `active_radius`,
`resident_radius`, and `autosave_interval`. Seed, generation version and RNG
state are unsigned 32-bit JSON integers; coordinates and radii are JSON
integers. `tick`, `next_id`, player ID and autosave interval are canonical
unsigned 64-bit **decimal strings** (digits only, no leading zeroes except
`"0"`). JSON objects require exact field sets; unknown and duplicate keys are
invalid.

Every row in `chunks` stores a complete snapshot of one 16×16 chunk, not a delta.
The root object has exactly `tiles`, `actors`, `objects`, and `ground_items`:

- `tiles`: exactly 256 row-major `{ "terrain": string, "damage": integer }`
  entries. Terrain is the stable string `TerrainDefinition::id` from the supplied
  content catalog (including custom terrain); damage is 0–255. Runtime
  `TerrainId` numeric values are never persisted. Every tile must resolve in that
  catalog; unknown strings, embedded NULs and numeric terrain values are invalid.
- `actors`: `{ "id": decimal-string, "position": {"x": int, "z": int},
  "player": boolean, "turns": decimal-string, "inventory": [...] }`.
- `objects`: `{ "id": decimal-string, "position": {"x": int, "z": int},
  "enabled": boolean, "updates": decimal-string, "contents": [...] }`.
- `ground_items` and nested `inventory`/`contents` arrays use items with `id`
  (decimal string), `kind` (`rock`, `backpack`, or `bandage`), and recursive
  `contents`. Ground items also have `position`; contained items do not. Only
  backpacks may contain items. Maximum nesting depth is 64 items including the
  root.

IDs must be unique across all chunks and less than `next_id`; entity positions,
ownership, chunks and session player identity/position are validated. Generic
`World` saves allow session player ID zero; `Server` requires a nonzero player.
`generation_version` accepts 1 (legacy) and 2 (current); other values fail with
`SaveError::UnsupportedVersion`. New worlds use generation version 2. Version 1
continues to generate unexplored chunks byte-for-byte as before, while version 2
uses the region planner. The SQL/JSON save schema remains version 2: save format
v1 and all other schema versions are rejected, with no migrations or backup
files. Content's own format version is independent of both versions.

## Checkpoints and API

`World::Save` writes changed full chunk snapshots and session state in one
transaction. `World::CreateSave` exclusively creates a new path and attaches it
only after the initial checkpoint succeeds. `World::LoadSave` requires a
pristine world, validates the database, and leaves chunk materialization to
demand. Mutations mark chunks dirty. Unloaded chunks remain as RAM archives,
including pending changes, until a successful save; only then may saved archives
be evicted. A failed save rolls back, preserving the prior durable state and
retaining dirty memory for retry.

`ReconcileChunks` returns `SaveResult<void>` because storage-backed loads can
fail. It stages chunk changes and leaves live world state unchanged on failure.
Save operations similarly return typed `SaveResult<T>` errors; optional
`Diagnostics*` appends structured diagnostic frames; `FormatDiagnostics` renders
them for display. Caller context is retained and outer boundary contexts are
appended as failures propagate. Unknown terrain diagnostics include the missing
string and a location path such as `chunks[-3,2].tiles[37].terrain`. SQLite and
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
