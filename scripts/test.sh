#!/usr/bin/env bash
set -u
if [[ $# -lt 1 ]]; then
    printf 'Usage: bash scripts/test.sh PRESET [CTEST_ARGS...]\n' >&2
    exit 2
fi
preset="$1"
shift
script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
mkdir -p out/test-results || exit 1
run_dir="$(mktemp -d "$(pwd)/out/test-results/run-XXXXXXXX")" || exit 1
ctest --preset "$preset" "$@" --output-on-failure --test-output-size-failed 0 \
    --output-junit "$run_dir/results.xml" >"$run_dir/ctest.log" 2>"$run_dir/ctest.err"
status=$?
cmake "-DRUN_DIR=$run_dir" "-DSTATUS=$status" -P "$script_dir/test-summary.cmake" || exit 1
exit "$status"
