# Owned player presentation (protocol 18)

`core/presentation.h` contains frontend-neutral owned values. They contain persistent
identities and catalog IDs, never handles, transport objects, or SDL objects.

`CapturePlayerPresentation(world, view, output)` captures a cell-aligned
`Vector<MapItemVisual>` and authoritative nearby `InteractionChoice`s. Empty map
entries are `{0, {0}}`. Only visible, actor-unoccluded ground stacks get a
representative (the lowest persistent item ID). Interactions retain the existing
SDL order: center, west, east, north, south, then diagonals, preserving provider
action order. Capture returns false on overflow; partial results must not be used.
The caller-owned `CaptureMapItems(world, view, output)` overload and presentation
capture retain vector capacity across updates; the value-returning overload remains
available for callers that need an independently owned result.

`LanClient::Presentation()` accompanies `View()` and is published atomically with
it. Protocol 18 snapshots append this sidecar to the existing basic WorldView codec.
The normal 50 ms snapshot cadence is unchanged. Sparse indexed map entries on the
wire avoid charging empty cells 12 bytes each; owning frontend values remain dense
for simple indexed rendering. ViewCell and persistent memory layouts are unchanged.

## Examine

`CapturePlayerInspection(server, player, requestId, cursor, radius, output)` uses
`ViewForPlayerAtLayer` and read-only world access. It never requests chunks or teaches
memory. The view remains centered horizontally on the player; `cursor.y` selects
the layer. A cursor no longer in the requested viewport returns `Unavailable`.

`TileInspection` contains only the visible actor's status, matching visible object's
state, and preorder item observations. Object contents require `CanUseObject`.
Another actor's combat feedback/target history is removed because the formatter
only needs status. `DescribeTile(content, view, cursor, details, output)` formats
the observation; the existing World overload captures and calls this formatter.
Oversized local descriptions report an explicit presentation-limit line.

`LanClient::Inspect(requestId, cursor, radius)` accepts strictly increasing nonzero
IDs within a connection, a valid coordinate inside the latest snapshot's horizontal
viewport extent for that radius, and radius 0..64. It returns `Busy` until the one
pending request or completed result is consumed. `TryTakeInspection(output)` moves
the result out. Callers compare its ID with their current UI cursor request before
displaying it; cursor changes do not retarget the old request. Reconnect resets IDs.

Replies carry session nonce, ID, cursor, status, layer view, map visuals, and typed
details. Reliable reply acknowledgement bounds host in-flight inspection output to
one; additional requests before acknowledgement are protocol errors. At most one
query is served per host pump. Every accepted request reaches `Complete`, `TooLarge`,
`Unavailable`, or `Disconnected`; transport failures and the five-second reply
deadline terminate the session and produce a disconnected result (see `GetError()`
for the reason). Session reset discards old results. Replies never update `View()`
or input context.

## Input context and validation

`GetInputContext()` returns the latest authoritative receipt/snapshot context,
including the server's actual last accepted input sequence, not the last attempted
submission. Receipts now transmit all three context fields. `Submit(command,
opportunity)` and `QueueLocal(command, opportunity)` preserve the originating
opportunity for frontend FIFOs; original overloads still target current context.
Opportunity zero is rejected. A receipt does not predict a WorldView.

Limits: 1 MiB packets, 129x129 maps, 256 interaction choices, 255-byte target/label
strings, 2048 inspection item observations, and nesting depth 64. Budget overflow
is rejected, never truncated. Decoding validates counts before allocation, enums,
booleans, coordinates, canonical absent fields, visibility/identity relationships,
item preorder depth, duplicate inspection item IDs, and catalog definitions before
publication. Normal views require origin Y equal to player Y; inspection views
validate selected-layer bounds and depth relative to that layer separately.
Requests, inspection replies and acknowledgements use reliable channel 0; only
snapshots may use channel 1. A pending inspection and snapshot can each occupy up
to one packet budget, with client receive headroom for both plus control traffic.
Rejections/timeouts remain terminal even if older packets are already queued.

## Integration and ownership

The engine builds `src/core/presentation.cpp`; the shared client formats inspection
values, and networking serializes them without a dependency on SDL or client UI.
SDL aliases the core `MapItemVisual` and capture functions. Codec/transport coverage
lives in `tests/presentation_network_tests.cpp`; capture/formatting coverage lives
in `tests/presentation_tests.cpp`.

Transport objects are confined to their owning thread. Overlapping runtime
lifetimes are supported, including runtimes owned by separate SDL session threads;
each runtime outlives its peers. The implementation uses ENet's balanced platform
initialization/cleanup and does not change ENet's global allocator callbacks or clock.
