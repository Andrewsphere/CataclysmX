# CataclysmX

C++23 roguelike with a deterministic world, chunk loading, an authoritative LAN
server, FTXUI terminal client, and scriptable headless client.

The optional SDL client (`win-msvc-sdl` preset) defaults to individual PNG graphics
under `assets/tiles/gen1_32x32`, assembled into atlas pages at startup. See
[sprite authoring](doc/source_tilesets.md) for package IDs, local metadata,
animations, variants and anchored sprites. The default uses plain asphalt and
upright grass tufts. Select another version-2 source pack with
`--tileset <directory>`; preassembled tileset atlases are unsupported.

## Play and inspect

After building:

```powershell
./out/build/win-msvc-debug/cataclysmx_game.exe
./out/build/win-msvc-debug/cataclysmx_headless.exe
Get-Content tests/scenarios/streaming.txt | ./out/build/win-msvc-debug/cataclysmx_headless.exe --script
```

The terminal client uses **arrows/hjkl** to move, **.** to wait, **g** to pick up,
**d** to drop, **o then a direction** to interact, **[/]** to halve/double the
runtime tick rate, **r** to toggle rate limiting, **p** to toggle pause-when-idle,
and **q/Escape** to quit. Press **F9** for the LAN menu: **O** opens the current
local world to LAN, **C** closes it (guests are parked), **R** refreshes the
same-subnet browser, **1–9** joins a discovered compatible game, **j** edits the
direct address (**Enter** accepts the edit; **Enter** again joins), **n** edits
your name, **D** leaves a guest session, and **X** joins the entered address as
a *new character*, replacing its saved identity after admission. A host can
open LAN mid-game; guest input and host input both flow through authoritative
rounds. Status and messages show pending actions and server receipts. Press
**i** for Inventory, select an item with arrows/jk, and **Enter** to drop it.
Inventory lists carried contents and accessible ground items with parent IDs,
mass, volume, charge and activity. **u** uses the selected item; from gameplay,
**u** opens item selection. A single functional use executes directly or asks
for a direction; multiple uses open a menu. Inventory **a** opens management:
install/remove batteries, store in a selected container, or extract to direct
inventory. Enter-to-drop applies to directly carried roots. These item keys are
configurable; see [item flows](doc/gameplay_ui.md#item-use-and-storage).
**P** opens Players with connection, readiness and grace status. **F1** opens
the complete controls reference. **a** opens Quick actions: use **Up/Down** and
**Enter** to select and activate a command or open another window.
These windows are hidden by default; **Escape**
closes them. Their opening keys and navigation are configurable in Settings.
See the [gameplay UI overview](doc/gameplay_ui.md) for layouts and modal behavior.

```powershell
./out/build/win-msvc-debug/cataclysmx_game.exe --host --port 24680 --name Host
./out/build/win-msvc-debug/cataclysmx_game.exe --discover --name Alice
./out/build/win-msvc-debug/cataclysmx_game.exe --join 192.168.1.20:24680 --name Alice
```

`--profile <path>` selects the local reconnect-token file (default
`cataclysmx-lan-profile.txt` in the working directory). Each endpoint has its
own token, written on admission; a rejected reconnect keeps the saved token.
To intentionally abandon an identity, use **X** with its address in the LAN
menu. Keep that file if you want to resume your character after disconnect.
The host owns world saves (`--save` or `--load`); guest profiles are not saves.
Literal address aliases have separate profile keys: use the same hostname or
numeric address to reconnect. A different world on the same endpoint may reject
the old token; **X** starts a fresh character explicitly.
Closing LAN/host exit parks guests so they can return when the same save is
hosted again. Use matching content files at both ends.
Press **F11** in-game for the world-generation debug menu; arrows/hjkl move its
cursor, **HJKL** jump by region, **1–5/Tab/Shift+Tab** change layers, **+/-** zoom,
**Home/c** recenter, **PageUp/PageDown** browse details, and **F11/Escape/q** close.
The demo has a backpack containing a bandage, a toggleable machine, a door, a
wandering NPC, and a multi-level stairwell/pit west of the start. `<` descends
and `>` ascends matching stairs; walking into open air falls immediately.
Simulation time uses 100 ticks per in-world second; default
actions cost 100 ticks. Successful actions spend their action cost; rejected
actions spend none.
During a cooldown you can queue the next action. The latest valid input replaces
the one pending intent, which is validated when the player becomes ready.
`cataclysmx_game --render` prints a snapshot without entering interactive mode.

**F2** opens settings and keybindings. Preferences resolve from built-in defaults,
global files, and sparse world overrides; saves within a world share its settings.
Use `--user-dir <path>` and `--world-dir <path>` to select configuration locations.
Player-facing terminal text comes from `data/loc/en.json5`. Ctrl bindings preserve
letter case when the terminal supports enhanced keyboard reporting; basic mode
reports unavailable combinations. See [settings and localization](doc/settings_api.md).

Terrain definitions live in [`data/terrain.json5`](data/terrain.json5), with UTF-8
symbols, named/RGB colors, appearance fallback, passability, sight blocking, and
door transitions. Both clients accept `--content <path>` to load another JSON5
document. The default path points to the bundled file in this source checkout.
See [content format and APIs](doc/content_format.md). Save V6 stores terrain and
item stable string IDs, nested ownership, and versioned item capability state;
older save versions are rejected and migrations are not implemented yet.

The world spans Y=-10..10 in single-layer 16×16 XZ chunks. Chunk requests cover
whole-height columns. It uses typed generational handles, nested item ownership,
resident/active load requests, and a following player reality bubble. Unloaded
objects are destroyed; value snapshots restore their persistent IDs with fresh
runtime handles. SQLite checkpoints persist full chunk snapshots and session
state; unloaded archives remain in RAM until saved successfully. Actors outside
active columns freeze; active actors above or below the player continue running.
See [save format and behavior](doc/save_format.md).

See [`doc/`](doc/README.md) for the world/server API, [Y-layer rules](doc/y_layers.md),
and headless command grammar.
The dependency probes remain available separately below.
Headless scripts can set `rate <ticks-per-second>`, `limit <on|off>`,
`idle-pause <on|off>`, and supply deterministic elapsed time with
`elapse <microseconds>`; `wait [base-cost]` optionally sets a positive wait cost.
`items` lists carried/ground persistent IDs, parent/installation links, mass,
volume, energy, activity, and available capability/action IDs with fallback
labels and typed refusal reasons. Item commands
work in both `--script` and live `--command`:

```text
use <item-id> <capability-id> <action> [item-id|n/s/e/w/up/down]
store <item-id> <container-id|0>
```

IDs are positive decimal values; capability IDs are definition-local ordinals
shown by inspection. Actions are behavior-local string IDs (for example `start`,
`install`, or `arm`); any syntactically valid ID is accepted by the parser, and
the server validates its declared argument schema. Destination `0` extracts
into direct inventory. Install and remove are capability actions. Refusals print the
specific authoritative reason and spend no action cost. Live runs also print
item details at completion; `items` itself is a script inspection command.
Live headless networking is a separate bounded real-time mode, leaving `--script`
deterministic and unchanged:

```powershell
./out/build/win-msvc-debug/cataclysmx_headless.exe --host --port 24680 --name Host --run-for 30000 --command "wait"
./out/build/win-msvc-debug/cataclysmx_headless.exe --discover --run-for 1500
./out/build/win-msvc-debug/cataclysmx_headless.exe --discover --discovery-address 127.0.0.1 --run-for 1500
./out/build/win-msvc-debug/cataclysmx_headless.exe --join 127.0.0.1:24680 --name Alice --run-for 3000 --command "move e"
```

`--discovery-port <n>` configures discovery (default 24681);
`--discovery-address <address>` directs queries to a host (useful for loopback
smoke tests), while omitting it broadcasts on the local subnet. `--port <n>`
sets the host port or the default port for `--join address` without `:port`.
Repeated
`--command` accepts `wait`, `pickup`, `drop`, `move n/s/e/w`,
`interact n/s/e/w`, and the item commands above. Host transport continues pumping throughout `--run-for`
without waiting on stdin. The live client prints admission, submitted sequence
numbers and server receipts. `--profile` works here too; `--new-character`
explicitly starts a fresh identity for `--join` and overwrites its saved token
only after successful admission.

## Git LFS assets

Install [Git LFS](https://git-lfs.com/) before cloning. In an existing checkout,
initialize its filters and push hook, then download the binary assets:

```sh
git lfs install --local
git lfs pull
```

PNG images (`.png`) and TrueType fonts (`.ttf`) use LFS throughout the repository.
The `assets/` directory is versioned, including font licenses, tileset metadata
and artwork references. Text metadata stays in ordinary Git for readable diffs.
When adding artwork or fonts, use `git add` normally; `.gitattributes` selects
LFS automatically. Run `git lfs status` before committing to inspect asset changes.

## Build on Windows

From the repository root, in PowerShell 7:

```powershell
./scripts/build.ps1
```

The script initializes the latest installed stable x64 Visual Studio C++ developer environment
(falling back to an Insiders/preview installation when needed),
bootstraps a pinned vcpkg checkout in `.tools/vcpkg` when `VCPKG_ROOT` is unset,
installs manifest dependencies, configures CMake, builds with Ninja, and runs CTest.
If Ninja is not on PATH, it uses the copy bundled with Visual Studio C++ CMake tools.
An existing `VCPKG_ROOT` is reused without changing its checkout. No environment-variable
persistence or OpenCode restart is needed. Git and CMake >= 3.25 must be on PATH;
Visual Studio needs C++ build tools and a Windows SDK.

For the SDL client, run `./scripts/build.ps1 -Preset win-msvc-sdl`.
The [Windows VS Code workflow](doc/vscode_windows.md) describes the SDL build,
run, and debug shortcuts.

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
| ENet | 1.3.18 |

`vcpkg.json` pins registry commit `6fdd23fa0a9479a9f9a1c48178696f60cf39255d`.
Transitive dependencies include EABase and fmt. unordered_dense and SlotMap are vendored
with exact commit IDs and licenses in [`third_party/README.md`](third_party/README.md).
`scripts/vendor.ps1` re-downloads those pinned files; normal builds use the checked-in
copies. ENet provides LAN transport; SQLite provides host-side saves.

Python is not a build prerequisite. On Linux, the manifest explicitly disables
D-Bus's default features to exclude optional systemd integration and its
Meson/Jinja2/Python build tools. The optional `sdl` manifest feature retains
IBus, X11, and Wayland support. Build the local SDL prototype with
`cmake --preset linux-gcc-sdl` and `cmake --build --preset linux-gcc-sdl`, then run
`out/build/linux-gcc-sdl/cataclysmx_sdl_game`. See
[SDL frontend](doc/sdl_client.md#running-the-prototype) for controls and scope.
Keep this constraint when changing dependency features or updating the baseline.

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
