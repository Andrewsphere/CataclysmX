# Modular Gen1 graphics references

These asphalt and tree references were generated with the built-in imagegen tool.
The tree used the original Gen1 atlas as a style reference. Exact prompts are in
`prompts.json`; the asphalt prompt requests plain, low-contrast charcoal aggregate
without markings, borders, grass or connection shapes.
Original generation outputs remain in Codex's generated-image library.

The generic Python [resize commands](../../../../doc/graphics_tool.md#individual-gen1-sprites-asphalt-and-tree)
center-sample the references to one full-cell 32x32 asphalt tile and one transparent
32x64 tree.
It preserves tree alpha. These references are outside the pack's discovered
`tile`/`sprite` directories and are never loaded at runtime.

Generated artwork is saved under `assets/tiles/gen1_32x32/tile/world/road` and
`assets/tiles/gen1_32x32/sprite/world/tree`. The individual production PNGs are the editable sources; atlas pages are assembled
in memory when the game starts.
