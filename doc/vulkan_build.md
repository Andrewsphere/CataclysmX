# Vulkan frontend builds

The Windows and Linux SDL frontends use the same rendering path: SDL 3.4's GPU
renderer with Vulkan, and GLSL compiled to SPIR-V at startup through shaderc.
The existing `win-msvc-sdl` and `linux-gcc-sdl` presets select the same vcpkg
`sdl` feature and pinned registry baseline. The feature adds SDL Vulkan support,
shaderc, PNG decoding and TrueType text.

## Prerequisites and commands

Use the C++23 compiler, CMake, Ninja and vcpkg setup in the [README](../README.md).
Windows uses MSVC and Linux uses GCC. Shaderc, glslang and SPIR-V tools are built
from source on both platforms; Python 3 is needed for source generation, and
vcpkg can acquire its build tools. No separate Vulkan SDK installation or shader
precompilation command is required. The first dependency build is larger than
later builds, which can reuse vcpkg's binary cache. Project sources still compile
without exceptions; dependencies retain their own compiler settings.

Linux:

```sh
export VCPKG_ROOT="$PWD/.tools/vcpkg"
cmake --preset linux-gcc-sdl
cmake --build --preset linux-gcc-sdl
./out/build/linux-gcc-sdl/cataclysmx_sdl_game
```

Windows, from a Visual Studio x64 developer shell:

```powershell
$env:VCPKG_ROOT = "$PWD/.tools/vcpkg"
cmake --preset win-msvc-sdl
cmake --build --preset win-msvc-sdl
./out/build/win-msvc-sdl/cataclysmx_sdl_game.exe
```

The existing build wrappers also support these presets and initialize their
platform's build environment. Run from the repository root, as in the existing
VS Code launches. Shader and bundled content paths currently refer to this
checkout; these commands do not produce a standalone distribution.

At runtime, install a Vulkan-capable graphics driver and loader. Windows graphics
drivers normally provide the Vulkan runtime; Linux needs the distribution's
Vulkan loader and the matching vendor or Mesa driver. A working desktop video
environment is needed for the game and smoke test. Validation layers are optional.
No alternative graphics backend is selected when Vulkan initialization fails.

## Renderer and shader contracts

`CreateVulkanRenderer(SDL_Window*)` in `sdl/gpu_renderer.h` is a main-thread
frontend API. Pass a window for presentation or null for offscreen GPU tests.
It returns an owned `SDL_Renderer*`, or null with `SDL_GetError()` diagnostics.
Call `SDL_DestroyRenderer` to release both the renderer and its internally owned
GPU device. The window must outlive its renderer. Destroy textures, text engines
and `SpriteShader` before the renderer.

The factory uses SDL renderer properties to request the GPU renderer, Vulkan and
SPIR-V custom shaders, then verifies the actual device backend and shader format.
Keeping device ownership inside SDL avoids an additional lifetime owner, and
using one factory keeps game startup and GPU tests on the same platform path.
SDL backend hints cannot turn this into a different supported rendering path;
an incompatible override fails initialization.

`SpriteShader` loads `assets/shaders/sprite.frag` and compiles its fragment `main`
through shaderc's C API for Vulkan 1.0/SPIR-V 1.0. Compiler handles, options and
results use RAII; allocation failure is fatal. Compilation errors include the
source filename and shaderc diagnostics through SDL errors. Compilation occurs
once per shader construction; there is no hot reload or generated shader asset.
Runtime compilation preserves the existing source-edit workflow on both platforms.
See [sprite shading](sprite_shading.md) for bindings, color math and draw contracts.

## Validation

After building, run the existing GPU pixel checks and hidden-window smoke test:

```sh
./out/build/linux-gcc-sdl/cataclysmx_tests '[gpu]'
ctest --preset linux-gcc-sdl -R sdl_frontend_smoke --output-on-failure
```

On Windows use `out/build/win-msvc-sdl/cataclysmx_tests.exe` with the same filter
and `ctest --preset win-msvc-sdl -R sdl_frontend_smoke --output-on-failure`.
GPU tests can skip unavailable video or Vulkan; report skips separately from
passes. The smoke test requires a real hidden GPU window and must pass to verify
startup and presentation. Do not use SDL dummy video or the software renderer
as a substitute for Vulkan validation.

### Linux verification (2026-11-11)

Configured `linux-gcc-sdl` with the pinned vcpkg baseline and built
`cataclysmx_sdl_game` and `cataclysmx_tests` using GCC 11.4.0. The focused CTest
selection below passed **41 tests, with no failures or skips**, including all
four Vulkan pixel/map tests and the hidden-window frontend smoke:

```sh
ctest --preset linux-gcc-sdl --output-on-failure \
  -R 'SDL|GPU map|GPU upright|map packet|map animation|map worker|tileset|animation|sdl_frontend_smoke'
```

The run used desktop/GPU access and hash-verified LFS assets. This checkout
initially contained asset pointers; the assets were materialized for validation
and the pointers restored afterward. Hydrate assets with Git LFS before running
the game or repeating asset-dependent tests. Windows was not built or tested
during this verification.
