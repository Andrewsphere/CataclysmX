# Perception benchmark matrix

`cataclysmx_perception_benchmark [samples] [sight] [resident] [scene] [viewport]`
accepts 2–10000 samples (default 100), sight `12` or `64` (default 12),
resident chunk radius `2` or `5` (default 2), and `flat` or `mixed` (default
`flat`), and viewport radius `12` or `64` (default 64). Arguments are positional.
Active radius remains 1. Sight is set on the actual world vision profile; the
viewport controls both server snapshots and terminal presentation (25×25 or 129×129). Sight
64 with resident 2 intentionally measures residency-clipped perception.

Build the benchmark target with both
presets using the Visual Studio and vcpkg environment described in
[`perception_benchmark_baseline.md`](perception_benchmark_baseline.md):

```powershell
cmake --preset win-msvc-debug
cmake --build --preset win-msvc-debug --target cataclysmx_perception_benchmark
cmake --preset win-msvc-release
cmake --build --preset win-msvc-release --target cataclysmx_perception_benchmark
foreach ($config in @('win-msvc-debug', 'win-msvc-release')) {
    foreach ($sight in @(12, 64)) {
        foreach ($resident in @(2, 5)) {
            & "./out/build/$config/cataclysmx_perception_benchmark.exe" 100 $sight $resident flat
        }
    }
    & "./out/build/$config/cataclysmx_perception_benchmark.exe" 100 12 2 mixed
}
```

Run sequentially without concurrent builds or benchmarks; record machine,
toolchain, revision and uncommitted changes with output. Flat-scene setup and
radius-64 planar queries can be expensive, especially in Debug.

## Method

The seed is 42, terminal screen 120×40, with eight warmup frames. Clocks are
outside the engine; rows report nearest-rank median, p95 and p99 in milliseconds.
Counters are totals across the row's samples, reset immediately before timing;
divide by `n` for per-sample averages. A burst sample contains eight moves and
frames. No timing threshold is asserted.

- `first_server_load_default` times eight fresh server constructors, using the
  **production defaults**, before the requested sight and residency can be set.
  Counters are summed across those servers. This is a warm-process constructor,
  not executable startup or disk-save loading; destruction is outside the timer.
- Flat setup removes active demo actors and objects and clears every resident
  layer to air except for a floor at y=0, across the initial residency and the
  additional eastward crossing footprint. Demo items remain. The east footprint
  is materialized and edited before timing, then archived by releasing its
  temporary request. Therefore `crossing_first_east` in flat mode is the first
  demand shift but loads prepared archived chunks, not freshly generated chunks.
  `crossing_cached_alternating` warms both directions first.
- Mixed setup retains the original baseline patch: floor x=-12..28, z=-12..12,
  air at y=1..2, generated terrain elsewhere. Use `100 12 2 mixed 12` for the
  closest comparison with the historical scene. The constructor initially uses
  its production resident radius before the requested radius is applied: if it
  already materialized incoming columns, the first crossing reloads archived
  chunks instead of generating them. Constructor defaults, implementation,
  counters, and timing boundaries can differ from the historical run, so compare
  individual rows with those caveats. The historical document remains the source
  of BEFORE data.
- `geometry_bulk_standalone` calls `BuildVisibilityMask` at the origin with
  standalone optics and a reused output buffer. `geometry_bulk_warm_optics_workspace`
  uses an explicitly prepared `OpticalField` and warmed `VisibilityWorkspace` in
  the same unchanged scene. Explicit stats are combined with world counters.
  `geometry_point_planar_8` calls `LineVisible` at four cardinal and four diagonal
  targets on the observer plane, each coordinate offset by half the sight radius.
  Each point query builds a planar FOV; this bounded row is **not comparable** to
  the historical sphere sweep below. These are geometry-only paths, not isolated
  lighting-filter timings.
- `lighting_unchanged_refresh` refreshes a separate warmed field;
  `lighting_fresh_rebuild` constructs a new field for each sample. Neither uses
  the world's cache, so these explicit calls do not increment its lighting counter.
  `memory_observation_warm` calls the real unchanged observation boundary, which
  may skip work using cached state; it is not an isolated memory-write cost.
