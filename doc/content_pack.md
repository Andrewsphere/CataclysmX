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
| `data/first_house.json5` | Curated FirstHouse pack: shared skill, learning and combat sources; demo and household content; the three slice item sources, low-tech items and slice recipes; all four expansion item/crafting pairs and shared expansion loot; furniture, cellar monsters and the furnished house. |
| `data/content.json5` | Expanded Town pack: shared FirstHouse sources plus urban resources, low-tech recipes, specialists and urban building layouts; includes all four expansion item/crafting pairs and shared expansion loot. |

The manifests reference shared definition files; they do not duplicate those
definitions within a loaded pack. See [urban content](urban_content.md) for the
complete roster and scenario guide.

The [80-item expansion](item_expansion.md) adds new definitions to the earlier
catalog; it does not upgrade existing IDs. Town's 178-item total is the previous
98 loaded definitions plus these 80, not a count of newly authored or necessarily
current items: loaded legacy definitions can include obsolete entries.
`worldgen/loot/expansion.json5` supplies shared themed pools for both packs.
Compact pools respect existing furniture limits, while dedicated ground-only pools
place bulky workshop stock, camping gear, outerwear, the toaster and bucket without
nesting them in general furniture loot. Expansion recipes include four craft-only
weapons; the remaining expansion items have loot routes.

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
