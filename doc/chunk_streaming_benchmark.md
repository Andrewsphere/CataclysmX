# Chunk streaming benchmark

`tools/chunk_streaming_benchmark.cpp` is a headless, synchronous benchmark using the bundled production content and seed 42. It reports nearest-rank median, p95, p99 and maximum in milliseconds, with no timing assertions. Invocation: `cataclysmx_chunk_benchmark.exe [samples] [2|5] [all|split|fresh|server|circle|disk] [circle_radius_tiles]`; defaults are `10 2 all` and the production `CircleResidency` default (currently 78 tiles). The square radius is in chunks; the optional circle radius is in tiles (0–128). Circle cases use that tile radius regardless of the square radius: explicitly pass 30 for a smaller radius-2 comparison or 78 for production. These shapes are not identical and are not equal-area comparisons. `all` includes both new modes. Each scenario uses the requested sample count. Small counts are useful for smoke runs, but cannot characterize tail latency.

* `archive_reconcile`: alternate a world request across X=15/16 after both sides were materialized. Its timer includes both request updates and `ReconcileChunks`; movement is outside the timer. Evicted columns reload from the in-memory archive.
* `archive_light_observe`: immediately after each reconciliation, time `GetLightLevel` and `UpdatePlayerObservations`. Counters are accumulated independently for each timed phase. The phase depends on the preceding reconciliation and is not an independent experiment.
* `fresh_reconcile`: move the request east one chunk per sample into previously ungenerated columns. It does not move the player or time observations; the archive grows during the run.
* `server_*`: move a player back and forth across X or Z at 15/16 or -1/0, including `Server::Send` and a radius-12 `Server::View`. Setup clears the route and removes initially active non-player actors and objects; surrounding generated terrain is retained. Warmup visits both sides. Setup is untimed, including the administrative teleport, and the moves synchronize the server bubble. This measures in-memory archive reload, simulation, observation and view construction together, not terminal rendering.
* `circle_*`: the same end-to-end seam scenarios explicitly select tile-circle residency before setup. `circle_forward_x_step_01` through `_16` instead report each forward step from X=0 through X=16 separately, including send and view. Each sample first returns over the same route outside timing; a complete out-and-back warmup precedes measurement. These are warm repeated traversals, not fresh generation. Step 16 crosses the chunk seam; steps 1–15 remain within the starting chunk. Per-step column counters make distributed loading visible. Loaded/archive values on these rows describe the world after the final return, not each step.
* `disk_reopen_reconcile`: checkpoint both square regions, including a distinctive marker in the incoming edge; evict that edge. For every sample, reopen a pristine `World` and load the starting square outside timing, then time the request update and reconciliation into the checkpointed edge. `disk_reopen_light_observe` separately times the following light query and observation. Reopening clears the world's in-process archive, but does not flush OS or SQLite caches; this is not a guaranteed physical cold read. The marker is checked after loading.
* `disk_same_world_reconcile` and `disk_same_world_light_observe`: repeatedly evict the incoming edge and checkpoint **outside** timing, then time the same reload and subsequent light/observation separately in one open world. Checkpoint makes evicted archives eligible for the bounded warm cache; eviction, cache capacity and item content can prevent retention. There are no cache hit or database I/O counters, so neither hits nor physical reads are asserted. A unique temporary directory owns the database and SQLite sidecars and is removed after the world closes on normal completion; abnormal termination can leave it behind.

Counts are totals across samples, except loaded/archived which are final chunk counts (including all vertical layers). `columns_loaded`/`columns_unloaded` count horizontal columns, while the lighting fields are the instrumented output/sky chunk and source-patch work counters. View counters are server totals. The `radius` CSV field always identifies the requested **square chunk radius**, even on circle rows; the circle tile radius is printed in the run metadata. The server constructor uses its production policy before applying the requested policy, so setup may populate the archive. Measurements include ordinary OS scheduling noise; use the same configuration, content, sample count and machine for comparisons.

## Build and run

