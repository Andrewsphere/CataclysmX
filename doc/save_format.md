# Save format

CataclysmX saves are SQLite database files. The strict v17 format is governed by
one authoritative `metadata.format_version = 17` for both SQL and JSON. The
`metadata` columns are `singleton INTEGER PRIMARY KEY CHECK(singleton=1)`,
`format_version INTEGER NOT NULL`, and `game_version TEXT NOT NULL`; its single
row is `(1, 17, "0.1.0")`. The other tables are
`world_state(singleton INTEGER PRIMARY KEY CHECK(singleton=1), payload TEXT NOT
NULL)` and `chunks(x INTEGER NOT NULL, z INTEGER NOT NULL, y INTEGER NOT NULL,
payload TEXT NOT NULL, PRIMARY KEY(x,z,y))`, and
`player_memories(player_id TEXT NOT NULL, x INTEGER NOT NULL, z INTEGER NOT NULL,
y INTEGER NOT NULL, payload TEXT NOT NULL, PRIMARY KEY(player_id,x,z,y))`.
Extra application tables/schema objects are rejected.

## JSON payloads

### Format 17: solar and integrated wood-gas sources

The SQL tables and exact object field set remain unchanged from format 16.
The `power` envelope writes version **2**, appending device kind **Solar=3**;
Battery=0, Generator=1 and Machine=2 retain their meanings. Wood gas uses the
existing generator state and fuel accounting without a new role or second store.
Power version 1 remains readable only without solar devices. Restore and the
first legacy checkpoint normalize the envelope to version 2, preserving grid IDs,
holds, fairness/charging markers, paid work, fuel and source allowances.

Solar behavior state version **1** has exactly `version`, `run_requested`,
`interval_initialized`, `interval_start`, and `output_remaining`. Its unsigned
state values use the existing behavior-state integer encoding, not global
power-counter decimal strings. Initialized intervals are quantum-aligned and no
later than the session tick; remaining output cannot exceed the integrated
daylight allowance for that interval and immutable peak output. Stopped sources
have zero output remaining. Direct exposure and current daylight/running status
are derived from resident geometry and the world clock, never saved.

Solar sources must match the global index's identity, position, role and
`enabled && !broken && runRequested` hold, even overnight or offscreen. For both
finite source kinds a processed charging marker requires an initialized source
interval at least as recent as that marker. Archive/index validation happens
before synchronization, so reload cannot repair corrupt ledgers or reissue output.
Fixed solar/wood-gas installations have no backing equipment item; placement
consumes their kit and creates neither fuel nor stored electricity.

Formats 7–16 remain readable under their existing migration rules, including
legacy skill-state conversion. The first checkpoint upgrades all payloads and
metadata transactionally to 17; opening alone does not rewrite the database.
Generation remains version 7; existing saved grids are not retrofitted.

### Format 15: global electricity

The world-state root adds required `power`, in the same snapshot transaction as
session, chunks and memories. The four SQL tables are unchanged. Power version 1
has exactly `version` (uint32 JSON integer), `next_grid_id`, `mains_cutoff_tick`,
`settings`, `grids`, `devices`, and `generated_bindings`. IDs and tick counters
are canonical unsigned uint64 decimal strings, including zero where permitted.
Settings has exactly integer `max_wiring_width` and `max_wiring_depth` (1–64).

- Grids have exactly `id`, `regions`, `mains_connected`, `fairness_cursor`,
  `processed_interval`, `interval_processed`, `mains_charge_interval`, and
  `mains_charge_processed`. Regions are inclusive `{min: {x,z,y}, max: {x,z,y}}`
  tile boxes, retaining geometry rather than expanding it into tiles.
- Devices have exactly `object`, `position`, `kind` (Battery=0, Generator=1,
  Machine=2), `workshop_hold`, `charge_interval`, and `charge_processed`.
  Holds and charge ledgers survive even for objects outside player residency.
- Generated bindings have exactly `origin: {x,z,y}` and `grid`.

