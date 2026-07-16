#!/usr/bin/env bash
set -euo pipefail

readonly SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
readonly REPO="$(cd -- "$SCRIPT_DIR/../.." && pwd -P)"
readonly BUILD_DIR="$REPO/__cmake/build/linux-clang-namesym-x64"

if [[ -x "$REPO/__cmake/tool-venv/bin/cmake" ]]; then
    readonly CMAKE_BIN="$REPO/__cmake/tool-venv/bin/cmake"
else
    readonly CMAKE_BIN="cmake"
fi

exec "$CMAKE_BIN" --build "$BUILD_DIR" --config opt --target nwb_stress_test_smoke