From the project root with the configured MSVC debug preset:

```powershell
cmake --build --preset win-msvc-debug --target cataclysmx_chunk_benchmark
.\out\build\win-msvc-debug\cataclysmx_chunk_benchmark.exe 10 5 all
.\out\build\win-msvc-debug\cataclysmx_chunk_benchmark.exe 10 2 circle 30
.\out\build\win-msvc-debug\cataclysmx_chunk_benchmark.exe 10 5 disk
```

The historical executable and results below refer to the earlier baseline source and engine artifact; do not substitute the current executable and assume identical scenarios or counters.

## Preserved MSVC debug baseline, 2026-11-10

Before production edits, `git status --short` was empty at commit `565c136` and `ninja -C out/build/win-msvc-debug -n cataclysmx_engine` reported no work. The existing engine library was copied to `C:\Users\RELAXE~1\AppData\Local\Temp\opencode\chunk-baseline-engine.lib` without building shared targets. Ninja freshness and a clean checkout support, but do not prove, artifact provenance. The copied library is a debug build, not a release performance estimate.

From PowerShell, the exact standalone compile/link command (the working directory is the approved temp directory) was:

```powershell
cmd /c 'call "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 >nul && cl /nologo /std:c++latest /MDd /Od /RTC1 /EHs-c- /D_HAS_EXCEPTIONS=0 /DEASTL_EXCEPTIONS_ENABLED=0 /DCATACLYSMX_TERRAIN_FILE=\"C:/Work/Gamedev/CataclysmX/data/terrain.json5\" /I"C:\Work\Gamedev\CataclysmX\src" /I"C:\Work\Gamedev\CataclysmX\out\build\win-msvc-debug\vcpkg_installed\x64-windows-cataclysmx\include" "C:\Work\Gamedev\CataclysmX\tools\chunk_streaming_benchmark.cpp" "C:\Users\RELAXE~1\AppData\Local\Temp\opencode\chunk-baseline-engine.lib" /link /out:chunk-benchmark.exe /libpath:"C:\Work\Gamedev\CataclysmX\out\build\win-msvc-debug\vcpkg_installed\x64-windows-cataclysmx\debug\lib" EASTL.lib spdlogd.lib fmtd.lib sqlite3.lib yyjson.lib'
```

Runs from the project root:

```powershell
$env:PATH = "C:\Work\Gamedev\CataclysmX\out\build\win-msvc-debug;$env:PATH"
& "C:\Users\RELAXE~1\AppData\Local\Temp\opencode\chunk-benchmark.exe" 10 2 all
& "C:\Users\RELAXE~1\AppData\Local\Temp\opencode\chunk-benchmark.exe" 10 5 all
```

| Case | Radius 2 median / max ms | Radius 5 median / max ms |
|---|---:|---:|
| archive reconcile | 13.599 / 13.856 | 125.499 / 132.323 |
| archive light + observe | 6.156 / 6.295 | 30.789 / 31.744 |
| fresh reconcile | 11.757 / 13.144 | 112.289 / 115.422 |
| server X 15/16 | 24.723 / 26.491 | 159.686 / 169.049 |
| server X -1/0 | 25.668 / 28.006 | 160.904 / 189.431 |
| server Z 15/16 | 24.929 / 25.468 | 173.863 / 176.412 |
| server Z -1/0 | 24.649 / 25.388 | 172.008 / 180.433 |

With only ten samples, both p95 and p99 equal the maximum in every row. Each archive reconciliation accumulated 10 demand reconciliations and no lighting or observation rebuilds; its paired observation phase accumulated 10 each of lighting, observation, geometry and optical rebuilds. Each server row accumulated 20 demand reconciliations, 10 each of lighting and observation rebuilds, and 10 view/map constructions. Radius 2 ended with 525 resident chunks, radius 5 with 2541. Fresh runs ended with 1050 and 2310 archived chunks respectively; the warm split runs ended with 105 and 231.

## Integrated results, 2026-11-10

