# LAN networking API

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

## World, item and combat protocol (version 12, `0x43584e0c`)

Version 12 is wire-incompatible with earlier versions and supports maps up to
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
row-major map is encoded as `(u16 run length, 36-byte cell payload)` records until
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
included in content fingerprints. Selected-layer Examine views remain local and
do not change the network snapshot origin contract.

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
also rejects noncanonical states. A uniform 129×129 map uses one 38-byte run;
the worst case of distinct adjacent cells uses 632,358 bytes before header,
items and player statuses. The 1 MiB packet limit still applies. Compressing
repeated unknown/empty tiles avoids filling ENet's reliable window with redundant
snapshot data during admission and delaying the first command opportunity.
`wire::ValidTerrainDefinitions(view, content)` performs the catalog-dependent
terrain check after structural decoding, alongside `ValidItemDefinitions`.

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
