# Running tests

After configuring and building a preset, run tests from the project root:

```sh
bash scripts/test.sh linux-clang-debug -R 'pattern'
```

```powershell
./scripts/test.ps1 -Preset win-msvc-debug -R 'pattern'
```

The first argument selects a CTest preset; remaining arguments are passed to CTest, including filters such as `-R`, `-E`, and `-L`. The build scripts use the same runner. Run from the directory containing the preset files. CTest's exit status is returned to the caller, and the preset controls how an empty selection is treated. A successful invocation without a test-run summary is reported separately, for example when using `-N`.

Each invocation writes a separate directory under `out/test-results/` in the current directory, containing `ctest.log` (complete CTest stdout), `ctest.err`, and, when CTest produces it, `results.xml` (JUnit). The runner enables failure output and removes CTest's per-failed-test output limit. A successful run prints one summary; a failure prints bounded excerpts from the beginning and end of stdout and the end of stderr plus the full log and report paths. Excerpts share an approximately 12 KiB console budget, so a large failure list or diagnostic may be truncated. The full files retain details. CTest progress is captured rather than streamed to the console. Options that alter execution or output, such as `--stop-on-failure`, `--quiet`, or alternate report destinations, can change these guarantees and should be used deliberately.

For perception correctness coverage and targeted tags, see [bulk perception](bulk_perception.md)
and [surface lighting](surface_lighting.md). The [perception benchmark matrix](perception_benchmarks.md)
documents reproducible Debug/Release scenarios, counters, and timing methodology;
integrated results include both full 462-test presets and the six-run timing matrix. Historical measurements are labeled in the
[baseline](perception_benchmark_baseline.md) and [perception guide](perception.md#historical-performance-check).
Benchmarks measure outside simulation and do not impose hardware-sensitive test thresholds.
