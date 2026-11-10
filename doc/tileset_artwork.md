# Tileset artwork

The SDL default is `assets/tiles/gen1_32x32/tileset.json5`, selectable with
`--tileset assets/tiles/gen1_32x32`. It assembles individual PNGs into in-memory
atlas pages on every launch. The individual files are the editable production
artwork; no preassembled tileset atlases are shipped.

The pack maps all ten terrain types, six generic overlays and the backpack. Grass
uses horizontal mirroring so tufts stay upright. Roads use plain full-cell asphalt
without lane markings or connection shapes. Trees use a transparent 32x64 upright
sprite with a separate grass underlay. Actors, ground items and machines preserve
transparent surroundings so terrain remains visible beneath them. PNG colors are
independent of content glyph and UI colors.

Separate `sprite/item/bulb/off.png` and `on.png` images provide lightbulb artwork.
Local `loop.meta` and `once.meta` clips provide the continuous/toggled 500/500 ms
loop and the single 120/120 ms blink. These are presentation animations; content
and gameplay timing are independent. See [sprite playback](sprite_animation.md).

Asphalt and tree generation references and exact prompts remain under
`assets/tiles/references/modular_gen1`, outside source discovery. The generic
[graphics tool commands](graphics_tool.md#individual-gen1-sprites-asphalt-and-tree)
center-sample these references into their production dimensions while preserving
tree alpha. The tool is optional and has no build or runtime role.

See [individual sprite tilesets](source_tilesets.md) for package IDs, local
metadata, anchors, animation sheets, variants and optional connected terrain.
