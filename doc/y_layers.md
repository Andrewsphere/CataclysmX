# Y layers and vertical space

The world is a bounded 3D tile grid: X and Z are horizontal, and Y is elevation.
Valid Y values are `-10..10` (21 layers), with `0` the legacy surface. `TileCoord`
and `ChunkCoord` fields are ordered `{x, z, y}`. Keeping Y last preserves existing
surface aggregate initializers such as `{x, z}`; APIs that accept coordinates use
that same order. Horizontal chunk geometry remains 16×16: each chunk is one
16×16 X/Z layer, not a volume. `ChunkRegion` requests select horizontal chunk
columns through all 21 layers; `center.y` is ignored. Active and resident state,
unloading, and simulation freezing apply to the whole column height.

This layout keeps existing surface maps, coordinate literals, and surface
generation stable while making vertical tiles independently addressable. Y is
bounded and stored explicitly rather than inferred from terrain, so travel,
visibility, persistence, and loading all share one coordinate contract.

## Terrain roles and support

Terrain JSON may specify optional boolean `open_space` and `stairs` flags. The
valid combinations are:

| Role | Required properties | Meaning |
| --- | --- | --- |
| Ordinary/supporting terrain | Neither flag | Supports a floor; this includes walls and ordinary floor terrain. |
| Open space | `open_space: true` | Must be passable, not block sight, and not be stairs. It provides no floor support and transmits downward. |
| Stairs | `stairs: true` | Must be passable, not block sight, and not be open space. |

Both flags may be omitted or false on ordinary terrain, but cannot both be true.
The content loader rejects an open-space terrain that is impassable, sight
blocking, or stairs, and rejects stairs terrain that is impassable, sight
blocking, or open space. World-generation roles additionally require its `air`
terrain to be open space. This explicit role model lets the same terrain catalog
drive support, gravity, vertical travel, and vertical sight without special-casing
terrain IDs.

## Travel and gravity

Horizontal walking is cardinal and can trigger a fall immediately after entering
open air. Vertical movement is only by stairs: `<` descends one layer
(`dy = -1`) and `>` ascends one (`dy = +1`), at the same X/Z. Both source and
destination tiles must have the stairs role. A missing, unavailable, or unmatched
endpoint blocks the move. The ordinary horizontal command directions remain
north/south/east/west.

`World::SettleGravity()` performs deterministic bottom-up settlement without
damage, RNG, or elapsed simulation time. Unsupported actors and ground-item
trees fall through open space until they enter a passable floor tile or reach
Y=-10. An impassable tile or occupying actor stops them in the cell above it;
settlement retries when that obstruction moves or disappears. Parked actors are
frozen, do not fall, and do not obstruct falls. Spatial edits settle affected
resident columns as part of the mutation boundary. Gravity is therefore a world
state consequence, not an actor action or a source of extra action cost.

## Sight and presentation

Line of sight is a 3D spherical range, using deterministic integer voxel traversal.
Closed floor bottom faces block vertical sight. Open-space columns transmit sight;
matched adjacent stairs permit vertical sight through their shared connection.
The projected map remains an X/Z grid. For each horizontal cell, terrain is
projected downward from the observer's Y through open space, and each candidate
tile must independently pass straight 3D line of sight. The first non-open-space
tile ends the projection. Terrain depth and overlay depth are independent: the
view reports the selected terrain layer's depth and, separately, the nearest
visible actor/item/object layer's depth.

The text client blends deeper cells toward blue `(48, 96, 192)`. Foreground tint
strength is `min(70%, 15% + 10% × depth)` for positive depth; depth zero is
untinted. Background uses half that strength. Overlays use their own layer's
depth for foreground, while the terrain layer determines background tint.

## Generation and compatibility

World generation version 3 preserves the existing surface at `y=0`. Below it,
generated layers are solid rock/wall; above it, they are open air, with building
roofs at `y=1` over building footprints. The central legacy surface demo area is
still preserved. New demo stairs around `x=-3` connect a basement, surface, and
raised platform; nearby open air forms a pit.

Save schema version 6 stores Y in tile/entity/session coordinates and explicitly
rejects save versions 1–5; migrations are not implemented. The LAN protocol is
version 6 (`0x43584e06`), incompatible with earlier versions. It carries Y in
coordinates, vertical command `dy`, and view depth metadata. Content format
version remains 1: the terrain flags are optional additions to that schema.

Door-controller state version 2 stores signed `{x,z,y}` targets and rejects the
old two-dimensional version 1. Vertical targeting also checks the connecting
face when arming and at the deadline. See [save format](save_format.md), [LAN networking](networking.md),
[world API](world_api.md), [server API](server_api.md), and
[content format](content_format.md) for the corresponding subsystem contracts.