- Idle view and terminal rows distinguish snapshot construction, rendering an
  existing snapshot, and full frames. Frames include FTXUI screen creation,
  render and string conversion. Movement, facade, door, moving light, upper
  platform/stairs, and boundary scenarios follow the fixed historical ordering;
  setup is untimed and state/memory is retained between scenarios. The first
  crossing is one sample, so its percentiles are not a tail estimate.
- `crossing_ungenerated_default_residency` uses a separate fresh server at
  resident radius 5, clears only the local mixed patch, walks to x=15, then
  crosses east into a genuinely ungenerated outer column. It is one event.
- `moving_source_crossing_view_terminal` retains the historical x=0/-1 route,
  including residency changes. `moving_source_loaded_view_terminal` separately
  warms and measures x=8/7 beside the same facade without crossing a chunk seam.
  The multilayer route also crosses x=0/-1: its timing includes streaming, not
  just layered geometry. No timed stair traversal is claimed.

Counter columns are World `PerceptionStats` geometry rebuilds, point LOS calls,
traversed voxels, changed memory tiles, observation scans, lighting rebuilds and
demand reconciliations; Server `ViewStats` requests, full view constructions and
map constructions; and the change in terminal `GameplayUi::MapPreparations()`.
Additional columns count optical/working-volume rebuilds, lighting filter
rebuilds and sampled tiles, memory tiles actually examined, and `projectedCells`
(downward projection residency probes).
They count instrumented engine work, not inferred call counts. In particular,
explicit standalone lighting rebuilds are counted by their row's sample count.
Scenario counters exclude setup and resets do not invalidate caches. Sink, tick,
and final invariants help check repeatability; no keyboard queue, terminal output,
network, or allocation profiling is measured.

## Planar implementation results — 2026-11-10

Measured the integrated, uncommitted implementation with the normal
`win-msvc-debug` preset (MSVC 14.51, exceptions disabled), after builds/tests
finished. Runs were sequential, seed 42, resident radius 5 (2,541 chunks), sight
64, **129×129 snapshots**, and a 120×40 terminal. Each ordinary row has 30 samples
and eight warmup frames; p95 uses nearest rank, and p99 is the maximum at this
sample count. These are full synchronous preparation timings, not keyboard/OS
queue or terminal-emulator latency.

```powershell
./out/build/win-msvc-debug/cataclysmx_perception_benchmark.exe 30 64 5 flat 64
./out/build/win-msvc-debug/cataclysmx_perception_benchmark.exe 30 64 5 mixed 64
```

| Operation | Flat median / p95 ms | Mixed median / p95 ms |
| --- | ---: | ---: |
| Standalone bulk geometry including optical snapshot | 4.516 / 4.785 | 4.625 / 4.965 |
| Bulk geometry with warm optics/workspace | 0.735 / 0.758 | 0.525 / 0.555 |
| Unchanged lighting refresh | 0.469 / 0.511 | 0.471 / 0.496 |
| Fresh full lighting rebuild | 44.454 / 45.333 | 28.820 / 29.702 |
| Unchanged observation | 0.002 / 0.003 | 0.002 / 0.003 |
| Idle update + view + terminal | 3.201 / 3.717 | 3.118 / 3.644 |
| Loaded movement, server only | 1.249 / 1.301 | 1.463 / 1.620 |
| Loaded movement + view | 3.253 / 3.313 | 3.876 / 4.156 |
| Loaded movement + view + terminal | **6.423 / 7.033** | **7.169 / 7.923** |
| Facade walking + view + terminal | **6.901 / 7.457** | **7.053 / 7.531** |
| Door toggle + view + terminal | **9.527 / 9.967** | **8.470 / 9.010** |
| Moving source, loaded + view + terminal | **7.572 / 7.924** | **8.084 / 8.735** |
| Eight-command batch + eight frames | 52.329 / 54.496 | 57.577 / 60.655 |
| Cached chunk crossing + view + terminal | 195.674 / 204.244 | 180.589 / 187.560 |
| Multilayer chunk crossing + view + terminal | 199.399 / 205.743 | 185.589 / 190.726 |

