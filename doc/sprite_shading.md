# GPU sprite shading

The SDL client shades PNG artwork according to the same observed light, memory
and projection depth used for glyphs. SDL 3.4's GPU renderer is required in
production, including smoke mode. Windows and Linux both use Vulkan and require a
compatible Vulkan driver and video environment.

## Compilation and color

At startup `SpriteShader` loads the GLSL 450 source
[`assets/shaders/sprite.frag`](../assets/shaders/sprite.frag) and uses shaderc's C API
to compile `main` as a fragment shader targeting Vulkan 1.0 and SPIR-V 1.0. There
is no offline generation or hot reload; source edits take effect at the next
startup without rebuilding the game. Source paths and compiler diagnostics appear
in SDL errors. Failure to create the Vulkan renderer or shader fails startup.
The vcpkg `sdl` feature adds shaderc at the pinned baseline; the compiler is built
from source on both platforms. See [Vulkan builds](vulkan_build.md) for prerequisites.

The shader matches SDL's textured vertex outputs: color at location 0 and UV at
location 1, with fragment color output at location 0. The combined atlas sampler
uses set 2/binding 0; the `std140` perception block uses set 3/binding 0 and three
`vec4` rows (48 bytes). These explicit bindings follow SDL GPU's SPIR-V contract.

The transform operates on encoded RGB, intended for SDL's default SDR sRGB
output. It is not a linear-light or HDR color pipeline. Alpha is preserved by
the perception transform, then normal vertex modulation and alpha blending apply.
The three `vec4` rows each compute `dot(vec4(rgb, 1), row)` on normalized
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
that the GPU device uses Vulkan and supports SPIR-V, `Apply(transform)` selects a
state and `Clear()` restores the default. These operations return `bool` and use SDL errors. States
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
with the same Catch2 filter. GPU pixel tests use the same Vulkan creation path
as production on both platforms and check the selected driver; unavailable video
or Vulkan can be skipped. Full-map checks cover independent styles, alpha,
overhang and remembered artwork, with packet/client tests covering preparation
and CPU behavior. Smoke creates an actual hidden GPU window, loads the default
pack and exercises movement, pickup, wait, pause and resume; it does not use
dummy video or a software renderer.
