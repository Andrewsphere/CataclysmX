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

| Manifest | Purpose and sources |
| --- | --- |
| `data/demo.json5` | Shared legacy definitions from `terrain/`, `items/`, `crafting/`, `monsters/`, `objects/`, and `worldgen/buildings/`. |
| `data/first_house.json5` | Curated FirstHouse pack: demo sources plus household items, crafting, furniture, loot, cellar monsters, and furnished house. |
| `data/content.json5` | Expanded Town pack: the same shared sources plus urban resources, specialists, and 25 additional populated buildings. |

The manifests reference shared definition files; they do not duplicate those
definitions within a loaded pack. See [urban content](urban_content.md) for the
complete roster and scenario guide.

The house has two equally weighted cellar layouts. The workshop variant has a
30% chance of a zombie away from the stairs. Movable lamps light both surface
rooms and each cellar without blocking movement.

The kitchen cabinet at (1,1) always contains a knife, and the bedroom cabinet at
(10,6) always contains clean rags. Collect the knife, then craft improvised
bandages within reach of the bedroom cabinet (or collect the rags too). Both
placements have guaranteed rolls with single-entry pools, so this recipe path is
independent of seed and cellar choice.