Flat and mixed runs reported `invariants=ok`, tick `55400`, and sinks
`28380048` / `31035230` respectively. Flat loaded movement visits 16,897 planar
scan cells per FOV build, performs **zero downward probes**, and examines 129
new-footprint memory cells per move. It performs no optical snapshots, workspace
refreshes, lighting rebuilds, or per-tile light filtering after warmup. The mixed
fixture retains generated walls and does filter visible targets, without returning
to per-target 3D rays.

The default-64 renderer resolves appearances and rasterizes only panel-visible
cells. Server projection borrows one prepared visibility mask and reuses
chunk-contiguous lighting and empty-overlay summaries across horizontal runs.
These changes prevent the enlarged viewport from moving the bottleneck into
snapshot construction or offscreen FTXUI elements.

**Remaining limitation:** chunk-boundary streaming still synchronously reconciles,
archives/restores/settles chunks and rebuilds resident lighting. First genuinely
ungenerated crossings were 176.575 / 177.341 ms (one event per run, no tail claim).
The loaded movement/building/light cases meet the 16 ms Debug preparation goal in
these measurements; streaming does not. Historical numbers below use a smaller
viewport and different visibility semantics, so they are not identical-workload
speedup ratios.

### Final validation

- Debug production targets built, followed by `ctest --preset win-msvc-debug -j 4`:
  **492/492 passed**, including frontend, replay, save/load, LAN admission and
  restart/reconnect. During integration the raw enlarged LAN snapshots delayed
  admission; protocol 9's lossless cell runs fixed that without changing pacing
  or increasing smoke-test time budgets.
- Release production targets built. The focused Catch filter
  `[bulk],[lighting],[memory],[perception],[settings],[terminal],[network],[view]`
  passed **192 cases / 3,675,901 assertions**. Full Release CTest was not run.
- Both explicit Debug benchmark commands above completed successfully.

## Historical integrated results — 2026-11-09

The following records the implementation and benchmark as they existed on that
date, including a 25×25 viewport and a 3D point-ray sphere sweep. It does not
measure the current planar FOV, downward projection, lighting, or protocol, and
its point-query row is not comparable to the current bounded eight-query row.

Same machine/toolchain as the [baseline](perception_benchmark_baseline.md): AMD
Ryzen 9 9950X3D, Windows 11 Pro, VS 2026 Community 18.11.2, MSVC toolset
14.51.36231, Ninja Debug/Release presets, existing vcpkg dependencies. Measurements
used the integrated uncommitted implementation, with all builds/tests finished
before sequential benchmark execution. No affinity or power-policy changes.

Commands run (builds and tests were parallelized by configuration; benchmark
runs were sequential in the order shown):

```powershell
./scripts/build.ps1 -Preset win-msvc-debug -SkipTests
cmake --build --preset win-msvc-debug --target cataclysmx_perception_benchmark
./scripts/build.ps1 -Preset win-msvc-release -SkipTests
cmake --build --preset win-msvc-release --target cataclysmx_perception_benchmark
./scripts/test.ps1 -Preset win-msvc-debug -j 4
./scripts/test.ps1 -Preset win-msvc-release -j 4
./out/build/win-msvc-debug/cataclysmx_perception_benchmark.exe 100 12 2 mixed
./out/build/win-msvc-release/cataclysmx_perception_benchmark.exe 100 12 2 mixed
./out/build/win-msvc-debug/cataclysmx_perception_benchmark.exe 100 12 5 flat
./out/build/win-msvc-debug/cataclysmx_perception_benchmark.exe 100 64 5 flat
./out/build/win-msvc-release/cataclysmx_perception_benchmark.exe 100 12 5 flat
./out/build/win-msvc-release/cataclysmx_perception_benchmark.exe 100 64 5 flat
```

