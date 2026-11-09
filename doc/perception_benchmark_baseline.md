# Radius-12 perception baseline (stage 0)

Captured 2026-11-09, **before perception optimization**, against the existing
uncommitted lighting/memory implementation. This is a BEFORE measurement, not a
measurement of the future radius-64 implementation. Only the benchmark target,
`tools/perception_benchmark.cpp`, and this document were added by this task.
Existing CMake lighting/test additions and all other user changes were preserved.

## Reproduction

From the repository root, PowerShell 7, with the existing bootstrapped vcpkg:

```powershell
$vswhere = "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe"
$vs = & $vswhere -latest -prerelease -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
& "$vs/Common7/Tools/Launch-VsDevShell.ps1" -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
$env:VCPKG_ROOT = "$PWD/.tools/vcpkg"
cmake --preset win-msvc-debug
cmake --build --preset win-msvc-debug --target cataclysmx_perception_benchmark
cmake --preset win-msvc-release
cmake --build --preset win-msvc-release --target cataclysmx_perception_benchmark
./out/build/win-msvc-debug/cataclysmx_perception_benchmark.exe 100
./out/build/win-msvc-release/cataclysmx_perception_benchmark.exe 100
```

The target requires `CATACLYSMX_BUILD_TERMINAL=ON`, is `EXCLUDE_FROM_ALL`, and
compiles without exceptions using the existing project helper. It adds no
dependency. Default sample count is 100; an optional integer argument selects
2..10000 samples. It intentionally asserts `playerSightRange == 12` so a later
default change cannot silently turn this baseline into a different experiment.
When extending to sight 64, expose an explicit sight policy in the real observer
path; changing viewport radius alone does not change sight.

Environment: AMD Ryzen 9 9950X3D, 16 cores / 32 logical processors; Windows 11 Pro
10.0.26200; Visual Studio 2026 Community 18.11.2; MSVC 19.51.36260.0, toolset
14.51.36231; Ninja presets `win-msvc-debug` and `win-msvc-release`, vcpkg triplet
`x64-windows-cataclysmx`. No affinity or power-policy changes. Final timed runs
were sequential, after builds and focused tests finished. An earlier exploratory
Debug run overlapped Release compilation and is excluded from these results.

## Benchmark design and interpretation

- All clocks are `steady_clock` reads in the tool, outside deterministic engine
  code. Seed 42, production terrain content and English localization, fixed
  sight 12, viewport 25x25, screen 120x40. Samples are sorted and percentiles use
  nearest rank (`ceil(n*p/100)-1`); median is the lower middle for even counts.
  There are no hardware-sensitive pass/fail thresholds.
- `first_server_load`: eight separately constructed/destroyed real Servers,
  including synchronous initial generation, demo setup, lighting, and initial
  observation. Content/localization loading is outside the timer. This measures
  fresh worlds in a warm process, **not cold OS caches, disk save loading, or
  executable startup**. At n=8, p95/p99 are both the maximum.
- Untimed setup removes active demo NPCs and objects, makes surface floor over
  x=-12..28, z=-12..12 and air at y=1..2, retaining generated terrain elsewhere
  and demo items. Default bubble radii remain active=1, resident=2: 525 resident
  single-layer chunks. One active player, zero active objects. Eight alternating
  x=0/1 moves and full frames warm the scene. Scenario order is fixed and state
  is retained, including memories; comparisons must use the same order.
- `geometry_point_los_sphere`: calls actual `LineVisible` once for every valid
  radius-12 spherical offset, with world Y clamped to -10..10: **7013 point
  queries per sample**. This isolates reference geometry queries, including
  early exits at floors. It is not a new bulk-FOV implementation or a count of
  rays used inside Server. No separate bulk-FOV API exists in the baseline.
- `lighting_explicit_rebuild`: warms an actual `LightingField`, then calls its
  production `Rebuild(world)` once per sample over the whole resident field.
  This is a sky-only scene and isolates full rebuild cost. It is a separate
  field, not a forced invalidation of World's private cache.
