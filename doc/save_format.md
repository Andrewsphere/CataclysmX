# Save format

CataclysmX saves are SQLite database files. The strict v1 format is governed by
one authoritative `metadata.format_version = 1` for both SQL and JSON. The
`metadata` columns are `singleton INTEGER PRIMARY KEY CHECK(singleton=1)`,
`format_version INTEGER NOT NULL`, and `game_version TEXT NOT NULL`; its single
row is `(1, 1, "0.1.0")`. The other tables are
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
  entries. Terrain is `floor`, `wall`, `closed_door`, or `open_door`; damage is
  0–255.
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
`generation_version` must currently equal 1; other values fail with
`unsupported_version`. There are no implemented migrations or backup files.
Explicit transactional migration and a recoverable backup strategy are future
design considerations, not current behavior.

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
`Diagnostics*` supplies a human-readable explanation. SQLite and JSON internals
are not public simulation APIs.

Opening and committing scan all saved chunks to validate global ID uniqueness
and other data. Validation decodes at most one payload at a time, plus retains
the ID set: world chunks need not all be resident, but validation work and ID
memory are O(total saved data).

```cpp
#include "core/server.h"

cataclysmx::Server server(42);
auto created = server.CreateSave("run.sqlite"); // first save; refuses existing path
if (!created) { /* inspect created.error() */ }
auto saved = server.Save();
auto loaded = cataclysmx::Server::LoadSave("run.sqlite");
if (loaded) {
    // *loaded is eastl::unique_ptr<cataclysmx::Server>.
}
```

Keep the existing server until `LoadSave` succeeds when replacing a running
session. Administrative load-request handles are not persisted; loading restores
the player's saved bubble requests.