Flags are JSON booleans. `ValidPowerState(state, session.tick)` owns semantic
validation: ordered unique grid/device IDs, bounds and cross-grid overlap,
binding identities/references, cutoff, settings and interval markers. Processed
markers must be quantum-aligned and not in the future; unprocessed markers have
zero interval. The database additionally bounds object/cursor IDs by `next_id`
and requires each registered device to identify a durable typed object at the saved
position and with the matching equipment role. Every saved generator, grid battery,
and grid-powered machine must appear in the index, including offscreen objects.
The saved hold must match `enabled && !broken && runRequested` and, for machines,
`hasJob`. Behavior state is validated before these checks; generator budget
intervals must be aligned and no later than the session tick. Cursors can refer
to former contenders. Runtime handles are absent.
Battery charge, generator fuel, machine paid work and item/object behavior state
remain authoritative in their existing entity payloads; power does not copy them.
The interval ledgers prevent a checkpoint from granting the same budget twice.

Formats 7–14 retain their exact original root fields and decode missing electricity
as a fresh empty `SavedPowerState`, with `nextGridId = 1`, default settings, no
grids/devices/bindings and `SeededMainsCutoff(seed)`. Migration never creates charge
or fuel. Opening remains read-only; the first checkpoint rewrites all old chunks
and the snapshot and advances metadata to the current format atomically. Rollback restores the
original metadata and payloads. Generation-version acceptance follows
`world_generation_version` independently; migration preserves the saved version.

### Format 14: equipment, combat arts, terminal life, and player sessions

Format 14 retains the four SQL tables above. Each actor payload adds required
`equipment` and `character_combat` structured values. Combat envelope version 3
is required on disk. Formats 7–13 are decoded incrementally according to their
original exact field sets; the next checkpoint rewrites all chunks and session
state and updates metadata in the same transaction. Rollback retains the original
metadata and payloads. Generation versions are independent of save versions.

Each roster entry adds `session`, a nonzero canonical decimal-string persistent
ID. `actor` identifies the current character; `session` identifies its stable
player session across character replacements. Source aggregates may leave
`SavedPlayer::session` zero, meaning the actor ID, and legacy saves migrate to that
same identity. Current actor IDs, stable session IDs, and nonzero reconnect tokens
are each unique within a roster and IDs must be below `next_id`.
`SessionState::player` remains the current **host actor** ID, with the existing
position/token rules. Dead historical player actors need not remain in the
current-character roster. Their actor-owned memories retain their original
identity; a replacement does not inherit those memories or skills.

`combat` version 3 has exactly `version`, `legacy`, `style`, `life`, `corpse`,
`inexhaustible`, `authoredTrainingRanks`, `offensiveTrainingRank`,
`defensiveTrainingRank`, `attackRating100`, `defenseRating100`, `reach`,
`staggerRemaining`, `focusRemaining`, `focusRatingBonus100`,
`combatLockRemaining`, `exertion`, `staminaRegenerationRemainder`,
`legacyAccuracyOverride`, `intrinsicMissOverride`, `moveCooldowns`, `openings`,
and `action`. `legacy` is an exact version-2 projection of anatomy, injury profile,
protection, numeric stamina, and old compatibility fields. For definition-driven
actions its action is idle; the outer `action` is authoritative. Keeping this
projection reuses the established strict injury codec and keeps migration
explicit, rather than duplicating body-validation rules.

The outer action has exactly `kind`, `target`, `move`, `weapon`, `accuracyPenalty`,
`recoveryTicks`, `moveId`, `committedStyle`, `trainingArt`, `ability`,
`abilitySource`, `sources`, `abilityTarget`, `ratingPenalty100`, `trainingWork`,
`counter`, and `definitionDriven`. Definition identities are stable strings;
empty strings mean absent where allowed. Numeric `move` is only the old closed
legacy enum, never a catalog index. `abilitySource` has `kind` and stable `art`;
Art requires a nonempty art and Innate requires an empty art for actual abilities.
An unused action retains its unbound default grant key. `abilityTarget` records
`kind`, persistent `actor`, signed `x`/`z`/`y`, and `direction`. Stale actor/item
targets remain valid saved commitments; execution revalidates them without
substituting sources or refunding the paid cost.