- `memory_observation_warm`: real `World::UpdatePlayerObservations()` at unchanged
  position with lighting already cached. Includes perception/LOS, memory lookup
  and writes; it **does not isolate pure memory-write time**. It is deliberately
  an already-known scene, exposing repeated unchanged-observation cost.
- `view_idle`: constructs a real `Server::View(12)` every sample, including its
  real projection/visibility and metadata. `terminal_existing_snapshot` uses one
  unchanged real snapshot and calls production `GameplayUi::Render`, FTXUI
  `Render` into a freshly allocated screen, and `Screen::ToString`. This includes
  full gameplay map/status DOM preparation, layout, rasterization, output-string
  creation and temporary destruction, not just glyph selection.
- `idle_view_and_terminal` combines those two paths. `idle_update_view_terminal`
  additionally supplies a fixed 20000 microseconds to `Server::Update`, matching
  the terminal timer interval without reading time inside simulation.
- `warm_move_server`: alternating successful x=0/1 moves through `Server::Send`,
  including authoritative simulation, observation boundaries and bubble handling.
  `warm_move_and_view` adds one real view; `warm_move_view_terminal` adds the full
  frame. `burst8_move_view_terminal` executes eight ordered alternating successful
  moves, each with its view and full frame, per sample. Burst numbers are **per
  eight-command batch**, not per movement. This is uncapped, single-player local
  handling; it does not replace or bypass rate-capped queued-intent semantics.
- `crossing_first_east`: travel to x=15 is untimed, then time x=15 -> 16 plus a
  frame. This is the first demand shift into a new chunk column, including new
  outer-edge generation, with default residency. It is **one event**: its printed
  percentiles are identical and do not estimate a first-crossing tail.
  `crossing_cached_alternating` first warms both directions, then alternates
  x=16/15. It includes archive unload/reload/reconciliation and lighting effects,
  not just movement between two pinned chunks. No save database is involved.
- Return to origin untimed. Build a wall facade at x=1, z=-8..8, with a closed
  door at z=0. `facade_walk_view_terminal` alternates z=0/1 beside it.
  `door_toggle_view_terminal` opens/closes through real interaction commands.
  Each action retains its own observation and frame.
- Replace the doorway with floor and give the player a `{240,12}` light through
  `SetActorLight`. `moving_source_view_terminal` alternates x=0/-1 beside the
  facade in daylight, exercising real source-movement invalidation. Warm the
  light before timing; no manual light rebuild is inserted in movement handling.
- Remove the light, build an upper platform at y=1 beside air/open lower terrain,
  and matched stairs at x=-2 on y=0/1. Move the player to y=1 untimed.
  `multilayer_move_view_terminal` alternates x=0/-1 on the platform and exercises
  actual lower-layer projection. Stairs are present; timed stair traversal is
  not measured.
- Command outcomes, view size, and final world invariants are checked. The sink
  consumes query values, snapshots and serialized screens to keep work observable.
  Identical sink/tick totals across builds are a useful reproducibility check,
  not a substitute for the existing correctness suite.

### Synchronous terminal path and remaining measurement limits

Inspection of `src/terminal/main.cpp` confirms the local path calls `Server::Send`
synchronously. Rendering calls `CurrentView()` -> `Server::View()` ->
`GameplayUi::Render()`. Modal event handling can request another `CurrentView()`.
The event callback also pumps the host before action dispatch. A separate thread
posts Custom events every 20 ms; no benchmark timing is inserted into that loop.

`MapViewport` constructs per-cell FTXUI elements, row collections and a new map
node every render. The baseline includes their normal allocation costs plus a
new screen and output string, but **does not count allocations**. The isolated
Debug terminal median is 10.355 ms, so presentation is material as well as the
19.580 ms unchanged observation pass. Warm movement plus view alone exceeds the
16 ms target (21.195 ms median), before adding terminal preparation (31.420 ms).

