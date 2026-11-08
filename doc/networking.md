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
sequence, visible terrain, inventory and sorted player status. A content hash
includes ordered terrain IDs/definitions and building patterns. It is checked
before joining; incompatible catalogs are rejected. Integers are fixed-width,
big-endian; strings, item/player lists, and packet sizes are bounded, enums and
booleans are validated, and hidden map cells send no underlying world data.
Only one full snapshot can await application acknowledgement per connection;
newer revisions supersede the unsent state and are encoded after the ack. Sends
are throttled to at most roughly 20 Hz. Five seconds without a snapshot ack
disconnects a stalled guest; unencodable snapshots return `SnapshotTooLarge`
instead of silently freezing. Inventory supports up to 2048 direct entries,
subject to the 32 KiB packet bound. Control packets and inputs carry a fresh
per-connection nonce; the host checks ENet connection generation and nonce before
assigning an actor. Joining has a five-second
explicit-elapsed timeout. The server does not use wall time or RNG for gameplay;
network-only tokens and discovery identifiers use OS entropy.

Discovery uses a separate ENet UDP socket on port 24681. `LanDiscovery::Open()`
binds the request socket, `Refresh(port)` broadcasts on the local IPv4 subnet,
and `Refresh(port, "127.0.0.1")` makes a directed request (also useful for tests).
`Pump(elapsed)` periodically retries, deduplicates by instance ID, expires entries
after three seconds, and publishes `Sessions()` including `compatible`.
Discovery is unauthenticated LAN metadata; game state travels only via the
authoritative ENet session. Binding port zero allocates an ephemeral port;
`GetPort` and `GetDiscoveryPort` return actual bound host ports.