Both `win-msvc-debug` and `win-msvc-release` were rebuilt from the integrated
sources. Runs were sequential, without concurrent builds or test processes.
The original preserved Debug executable was also rerun at 100 samples. The same
bundled content, seed, route, sight policy, and radius-12 view were used.

### Border oscillation: 100 samples per row

All values below are **median / p99 / maximum milliseconds**.

| Route | Original Debug, square radius 5 | Integrated Debug, square radius 5 | Integrated Debug, circle 78 | Integrated Release, circle 78 |
|---|---:|---:|---:|---:|
| X 15/16 | 169.799 / 204.292 / 206.495 | 19.020 / 20.123 / 20.263 | 1.889 / 1.957 / 2.594 | 0.299 / 0.351 / 0.449 |
| X -1/0 | 170.376 / 195.340 / 216.750 | 19.382 / 20.366 / 20.485 | 2.066 / 2.539 / 2.553 | 0.283 / 0.320 / 0.344 |
| Z 15/16 | 172.599 / 209.209 / 218.466 | 19.459 / 20.581 / 20.883 | 2.279 / 2.367 / 2.398 | 0.340 / 0.359 / 0.375 |
| Z -1/0 | 164.786 / 197.803 / 204.953 | 19.441 / 22.737 / 25.573 | 2.063 / 2.223 / 2.325 | 0.286 / 0.345 / 0.420 |

The square comparison isolates the storage/lighting improvements while retaining
the 121-column resident footprint. Each move still loads and unloads 11 columns,
but final lighting work is only 9–18 chunk outputs per move in this scenario.

The circle is a **policy change**, not an equal-area container comparison. At
these seams it has 88 resident columns and does not load/unload any columns;
only active demand changes. Its loading thresholds occur elsewhere within the
16-tile interval. The following route measures those thresholds explicitly.

### Circle forward route: 100 samples at each tile offset

Circle radius 78, active square radius 1. Columns are per-step counts, not
100-sample totals. Timing triples are **median / p99 / maximum milliseconds**.
All 16 forward offsets were measured; returning and warmup were outside timing.

| Destination X | Columns loaded / unloaded | Debug | Release |
|---|---:|---:|---:|
| 1 | 0 / 0 | 1.780 / 1.887 / 2.182 | 0.250 / 0.286 / 0.308 |
| 2 | 1 / 0 | 7.274 / 8.094 / 8.523 | 0.787 / 0.832 / 1.128 |
| 3 | 2 / 0 | 7.827 / 8.689 / 9.207 | 0.877 / 0.956 / 1.056 |
| 4 | 4 / 0 | 9.287 / 10.153 / 11.717 | 1.067 / 1.138 / 1.161 |
| 5 | 1 / 0 | 7.797 / 9.454 / 10.101 | 0.830 / 0.892 / 1.682 |
| 6 | 0 / 1 | 7.917 / 9.797 / 10.072 | 0.904 / 1.171 / 1.478 |
| 7 | 0 / 1 | 7.906 / 8.486 / 9.322 | 0.903 / 0.982 / 1.143 |
| 8 | 0 / 0 | 1.900 / 2.000 / 2.332 | 0.271 / 0.309 / 0.337 |
| 9 | 1 / 0 | 7.848 / 8.697 / 9.501 | 0.820 / 0.934 / 0.956 |
| 10 | 1 / 0 | 7.836 / 8.687 / 8.831 | 0.815 / 0.854 / 0.855 |
| 11 | 0 / 1 | 7.271 / 7.953 / 9.596 | 0.820 / 0.872 / 0.909 |
| 12 | 0 / 4 | 7.587 / 8.148 / 8.199 | 1.058 / 1.105 / 1.141 |
| 13 | 0 / 2 | 7.100 / 7.585 / 7.609 | 0.890 / 0.951 / 0.962 |
| 14 | 0 / 1 | 6.891 / 7.248 / 7.882 | 0.803 / 0.825 / 0.855 |
| 15 | 0 / 0 | 1.412 / 1.565 / 1.656 | 0.222 / 0.236 / 0.265 |
| 16 | 0 / 0 | 2.040 / 2.378 / 2.506 | 0.349 / 0.386 / 0.391 |

