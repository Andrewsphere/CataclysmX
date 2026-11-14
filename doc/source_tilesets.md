# Individual sprite tilesets

The SDL default is `assets/tiles/gen1_32x32`. Select another pack with
`--tileset <directory>`. Version 2 assembles atlas pages in memory on every launch;
editing a PNG or its metadata takes effect after restarting. No atlas coordinates,
generated atlas files, build step, disk cache, or simulation changes are required.
Only format version 2 is supported. Preassembled tileset atlases are unsupported.

## Files and names

```text
my_pack/
  tileset.json5
  tile/world/grass/meadow.png
  tile/world/grass/meadow.png.meta       # optional PNG settings
  tile/world/grass/varied.meta           # weighted visual definition
  sprite/character/zombie/default.png
  sprite/item/bulb/frames.png
  sprite/item/bulb/frames.png.meta
  sprite/item/bulb/loop.meta
```

Paths become dot-separated graphics IDs: `tile.world.grass.meadow` and
`sprite.character.zombie.default`. Each directory/filename segment accepts
lowercase ASCII letters, digits, underscores, and hyphens. Empty segments, dots
inside segments, uppercase names and traversal are invalid. References are full
package IDs, independent of the referencing file's directory. Files and IDs are
case-sensitive in the format, including on Windows.

Discovery scans lowercase `.png` and `.meta` files under `source_directories`
(default `['tile','sprite']`), recursively. Missing source directories are empty.
Directory roots must be relative, nonoverlapping package paths. Other files are
ignored. PNG sidecars require a neighboring PNG. A PNG and standalone `.meta`
cannot define the same ID; a `.png.meta` only modifies its PNG. Symlinks outside
the pack and directory symlinks are rejected. The root manifest is also contained.

All metadata uses JSON5. Unknown or duplicate fields, invalid dimensions, and
unresolved references fail loading with file diagnostics. Definitions stay local
so artists can edit unrelated assets without sharing one large manifest.

```json5
{
  format_version: 2, id: 'my_pack', name: 'My pack',
  cell_width: 32, cell_height: 32,
  source_directories: ['tile', 'sprite'],
  terrain: {grass: {visual: 'tile.world.grass.varied'}},
  items: {toggle_lightbulb: {
    visual: 'sprite.item.bulb.off',
    activation: {animation: 'sprite.item.bulb.loop', mode: 'toggle_loop'},
  }},
  generic: {npc: {visual: 'sprite.character.zombie.default'}},
}
```

Terrain/item keys are existing content IDs. Generic keys are `player`,
`other_player`, `npc`, `ground_items`, `object_enabled`, and `object_disabled`. Omitted mappings retain glyph fallback. Graphics names do not become
content IDs or affect content fingerprints. Activation references an animation
definition and supports `toggle_loop` and `once`, with existing playback semantics.
Terrain mappings may additionally specify `ground: 'tile.world.grass.meadow'`:
an ordinary static ground-layer PNG drawn beneath that terrain's visual. The
default tree uses this small underlay facility so its transparent trunk and canopy
remain independent of the grass artwork. Item/generic mappings do not accept it.

## Placement and sheets

A PNG needs no sidecar. Its whole image is one sprite, anchored at its bottom
center and attached to the map cell's bottom center. Images need not match cell
dimensions: a 32×64 tree naturally extends one cell upward. Native pixel size
scales with map zoom; sprites are not stretched to fit one cell.

```json5
// tall.png.meta
{description: 'Tall tree', anchor: [16,64], layer: 'upright'}

// meadow.png.meta: mirror grass around the cell center without moving it.
{anchor: [16,16], cell_anchor: [16,16], layer: 'ground'}

// frames.png.meta: a uniform grid, addressed in row-major order.
{frame_width: 32, frame_height: 32}
```

`anchor` is an integer `[x,y]` in each source frame, not the whole sheet;
its coordinates are bounded to ±8192. The default source center may be fractional
for odd widths. Optional `cell_anchor` is a point within the pack's cell, in native
pixels; the default is bottom center. This separate attachment point supports both
centered road rotations and foot-anchored upright artwork with one placement rule.

`layer` is `ground` or `upright`, defaulting to upright under `sprite/` and ground
elsewhere. Both frame dimensions must be specified together, positive, and divide
the image exactly. An ordinary reference to a sheet selects frame zero. Other
frames are selected explicitly by animations; generated frame IDs are internal.

## Animations, variations and optional connections

```json5
// sprite/item/bulb/loop.meta
{kind: 'animation', frames: [
  {sprite: 'sprite.item.bulb.frames', frame: 1, duration_ms: 500},
  {sprite: 'sprite.item.bulb.off', duration_ms: 500},
]}

// tile/world/grass/varied.meta
{kind: 'variants', alternatives: [
  {visual: 'tile.world.grass.meadow', weight: 4},
  {visual: 'tile.world.grass.meadow', flip_x: true},
]}
```

Gen1 uses only horizontal grass mirroring to preserve upright tufts. Its road
mapping selects the full-cell static PNG `tile.world.road.asphalt`; no connected
road artwork or definitions are shipped. Custom packs may still use connection
sets with their own six shape PNGs, for example:

