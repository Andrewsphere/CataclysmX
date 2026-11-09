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
Visible and remembered terrain IDs must be valid in that catalog.

Overlays replace the symbol and foreground in this priority order:

| Overlay | Symbol | Foreground RGB |
| --- | --- | --- |
| Player actor | `@` | 255, 255, 255 |
| Other actor | `z` | 0, 255, 0 |
| Ground items | `!` | 255, 255, 0 |
| Generic object | `*` active, `&` inactive | 0, 255, 255 active; 0, 128, 128 inactive |
| Chair | `h` | Same object colors |
| Table | `T` | Same object colors |
| Lamp | `*` active, `i` inactive | Same object colors |

Broken objects use `(160,96,64)` before perception styling. Furniture identity comes
from `ObjectAppearance`, not persistent IDs or core sprites; facing is retained in
the descriptor for future directional glyphs. Remembered furniture renders even
with object ID zero. Actors and items render only from a Visible overlay channel.

`terrainVisibility` and `overlayVisibility` are independent: Unknown, Remembered,
or Visible. Unknown terrain is never resolved through the catalog; an unknown
overlay never contributes a glyph. Both unknown yields a black blank. Every overlay
preserves the terrain background, styled by terrain visibility/light/depth; the
selected foreground uses its own contribution's visibility/light/depth.
Projection selects the nearest live overlay when one is visible, falling back to
the nearest remembered furniture. A stale upper object never hides a genuinely
visible lower actor; two live overlays retain the existing nearest-layer priority.
Live projection follows current open-space geometry and tests each candidate's
perception independently, so unlit unknown air does not hide a visible lit floor
below. The fallback memory path follows known geometry independently; a hidden
world edit cannot change how far that remembered path descends.

Visible contributions apply their own `LightBand`: Ordinary preserves original
colors exactly, Bright blends one third toward white, Dim scales to 55%, and Dark
scales backgrounds to 25% and foregrounds to `24 + channel / 4`, keeping near vision
readable. Remembered contributions ignore current light and use sepia: weighted
luminance `(30R + 59G + 11B) / 100`, then RGB `(40 + 55%L, 30 + 43%L, 20 + 28%L)`.
The frontend deliberately styles observations rather than consulting world light
or object handles, preserving the observation boundary.

Projected cells may represent terrain below the observer. Foreground tint uses
overlay depth when an actor/item/object is present, otherwise terrain depth;
background always uses terrain depth. Positive depth blends toward blue
`(48,96,192)` at `min(70%, 15% + 10% × depth)`; background applies half that
strength. Memory uses one quarter of those strengths, after sepia conversion.
See [Y layers](y_layers.md) for projection and visibility rules.

The gameplay status shows localized `Light: <band>` from `WorldView::playerLight`
in both wide and compact layouts (`game.ui.light`, `game.light.*`).

`render_text` preserves the tick/position header, row-major map and inventory
footer. It emits complete UTF-8 symbols without ANSI colors or display-width
substitution. A wide or combining scalar therefore survives headless output
unchanged; the textual map is not guaranteed to occupy one terminal column per
cell.

`src/terminal/render.h` exposes the terminal frontend API:

```cpp
ftxui::Element RenderTerrainCell(const TerminalAppearance& appearance);
```

The interactive frontend uses this function when preparing its map tree. It accepts the
resolved appearance so catalog lookup and overlay selection stay separate from
FTXUI rendering. The returned element owns its glyph and applies the actual RGB
foreground/background, including when substituting a fallback glyph. FTXUI types
remain within the terminal frontend boundary. The existing `render_probe()` API
is preserved alongside it.

`GameplayUi` reuses the prepared terrain DOM when projected cells, dimensions,
and catalog identity are unchanged. Status and layout are rebuilt independently;
timer wakeups are coalesced without coalescing keyboard input. See
[terminal performance](terminal_performance.md) for lifetimes and limitations.
Opaque terrain's displayed light uses observer-facing surface illumination; see
[surface lighting](surface_lighting.md).

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