Each layer remains available to arbitrary light consumers. Lazy output counts
range from zero to 28 chunks per step here; sky dependencies can compute other
layers without materializing their final light output. These scenarios remove
the demo lamps, so their local-source patch count is zero. Source and incident
face correctness is covered separately by the lighting transport regressions.

### Split phases and database-backed runs: 10 samples

Values are medians in milliseconds; use the 100-sample route above for tail
observations. First-time generation advances the request through ten chunk
positions, loading a new edge strip at each step; it is distinct from the warm circle route.

| Case | Debug radius 2 | Release radius 2 | Debug radius 5 | Release radius 5 |
|---|---:|---:|---:|---:|
| Archive reconciliation | 3.698 | 0.429 | 16.948 | 1.948 |
| Following light/observation | 3.128 | 0.510 | 11.886 | 2.235 |
| Fresh reconciliation | 2.151 | 0.453 | 5.780 | 1.257 |
| Reopened-world disk reconciliation | 15.837 | 3.478 | 36.326 | 8.043 |
| Same-world post-checkpoint reconciliation | 1.756 | 0.333 | 7.526 | 1.065 |

The reopened world also starts with cold derived lighting, while the same-world
case can retain it. Their separate observation times therefore are not an
isolated measure of archive caching. Disk timings include SQLite reads/decoding
but do not flush operating-system caches. The optional warm cache currently
admits item-free snapshots only; authoritative unsaved archives remain separate.

At square radius 2, the integrated end-to-end Debug seam medians were
7.380–7.729 ms (Release 1.387–1.530 ms). The radius-30 circle was also exercised
at all seams and all 16 offsets in both builds. Its sight is intentionally
residency-clipped. Radius-2 server setup now archives fewer initial columns than
the old constructor, so that comparison includes the changed initialization
history; the standalone World split cases avoid that difference.

### Reproduction and verification

```powershell
# Developer command prompt / imported MSVC environment:
cmake --build --preset win-msvc-debug --target cataclysmx_tests cataclysmx_headless cataclysmx_game cataclysmx_chunk_benchmark -j 6
cmake --build --preset win-msvc-release --target cataclysmx_tests cataclysmx_headless cataclysmx_game cataclysmx_chunk_benchmark -j 6
ctest --preset win-msvc-debug --output-on-failure -j 4
ctest --preset win-msvc-release --output-on-failure -j 4

.\out\build\win-msvc-debug\cataclysmx_chunk_benchmark.exe 100 5 server
.\out\build\win-msvc-debug\cataclysmx_chunk_benchmark.exe 100 5 circle
.\out\build\win-msvc-release\cataclysmx_chunk_benchmark.exe 100 5 circle
.\out\build\win-msvc-debug\cataclysmx_chunk_benchmark.exe 10 5 all
.\out\build\win-msvc-release\cataclysmx_chunk_benchmark.exe 10 5 all
.\out\build\win-msvc-debug\cataclysmx_chunk_benchmark.exe 10 2 all 30
.\out\build\win-msvc-release\cataclysmx_chunk_benchmark.exe 10 2 all 30
```

Validation performed: focused Debug `[world],[server],[lighting],[save],[settings]`
passed 275 cases / 3,623,798 assertions before the final boundary regressions.
Full Debug CTest initially passed 525/527. The two failures were test expectations:
the streaming script's initial archive count changed with the new default shape,
and the new parked-guest test used a square radius that actually fit the positive
world edge. After correcting those expectations, both passed in a targeted rerun.
Thus all 527 Debug tests passed across the full run and rerun. Full Release CTest
passed 527/527 in one run, including headless/LAN/settings integration tests.

These measurements describe this machine and these scenes, not a universal frame
budget or a guarantee for populated worlds, teleports, or cold physical storage.
