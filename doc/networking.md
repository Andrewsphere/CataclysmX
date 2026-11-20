# LAN networking API

## Current protocol 28

The current protocol constant is `0x43584e1c`. Peers need matching builds and
content. Protocol 28 adds selectable machine commands, information transfer and
deletion, typed receipts, and owned machine/library views. Digital device,
information-package and machine-recipe definitions participate in content
fingerprints. See [digital runtime](digital_runtime.md) for APIs and commands.

Protocol 27 added separate solar output/counts to meter snapshots and
derived daylight/exposure to object inspection. Catalog-aware decoding checks
solar's canonical empty fuel/job fields, interval allowance, clock-derived
daylight and run state. Solar definitions and behavior semantics participate in
content fingerprints. See [renewable power](renewable_power_design.md).

Protocol 26 added per-skill ceiling semantics without changing learning
view byte layout. Structural decoding permits zero required XP only with canonical
cap accounting and learned level 1–10. Catalog-aware validation requires zero
required XP exactly at `maxLevel`, the correct threshold below it, and rejects
over-cap levels. Ordinary/advanced accounting applies only below the cap.
Skill fingerprints include each maximum and its active thresholds; omitted and
explicit equivalent default curves agree. Completed full-work unused books are
valid views, including when no learning benefit remains eligible.