Catalog validation checks relationships as well as identity existence. Offensive
move commitments must match their style repertoire, training art, trigger/counter
flag, execution phase, and exact source requirements. Sweep must borrow precisely
its ability definition's `sweepMove`; Focus and Leap carry neither a strike move
nor source commitments. Ability training art follows the selected grant source,
which can differ from the committed style's art. Art-derived grants/cooldowns
must refer to abilities declared by that art; a cooldown does not require a
currently learned art or live grant. Manual progress must reference an authored
book/art pair, with funding bounded by both remaining progress and the 100-tick
funding slice. These checks never require committed entity IDs to remain live.

Actor-aware v1/v2 migration initializes `combatLockRemaining` from the already-paid
relative deadline: remaining windup plus captured recovery for MeleeWindup, or
remaining recovery for Recovery. The sum is checked for uint64 overflow. No
stamina is charged again, and inactive time remains frozen. The legacy unarmed
heavy adapter preserves its original empty-hand binding for execution-time
revalidation rather than substituting a currently held item.

`sources` has exactly `hands` (two `{item, grip, required}` records),
`functionalHands`, `functionalLegs`, `primaryHand`, `primaryKind`, `secondaryKind`,
`naturalApparatus`, and `paired`. Item IDs are persistent IDs, not handles.
`reach` has `distance`, `height`, and `regions`. `moveCooldowns` is a sparse list
of `{move: stable-name, remaining}` with unique moves and positive durations;
`openings` is a sparse list of `{opponent, remaining}` with unique nonzero
persistent opponents and positive durations. All timers remain relative active
simulation time; the World validates absolute deadline arithmetic at restore.

Life is Alive or Dead. Detailed Head **or** Torso zero, or simple Core zero, is
terminal. Legacy zero-vital actors migrate to Dead with their pending action and
active study canceled and deadline suspended. `corpse` is zero until the World
performs its single terminal finalization, then the persistent corpse item ID.
Reading state never creates a corpse or transfers inventory. Dead state requires
terminal anatomy and no actor action; restoring health does not implicitly revive
a dead human. Dead actors do not occupy movement cells for archive validation.

`equipment` version 1 has exactly `version`, `hands`, and `worn`. `hands` contains
two `{item, grip}` records, left then right. Zero means empty, with OneHanded as
the empty-slot grip. TwoHanded repeats the same nonzero item in both slots;
OneHanded cannot repeat an item. `worn` is an ordered list of unique nonzero IDs,
exclusive with held IDs. The codec performs shape checks; the database also checks
direct inventory ownership and the left-first `wielded_item` projection. Legacy
wielded equipment is expanded using the item's authored hand count. The World
rebinds fresh handles after all entities exist and validates actual equipment
eligibility. `CaptureEquipment` resolves every current handle afresh.

`character_combat` is `{}` for a null optional character state. A present empty
state is distinct: version 1 has exactly `version`, `arts`, `grants`, `cooldowns`,
and `manuals`, even when all lists are empty. Art progress records are
`{art: stable-name, xp}`. Grants are `{ability: stable-name, source}`; cooldowns
add positive `remaining` and are keyed independently by `(ability, source)`.
Cooldowns need not have a currently held grant. Manual attempts record stable
`book` and `art`, `remaining`, `totalDuration`, `successChance`, `active`,
`fundedTicks`, and `pauseRequested`. Remaining duration and the paid scheduler
slice are separate: the actor deadline matches `fundedTicks`, not the whole
remaining attempt. Paused attempts retain progress but have no funding or pending
pause request. Only one manual may be active and it is exclusive with ordinary
book study, actor combat actions, and crafting assignment.