This tool measures synchronous preparation, not keyboard decoding, OS input
delivery, live FTXUI event queue residence/coalescing, terminal writes/emulator
painting, LAN host/guest paths, or allocation call stacks. It cannot establish
actual event-backlog length. The eight-command Debug batch takes 253.518 ms
median; the timer can post roughly 12 events during that much elapsed wall time,
but that is an interval comparison, **not a measured queue depth**. An interactive
event timestamp/queue-depth probe and allocation profiler are still needed to
attribute real-session stalls beyond these measured stages.

## Exact final Debug output

```text
radius=12 viewport=25x25 screen=120x40 seed=42 samples=100 warmup=8 burst=8
scenario,n,median_ms,p95_ms,p99_ms
first_server_load,8,40.583,41.115,41.115
resident_chunks=525 active_actors=1 active_objects=0
point_los_calls_per_sweep=7013
geometry_point_los_sphere,100,19.592,20.246,21.802
lighting_explicit_rebuild,100,14.307,16.587,20.387
memory_observation_warm,100,19.580,23.313,25.180
view_idle,100,0.841,0.922,1.064
terminal_existing_snapshot,100,10.355,12.128,13.008
idle_view_and_terminal,100,11.277,16.049,17.173
idle_update_view_terminal,100,11.076,15.097,15.960
warm_move_server,100,20.299,26.542,30.228
warm_move_and_view,100,21.195,24.057,26.630
warm_move_view_terminal,100,31.420,38.664,40.641
burst8_move_view_terminal,100,253.518,277.897,293.372
crossing_first_east,1,59.410,59.410,59.410
crossing_cached_alternating,100,59.962,68.643,77.638
facade_walk_view_terminal,100,27.409,32.878,34.206
door_toggle_view_terminal,100,41.319,45.741,54.130
moving_source_view_terminal,100,79.040,89.190,93.610
multilayer_move_view_terminal,100,60.056,62.272,63.837
invariants=ok sink=147522913 final_tick=164200
```

## Exact final Release output

```text
radius=12 viewport=25x25 screen=120x40 seed=42 samples=100 warmup=8 burst=8
scenario,n,median_ms,p95_ms,p99_ms
first_server_load,8,5.896,6.028,6.028
resident_chunks=525 active_actors=1 active_objects=0
point_los_calls_per_sweep=7013
geometry_point_los_sphere,100,2.362,2.520,2.551
lighting_explicit_rebuild,100,1.913,2.103,2.258
memory_observation_warm,100,2.334,2.509,2.871
view_idle,100,0.105,0.138,0.167
terminal_existing_snapshot,100,1.253,1.434,1.580
idle_view_and_terminal,100,1.394,1.565,1.914
idle_update_view_terminal,100,1.366,1.625,1.902
warm_move_server,100,2.407,2.590,3.428
warm_move_and_view,100,2.513,2.679,2.777
warm_move_view_terminal,100,3.701,4.065,4.413
burst8_move_view_terminal,100,31.259,34.945,39.017
crossing_first_east,1,9.300,9.300,9.300
crossing_cached_alternating,100,8.392,11.480,11.608
facade_walk_view_terminal,100,3.428,4.218,4.426
door_toggle_view_terminal,100,5.621,6.518,6.791
moving_source_view_terminal,100,10.816,12.789,13.079
multilayer_move_view_terminal,100,8.526,10.688,11.278
invariants=ok sink=147522913 final_tick=164200
```

## Work counts and hooks needed by subsequent stages

The tool can count its own explicit calls without editing core. Per ordinary
timed sample: one point sweep = 7013 `LineVisible` calls; one explicit lighting
sample = one full `LightingField::Rebuild`; one observation sample = one
`UpdatePlayerObservations`; one view sample = one full view construction; one
frame = one view plus one UI render/rasterization/string conversion; one burst =
eight successful sends and eight full frames. The existing-snapshot terminal
sample constructs zero new views. These are boundary call counts, not inferred
counts of internal work performed by Send/View.

