# CataclysmX

C++23 build skeleton and dependency integration harness for a deterministic roguelike.

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
over-aligned EASTL allocations. No custom allocator system has been introduced.

## Run the probes

```powershell
./out/build/win-msvc-debug/cataclysmx_headless.exe
./out/build/win-msvc-debug/cataclysmx_terminal_smoke.exe --render
./out/build/win-msvc-debug/cataclysmx_terminal_smoke.exe
```

The headless probe prints `seed=42 checksum=3365722423` and does not link FTXUI or
initialize a terminal. These are small dependency probes, not a gameplay implementation
or the final Simulation API. Logging and parsing probes live in the integration engine
target only to exercise the no-exceptions binary boundary.

CTest covers containers/stale handles, JSON5 (including unquoted keys and single quotes),
invalid input, repeatable checksums, console/rotating-file logging, an intentional failed
Catch2 assertion, and a failing spdlog sink in a child process. The intentional failure
uses CTest's `WILL_FAIL`; it is expected and does not indicate a broken test run.

Run the terminal probes in **Windows Terminal** and independently in a WSL terminal.
The interactive probe displays a changing 120x40 grid, Cyrillic, combining and wide
characters, RGB color, and handles arrows, Escape and Ctrl+C. Give the terminal enough
space for the grid plus two header rows. Check Unicode alignment, repeated input,
resize, and restoration after exit. These manual checks are not registered as CTests.
The guide's 1,000–10,000-frame performance workload still needs a dedicated measured run.

See [`md/build_verification_2026-11-07.md`](md/build_verification_2026-11-07.md)
for the actual verification record and remaining checks.