Catalog-free combat codecs remain for legacy setup/default aggregate snapshots;
the catalog-free writer fails fast rather than discarding expanded state.
Catalog-aware overloads migrate v1/v2 style, heavy/reaction cooldowns, intrinsic
accuracy, stamina fraction, and pending move/source commitments by explicit basic
definition names. Recovery and paid windup time are preserved. Missing required
definitions are errors; no lookup failure selects a substitute. All envelopes
reject unknown fields, invalid types, overflow, unsupported versions, and duplicate
sparse identities. Captured trees use values and explicit character deep-copy/
value-equality helpers, never `unique_ptr` address equality.

Version 13 added actor learning state. The sections below document retained
earlier layouts and the remaining world/item payloads.

Version 12 adds stable object type strings to remembered furniture. Missing types
in legacy memory remain unknown until reobserved; formats 7?11 remain readable
and upgrade on checkpoint. See [furniture sprites](furniture_sprites.md).

Version 11 adds required actor `wielded_item`, a canonical unsigned decimal-string
persistent item ID (zero means unarmed). It must refer to the actor's direct
inventory. Runtime handles are never serialized. Combat envelope version 2 adds
unsigned `action.weapon`, zero outside melee windup and otherwise the exact
committed weapon ID (zero for unarmed). A nonzero pending weapon must match the
equipped item. Restoration binds equipment after all items have been restored;
ownership validation rejects inconsistent selections before materialization.
Formats 7–10 remain readable and upgrade transactionally on write; they default
to unarmed. Combat envelope version 1 remains readable with a zero weapon ID.