Both builds and all six benchmark runs succeeded. Full CTest results were
**462/462 Debug (50.33 s)** and **462/462 Release (21.64 s)**, including terminal,
save/load, replay, bulk/reference geometry, lighting/reference equivalence,
cache invalidation, and LAN roundtrip coverage. The full presets were run
concurrently; benchmarks were sequential. An earlier focused Debug run passed
48/49, and an earlier full run 449/455; the surface regression and old-default
fixtures were subsequently corrected. The LAN smoke reconnect budget was made
consistent with its initial round after measuring activation plus the existing
one-second host grace; a new exact 999999+1 microsecond regression preserves
the command barrier contract.

All benchmark runs reported `invariants=ok`, final tick `174400`. Sinks matched
between builds: `165417720` for sight 12 and `176392762` for sight 64. The extra
loaded-source scenario changes these totals from the historical tool.

### Before/after, original sight 12 and resident radius 2

Each entry is **median / p95 / p99 milliseconds**, 100 samples. Both use the
historical mixed scene and fixed 25x25 viewport. Constructor/first-generation
rows are excluded from this comparison because production defaults changed.
The baseline lacks internal counters; only its explicit boundary-call counts
can be compared, not invented historical FOV/memory-write counts.

| Path | Debug before | Debug after | Release before | Release after |
| --- | --- | --- | --- | --- |
| Warm movement + view | 21.195 / 24.057 / 26.630 | 0.769 / 0.807 / 0.843 | 2.513 / 2.679 / 2.777 | 0.079 / 0.083 / 0.107 |
| Warm movement + view + terminal | 31.420 / 38.664 / 40.641 | 9.411 / 10.289 / 10.426 | 3.701 / 4.065 / 4.413 | 1.068 / 1.204 / 1.564 |
| Eight-command burst + frames | 253.518 / 277.897 / 293.372 | 75.866 / 92.717 / 98.927 | 31.259 / 34.945 / 39.017 | 8.428 / 9.177 / 9.522 |
| Unchanged observation | 19.580 / 23.313 / 25.180 | 0.001 / 0.001 / 0.002 | 2.334 / 2.509 / 2.871 | <0.001 / <0.001 / 0.001 |
| Unchanged view | 0.841 / 0.922 / 1.064 | 0.002 / 0.002 / 0.002 | 0.105 / 0.138 / 0.167 | <0.001 / 0.001 / 0.004 |
| Existing-snapshot terminal | 10.355 / 12.128 / 13.008 | 5.173 / 5.744 / 6.006 | 1.253 / 1.434 / 1.580 | 0.684 / 0.769 / 1.192 |
| Cached crossing + frame | 59.962 / 68.643 / 77.638 | 46.148 / 55.647 / 62.566 | 8.392 / 11.480 / 11.608 | 5.916 / 6.405 / 6.711 |
| Facade walking + frame | 27.409 / 32.878 / 34.206 | 11.462 / 13.658 / 14.283 | 3.428 / 4.218 / 4.426 | 1.068 / 1.184 / 1.240 |
| Door toggle + frame | 41.319 / 45.741 / 54.130 | 25.757 / 31.017 / 36.469 | 5.621 / 6.518 / 6.791 | 2.863 / 3.832 / 4.227 |
| Moving source, crossing + frame | 79.040 / 89.190 / 93.610 | 51.263 / 57.901 / 68.788 | 10.816 / 12.789 / 13.079 | 7.093 / 10.730 / 11.097 |
| Multilayer, crossing + frame | 60.056 / 62.272 / 63.837 | 46.268 / 51.956 / 60.448 | 8.526 / 10.688 / 11.278 | 6.103 / 7.627 / 8.860 |

There is no BEFORE end-to-end radius-64 result: the original actual observation
path was bounded to 12. The following matrix measures both ranges through the
revised implementation, with identical residency, viewport, and scene setup.

### Production residency 5, same viewport and flat setup

Entries remain **median / p95 / p99 ms**, 100 samples unless noted.

