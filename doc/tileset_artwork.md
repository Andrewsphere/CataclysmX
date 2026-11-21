# Tileset artwork

The SDL default is `assets/tiles/gen1_32x32/tileset.json5`, selectable with
`--tileset assets/tiles/gen1_32x32`. It assembles individual PNGs into in-memory
atlas pages on every launch. The individual files are the editable production
artwork; no preassembled tileset atlases are shipped.

## World tiles

All 120 PNGs from `world_tiles_final` are bundled at their original dimensions
and alpha. The batch supplies 59 existing terrain IDs and 18 existing object
IDs; same-ID graphics replace previous artwork. Pavement now has its own image
instead of reusing the floor, and lockable doors still share the standard open
and closed door sprites. Roads use full-cell art without connection shapes.
Patched-asphalt terrain mostly uses the worn image, with the patched image
receiving 4 of 23 selection weights (about 17%). Both images have four rotations
about the cell center. The odd total avoids the checkerboard caused by hashing
coordinates into two equal buckets. Plain roads and worn-asphalt terrain keep
their own fixed graphics.
Existing `tree` terrain uses eight
coordinate-selected 32×64 upright variants over the unchanged meadow underlay.

Grass terrain selects by map coordinate from the existing meadow (including its
horizontal mirror) and ten 32×32 tiles imported unchanged from
`grass_variations_final`: clover, dry tips, matted, short, and tufted, each in
plain and detailed versions. Plain versions are weighted three times as often
as their detailed counterparts. No terrain IDs or world generation rules change.

The rest are prewired in `tileset.json5` without adding gameplay content. Add
terrain definitions with the matching filename stems for door/window states,
paths, the grass hole, the puddle, shorelines, water, sand, or named trees. Add
object definitions with matching stems for street lamps and `trash_can`.
For each `wall_ad_*` or `wall_poster_*` ID, choose either an object overlay
that keeps the underlying tile visible or terrain with a plain wall underlay.
The latter lets building patterns use decorated walls without placing furniture
on blocked terrain. Puddles use a dirt underlay and shore shapes use shallow
water. Shapes are explicit terrain IDs, not automatic connections.

`sprite.effect.flash` and `sprite.effect.splatter` are available to future
effects code but do not play on their own. Every terrain and placed-object ID
in the full content pack now has a Gen1 artwork mapping.

## Actors, items, and other overlays

The pack also maps six generic overlays and 179 item types: 176
[static item sprites](item_sprites.md) and three animated bulbs. Transparent
actors, ground items and objects leave the terrain visible below them. PNG
colors are independent of content glyph and UI colors.

Separate `sprite/item/bulb/off.png` and `on.png` images provide lightbulb artwork.
Local `loop.meta` and `once.meta` clips provide the continuous/toggled 500/500 ms
loop and the single 120/120 ms blink. These are presentation animations; content
and gameplay timing are independent. See [sprite playback](sprite_animation.md).

See [individual sprite tilesets](source_tilesets.md) for package IDs, local
metadata, anchors, animation sheets, variants and optional connected terrain.

The original [furniture sprite batch](furniture_sprites.md) still maps 33
additional object types to normalized transparent 32×32 artwork. Both object
batches use the same remembered-appearance handling.