The baseline provides no read-only work-counter API. To measure reuse accurately
in the optimizing implementation, add narrow optional counters at these sites:

| Needed counter | Suggested owning site |
| --- | --- |
| Geometry/FOV rebuilds and cache hits | Actual observer visibility builder/cache |
| Point LOS calls and traversed voxels | `LineVisible` traversal, including lighting callers |
| Lighting rebuilds, chunks/cells processed | World cache refresh and `LightingField::Rebuild` |
| Observation passes, candidates, changed terrain/object memories | `World::UpdatePlayerObservations` and memory writes |
| Full views and reused views | `Server::ViewForPlayer` |
| Map preparations, cache hits | `GameplayUi::Render` / `MapViewport` |
| Reconciliation fast-path hits and generated/reloaded/unloaded chunks | World residency boundary |

Expose snapshots/reset or caller-owned diagnostic counters without clocks, RNG
access, simulation decisions, or persisted state. In particular, count **actual
value changes** separately from attempted unchanged memory writes. Pure memory
update cost cannot be separated from visibility by subtracting unrelated
benchmark medians; an explicit observation-result/apply boundary or profiler is
needed. Queue timestamps belong at terminal enqueue/dequeue boundaries, and heap
counts should come from an allocation profiler rather than a new engine allocator.
Internal counts listed above are **unmeasured** in this BEFORE capture.

## Validation and baseline identity

Both targeted Debug/Release builds succeeded. Existing MSVC exception-flag
override warnings were emitted. Both final benchmark runs exited successfully
and reported `invariants=ok`, identical sink 147522913 and final tick 164200.

Focused existing Debug tests were run (no full suite or Release CTest run):

```powershell
./scripts/test.ps1 -Preset win-msvc-debug -R 'light|Light|perception|Perception|memory|Memory|projection|Projection'
```

Output: `CTest: 100% tests passed, 0 tests failed out of 31 (2.85 sec)`.

HEAD was `7d26b21f395ee9d8dabea6ce6260df575c5ca5e3`, but HEAD alone does not
identify this uncommitted baseline. The following `git hash-object` fingerprints
were checked before/after measurement; the listed core/client/terminal files
were unchanged throughout:

| File | Git blob fingerprint |
| --- | --- |
| `src/core/world.cpp` | `0635a56c246d73f755a15a0404711455d9d0f14a` |
| `src/core/server.cpp` | `0b979e3b777ca2d817a5a6c9c6e6a68054c9598f` |
| `src/core/simulation.cpp` | `37986809de7c4ffa7a435df5bba68cf344a51089` |
| `src/core/lighting.cpp` | `767c529c10e3abad5733e597b3ee755cee25d6e3` |
| `src/core/perception.cpp` | `da70cfe7c594d431636ce523789a072c88838956` |
| `src/core/world_types.h` | `a7de10f1a83d4523bcda3599f236ca537b20afea` |
| `src/core/perception_types.h` | `ffb5f3d33b366d9c003b39611937541467dfb272` |
| `src/terminal/gameplay_screen.cpp` | `3751bd4c363558f3e19e5ce8e55999ca95582c4c` |
| `src/terminal/main.cpp` | `6443ec973bc28caac2eb3a8c156e264d4aa85de6` |
| `src/client/text.cpp` | `8b76cc23435ad7dcb0e92378fc2dbad2ea8b3130` |
| `tools/perception_benchmark.cpp` | `73fd72a58d9834d294d491c15b17774c0333862c` |
| `out/build/win-msvc-debug/cataclysmx_perception_benchmark.exe` | `b31bec8470f22c22c384b36102e4b0b0efef95e2` |
| `out/build/win-msvc-release/cataclysmx_perception_benchmark.exe` | `772af13338d46c79a73854af20deb04fe84ec778` |

These fingerprints identify the measured inputs/artifacts; they do not archive
the uncommitted source. Keep the baseline executables if rebuilding after core
changes. No source commit, reset, or dependency-version change was performed.