| Path | Debug sight 12 | Debug sight 64 | Release sight 12 | Release sight 64 |
| --- | --- | --- | --- | --- |
| Cold standalone bulk geometry | 5.482 / 5.894 / 6.756 | 5.564 / 5.700 / 5.742 | 1.220 / 1.375 / 1.391 | 1.255 / 1.408 / 1.573 |
| Reference point-LOS sphere | 21.347 / 24.264 / 28.983 | 1623.281 / 1781.356 / 1811.940 | 2.324 / 2.388 / 2.605 | 202.201 / 206.004 / 210.152 |
| Unchanged lighting refresh | 3.717 / 3.858 / 4.206 | 3.570 / 3.629 / 3.772 | 0.466 / 0.560 / 0.583 | 0.453 / 0.571 / 0.720 |
| Fresh full lighting rebuild | 70.618 / 77.033 / 83.042 | 69.697 / 71.158 / 72.355 | 10.660 / 11.518 / 12.122 | 10.453 / 11.237 / 11.762 |
| Unchanged observation | 0.002 / 0.002 / 0.004 | 0.002 / 0.002 / 0.002 | 0.001 / 0.001 / 0.001 | 0.001 / 0.001 / 0.001 |
| Unchanged view | 0.002 / 0.002 / 0.002 | 0.002 / 0.002 / 0.002 | 0.001 / 0.001 / 0.001 | 0.001 / 0.001 / 0.001 |
| Existing-snapshot terminal | 5.267 / 6.542 / 7.921 | 5.269 / 5.901 / 5.934 | 0.695 / 0.777 / 1.124 | 0.691 / 1.078 / 1.279 |
| Idle view + terminal | 5.311 / 7.006 / 8.239 | 5.238 / 5.781 / 5.974 | 0.683 / 0.785 / 1.099 | 0.691 / 0.934 / 1.201 |
| Idle update + view + terminal | 5.277 / 7.194 / 8.107 | 5.282 / 5.828 / 6.142 | 0.694 / 0.795 / 1.104 | 0.688 / 0.921 / 1.171 |
| Warm movement, server only | 0.516 / 0.708 / 0.736 | 1.154 / 1.218 / 1.445 | 0.058 / 0.092 / 0.147 | 0.278 / 0.337 / 0.456 |
| Warm movement + view | 1.223 / 1.352 / 1.959 | **1.955 / 2.019 / 2.062** | 0.120 / 0.123 / 0.135 | 0.352 / 0.361 / 0.373 |
| Warm movement + view + terminal | 11.428 / 13.786 / 16.744 | **10.795 / 11.584 / 11.703** | 1.155 / 1.280 / 1.537 | 1.324 / 1.728 / 1.960 |
| Eight-command burst + frames | 91.897 / 105.682 / 112.120 | 85.848 / 89.442 / 92.446 | 9.104 / 9.883 / 10.242 | 10.805 / 11.696 / 12.148 |
| Cached crossing + frame | 236.339 / 265.454 / 315.773 | 224.838 / 229.946 / 233.270 | 31.500 / 35.866 / 39.856 | 28.500 / 30.561 / 30.880 |
| Facade walking + frame | 11.693 / 13.348 / 14.444 | 30.482 / 35.623 / 40.185 | 1.277 / 1.681 / 1.854 | 3.930 / 4.371 / 4.539 |
| Door toggle + frame | 76.779 / 90.777 / 123.981 | 103.010 / 113.821 / 117.956 | 12.188 / 13.397 / 15.063 | 13.847 / 14.651 / 15.979 |
| Moving source, crossing + frame | 251.054 / 283.304 / 305.916 | 276.212 / 298.119 / 305.623 | 32.017 / 38.211 / 43.490 | 35.331 / 36.591 / 37.557 |
| Moving source, loaded + frame | 24.161 / 29.733 / 31.889 | 54.985 / 61.030 / 76.383 | 3.654 / 4.835 / 5.107 | 9.229 / 9.743 / 9.914 |
| Multilayer, crossing + frame | 230.407 / 251.275 / 282.176 | 237.472 / 244.622 / 245.606 | 31.041 / 35.672 / 39.566 | 29.874 / 32.730 / 32.980 |

