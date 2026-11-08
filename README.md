# CataclysmX

C++23 single-player roguelike foundation with a deterministic world, chunk loading,
local authoritative server, FTXUI client, and scriptable headless client.

## Play and inspect

After building:

```powershell
./out/build/win-msvc-debug/cataclysmx_game.exe
./out/build/win-msvc-debug/cataclysmx_headless.exe
Get-Content tests/scenarios/streaming.txt | ./out/build/win-msvc-debug/cataclysmx_headless.exe --script
```

The terminal client uses **arrows/hjkl** to move, **.** to wait, **g** to pick up,
**d** to drop, **o then a direction** to interact, and **q/Escape** to quit.
The demo has a backpack containing a bandage, a toggleable machine, a door, and
a wandering NPC. Successful actions spend one turn; rejected actions spend none.
`cataclysmx_game --render` prints a snapshot without entering interactive mode.

Terrain definitions live in [`data/terrain.json5`](data/terrain.json5), with UTF-8
symbols, named/RGB colors, appearance fallback, passability, sight blocking, and
door transitions. Both clients accept `--content <path>` to load another JSON5
document. The default path points to the bundled file in this source checkout.
See [content format and APIs](doc/content_format.md). Save V2 stores terrain
string IDs and rejects older save versions; migrations are not implemented yet.

The world has 16×16 XZ chunks, typed generational handles, nested item ownership,
resident/active load requests, and a following player reality bubble. Unloaded
objects are destroyed; value snapshots restore their persistent IDs with fresh
runtime handles. SQLite checkpoints persist full chunk snapshots and session
state; unloaded archives remain in RAM until saved successfully. Offscreen actors
freeze. See [save format and behavior](doc/save_format.md).

See [`doc/`](doc/README.md) for the world/server API and headless command grammar.
The dependency probes remain available separately below.

## Build on Windows

From the repository root, in PowerShell 7:

```powershell
./scripts/build.ps1
```

The script initializes the latest installed x64 Visual Studio C++ developer environment,
bootstraps a pinned vcpkg checkout in `.tools/vcpkg` when `VCPKG_ROOT` is unset,
installs manifest dependencies, configures CMake, builds with Ninja, and runs CTest.
An existing `VCPKG_ROOT` is reused without changing its checkout. No environment-variable
persistence or OpenCode restart is needed. Git, CMake >= 3.25 and Ninja must be on PATH;
Visual Studio needs C++ build tools and a Windows SDK.

```powershell
./scripts/build.ps1 -Preset win-msvc-release
./scripts/build.ps1 -SkipTests
./scripts/build.ps1 -Preset win-msvc-asan
```

The ASan preset uses `RelWithDebInfo` to avoid Debug's incompatible `/RTC1`.
It additionally requires **C++ AddressSanitizer** in Visual Studio Installer
(`Microsoft.VisualStudio.Component.VC.ASAN`). The build script checks for that runtime
before starting this preset.

This preset instruments project targets with ASan and uses ordinary vcpkg binaries.
It defines `_DISABLE_STL_ANNOTATION` on project targets to match those libraries and
avoid MSVC `LNK2038` mismatches for `annotate_string`, `annotate_vector`, and
`annotate_optional`. ASan memory checks remain enabled, but the additional MSVC STL
container-boundary annotations are disabled.

VS 2026 was approved and verified for this setup; the original environment guide used
VS 2022. The script discovers the installed toolchain rather than hardcoding a VS path.

## Open a Visual Studio solution in Rider

With Visual Studio 2022 Community, MSVC v143 14.44 C++ tools and a Windows SDK installed, generate a solution
from PowerShell at the repository root:

```powershell
./scripts/bootstrap.ps1
cmake --preset win-vs2022
```

Open `out/build/win-vs2022/CataclysmX.sln` in Rider and select the Debug or Release
configuration. Set `cataclysmx_game` as the startup project for the terminal client.
This generator uses MSBuild and discovers Visual Studio directly; an initialized
developer shell is not required. Its build directory is separate from the Ninja
presets. Regenerate with the same configure command after changing CMake files.
The preset selects the 14.44 toolset for the project and the dedicated
`x64-windows-v143-cataclysmx` vcpkg triplet for dependencies. The triplet pins
v143/14.44 even during IDE-triggered CMake regeneration, which does not inherit
the configure preset's environment. The Ninja presets use their own toolset selection.

If an existing solution reports unresolved `__std_*` symbols from Catch2, refresh
its CMake cache and dependency selection once, then reload the solution in Rider:

```powershell
./scripts/bootstrap.ps1
cmake --fresh --preset win-vs2022
cmake --build --preset win-vs2022-debug
ctest --preset win-vs2022-debug
```

This replaces dependency binaries built with a newer MSVC toolset; editing linker
library paths or installing a redistributable does not fix that build-time mismatch.

To build and test the solution from the command line:

```powershell
cmake --build --preset win-vs2022-debug
ctest --preset win-vs2022-debug
```

