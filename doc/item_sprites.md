# Item sprites

Gen1 assigns artwork to all 179 authored items in the full content catalog:
176 static sprites and the three animated lightbulb items. The 89 PNGs from
`C:/ComfyUI/ComfyUI/output/CataclysmX/item_sprites_missing/items_final` were
already transparent 32×32 images and were copied unchanged into
`assets/tiles/gen1_32x32/sprite/item/`. Each filename stem matches its item ID.
The import leaves the earlier artwork untouched.

`tileset.json5` maps each static item to `sprite.item.<item_id>`. These mappings
are optional so scenarios with smaller catalogs can load the same pack. The
default upright layer and bottom-center anchor place the PNGs on the map; the
SDL inventory and item browser use the same artwork for icons. The lightbulb
frames and activation clips remain under `sprite/item/bulb/`.

The generated `combat_corpse` and `unfinished_craft` items have no dedicated
sprites. They retain the generic ground-item sprite on the map and the fallback
glyph in item lists.