Production-default construction (eight samples, sight always 64) was
191.662 / 193.030 / 193.030 ms Debug and 36.076 / 37.468 / 37.468 ms Release in
the radius-64 runs. Its predecessor with smaller defaults was 40.583 / 41.115 /
41.115 and 5.896 / 6.028 / 6.028 ms, respectively: larger residency/sight costs
more to initialize. These are not like-for-like scene sizes.

Single-event first crossings (median=p95=p99; no tail claim):

| Crossing | Debug sight 12 | Debug sight 64 | Release sight 12 | Release sight 64 |
| --- | ---: | ---: | ---: | ---: |
| Ungenerated, production residency | 219.625 | 283.140 | 29.205 | 42.300 |
| Prepared archive, first east demand shift | 246.065 | 223.027 | 31.145 | 29.276 |

### Work counts and remaining limits

Counts below are totals for **100 samples**, production residency 5, sight 64;
Debug and Release counts agree. All idle rows performed zero geometry, lighting,
observation, map construction, or map preparation work after warming. Idle view
requests were 100, full constructions zero. The old idle-view path built a full
view on each of those 100 calls and prepared the map on every terminal render.

| Scenario | FOV builds | Light rebuilds | Effective reconciliations | Memory examined / changed | Full views / maps / terminal preparations |
| --- | ---: | ---: | ---: | --- | --- |
| Warm movement + view | 100 | 0 | 0 | 139900 / 0 | 100 / 100 / 0 |
| Warm movement + frame | 100 | 0 | 0 | 139900 / 0 | 100 / 100 / 100 |
| Cached crossing + frame | 100 | 100 | 200 | 139900 / 0 | 100 / 100 / 0 |
| Facade walking + frame | 100 | 0 | 0 | 79850 / 755 | 100 / 100 / 100 |
| Door toggle + frame | 100 | 100 | 0 | 727400 / 101 | 100 / 100 / 100 |
| Moving source, loaded + frame | 100 | 100 | 0 | 226400 / 0 | 100 / 100 / 100 |
| Multilayer, crossing + frame | 100 | 100 | 200 | 143100 / 111 | 100 / 100 / 100 |

Warm flat movement has zero point rays, resolved ray voxels, optical/volume
rebuilds, and per-tile light samples. At sight 12 it examines 223 new-footprint
tiles per move; at 64, 1399. The sphere's entire visible sky is not rescanned into
memory. The reference sphere explicitly executes 7013/267689 rays per sweep at
12/64; its counters record 54140/9224904 cursor visits. Facade/door/loaded-source
bulk geometry resolves 64900/73100/770800 voxel events across 100 moves, with
7700050/8425000/12741000 filter samples. Source-private LOS is separate from the
public point-LOS counter; zero public calls does not claim zero lighting rays.

The routine, already-loaded empty **movement + view** goal is met comfortably in
Debug at both sight radii. Radius-64 full terminal preparation also stayed below
16 ms in this run. Presentation still costs several Debug milliseconds even on
idle redraws because FTXUI layout/rasterization/string construction remain.

Remaining measured limitations are material: partial building geometry retains
an exact shared-ray fallback; doors/residency changes rebuild complete optical
and sky fields; shifting the 2541-chunk bubble synchronously stages and settles
chunks. Thus facade movement, moving lights beside that facade, door edits, and
streaming are **not universally within 16 ms**, and radius-5 streaming is slower
than the old smaller bubble. Incremental source contributions reduce lighting
cost without subtracting max-combined fields, but optical changes deliberately
invalidate complete shadow footprints. See [surface-light measurements](surface_lighting.md)
and [isolated geometry/filter measurements](bulk_perception.md) for attribution.
Regional sky/geometry streaming work is the main remaining performance follow-up.

The bounded timer gate prevents accumulating timer-only wakeups; command input is
not discarded or merged beyond existing queued-intent semantics. This tool does
did not measure OS key delivery, live event-queue residence, terminal writes/emulator
painting, or allocation call stacks. At the time no larger viewport or new packet
format was introduced; save and wire versions were **7**.
