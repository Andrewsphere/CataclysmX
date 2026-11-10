# Tileset metadata API

Tilesets use format version 2. The authoring format, local metadata, source discovery,
packing rules and limits are documented in [individual sprite tilesets](source_tilesets.md).
The loader compiles PNG sources into atlas placements at startup; packs do not supply
atlas filenames or rectangles.

## Loading and compilation

`content/tileset.h` exposes `LoadTilesetFile(path, content, diagnostics)` as the sole
filesystem loading entry point. It reads a bounded manifest, checks the format
version before discovering files, and compiles contained PNGs and local metadata.
The result is an SDL-free `ContentResult<TilesetDefinition>` with optional diagnostics.
Unsupported integer versions return `ContentError::UnsupportedVersion`; missing or
malformed versions return `ContentError::InvalidData`. Manifest filesystem/read
failures return `ContentError::Io`.

`content/tileset_source.h` exposes `ParseSourceTileset(manifest, files, content,
diagnostics)` for callers that already own source metadata and validated PNG header
dimensions. Each `SpriteSourceFile` contains a pack-relative filename, optional
sidecar/definition text, and image dimensions. This separate compiler input is
necessary because a source pack cannot be resolved from one JSON5 document.
Both entry points accept only version 2 and reject unknown or duplicate fields.

PNG files, metadata and symlinks must stay within the selected pack. There is no
shared artwork prefix. The built-in default lives under `assets/tiles/gen1_32x32`;
`--tileset <directory>` selects another source pack. Explicit failures never select
another pack automatically.

## Compiled graphics

`TilesetDefinition` owns source images, generated atlas page dimensions, sprites,
clips, visual groups and resolved mappings. Atlas descriptors have an ID and pixel
dimensions; source file paths belong to source images. Keeping filesystem paths out
of atlas descriptors makes generated pages independent of authored files.

Terrain and item vectors are indexed by the corresponding catalog ID's `value`,
including the unused zero entry. Missing entries use `TilesetMappingKind::Missing`,
allowing the frontend's built-in glyph fallback. Generic mappings are indexed by
`GenericSprite`. Graphics references resolve during loading, keeping name lookup
out of drawing and preserving catalog ownership boundaries.

`ResolveVisual` accepts explicit world coordinates, entity identity and a neighbor
mask. It resolves weighted variants and connections into a sprite or clip mapping.
`ComposeSpriteTransforms` combines the closed quarter-turn/flip set explicitly.

`sdl/atlas_builder.h` exposes `BuildSourceAtlases` to decode sources and assemble
caller-owned CPU surfaces on the asset worker. Every source pack uses this path,
including an empty pack with no atlas pages. Partial surfaces remain caller-owned
on failure; `SdlAssets` releases them on cancellation/failure and uploads complete
pages on the render owner. Activation waits for all uploads to succeed.

## Animation sampling and activation

`SampleAnimation` takes explicit elapsed milliseconds and an optional
`AnimationPlayback` (default `Loop`). It returns a sprite index, an optional positive
boundary delay, and `completed`. Loops wrap at the total duration; one-frame loops
have no deadline. `Once` samples without wrapping, including a deadline for a
single-frame clip. At or after its total duration it returns the final sprite,
`completed = true`, and no delay. The frontend then restores the base appearance.
Sampling requires a valid compiled clip; invalid clips are a broken invariant.
Durations and totals are bounded so conversion to nanoseconds cannot overflow.

Mappings select a base `visual` and may declare `activation: {animation:
'sprite.item.bulb.loop', mode: 'toggle_loop'}` or mode `'once'`. Compilation resolves
this to an optional `SpriteActivationBinding`. Missing clips/fields and unknown
modes are invalid metadata. Clips can mix separate PNGs and uniform animation-sheet
frames, which are repacked into generated pages at launch. See
[sprite playback](sprite_animation.md) for usage and ownership.
