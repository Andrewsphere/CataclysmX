#!/usr/bin/env bash
set -euo pipefail
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"
preset="${1:-linux-clang-debug}"
case "$preset" in
    linux-clang-debug|linux-clang-sanitizers|linux-gcc-debug) ;;
    *) printf 'Unknown Linux preset: %s\n' "$preset" >&2; exit 1 ;;
esac
export VCPKG_ROOT="${VCPKG_ROOT:-$root/.tools/vcpkg}"
if [[ ! -d "$VCPKG_ROOT/.git" ]]; then
    if [[ -e "$VCPKG_ROOT" ]]; then
        printf 'VCPKG_ROOT exists but is not a git checkout: %s\n' "$VCPKG_ROOT" >&2
        exit 1
    fi
    mkdir -p "$(dirname -- "$VCPKG_ROOT")"
    git clone https://github.com/microsoft/vcpkg.git "$VCPKG_ROOT"
    baseline="$(sed -n 's/.*"builtin-baseline": "\([a-f0-9]*\)".*/\1/p' vcpkg.json)"
    git -C "$VCPKG_ROOT" checkout --detach "$baseline"
fi
if [[ ! -x "$VCPKG_ROOT/vcpkg" ]]; then
    "$VCPKG_ROOT/bootstrap-vcpkg.sh" -disableMetrics
fi
cmake --preset "$preset"
cmake --build --preset "$preset"
cmake -E copy_if_different "out/build/$preset/compile_commands.json" compile_commands.json
ctest --preset "$preset"