Use `win-vs2022-release` for Release builds/tests. Executables are placed under
`out/build/win-vs2022/Debug/` or `Release/`. Visual Studio does not export a
`compile_commands.json`; the Ninja wrapper scripts remain available for clangd.

## Build on Linux / WSL2

Keep a separate source checkout inside the Linux filesystem. Install prerequisites:

```bash
sudo apt update
sudo apt install -y build-essential clang cmake ninja-build git curl zip unzip tar pkg-config
```

Use CMake >= 3.25. From the Linux checkout:

```bash
bash scripts/build.sh
bash scripts/build.sh linux-clang-sanitizers
# Optional second compiler:
bash scripts/build.sh linux-gcc-debug
```

Linux uses its own `.tools/vcpkg` and binary cache. During setup a source snapshot was
placed at `/home/relaxed_pillow/src/CataclysmX-setup-20261107` in Ubuntu for actual
build verification. It is a snapshot, not a synchronized Git checkout; subsequent edits
in Windows are not automatically copied there.

## CMake directly

The wrapper scripts are conveniences around standard CMake presets. In an initialized
MSVC developer shell, with `VCPKG_ROOT` set:

```powershell
$env:VCPKG_ROOT = "$PWD/.tools/vcpkg"
cmake --preset win-msvc-debug
cmake --build --preset win-msvc-debug
ctest --preset win-msvc-debug
```

Replace the preset name for Linux. Each preset owns `out/build/<preset>` including its
`vcpkg_installed` directory. Downloads and build products are ignored by Git.
Both wrappers copy the active preset's `compile_commands.json` to the source root for
clangd; the original remains inside the build directory. Use the matching platform's
database. The scripts do not edit OpenCode configuration.

## Dependencies

The environment guide's FTXUI decision takes precedence over the earlier Notcurses notes.

| Dependency | Pinned vcpkg version |
| --- | --- |
| EASTL | 3.27.1 |
| spdlog (compiled) | 1.17.0, port revision 1 |
| Catch2 | 3.16.1 |
| yyjson | 0.13.0 |
| FTXUI (compiled) | 7.0.3 |

`vcpkg.json` pins registry commit `6fdd23fa0a9479a9f9a1c48178696f60cf39255d`.
Transitive dependencies include EABase and fmt. unordered_dense and SlotMap are vendored
with exact commit IDs and licenses in [`third_party/README.md`](third_party/README.md).
`scripts/vendor.ps1` re-downloads those pinned files; normal builds use the checked-in
copies. ENet and other deferred candidates are not required by the manifest.

The project triplets enable `SPDLOG_NO_EXCEPTIONS` **while compiling spdlog**, whose
exported target then propagates the matching definition to consumers. No overlay port
or global no-exception flags are needed. Engine, headless, and terminal probe sources
disable exceptions; Catch2 and its test translation unit enable them.

The integration probes use upstream `eastl::allocator_malloc` explicitly for ordinary
integer vectors. EASTL's default allocator requires application-provided allocation
hooks; these probes do not establish a final game allocation policy or validate
over-aligned EASTL allocations. Game containers use a small fatal-allocation adapter
around it, so null allocations abort in release too. Public EASTL container headers
use `EASTL_EXCEPTIONS_ENABLED=0` in both engine and test consumers; Catch2 itself
retains exceptions. There is no allocator system or custom memory pool.

## Run the probes

```powershell
./out/build/win-msvc-debug/cataclysmx_headless.exe --probe
./out/build/win-msvc-debug/cataclysmx_terminal_smoke.exe --render
./out/build/win-msvc-debug/cataclysmx_terminal_smoke.exe
```

The headless probe prints `seed=42 checksum=3365722423` and does not link FTXUI or
initialize a terminal. These are small dependency probes, not a gameplay implementation
or the gameplay Simulation API. Logging and parsing probes live in the integration engine
target only to exercise the no-exceptions binary boundary.

CTest covers containers/stale handles, JSON5 (including unquoted keys and single quotes),
invalid input, repeatable checksums, console/rotating-file logging, an intentional failed
Catch2 assertion, and a failing spdlog sink in a child process. The intentional failure
uses CTest's `WILL_FAIL`; it is expected and does not indicate a broken test run.

Gameplay tests cover ownership transfers/cycle rejection, destruction, chunk request
overlap, resident-only freezing, nested inventory unload/reload, deterministic actor
order/replay, command timing, visibility snapshots, and an end-to-end headless
streaming script. Automated render checks do not verify interactive terminal behavior.

Run the terminal probes in **Windows Terminal** and independently in a WSL terminal.
The interactive probe displays a changing 120x40 grid, Cyrillic, combining and wide
characters, RGB color, and handles arrows, Escape and Ctrl+C. Give the terminal enough
space for the grid plus two header rows. Check Unicode alignment, repeated input,
resize, and restoration after exit. These manual checks are not registered as CTests.
The guide's 1,000–10,000-frame performance workload still needs a dedicated measured run.

See [`md/build_verification_2026-11-07.md`](md/build_verification_2026-11-07.md)
for the actual verification record and remaining checks.
