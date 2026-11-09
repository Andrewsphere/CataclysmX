# Terminal view and LAN snapshots

## Display radius

`Settings::viewRadius` defaults to **64**, with an inclusive validated range of
0–64. Sparse global/world overrides retain their normal precedence, including
explicit zero and older radius-8 preferences. The settings format stays at 1.

Local and hosting terminals request `Server::View(effective.viewRadius)`.
`LanHost` explicitly requests `ViewForPlayer(player, 64)` for every guest,
independently of the hosting terminal's preferences. `HostSettings` and
`ConnectionSettings` therefore need no additional wire-negotiated radius field.
`GameplayPresentation::viewRadius` carries the resolved display preference to
the renderer (default 64 for other callers). A guest can change this preference
immediately without reconnecting. It limits the displayed square around the
player, not the received snapshot, authoritative perception, remembered terrain,
or lighting calculations. Small terminals never reduce authoritative sight.

## Screen-sized rasterization

`GameplayUi` shares a compact cache of only the last clipped source rectangle
with its map nodes. Each entry holds a source `ViewCell`, normalized glyph, and
converted FTXUI foreground/background colors. It never prepares appearances for
the full snapshot. There is no full-map equality pass or UI-side full-map cache
copy on changed frames, and no per-cell DOM allocation or layout traversal.

`MapViewport` is one custom FTXUI node with one text child for the cropping
indicator. `SetBox` performs constant-size arithmetic using the panel's actual
allocation: apply the display radius, center around the player, clamp the source
rectangle at map edges, and center maps smaller than the panel. When cropped,
one row is reserved for the localized indicator if the panel has room.
`Render` intersects that rectangle with the parent stencil and physical screen
before comparing source cells or resolving any appearance. It compares only
visible cells, resolves changed/newly exposed cells, and writes the cached glyphs
and colors. With an unchanged rectangle it updates entries in place. A resize,
anchor change, or parent clip change reuses the overlapping source rectangle and
resolves only newly exposed or changed entries. Content/dimension changes
invalidate the visible cache. Work and retained appearance-cache size are bounded
by the visible panel, even when every cell in a 129×129 snapshot changes.

`GameplayUi::Render` takes `WorldView` by value and transfers its cell vector into
the returned map node. This keeps deferred layout/rasterization safe when called
with a temporary `CurrentView()`. Temporary/moved snapshots transfer ownership
without another full-map copy; callers retaining an lvalue snapshot incur its
value-copy cost. The node owns its immutable cell values and shares the bounded
visible cache; no dangling reference to a caller's snapshot is retained. This
ownership choice avoids requiring application-side lifetime management or a new
snapshot abstraction. Only the source values needed for visible comparisons are
copied into the appearance cache.

Glyphs use the same single-column FTXUI width check and `?` fallback as
`RenderTerrainCell`; foreground/background colors come from the shared
`CellAppearance` function, preserving lighting, terrain memory, depth styling,
and player/actor overlays. The renderer does not invent visible cells.
The overall UI retains its existing transient-element contract (draw before the
next `GameplayUi::Render` call or UI destruction), because modal scroll state is
still UI-owned. Map snapshot/cache ownership no longer depends on that contract.
`MapPreparations()` counts first preparation, content/dimension invalidations,
and draws finding changed previously cached visible cells. Status-only redraws
and newly exposed resize cells do not increment it; changes entirely offscreen
are intentionally not detected. `MapRasterizedCells()` reports the last map draw's
pixel count; `MapResolvedCells()` reports its appearance-resolution count, bounded
by that pixel count and zero for unchanged status redraws at the same layout.

## Protocol compatibility and bounds

Protocol **9** (`0x43584e09`) deliberately replaces earlier versions. Dimensions
are u8 and the expanded cell count is u16, both sufficient for a 129×129 map
(16,641 cells). Cells are now encoded as bounded runs of identical values:
`u16 length` followed by the existing 36-byte cell payload. There is one encoding
for every snapshot. Earlier peers fail admission cleanly; discovery uses the same
version. The save format remains 7.

The wire limit is now **1 MiB**, including message headers, with ENet receive
packet/waiting-data limits derived from it on host and client. An entirely unknown
joining map occupies one 38-byte run instead of 599,076 raw cell bytes. This avoids
fragmented redundant maps consuming the peer's reliable window and delaying
activation or first input. Distinct adjacent cells require at most 632,358 bytes,
leaving room for normal inventory/session metadata. Large inventories can still exceed the total byte
limit and return `SnapshotTooLarge`; item-entry/action bounds remain in force.
Existing reliable snapshot acknowledgements keep at most one snapshot in flight
per guest and coalesce newer revisions while it is outstanding.

Synchronous host shutdown cancels obsolete queued snapshot traffic with
`enet_peer_disconnect`, then services its acknowledged transport control command
within the existing bounded shutdown interval. It does not wait for snapshot
fragments to drain: same-thread clients cannot acknowledge those fragments while
the host is closing, and a maximum-size snapshot exceeds ENet's reliable window.

Both encoder and decoder reject zero/even/non-square dimensions, dimensions
above 129, inconsistent cell counts, off-center player/origin pairs, invalid
elevation, and map bounds whose positive edge exceeds signed coordinate storage.
Zero/oversized runs are rejected, and each expanded cell is independently checked.
Dimension validation precedes size multiplication and cell coordinate arithmetic,
so malformed sizes cannot overflow those operations. Unknown cells do not bypass
the rectangle's overflow checks. Existing per-cell semantic/content validation
still runs before snapshot publication.
