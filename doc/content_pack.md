# Content packs

`LoadContentPack(path, diagnostics)` loads a JSON5 manifest with exactly
`format_version: 1` and a nonempty `files` array. Paths are relative to the
manifest directory. Absolute paths, drive paths, parent components, embedded
NULs and duplicate normalized paths are rejected. Both slash styles are treated
as separators; `.` components and repeated separators are normalized. The
registry overload supplies custom item behavior parsers.

All sources are loaded together, so references can point to definitions in a
later source. Errors in the manifest carry its path and field/index; errors in
definitions carry their source path. `LoadContentFile` continues to load one
definition document, not a manifest.

The manifest directory is also the data root for item art references such as
`/art/items/rock/art.txt`; the leading slash is virtual, not an OS root. Asset
paths are canonical and confined to that root, including symlink resolution. Art
files are dependencies referenced by item definitions, **not** entries in the
manifest `files` array. Deploy the entire data tree with the manifest. Add
`flags/core.json5` to each pack containing items that reference its flags, and
include all item and recipe source dependencies required by that pack.

For standalone definition files with external art use
`LoadContentFiles(paths, dataRoot[, diagnostics])`; in-memory sources use
`LoadContent(sources, ContentAssetReader[, diagnostics])`. The reader is a
borrowed synchronous callback returning owned `String` bytes. No-reader and
no-root overloads reject external references. Assets are read once per path per
load and the catalog owns the resolved art; fingerprinting uses resolved RGB art,
not paths. See [content format](content_format.md) and
[item presentation](item_presentation.md) for details.

| Manifest | Purpose and sources |
| --- | --- |
| `data/demo.json5` | Shared legacy definitions from `terrain/`, `items/`, `crafting/`, `monsters/`, `objects/`, and `worldgen/buildings/`. |
| `data/first_house.json5` | Curated FirstHouse pack: demo sources plus household items, crafting, furniture, loot, cellar monsters, furnished house, and the three slice item sources with low-tech pilot items and current slice recipes. |
| `data/content.json5` | Expanded Town pack: shared sources plus urban resources, the complete 60-item slice, specialists, 32 populated building layouts, a park and a square, and three legacy patterns (37 total). |

The manifests reference shared definition files; they do not duplicate those
definitions within a loaded pack. See [urban content](urban_content.md) for the
complete roster and scenario guide.

The house has two equally weighted cellar layouts. The workshop variant has a
30% chance of a zombie away from the stairs. Movable lamps light both surface
rooms and each cellar without blocking movement.

The kitchen cabinet always contains the current `chef_knife`, and the bedroom cabinet contains
clean rags. Collect the knife, then craft improvised
bandages within reach of the bedroom cabinet (or collect the rags too). Both
placements have guaranteed rolls with single-entry pools, so this recipe path is
independent of seed and cellar choice.

The old rag/bandage tutorial route remains available with obsolete dependency
items. Current cloth-patch bandages and the other new acquisition routes are
documented in [the broad item slice](item_slice.md).
