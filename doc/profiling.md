# In-game profiling with Visual Studio 2022

## Build

From the repository root in PowerShell 7:

```powershell
./scripts/build.ps1 -Preset win-vs2022-profile -SkipTests
```

Requires Visual Studio 2022 with Desktop development with C++, MSVC v143 14.44,
a Windows SDK, and the Visual Studio profiling tools. The existing v143 vcpkg
triplet keeps project and dependency toolsets matched. The script bootstraps
vcpkg and uses the VS generator's tool discovery, so no developer shell is needed.

Outputs:

```text
out/build/win-vs2022-profile/CataclysmX.sln
out/build/win-vs2022-profile/RelWithDebInfo/cataclysmx_game.exe
out/build/win-vs2022-profile/RelWithDebInfo/cataclysmx_game.pdb
```

The solution also contains the engine, network, client, and terminal libraries.
`cataclysmx_game` is its startup project. Keep the executable, its matching PDB,
and the generated dependency DLLs together when profiling.

Equivalent individual commands, or rebuilding just the game:

```powershell
./scripts/bootstrap.ps1
cmake --preset win-vs2022-profile
cmake --build --preset win-vs2022-profile --target cataclysmx_game
```

The normal build includes tests. To run them for this configuration:

```powershell
./scripts/test.ps1 -Preset win-vs2022-profile -j 4
```

## Record actual gameplay

1. Open the generated solution in **VS2022**, select **RelWithDebInfo / x64**,
   and open **Debug > Performance Profiler** (`Alt+F2`, without the debugger).
2. Under **Choose target**, select **Executable**. Browse to the full path of
   `RelWithDebInfo/cataclysmx_game.exe` above. Explicit executable targeting also
   works when the repository is open as a CMake folder instead of a solution.
3. Set the working directory to the repository root. Leave arguments empty for
   the normal interactive game, or use `--load "C:\path\world.sqlite"` to replay
   a particular world. `--user-dir "C:\path\profile-settings"` can select a
   repeatable settings directory. Do not use `--render` for an interactive run.
4. Select **Instrumentation** for function call counts and wall-clock timings.
   Native C/C++ instrumentation is supported by recent VS2022 versions (17.6+);
   current 17.14 supports selecting the functions/modules to instrument. If the
   tool is disabled, deselect other tools and check the executable target.
   Alternatively select **CPU Usage** for sampled CPU hot paths.
5. Start the session. For instrumentation selection, include the game's module;
   engine, world, lighting, and terminal code are statically linked into it. Use
   **Start with collection paused** when available, load/warm the desired area,
   then record while performing the movement or interaction sequence.
6. Play in the game's console window. Stop collection in VS or quit normally.
   Inspect **Call Tree**, **Functions**, and **Caller/Callee**, including **Self**
   versus **Total** time. Keep the resulting `.diagsession` with the matching
   build and the command/settings used to reproduce it.

Useful entry points include `Server::Send`, `Server::Update`,
`World::UpdatePlayerObservations`, `BuildVisibilityMask`, `LightingField::Rebuild`,
`World::ReconcileChunks`, `Server::ViewForPlayer`, and `GameplayUi::Render`.
Disable the profiler's "Just My Code" filtering when inspecting FTXUI/system work.
Third-party source-level attribution depends on the symbols shipped with the
dependency build; all project code gets its own symbols here.

If functions lack names or source lines, check the report's symbol-loading
details and add the executable directory under **Tools > Options > Debugging >
Symbols**. Keep the PDB from that exact executable, rather than a later rebuild.

## Debug stalls and Ninja builds

The same VS profiling solution provides a **Debug** configuration for measuring
Debug-only stalls, with the profiling link flags still enabled:

```powershell
cmake --build --preset win-vs2022-profile --config Debug --target cataclysmx_game
```

Select `out/build/win-vs2022-profile/Debug/cataclysmx_game.exe` in the profiler.
This retains Debug code generation and Debug dependencies. Compare Debug and
optimized traces separately; instrumentation adds overhead to both, especially
when instrumenting very small, frequently called functions. Sampling is useful
for checking whether an instrumentation hot path is also hot without call hooks.

For the existing Ninja/latest-installed-MSVC workflow:

```powershell
./scripts/build.ps1 -Preset win-msvc-profile -SkipTests
```

Its executable and PDB are directly under `out/build/win-msvc-profile/`. It uses
the build script's normal compiler selection; the VS2022 preset above explicitly
pins v143/14.44 for profiling with that IDE. Both presets are separate from
ordinary Debug/Release builds and require no source-level timing hooks.

## Build contract and rationale

`CATACLYSMX_ENABLE_PROFILING=ON` is an opt-in MSVC CMake option. It applies to all
project targets, including static libraries, so symbol coverage includes the
simulation and frontend rather than just `main`:

- `/Zi` generates compiler debugging information; `/Zo` improves optimized-code
  debugging information. `RelWithDebInfo` retains optimization and release runtime
  dependencies, using CMake's normal configuration flags.
- `/PROFILE` emits the CV/fixup information used by Visual Studio native
  instrumentation. `/DEBUG:FULL` and `/INCREMENTAL:NO` are explicit to override
  configuration defaults and produce a complete linker PDB.
- `/OPT:REF` removes unreferenced code; `/OPT:NOICF` keeps distinct functions at
  distinct addresses for clearer attribution. These are also implied by
  `/PROFILE`, but explicit flags prevent configuration defaults from overriding
  them. Optimized functions can still inline, so a source function need not have
  an independently instrumentable call on every path.

Profiling and sanitizer options are separate configurations. Engine exceptions
remain disabled. The presets prepare the binary for the profiler; VS instruments
it when collecting, and merely building/running this configuration does not
start a profiler or change gameplay timing inputs.

Record sight/bubble settings, viewport and terminal dimensions, save/seed,
whether the area was loaded, and the input sequence. Capture empty movement,
buildings/doors, moving lights, and chunk crossings separately. Instrumentation
includes wall-clock waits; CPU sampling measures on-CPU work. Both complement
the [headless/terminal-preparation benchmark](perception_benchmarks.md), while
this interactive workflow includes real input handling and terminal output.

References: Microsoft [`/PROFILE`](https://learn.microsoft.com/en-us/cpp/build/reference/profile-performance-tools-profiler?view=msvc-170)
and [Visual Studio Instrumentation](https://learn.microsoft.com/en-us/visualstudio/profiling/instrumentation?view=vs-2022).

## Validation

Validated on 2026-11-09 with VS2022 17.14.7 / MSVC 14.44.35207:

- `./scripts/build.ps1 -Preset win-vs2022-profile -SkipTests` succeeded.
- `./scripts/test.ps1 -Preset win-vs2022-profile -j 4`: **462/462 passed**.
- The Debug game target built with the command above; its `terminal_game_render`
  CTest passed using `-CTestArgs @('-C', 'Debug', '-R', '^terminal_game_render$')`
  with the same test preset.
- Generated projects have profiling enabled, full debug information, disabled
  incremental linking and COMDAT folding. Both game PDBs were produced; `dumpbin
  /headers` confirmed the optimized executable's matching PDB reference.
- `win-msvc-profile` also built with the installed VS2026 toolchain; its
  `headless_smoke`, `headless_game`, `terminal_game_render`, and
  `save_client_roundtrip` CTests passed (4/4).

An interactive Visual Studio profiler capture was not run during this validation.