Protocol 25 added explicit forge load/ignite/extinguish/discard commands,
fuel/readiness on station and tile-inspection snapshots, and typed heat refusals.
Forge settings, fuel identities/yields and recipe heat costs participate in content
fingerprints. See [forge controls](power_presentation.md#fueled-workshop-controls).
Protocol 24 added `place_station` (item ID and position), `pack_station`
(target object ID), typed station outcomes, nearby station inspection and sorted
active station IDs on crafting jobs. Views carry at most 289 nearby stations
(radius 8); each has persistent identity, content type, position, enabled/broken
flags, worker identity and packing availability. Object traits and packing mappings
participate in content fingerprints. See [workshop design](workshop_design.md).
Protocol 23 added grid wiring/equipment placement, typed power outcomes,
meter-gated coverage and supply views, and powered-object inspection. Future mains
shutdown timing remains host DEBUG information. See [power presentation](power_presentation.md).
Protocol 22 added sparse injured enemy health to ordinary gameplay views.
Protocol 21 introduced expanded combat and character lifecycle, protocol 20
introduced learning views and book study, protocol 19 added observed object types,
and protocol 18 introduced owned player presentation, typed inspection and
authoritative input context.
See [network presentation](network_presentation.md) for the current contracts.

### Expanded combat wire contract

Commands append `EquipmentRequest` (u8 operation, u64 persistent item, u8 hand,
u8 grip), u32 combat style ID, and `AbilityRequest` (u32 ability ID, u8 grant
source kind, u32 source art ID, u8 target kind, u64 actor, three i32 tile
coordinates in X/Z/Y order, u8 cardinal direction), followed by u32 art ID.
The command enum includes equipment, style selection, ability use, art study,
pause art study and fresh-character creation. These fields are always present.
The codec and local submissions call the same `ValidCommandShape` admission
function, requiring canonical unused fields for **all** commands, including
historical commands. Innate ability sources have zero art;
art sources require a nonzero art. Only the selected target branch may carry
actor/tile/direction data. Hotkeys are client-local and are never serialized.

Receipts append u8 `CombatRefusal` and accept outcomes through
`character_created`. Player status appends u64 character ID and u8 life state.
Inventory entries append boolean worn and u8 held-hand mask after the historical
wielded/stackable flags. Worn/held entries must be direct inventory roots and
cannot form interchangeable UI stacks.

Both ordinary and inspection combat views carry catalog style ID, life state,
inexhaustibility, stagger/focus durations, and expanded attack results (move ID,
refusal, strike counts, actual HP loss). World views append character ID,
`CombatInfoView` and combat events after learning. Combat info includes a full
combat view, two held-item records, worn IDs, per-contact encumbrance, moves,
source-qualified abilities and availability/cost/cooldowns, art XP and manual
study progress. Events include all strike/defense/contact IDs and outcomes,
block/armor absorption, incoming/post-protection damage, actual/max HP loss,
disruption, death, action costs and learning metadata. No runtime handles cross
the boundary.

Each event appends a u64 session-local presentation `serial`, assigned by the
server after outcome ordering. Published view histories require strictly
increasing nonzero serials (gaps are legal); equal ticks are legal. Raw combat
runner events may retain serial zero before publication. Serials are not save
identities or simulation inputs. Each encoded history record is 133 bytes.

Combat variable lists use u32 counts checked before iteration against the packet
budget and their minimum encoded record size; worn items retain the 2048-entry
inventory limit. There is no small fixed catalog limit on moves, grants, arts or
manuals. Enum ranges, booleans, grant shapes, duplicate keys, unit ratios,
remaining study work and resolved strike counts are checked. Effective costs
are u32 values, including scaling, rather than being capped to authored costs.
Catalog validation checks nested style/move/ability/art/book IDs, ability source
grant membership, target kinds and manual book/art mappings before publication.
Default/empty expanded views remain valid for existing fixture callers.

`ContentFingerprint` hashes the canonical `WriteCombatContent(content)` object
in addition to historical content. This includes equipment armor/grips, weighted
moves/contacts/styles, abilities, art unlocks, manual study and combat bindings.
Object members are hashed by sorted name and list ordering remains meaningful.

The connection, input context and receipt player ID identify a stable **session**.
`WorldView::characterId` and `PlayerStatus::character` identify its current actor;
they may differ from the session ID after death and character replacement.
`new_character` travels through the ordinary sequenced input path to
`Server::Submit`, which owns its special lifecycle handling. Transport never
creates a character or substitutes an actor ID for a session ID.

## Inventory protocol 17 history

Protocol 17 introduced commands with two canonical
`ItemStorage` values (u8 kind, three i32 coordinates in X/Z/Y order, u64 ID).
`wield_item` uses an explicit item ID or zero to unwield; `transfer_item` requires
a nonzero item and exact source/destination. These two commands reject unused
fields. Historically, legacy commands preserved unused-field behavior but rejected
nondefault source/destination fields. The
connection remains the sole authority for the acting actor identity.

Receipts append the bounded `InventoryOutcome` enum. Views append wielded item
ID, nearby flattened entries, and bounded storage metadata. Every item entry
adds immediate storage identity plus boolean wielded/stackable flags. Storage
metadata contains accessibility/reason, recursive mass/direct volume, optional
actual mass/volume limits, a bounded name and object X/Z/Y coordinates (zero for
non-object sources). Object coordinates are independent of viewport radius and
must stay in the player's same-layer neighborhood. Each entry list is capped at 2048,
storage metadata at 4115, and the complete packet at 1 MiB. Decoders validate
canonical storage shapes, parent links, unique equipment identity and booleans.
Content fingerprints include description, parsed art runs and effective melee
profiles in canonical order. No runtime entity handles cross this boundary.
Art colors hash a canonical `useDefault` boolean and, only for non-default
colors, the exact red/green/blue channels. Run text, cell counts, line boundaries
and art width are included. Palette aliases and asset paths are not identities:
equivalent resolved artwork has the same fingerprint.

The shared flag vocabulary is hashed in canonical catalog order, including each
string ID, name, description, kind and membership in all four domains (item,
monster, terrain and object). Item flag attachments hash their canonical sorted
stable string IDs. Definition, domain and attachment authoring order therefore
does not matter; changed meaning or attachments reject admission, including
changes to unused vocabulary. These fingerprint changes use the existing
matching-content check and do not change the current protocol's wire layout.

`network/network.h` exposes `NetworkRuntime`, `LanHost`, `LanClient` and
`LanDiscovery`. The content catalog and server must outlive their network peers;
keep the runtime alive until all peers are destroyed. All methods are called on
one application thread. No engine interface contains ENet types. `Create`, `Open`,
`Connect`, `Refresh`, and client `Submit` return typed `NetworkResult` errors.
`NetworkErrorMessage(error)` provides frontend-facing diagnostics. Enum members
are PascalCase (`NetworkError::Disconnected`, `ConnectionState::Active`, etc.).

```cpp
auto runtime = network::NetworkRuntime::Create();
network::LanHost host(**runtime, server, content);
auto opened = host.Open({24680, 24681, "My game"});
auto sequence = host.QueueLocal({CommandKind::move, 1, 0});
auto update = host.Pump(elapsedMicroseconds);
// Consume update.commandResults, including the local queued and final receipts.
```

`LanHost` works without `Open` for local-only sessions. The host must use
`QueueLocal`, not `Server::Send`, while hosting; host and guests share the same
session inbox. `QueueLocal` returns zero only if its bounded local input buffer
is full or the sequence space is exhausted. `Pump` first advances the *previous* elapsed interval, then polls
transport, submits buffered inputs, and calls `Update(0)`. A newly received input
therefore cannot beat a deadline already expired in that interval. Input envelopes
carry monotonically increasing sequences and an opportunity number; the server
validates both. The actor identity is bound to the ENet connection, never read
from input packets. Receipts include immediate queued/rejected feedback and later
execution results; each `Pump` returns all receipts exactly once to the host.
`HostUpdate::networkError` reports delivery/snapshot failures; affected guests
are disconnected rather than losing command receipts silently.

The client connects with name and optional saved reconnect token. It receives
a server-issued token via `GetReconnectToken()` after admission; store it in the
local profile for a future reconnect. Unknown tokens report
`NetworkError::UnknownToken` rather than creating a different identity.
New guests remain `ConnectionState::Joining` until they receive an initial
snapshot, acknowledge it and send Ready, and receive the **post-activation**
snapshot. Joining snapshots are never exposed through `View()`. `HasView()` gates `View()`;
`TakeResults()` drains command receipts. `GetState()` and `GetError()` report
admission, transport and protocol outcomes. Disconnect parks the player in the
session, preserving their identity and inventory. `Close()` parks all guests.
Malformed command shapes return `NetworkError::InvalidCommand` locally without
sending or replacing an existing intent; the server still validates every input.
Client input opportunity follows the latest **authoritative receipt** or snapshot
revision, so the next command can follow a completed action before the next
50 ms world snapshot. `View()` itself is never predicted or modified by receipts.
Client sends permit at most 64 unacknowledged commands; excess submissions
return `SlowPeer`. Client receipt buffers are bounded and drained by `TakeResults()`.

Game transport uses ENet IPv4 with two reliable channels: commands/receipts and
full per-player snapshots. Snapshots include revision, opportunity, last input
sequence, visible terrain, complete item trees and sorted player status. A content hash
includes ordered terrain IDs/definitions, building patterns, and item definitions. It is checked
before joining; incompatible catalogs are rejected. Integers are fixed-width,
big-endian; strings, item/player lists, and packet sizes are bounded, enums and
booleans are validated, and hidden map cells send no underlying world data.
Only one full snapshot can await application acknowledgement per connection;
newer revisions supersede the unsent state and are encoded after the ack. Sends
are throttled to at most roughly 20 Hz. Five seconds without a snapshot ack
disconnects a stalled guest; unencodable snapshots return `SnapshotTooLarge`
instead of silently freezing. Each item list supports up to 2048 flattened entries,
subject to the 1 MiB packet bound. Control packets and inputs carry a fresh
per-connection nonce; the host checks ENet connection generation and nonce before
assigning an actor. Joining has a five-second
explicit-elapsed timeout. The server does not use wall time or RNG for gameplay;
network-only tokens and discovery identifiers use OS entropy.

## World, item and combat protocol history (version 17, `0x43584e11`)

Version 17 is wire-incompatible with earlier versions and supports maps up to
129×129 cells with lossless run-length encoding. Coordinates encode signed
32-bit X/Z/Y. Commands append signed `dy` after their item arguments; movement,
and directional item use accept six axial directions. Movement also accepts the
four horizontal diagonals. Legacy interaction accepts the same tile and eight
horizontal neighbors only; cross-layer interaction is rejected. Snapshot
cells carry terrain and overlay depth as bytes, bounded to `0..20` and to the
distance from the player layer to the bottom of the world. View origin Y must equal
player Y; unknown channels are entirely default. Inputs include a definition-local
`ItemCapabilityId` (u16), local action ID (bounded string), and secondary persistent
item ID (u64) after the existing command fields. Action IDs and reason codes use
`[a-z][a-z0-9_]*`, with at most 64 bytes. All strings have a u8 byte-length prefix.
Every command appends a 26-byte interaction payload after target/style: signed
X/Z/Y (three i32), object persistent ID (u64), expected terrain ID (u32),
interaction kind (u8: Open, Close, Object, Lock, Unlock), and object action (u8). `interact_action` validates its
absolute target and exact action against the current authoritative query.
Object targets require a nonzero object ID and zero terrain ID; terrain targets
require the inverse. Non-interaction commands use default interaction fields.

Version 16 adds `grab` (one of eight horizontal unit directions) and
`release_grab` (zero direction), without changing the command payload layout.
Views append a u64 `grabbedObject` after the crafting job list.
Receipts append an `objectName` string (u8 length, at most 255 bytes) after the
crafting outcome for named grab/release/obstacle messages. New grip outcomes are
zero-cost, non-advancing responses. See [grabbing](grabbing.md).

`use_item` requires an explicit source item, valid capability and nonempty action
ID. A secondary item must differ from the source and cannot accompany a direction;
a direction must be a six-axis unit step. Shape validation deliberately accepts unknown local
action IDs and does not infer arguments from their spelling. `ItemSystem::Use`
validates the selected behavior's declaration and returns `InvalidAction` for
unknown actions or incorrect declared argument forms. Non-use commands may have an
empty action. `store_item` carries the source item and destination container
in `otherItem`; zero destination extracts to the actor's inventory. Client submit
and wire decoding use the server's shared `ValidCommandShape` so malformed input
cannot acquire different meanings at different boundaries. Actor identity,
ownership, current capability state and opportunity remain server-authoritative.
Receipts append `CommandResult::itemOutcome` (u8: `Succeeded`, `InvalidAction`,
`Inaccessible`, `Refused`) and `itemReason` (code, localization key, fallback).
An entirely empty reason is valid for every outcome. A nonempty reason is accepted
only for `Refused`, must have a valid reason code, and may carry arbitrary bounded
localization text. Built-in refusals supply diagnostics; networking does not know
their codes. Text keys and fallbacks are independently bounded to 192 bytes.
UTF-8 text and spaces are preserved; C0 bytes and DEL are rejected, as for other
wire strings. `Writer::CommandValue` and `Writer::Result` return `bool`; callers
must check encoding failures before sending. Invalid receipt encoding reports
`Protocol`, and invalid client commands report `InvalidCommand`.

### Lighting and memory cell record

The view header remains 87 bytes through its u16 **expanded** cell count. The
row-major map is encoded as `(u16 run length, 44-byte cell payload)` records until
that count is reached. Runs repeat identical cell values and may cross row
boundaries. Zero runs and runs exceeding the remaining count are invalid. Every
expanded position is validated, including coordinate bounds; repeated values do
not exempt later cells from checks. The fixed payload makes independent channels
explicit. Offsets below are relative to the payload, after the run length:

| Offset | Field | Encoding |
| --- | --- | --- |
| 0 | terrain visibility | u8: Unknown=0, Remembered=1, Visible=2 |
| 1, 5 | terrain ID, damage | u32, u8 |
| 6, 14 | actor ID, player | u64, boolean u8 |
| 15, 19 | ground item count, object ID | u32, u64 |
| 27 | object kind | u8: None=0, Generic=1, Chair=2, Table=3, Lamp=4 |
| 28, 29, 30 | facing, active, broken | u8 North=0/East=1/South=2/West=3; two boolean u8s |
| 31, 32 | terrain depth, overlay depth | two u8s |
| 33 | overlay visibility | same visibility enum |
| 34, 35 | terrain light, overlay light | u8: Dark=0, Dim=1, Ordinary=2, Bright=3 |
| 36 | monster type | u32 catalog-local MonsterTypeId; zero means untyped |
| 40 | observed object type | u32 catalog-local ObjectTypeId; zero means untyped |

Protocol 19 appends the observed object type to the historical cell layout above.
`ValidObjectDefinitions` checks it against the admitted catalog for ordinary and
inspection views. Remembered cells retain their last observed object type without
a live entity ID; unknown overlays and empty appearances cannot carry a type.
See [furniture sprites](furniture_sprites.md).

`playerLight` is appended after the player-status list as the same light-band u8.
It is followed by the player's `CombatView`: body kind, style and action (u8),
stamina (u32), accuracy penalty (u8), then nine body parts (u32 HP, u32 effective
maximum, boolean broken; HP in hundredths). Last feedback follows: outcome u8,
move u32, target u64, part u8, injury u32, cost u64, committed/contact/lost
capability booleans, disruption u8, incoming boolean and tick u64. Enum, boolean,
HP, stamina and penalty bounds are validated; absent body parts must be zero.
Feedback is presentation-only and is not persisted. Commands also carry a target
persistent ID and combat style; the server validates and resolves them
authoritatively. See [combat API](combat_api.md).
Locked/unlocked outcomes are part of protocol v12. Terrain locking references are
included in content fingerprints. Protocol 18 adds remote selected-layer Examine;
its separate inspection view does not change the normal snapshot origin contract.

Object kinds are fixed protocol constants, independent of content ordering.
None descriptors must have North facing and false state flags. A live object ID
requires a nonempty descriptor and vice versa. Remembered objects require a
descriptor but object ID zero; actors, player flags and items are forbidden in
remembered overlays. An absent overlay has no payload, light or depth. Remembered
terrain has damage zero, and both remembered channels carry Dark as an unavailable
light sentinel. Unknown channels require every associated field to be default.
Non-Unknown channels must refer to in-world columns even when the projection bounds
extend beyond the world. Terrain IDs are checked against the admitted catalog at
the client boundary for both Visible and Remembered terrain, before publication.
All enum values, state booleans and depth bounds are checked on decode; the encoder
also rejects noncanonical states. A uniform 129×129 map uses one 46-byte run;
the worst case of distinct adjacent cells uses 765,486 bytes before header,
items and player statuses. The 1 MiB packet limit still applies. Compressing
repeated unknown/empty tiles avoids filling ENet's reliable window with redundant
snapshot data during admission and delaying the first command opportunity.
`wire::ValidTerrainDefinitions(view, content)` performs the catalog-dependent
terrain check after structural decoding, alongside `ValidItemDefinitions` and
`ValidMonsterDefinitions`. The latter resolves every nonzero monster type against
the admitted catalog before a LAN snapshot is acknowledged or published. Structural
validation permits a nonzero type only on a Visible overlay with a nonzero actor
ID and a false player flag. Hidden, remembered, empty and player cells carry zero;
visible untyped creatures remain valid. Projection copies the selected visible
actor's type at its overlay depth, including downward views, without consulting
hidden actors or adding identity to terrain/object memory. RLE compares the type
as part of the complete cell, so adjacent different types never share a run.
Numeric types are session-local; the matching-content handshake and canonical
catalog ordering keep them meaningful. Monster names and recipes remain covered
by the content fingerprint; save identities use stable definition strings.

The content fingerprint includes terrain light intensity and range, in addition
to appearance, movement, support and stair connectivity, so differing emitters
cannot join the same session.

Every snapshot replaces **both** `inventory` and `groundItems`. Inventory contains
all carried descendants; ground contains all item trees on the player's current
tile. Each list is ordered by strictly increasing persistent ID. An entry carries
its item type, contained count, parent ID, installation capability, mass including
descendants, physical volume, container capacity/used volume, energy stores,
activities and contextual actions. Snapshot totals include carried mass and carry
capacity (u64 grams). These are typed presentation fields rather than arbitrary
save-state blobs; hidden state and off-tile ground inventories are not serialized.

The item lists are u16-counted. Each entry uses, in order:

| Field | Encoding |
| --- | --- |
| ID, type, contained count | u64, u32, u32 |
| Parent, installation capability | u64, u16 |
| Mass, volume, capacity, used volume | u64, u32, u32, u64 |
| Energy | u8 count; repeated u16 capability, u32 energy, u32 capacity |
| Activities | u8 count; repeated u16 capability, u8 active, u64 remaining ticks, status key, status fallback |
| Actions | u8 count; repeated u16 capability, action string, u8 argument, u8 availability, u32 base cost, u8 category, label key, label fallback, reason code, reason key, reason fallback |

Energy and activity lists permit at most 16 capabilities and use increasing
capability IDs. Actions permit at most 64 entries ordered by capability then lexical
action string, with no duplicate tuples. Argument kinds are `None`, `Item`, and
`Direction`; categories are `Operate` and `ManageContents`. Availability uses the
same four generic outcomes and reason rules as receipts. All text fields use the
192-byte limit except action IDs and reason codes (64 bytes).
All booleans, enums, nonzero IDs and bounded
counts are checked. Parents must exist in the same list, IDs cannot appear in both
lists, and parent chains must terminate within 64 links. The encoder checks the
packet budget while writing entries; a large but count-valid snapshot can still
produce `SnapshotTooLarge`, including the packet header/nonce in that budget.
No partial list or truncated snapshot is sent.

`wire::ValidItemDefinitions(view, content)` is the internal second-stage validator
used by the client after structural decoding and before acknowledgement/publication.
It resolves every type/capability against the admitted catalog. Shared energy
stores provide capacities; each behavior has one presentation entry, and its
schema declares whether it can be active, countdown semantics and maximum delay.
Inactive or non-countdown entries cannot carry remaining ticks, and behaviors
without activity support must be inactive. Status localization is generic and need not match a fixed
catalog string. Installation validation calls the parent behavior definition's
`AcceptsInstallation(childDefinition)` and rejects duplicate occupants of a mount.
Complete trees are checked for direct child counts, subtree masses and ordinary
contents volumes. Action argument, category, base cost and label must exactly match
the selected behavior's declared local action. Keeping catalog checks out
of the structural reader makes byte-boundary checks independently testable while
preventing invalid catalog indices from reaching UI code. Contextual availability
is advisory: clients submit intent, and the server checks it again at execution.

The content fingerprint covers item IDs/names, mass/volume, ordered capability
names, capability type strings and normalized effective `DataObject` configs
(including defaults). Structural hashing includes explicit `DataType` tags,
lengths/counts and values; list order matters, while object keys are traversed in
lexical order at every nesting level. It does not hash JSON spelling, insertion
order, or pointer identity. Each behavior's complete schema is also hashed:
sorted action IDs, arguments, categories, costs, label keys/fallbacks, activity
flags, maximum delay and implementation version. Different schemas therefore
reject admission even when normalized config data matches. New behavior types
require no codec branches or networking registration: their definitions supply
canonical config, schema and installation policy through the existing interfaces.

Discovery uses a separate ENet UDP socket on port 24681. `LanDiscovery::Open()`
binds the request socket, `Refresh(port)` broadcasts on the local IPv4 subnet,
and `Refresh(port, "127.0.0.1")` makes a directed request (also useful for tests).
`Pump(elapsed)` periodically retries, deduplicates by instance ID, expires entries
after three seconds, and publishes `Sessions()` including `compatible`.
Discovery is unauthenticated LAN metadata; game state travels only via the
authoritative ENet session. Binding port zero allocates an ephemeral port;
`GetPort` and `GetDiscoveryPort` return actual bound host ports.
