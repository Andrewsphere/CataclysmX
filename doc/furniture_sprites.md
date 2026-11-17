# Furniture sprites

Gen1 includes 33 furniture and machine images from the supplied ComfyUI batch
`C:/ComfyUI/ComfyUI/output/CataclysmX/new`. Production images live in
`assets/tiles/gen1_32x32/sprite/object/`, named after existing object content IDs.
The source-to-content mapping, original nontransparent bounds and resized
dimensions are recorded in [the import manifest](furniture_sprites.json).
`_pitch_corrected`, `_v2` and `_v3` are source revision suffixes, not content IDs.

## Artwork normalization

Each source is a transparent 32×32 PNG with one painted view. Crop to the bounds
of pixels with nonzero alpha; resize proportionally so its longer dimension is
30 pixels, using nearest-neighbor center sampling. Round the other dimension to
the nearest integer (half up, minimum one). Place it on a transparent 32×32
canvas at `x = (32 - width) / 2` rounded down and `y = 31 - height`.
This gives a common baseline and a clear border without stretching the artwork.
Colors and alpha come directly from the selected source pixels.

The import uses the existing offline PNG operations in `tools/graphics.py`:
`ReadPng`, `Crop`, `Resize`, `CopyInto` and `EncodePng`. No runtime processing,
generated on-disk atlas, new dependency or ComfyUI installation is required.
The original ComfyUI files are unchanged. Production PNGs are editable sources.

All furniture uses the normal upright layer and bottom-center anchor. The single
painted perspective stays upright for all object facings. The same image applies
to enabled, disabled and broken objects; those states retain their existing
gameplay and glyph behavior.

## Object mappings and observations

The version-2 tileset manifest accepts `objects`, keyed by object content ID:

```json5
objects: {
  house_sofa: {visual: 'sprite.object.house_sofa', optional: true},
}
```

Object mappings support static sprites, animation clips and variant sets, with
the same `visual` and `activation` fields as item mappings. They reject terrain
connections and ground underlays. `optional: true` permits an absent content ID,
but still validates the visual reference. Gen1 marks mappings optional because
demo, FirstHouse and full content expose different object catalogs.
`TilesetDefinition::objects` is indexed by `ObjectTypeId::value`, keeping name
lookups out of rendering. Missing and untyped objects retain generic artwork.
Actors and ground items retain their existing overlay priority.

`ObjectAppearance::type` is an observed catalog identity, defaulting to zero.
It accompanies semantic kind, facing, active and broken state through live views
and player memory. Remembered rendering uses only this observation and applies
the existing sepia shading and animation freeze. Unseen changes never replace
it; a new observation updates identity or clears stale furniture.

## Compatibility

Network protocol 19 appends a 32-bit object type to each cell run. Structural
validation rejects identity on empty/unknown appearances; content validation
rejects unknown nonzero types before publishing ordinary or inspection views.
Hosts and clients must use the same protocol version.

Save format 12 writes stable object ID strings in remembered objects' `type`
field. Empty or absent strings mean unknown type and use generic artwork.
Absent fields remain accepted after upgrading old saves, including untouched
memory rows. Unknown nonempty strings and non-string fields are invalid.
Supported versions 7–11 remain readable and upgrade transactionally on the next
checkpoint. Old memories acquire exact types only when observed again. Runtime
object serialization and gameplay content remain unchanged.
