# Terrain rendering

`src/client/text.h` exposes two catalog-aware renderer APIs:

```cpp
TerminalAppearance CellAppearance(const ViewCell& cell, const ContentCatalog& content);
String render_text(const WorldView& view, const ContentCatalog& content);
```

`CellAppearance` returns an owned UTF-8 symbol and RGB foreground/background.
Visible terrain uses the catalog's fully resolved appearance, including
`looks_like` inheritance. The explicit catalog argument keeps rendering free of
global content state and ties terrain IDs to the same catalog as the world.
Visible terrain IDs must be valid in that catalog.

Overlays replace the symbol and foreground in this priority order:

| Overlay | Symbol | Foreground RGB |
| --- | --- | --- |
| Player actor | `@` | 255, 255, 255 |
| Other actor | `z` | 0, 255, 0 |
| Ground items | `!` | 255, 255, 0 |
| Enabled object | `*` | 0, 255, 255 |
| Disabled object | `&` | 0, 128, 128 |

Every overlay preserves the terrain background. Hidden cells return a space with
black foreground and background before inspecting terrain or overlays, so no
appearance information escapes visibility filtering.

Projected cells may represent terrain below the observer. Foreground tint uses
overlay depth when an actor/item/object is present, otherwise terrain depth;
background always uses terrain depth. Positive depth blends toward blue
`(48,96,192)` at `min(70%, 15% + 10% × depth)`; background applies half that
strength. See [Y layers](y_layers.md) for projection and visibility rules.

`render_text` preserves the tick/position header, row-major map and inventory
footer. It emits complete UTF-8 symbols without ANSI colors or display-width
substitution. A wide or combining scalar therefore survives headless output
unchanged; the textual map is not guaranteed to occupy one terminal column per
cell.

`src/terminal/render.h` exposes the terminal frontend API:

```cpp
ftxui::Element RenderTerrainCell(const TerminalAppearance& appearance);
```

The interactive frontend calls this function for each cell. It accepts the
resolved appearance so catalog lookup and overlay selection stay separate from
FTXUI rendering. The returned element owns its glyph and applies the actual RGB
foreground/background, including when substituting a fallback glyph. FTXUI types
remain within the terminal frontend boundary. The existing `render_probe()` API
is preserved alongside it.

`RenderTerrainCell` uses `ftxui::string_width` from
`ftxui/screen/string.hpp` and substitutes `?` when a symbol does not occupy exactly
one column according to FTXUI. This keeps map geometry stable for wide,
combining and other non-one-column scalars without maintaining another Unicode
width table. This is only a width fallback: it does not detect unsupported font
glyphs. A one-column scalar passes through even if the user's terminal font
cannot display it.

`tests/terminal_render_tests.cpp` verifies the function offscreen using a fixed
three-column, one-row FTXUI `Screen` and `ftxui::Render()`. Adjacent rendered cells
must occupy their exact columns, and `Screen.PixelAt` checks the resulting UTF-8
characters and RGB foreground/background on every cell. Cases cover ASCII,
one-column U+2502, wide CJK U+754C, combining U+0301, and supplementary-plane emoji
U+1F600; the latter three must render as `?` while retaining their colors. These
checks need neither an interactive terminal nor terminal font coverage.

## Application content lifetime

Both applications accept `--content <path>` for a single terrain content file.
The default is the absolute bundled path supplied by `CATACLYSMX_TERRAIN_FILE`.
Content loads before server construction or save loading, and its result lives
on the main stack longer than the server. Headless scripted `load` reuses that
same catalog. Multi-file composition remains available through the separate
content-loader API.

Content and save failures print the typed error's numeric code together with
`FormatDiagnostics`. Reused diagnostics are cleared between independent
operations. The standalone headless probes retain their existing startup path.
