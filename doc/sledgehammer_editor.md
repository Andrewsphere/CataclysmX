# Sledgehammer Editor

Sledgehammer Editor opens a content pack in a local browser and saves edits
directly into the checkout. The game reads those changes the next time it loads
the pack.

## Launch

Configure and build the opt-in worker using a normal Catax preset. The editor
can build with SDL disabled. It needs Python 3.10 or later and a current browser.
Set `VCPKG_ROOT` to your vcpkg checkout as described in the [build guide](../README.md#cmake-directly).

```sh
cmake --preset linux-gcc-debug -DCATACLYSMX_BUILD_SLEDGEHAMMER_EDITOR=ON
cmake --build --preset linux-gcc-debug --target sledgehammer_editor
python3 tools/sledgehammer_editor/serve.py \
  --worker out/build/linux-gcc-debug/sledgehammer_editor
```

From the checkout root, `sh sledgehammer_editor.sh` launches the Linux debug build.
The launcher prints a loopback URL and opens it in the default browser.
Use the full URL, including its fragment, when opening it manually. Pass
`--no-browser` to skip launching a browser. `--checkout` selects a different
Catax checkout. `--tileset` selects a source tileset under that checkout's
`assets/tiles` directory. The initial pack is Town and the default tileset
is Gen1. Use the pack selector to switch among manifests in `data/`.

## Edit a layout

- Choose a pattern or use **New** to start a building, landmark, park, or square.
  In **Pattern**, use **Duplicate** to start from a known working layout.
- Select a palette tile or choose a terrain brush. The palette groups tiles with
  the same terrain and object. Select the group to choose a facing, loot, or
  spawn variant. Use **Brush**, **Rectangle**, or **Fill** to paint.
  **Eyedropper** picks up the whole cell recipe. **Select**
  opens the cell inspector for terrain, furniture facing, loot, and monsters.
  Arrow keys move the cursor on a focused map. Enter applies the current tool.
- Switch elevations in the toolbar. Add authored floors with **Layer** and
  weighted Y −1 alternatives with **Basement**. Use **Pattern** to edit the
  entrance, footprint, generation weights, and coordinate placements.
- Each glyph can carry terrain, furniture, and spawn recipes. Editing one
  cell reuses an equivalent glyph or allocates a new glyph. Existing shared
  glyphs and coordinate placement order stay intact. See [Building format](house_content.md).

## Add definitions and artwork

In **Catalog**, choose a definition kind and create or duplicate a record.
Reference inputs suggest definitions from the open pack. Create an item and
assign it to a loot pool, then select that pool in a cell's inspector. Create
a monster and add it to a monster group before placing it. Furniture-destination
loot requires a storage object on the same tile. Chances use 10,000 for 100%.

Use **Visual variants** to author weighted sets from existing sprites and
animations. Assign a sprite or set under **Tileset artwork**. Assigning artwork
to a terrain in shared Gen1 includes its definition source in the available
bundled manifests. Any references required by that source must also be available
in those packs. The layout view uses representative sprite thumbnails.
[Content format](content_format.md) · [Tileset format](source_tilesets.md)

## Preview and save

In **Region preview**, enter a seed, region X/Z, and elevation, then choose
**Generate**. Select terrain, roads, parcels, biomes, or spawns as the map
overlay. Click a tile to see its actual generated content. A building location
can open its source pattern. The preview uses the current edits and the game's
current generation version. Authored layouts can be inspected even when the
seeded region contains no instance of them.

Scroll the mouse wheel over either map to zoom around the pointer.

**Save** or **Ctrl+S** validates and writes the changed JSON5 files and manifest
entries. Validation errors remain visible with source locations and leave the
files on disk intact. **Undo** and **Redo** apply to edits since the last save.
When another program modifies a file the editor is editing, reopen the pack
before saving it again. The editor keeps recovery data under
`out/worldgen-editor/` until a multi-file save finishes and recovers interrupted
saves at startup.

## Local worker interface

The launcher exposes a same-origin `POST /api` to the local browser. It forwards
one JSON object to the C++ worker on standard input and returns the JSON reply
from standard output. The launcher's session token and checkout lock bind requests
to the local session. PNG thumbnails are served from the selected tileset.

- **Open** takes `operation: "open"` and a checkout-relative `pack`. It returns
  the original manifest, source, and metadata text, and the available PNG IDs.
- **Validate**, **Preview**, and **Save** take the pack and an `edits` array of
  objects with `path`, `before`, and `text`. `before` is the original source
  text, or `null` for a new file. File paths are relative to the checkout.
  Preview also takes a decimal `seed` string and integer `regionX`, `regionZ`,
  and `y`. It returns terrain and overlay data for one region at that elevation.
- Successful replies contain `ok: true`. Failure replies contain `error` and
  may include engine `diagnostics` with source and field locations.

The browser works from source text because the typed catalog normalizes IDs,
fills defaults, and expands palette markers. The worker loads candidate files
through the normal game APIs so cross-file validation and generated previews use
the same semantics as gameplay. See [Content packs](content_pack.md) and
[Worldgen API](worldgen_api.md).