```json5
// Example custom-pack definition; not part of Gen1.
{kind: 'connections', connects_to: ['road'], shapes: {
  isolated: {visual: 'tile.world.road.isolated'},
  end:      {visual: 'tile.world.road.end'},       // north
  straight: {visual: 'tile.world.road.straight'},  // north/south
  corner:   {visual: 'tile.world.road.corner'},    // north/east
  tee:      {visual: 'tile.world.road.tee'},       // north/east/west
  cross:    {visual: 'tile.world.road.cross'},
}}
```

Each definition may include `description`. Clips contain positive millisecond
durations, support unequal durations and independent PNGs mixed with sheet frames,
and use the existing exact-boundary loop/once sampler. Frames within a clip must
use one drawing layer, but may have different dimensions and anchors.

Weights default to 1 and are positive 32-bit integers. Alternatives reference
sprites or clips. Connection shapes additionally accept variant sets. Variants
cannot reference variants/connections, clips reference only PNGs, and connection
sets cannot reference connection sets. This bounds resolution to two selections
without recursive asset graphs. Terrain mappings alone may reference connections.

Alternatives and connection shapes accept `rotation` (0/90/180/270 degrees
clockwise), `flip_x`, and `flip_y`. Flips act in source space before rotation about
the source anchor. Nested transforms apply the selected alternative first and the
connection rotation second; every animation frame retains the selected transform.

Connections compile into 16 masks: N=1, E=2, S=4, W=8. Canonical shapes rotate
clockwise, with the smallest turn count winning symmetric matches. Only visible
neighbors in `connects_to` at the same projected world elevation connect. Hidden
and out-of-view neighbors are disconnected; this can change road ends at the
visibility boundary without querying hidden world state.

Variant selection uses unsigned 64-bit FNV-1a: offset 14695981039346656037,
prime 1099511628211, UTF-8 group ID, zero delimiter, then identity tag (0 for terrain,
1 for entity). Tag 0 appends world X/Z/Y as little-endian unsigned 32-bit values
(negative coordinates converted modulo 2³²); tag 1 appends the persistent entity ID
as little-endian 64-bit. The hash modulo total weight selects cumulative intervals
in authored order. There is no simulation RNG, wall clock, viewport seed, or saved
visual state. Entity choices follow actors, items and objects when they move.

## Implementation boundaries and limits

`content/tileset_source.h` exposes `ParseSourceTileset(manifest, files, content,
diagnostics)` for SDL-free validation/compilation. `LoadTilesetFile`, declared in
`content/tileset.h`, provides bounded filesystem discovery. `SpriteSourceFile` carries a relative filename,
optional sidecar/definition text and validated PNG header dimensions. Both entry points accept
only version 2. Unsupported integer versions return `UnsupportedVersion` before
source discovery; missing or malformed versions return `InvalidData`.
A single-document parser is insufficient because compilation needs source files.

The compiled `TilesetDefinition` adds source images, page dimensions, placement,
and visual groups. `ResolveVisual` accepts explicit coordinates, entity identity
and neighbor mask, producing an ordinary sprite/clip mapping. Map preparation
calls it once; drawing never performs graphics-name lookup or variant selection.
`ComposeSpriteTransforms` explicitly composes the closed quarter-turn/flip set.

Sprites are packed by sorted package ID and row-major frame index, using
deterministic shelves and a one-pixel extruded border. Pages target 2048×2048 and
crop to used extents. Larger sprites receive dedicated pages. Padding counts
toward the 8192-pixel page bound, so individual frames are at most 8190 per axis.
Packing never rotates artwork. Limits are 4096 source files, 4096 total frames,
4096 entries per definition, 64 atlas pages, 4 MiB total metadata, 16 MiB per
encoded PNG, and 64 MiB of CPU atlas/decode image storage. Decoder/converted
RGBA buffers and the current encoded PNG are budgeted before decode.

`sdl/atlas_builder.h` owns the decode/copy boundary; `BuildSourceAtlases` appends
caller-owned CPU surfaces, including partial pages on failure. `SdlAssets` releases
these on failure/cancellation, uploads on the main thread, checks renderer texture
limits, and activates only after every page succeeds. No GPU work occurs on the
loader thread. No dependencies are added.

Rendering paints backgrounds and ground artwork, masks hidden ground, then paints
upright sprites in source world Z/X order (terrain before overlay on ties).
Transformed image geometry handles anchors and overhang. Upright artwork from a
visible source cell may cover fog, preserving tree canopies without showing the
hidden terrain or entities beneath transparent pixels. Hidden source cells never
draw. All artwork remains clipped to the viewport and view extent. Animation redraw
deadlines use the same rules and frame bounds, including overhang from an offscreen
source cell and upright overhang across fog.

## Gen1 assets and preparation

Gen1 contains separate PNGs for its terrain, actor/object overlays and bulb frames,
mirrors grass horizontally, uses plain full-cell asphalt, and maps trees to a
transparent 32x64 upright image. No preassembled tileset atlases are retained.

The individual PNGs are the production sources. Use the generic Python
[resize commands](graphics_tool.md#individual-gen1-sprites-asphalt-and-tree)
to prepare asphalt/tree artwork under `out/graphics/individual`. Existing output
files require `--force`, and source images cannot be overwritten.
Asphalt/tree references and prompts are retained under
`assets/tiles/references/modular_gen1`, outside source discovery. Preparation
center-samples the generated references and preserves tree alpha.
The optional tool needs only Python 3.10+ and its standard library. Runtime loading
does not depend on Python. Tooling tests are not retained after initial validation.
