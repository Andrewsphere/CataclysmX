# GPU sprite shading

The SDL client shades PNG artwork according to the same observed light, memory
and projection depth used for glyphs. SDL 3.4's GPU renderer is required in
production, including smoke mode. The current supported DXC package targets x64
Windows and Linux. A compatible GPU/video environment is required.

## Compilation and color

At startup `SpriteShader` loads the single HLSL source
[`assets/shaders/sprite.hlsl`](../assets/shaders/sprite.hlsl) and uses
`directx-dxc` to compile its fragment entry point at runtime: DXIL for Direct3D12
or SPIR-V for Vulkan, according to device formats. There is no offline shader
generation. The vcpkg `sdl` feature enables SDL Vulkan and adds DXC without a
baseline or version change. On Windows CMake explicitly deploys `dxil.dll` next
to the game and test executables because DXC loads this validator dynamically.
Failure to create the GPU renderer or shader fails startup with SDL diagnostics.

The transform operates on encoded RGB, intended for SDL's default SDR sRGB
output. It is not a linear-light or HDR color pipeline. Alpha is preserved by
the perception transform, then normal vertex modulation and alpha blending apply.
The three `float4` rows each compute `dot(float4(rgb, 1), row)` on normalized
RGB, clamped to the output range. This affine representation allows the same
shader to implement light, sepia and depth without changing source artwork.

`PerceptionColor` retains the existing CPU integer operations and rounding for
glyphs and flat colors. `MakePerceptionTransform` shares their constants and
ordering, but floating-point composition does not reproduce intermediate integer
rounding exactly: GPU results can differ by a few byte levels. Ordinary or Bright light at
depth zero is identity. Details of the rules are in [terrain rendering](terrain_rendering.md).

## Packet and draw contracts

`PrepareMapPacket` builds separate `terrainShading` and `overlayShading` values
from each channel's visibility, light and depth. Each transform belongs to its
source cell and applies to the entire sprite, including upright overhang across
fog; the destination cell's perception does not recolor it. Ground overhang is
masked over unknown cells before upright artwork is drawn. Unknown channels
contribute no artwork.

Remembered terrain and furniture resolve only observed data. Mapped PNG animations
freeze at their first frame and activation is removed; memory has no actors or
items. Sepia ignores current light, and its depth tint is one quarter of the
corresponding visible strength. Missing mappings use already-styled glyphs once,
without a second shader pass. `terrainGround` uses the terrain sprite's
foreground-strength shading, including in memory; the colored flat background
uses the existing half-strength depth tint.

The SDL-free shared API keeps the integer and GPU representations explicit:

```cpp
RgbColor glyph = PerceptionColor(color, visibility, light, depth, false);
PerceptionTransform sprite = MakePerceptionTransform(visibility, light, depth);
```

`SpriteShader` is main-thread RAII: it borrows the renderer and GPU device, owns
its shader and cached render states, and must be destroyed before the borrowed
renderer/device. `Ready()` reports initialization, `Supported(renderer)` checks
the GPU device's formats, `Apply(transform)` selects a state and `Clear()`
restores the default. These operations return `bool` and use SDL errors. States
are immutable after their uniforms are set and cached by transform: updating a
shared SDL uniform state during drawing would flush queued draws.

Production creates a ready shader for its GPU renderer and supplies it as the
final optional `DrawFrame` argument, for example:

```cpp
SpriteShader shader(renderer);
if (!shader.Ready()) { /* report SDL_GetError() */ }
// After loading assets and preparing a packet:
bool ok = DrawFrame(renderer, text, assets, &view, content, status, hints,
    elapsedMilliseconds, &packet, 0, 1.0f, {}, &playback, &shader);
```

The shader must belong to that same renderer. A null shader is useful for
software-rendered glyph-only or neutral (ordinary, depth-zero) artwork tests;
attempting to draw a PNG with a nonidentity transform without one returns an
SDL error. Production always uses the GPU shader. A null packet instead asks
`DrawFrame` to prepare a small map synchronously for isolated tests.

## Validation

After configuring and building an SDL preset, expected focused commands on Linux
are:

```sh
./out/build/linux-gcc-sdl/cataclysmx_tests '[gpu]'
ctest --preset linux-gcc-sdl -R 'sdl_frontend_smoke|map packet|perception'
```

On Windows use the `win-msvc-sdl` build/preset and its `cataclysmx_tests.exe`
with the same Catch2 filter. GPU pixel tests exercise default and Vulkan SPIR-V
backends, plus Direct3D12 DXIL on Windows, when available; unavailable video or
backends can be skipped. Full-map checks cover independent styles, alpha,
overhang and remembered artwork, with packet/client tests covering preparation
and CPU behavior. Smoke creates an actual hidden GPU window, loads the default
pack and exercises movement, pickup, wait, pause and resume; it does not use
dummy video or a software renderer.

### Windows validation (2026-11-10)

Configured `win-msvc-sdl` and built `cataclysmx_tests` and `cataclysmx_sdl_game`.
The final focused CTest selections covered 80 cases: **78 passed, 2 skipped**.
The skips were existing source-pack symlink checks, which require Windows symlink
privileges. Runtime DXIL/Direct3D12 and SPIR-V/Vulkan pixel checks both ran and
passed on Windows, as did full-map shading, remembered animations, transparent
overlays, overhang, shared CPU color rules, terminal rendering and the GPU game
smoke. A Linux build was not run for this change.
