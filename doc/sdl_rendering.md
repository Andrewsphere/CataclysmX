# SDL tilesets and rendering

The SDL client implements startup native tileset selection, PNG atlases, looping
and on-demand animations, worker-prepared map packets, and cached UTF-8 UI text. It keeps the
existing session thread and deterministic engine contracts. LAN/storage separation
and the inventory/settings screens in [the architecture](sdl_client.md) remain
separate milestones.

Run `cataclysmx_sdl_game` for the bundled `assets/tiles/gen1_32x32` pack, or pass
`--tileset <directory>`. An explicit missing/invalid pack fails with diagnostics;
it never switches to the default. The directory contains `tileset.json5`.
Version 2 discovers individual PNGs and local metadata, then assembles atlas pages
at startup. See [individual sprite tilesets](source_tilesets.md) for the format,
anchors, weighted variants and optional connection sets. Only version 2 is
supported; all source paths are contained within the selected pack, including
symlinks. See [metadata API](tileset_api.md) for loading contracts, limits, and
exact animation sampling rules.

The default Gen1 32x32 pack maps all ten bundled terrain types and six generic
overlays to individual sprites and adds plain asphalt, horizontally mirrored grass
and an oversized upright tree. Separate off/on PNGs supply the three lightbulb
items. Individual production PNGs and retained asphalt/tree references are
documented in [the pack notes](tileset_artwork.md). Actors, ground items and
machines have transparent surroundings so terrain shows through. PNG colors are
preserved independently of content glyph and UI colors.

The built-in monochrome fallback contains sixteen glyphs in a 512x32 texture and
supplies omitted PNG mappings; unsupported glyphs display `?`. A source pack with
no PNG mappings uses this fallback throughout.

The default pack provides [lightbulb animations](sprite_animation.md).
Left-click a visible toggle bulb to start/stop blinking, or a single-blink bulb
to play one cycle when the selected pack supplies activation bindings. These
controls consume no gameplay time and do not introduce simulated lighting.

`--font <file>` selects an independent TrueType font; the default is bundled
DejaVu Sans Mono with its redistribution license under `assets/fonts`.
`--font-size 8-48` and `--ui-scale 1-4` change the UI independently of tile size.
Press `z` to zoom in and `Z` (Shift+z) to zoom out; the mouse wheel does not zoom.
Zoom stays centered on the character, including when the map extends beyond the
viewport. These SDL shortcuts do not submit simulation commands and are inactive
in help modals. `UiPalette` controls screen colors separately from
artwork; terrain glyph colors still come from the catalog. PNG RGB is never tinted.
Font/theme persistence, runtime tileset reload, and font pickers are not implemented.
SDL_ttf's HarfBuzz/SVG features are disabled: UTF-8 and bundled Latin/Cyrillic text
are supported, but complex-script shaping is limited.

## Ownership and APIs

- `SdlAssets` prepares metadata, fallback pixels, and PNG surfaces on one worker.
  It canonicalizes paths (including symlinks), bounds files to 16 MiB, images to
  8192 pixels per dimension, and decoded atlas storage to 64 MiB per pack. PNG
  dimensions and sprite rectangles are checked before decoding/uploading.
  `Poll` uploads at most one texture per call on the render owner; `Ready` means
  the whole pack succeeded. Failures retain a typed `AssetError` and diagnostics.
  Read `Definition`, `Report`, and `Error` after preparation has reached a terminal
  state; the worker owns them during loading. Destruction cancels and joins the
  worker before releasing CPU/GPU resources.
- `PrepareMapPacket` converts an owned view into terrain and selected overlay
  mappings/fallback data, checking visibility before consulting content. The
  packet retains clip identities rather than frames. `MapPreparation` has one
  replaceable pending job and result, so optional work remains bounded. The startup
  asset worker and map worker are the two coarse presentation workers; no generic
  scheduling framework is needed. Catalog/tileset inputs remain immutable and
  alive until workers join.
- SDL snapshots additionally own representative ground-item IDs/types captured
  on the session owner. Map preparation resolves item artwork with generic
  fallback, keeping visibility, overlay priority, and projection depth intact.
- `SpritePlaybackController` owns item activation state independently of packets.
  Shared-phase loops and activation-relative one-shots use explicit time; session
  or asset generation changes reset them. See [sprite playback](sprite_animation.md)
  for the API, pack syntax, capture path, and lifetime decisions.
- `MapIdentity` tags session, view revision, asset generation, and viewport
  generation. The app rejects obsolete completions, retains a matching prior
  packet while awaiting a newer revision, and invalidates packets on layout changes.
  Storage retirement and preparation happen outside channel locks.
- `DrawFrame` samples the supplied packet with explicit elapsed milliseconds.
  It draws backgrounds and ground, masks hidden ground, then draws upright
  terrain and the highest-priority actor, ground item, or object. Visible trees
  retain their canopies over fog; hidden source cells contribute no artwork.
  `VisibleAnimationDelay` uses transformed sprite bounds inside the displayed map
  clip, including upright overhang across fog. Static scenes have no animation deadline. Animation
  draws do not submit map jobs, capture views, or upload textures.
- `MakeMapLayout` returns snapped render-pixel geometry, retaining rectangular
  pack aspect ratios and centering the player-centered view even when zoomed.
  Origins can be negative when the map exceeds the viewport. `MapCellAt` uses
  that same geometry; callers must convert
  window coordinates to render coordinates before hit testing on high-DPI displays.
- `SdlText` owns a font, renderer text engine, and eight screen text slots.
  `Draw` updates text/wrapping/color only when changed, and `Height` provides the
  wrapped layout size. All text and GPU calls stay on the SDL main thread.
- `MapZoomChange` maps case-sensitive SDL `z`/`Z` chords to +1/-1 zoom steps;
  other chords return zero. It stays frontend-specific to preserve shared
  terminal key bindings. Zoom changes only the layout, reusing prepared cells.

The event loop includes animation boundaries in its bounded channel-poll wait.
Animation remains active during an acknowledged local help pause. Minimized
windows do not draw or schedule animation wakeups; restoration samples the current
phase once. Presentation time never enters the simulation. Worker notifications
carry no owning pointers, and timeout polling covers failed/coalesced wakeups.
Shutdown joins session and presentation owners before SDL resources are released.

## Build and validation

In VS Code, select `CataclysmX: SDL Linux GCC Debug` or
`CataclysmX: SDL MSVC Debug` in Run and Debug, then press F5. Each launch builds
its SDL preset first. The default pack is `assets/tiles/gen1_32x32`; use
`--tileset <directory>` to select another pack.

The optional vcpkg `sdl` feature adds `sdl3-image` with PNG and `sdl3-ttf` without
HarfBuzz/SVG, retaining the pinned baseline and Linux D-Bus constraint. Python is
not a build dependency of these selected features. Use:

```sh
VCPKG_ROOT="$PWD/.tools/vcpkg" cmake --preset linux-gcc-sdl
cmake --build --preset linux-gcc-sdl
ctest --preset linux-gcc-sdl -R 'SDL|map packet|map animation|map worker|tileset|animation'
```

Focused tests cover metadata errors, exact animation boundaries, visible overlay
priority, owned worker results, path containment, PNG bounds/failures, partial
upload cleanup, UTF-8 text, coordinate mapping, and software-rendered animation
with an unchanged snapshot/map packet. The SDL smoke test exercises the bundled
default through movement, pickup, wait, pause, and resume.