The single `world_state.payload` object has exactly `seed`,
`generation_version`, `next_id`, `sky_light`, `session`, and (in v15) `power`. `sky_light` is a
required integer in 0–255: the fixed environment illumination (new-world default
192), independent of wall-clock time. The session object has exactly
`tick`, `rng_state`, `player`, `position` (`x`, `z`, `y`), `active_radius`,
`resident_radius`, `autosave_interval`, `pacing`, `players`, and `circle_residency`.
`circle_residency` is required in v8/v9/v10/v11/v12 and is either JSON `null` (legacy square
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
host must not be parked while alive; a terminal dead host may remain parked.
Sessions with both host ID zero and empty roster
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
  "controller": {"type": string, "state": structured-value}, "combat": structured-value }`.
  Carry capacity preserves the full uint64 range. The only optional actor field
  in v10 is `monster_type`: a stable `MonsterDefinition::id` string, never a runtime
  catalog index. Absent or empty string means untyped; writers emit it only when
  nonempty. Present nonstrings, unknown nonempty IDs, embedded NULs, duplicate
  fields and nonempty types on players are invalid. Resolution validates identity
  without reapplying spawn defaults: saved injuries, speed, controller, inventory
  and deadlines remain authoritative. Existing v10 saves without this field load
  as untyped. Metadata remains 10 and no SQL schema migration is required. Older
  executables reject saves containing the new field through their strict actor
  allowlist; this compatibility is new-reader/old-save only.
  Controller envelopes have exactly `type` and `state`. Stable controller names
  are `none` (empty object state), `wander` and `melee` (exact `{ "version": 1 }` state,
  with an unsigned version). Players, including parked players, require `none`;
  NPCs may use any controller. Missing/empty envelopes, unknown controller
  names, malformed state, duplicate keys and unsupported state versions fail.
  Controller read/factory helpers own the state schema and its validation.
  The required combat object is described below.
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
  Machine state requires its unsigned version. Absent energy/job/running fields
  default to zero/false/false; remaining work is required when a job exists and
  otherwise defaults to zero. Explicit malformed values still fail. See the
  [object state schema](objects_api.md#saved-state-format-and-safe-defaults).
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
  `AcceptsInstallation` policy. Metadata is version 12. Energy and charger state
remain version 1; door state version 2 adds required signed Y. These schemas use
unsigned JSON numbers and signed `$i64` target
coordinates. No capacity clamping or implicit state migration is introduced.

Parked actors must be player-marked. They retain position, inventory and delay
without occupying a tile. Parked actors may overlap live actors; two live
actors may not overlap. Unload, checkpoint and reload preserve parked state.
Session reconnect logic parks offline guests after loading; the durable roster
keeps their identities and tokens independently of transient connections.

Runtime actor and periodic-object scheduling uses absolute deadlines against
`World::GetTick()`. The saved `remaining_ticks` fields remain relative durations;
capture derives them at checkpoint time, and activation rebases them against the
current World clock. World checkpoints source `session.tick` from that clock.
Elapsed active chunks are dirtied even when their runtime deadlines stay fixed,
so incremental saves cannot retain stale durations. Chunk reconciliation rejects
saved delays that cannot be represented at the current tick before changing live
state. See [entity scheduling](entity_scheduling.md) for suspension and arithmetic.

IDs must be unique across all chunks and less than `next_id`; entity positions,
ownership, chunks and session player identity/position are validated. Generic
`World` saves allow session player ID zero; `Server` requires a nonzero player.
`generation_version` accepts 1..7; other values fail with
`SaveError::UnsupportedVersion`. New worlds use generation version 7, with
v6 street-led towns and persistent per-building electrical grids. Saved versions
1..6 retain their recorded generation algorithm;
version 4 introduced authored layers and population. Regeneration depends on the
supplied immutable content: changed patterns or palettes can change ungenerated
chunks even when the saved generation version is unchanged. Region plans are
regenerated, not serialized; global electrical state is serialized independently.
The SQL/JSON save schema is version 17; strict formats 7 through 16 are also
accepted and migrated as described below. Save formats v1 through v6 and all other schema
versions are rejected. In particular, v2 command-count ticks are not silently reinterpreted as
simulation ticks. Content's own format version is independent of both versions.
Version 7 explicitly adds illumination sources, object semantic identity,
fixed skylight and independent per-player observations. Individual state migrations are a separate,
explicit capability-version policy; no generic auto-clamp or fallback applies.

### Version 16 workshop equipment

Objects append `equipment`, a canonical decimal persistent-item ID string (zero
for fixtures and pre-workshop equipment). It identifies the original protected
direct child of a portable station; its type must match the object's
`packing_item`, and it must have no children or installation. The backing item
does not occupy public storage volume. Formats 7–15 decode this field as zero;
restoration never creates a missing backing item. Checkpoint upgrade writes the
new exact object field set transactionally.

Manual-job payload version 3 adds a sorted, unique `stations` list of at most four
nonzero persistent object IDs. Unassigned jobs have no claims; versions 1/2 migrate
with an empty list and retain their paid work. Database validation rejects duplicate
claims and claims without durable station objects. Unloading releases claims before
archive capture; a live checkpoint preserves active bindings.

W2 keeps envelope 16 and writes manual-job payload **4**, adding unsigned
`heat_remainder` strictly below 100000000, independently of electrical debt.
Versions 1–3 migrate with zero heat debt and retain already paid work.
Forge objects use their own strict version-1 behavior payload containing
`fuel` and `ready`. Fuel must fit authored capacity, and Ready requires positive
fuel. There is no initial fuel restoration or inactive-time catch-up.
Cold/exhausted forges may still be bound to an already funded manual slice;
the next slice rechecks readiness. See [forge ownership](objects_api.md#fueled-manual-forges).

### Combat state

The actor `combat` field is an owned version-1 `DataObject`. Its exact fields are
`version`, `body`, `profile`, `action`, `style`, `faction`, `protection`,
`attackDamage`, `accuracy`, `stamina`, `staminaElapsed`, `reactionCooldown` and
`heavyCooldown`. `body` contains `kind` and a fixed nine-entry `parts` list in
`BodyPart` order; each part has `hp`, `maxLoss`, `mendingElapsed`,
`healingElapsed`, `maxHealingElapsed` and `broken`. `profile` has
`mendingDuration`, `healingInterval`, `maxHealingInterval`, `mendedHp`,
`maxLossConversionPercent`, `maxLossCapPercent` and `regeneration`. `action`
has `kind`, `target`, `move`, `recoveryTicks` and `accuracyPenalty`.
`protection` is a fixed body-part by damage-type matrix. Numeric values are
unsigned DataObject values; flags are booleans. Nested field sets, enum bounds,
body/profile and action invariants are validated rather than repaired. An unknown
combat version is unsupported. Pending action targets are persistent IDs and may
legitimately refer to an actor that has gone away. The action deadline is saved
separately as `remaining_ticks`. Combat feedback is transient and not saved.
See [combat API](combat_api.md) and [DataObject encoding](data_object.md).

### Version 7/8/9 compatibility and checkpoint upgrade

Versions 7, 8 and 9 have the same SQL tables and memory encoding as v10. All
three lack actor `combat`; decoding synthesizes `MakeCombatState(player)` defaults.
All three also lack `monster_type` and decode as untyped; their strict field sets
reject that field even when empty. Identity is never inferred from AI or statistics.
Versions 7 and 8 additionally have actor
records lack `controller`; object records lack `type` and `state`. Decoding
synthesizes default player `none` / NPC `wander` envelopes and generic objects
with empty state. These legacy exact field sets reject the v9 additions.
Version 7 also excludes `circle_residency` from the exact session fields;
opening v7 preserves its chunk radii and selects square residency. Versions 8/9
require the field, as does v10, preserving any saved circle. Opening is read-only validation:
it does not rewrite payloads or metadata. Metadata selects the strict decoder;
relabeling modern chunks as legacy is invalid.

`SaveDatabase::Begin` starts the first checkpoint with `BEGIN IMMEDIATE`, checks
that metadata still matches the opened durable version, and validates the old
database. Before returning to the caller it rewrites **every** legacy chunk and
the snapshot, then sets metadata to 12, all inside that transaction. A coordinate
list allows decoding/writing one chunk at a time without modifying an active
chunk scan. Each old row is decoded exactly once during the rewrite; newly
written v12 rows are never decoded as legacy. Memory rows are unchanged by the
migration itself. Generation versions 1, 2 and 3 are preserved.

A separate transaction-local effective version switches to 12 only after the
full rewrite. Reads and commit validation use this effective version, including
new caller `WriteChunk` rows; the durable version advances only after SQLite
commits. Snapshot-only, chunk-only and empty checkpoints all migrate the full
database. An explicitly supplied snapshot may select a new residency policy.
Validation, SQL, busy-commit failures and explicit rollback restore the original
metadata, snapshot and every chunk. Rollback resets the effective decoder to the
durable version, permitting legacy reads and retry on the same connection.

RAM archive fields are appended for source aggregate compatibility:
`SavedActor::controller` is an owned `DataObject` envelope;
`SavedActor::monsterType` is an owned stable definition ID string (empty untyped);
`SavedObject::type` and `state` are an `ObjectTypeId` and owned `DataObject`.
An empty object controller in a source-constructed archive is a compatibility
sentinel: validation/encoding normalizes it to the player/NPC default via the
controller factory. This convenience applies only to RAM construction; real v10
JSON must contain a complete envelope. Object state has no default sentinel for
typed objects. Shared helper/factory validation keeps persistence independent of
behavior-specific state internals.

RAM archives also retain owned combat state and freeze pending actions, cooldowns
and recovery while absent; reactivation resumes from saved progress without
offline catch-up.

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
"active": boolean, "broken": boolean, "type": string}, ...] }`.

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
  Appearance stores kind/facing/active/broken and a stable object type string.
  The type may be empty or absent for legacy/untyped observations; unknown
  nonempty IDs are invalid. There is no entity ID, behavior,
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
rejecting those snapshots from the warm cache. All actor-containing snapshots are
also excluded, covering their owned monster identity strings, controllers and
combat trees without estimating recursive allocations. Typed objects are excluded
for the same reason: their state owns `DataObject` storage. Eviction falls back to validated
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

auto content = cataclysmx::LoadContentPack("data/demo.json5");
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
