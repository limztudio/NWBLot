#!/usr/bin/env bash
# Launch stress_test_smoke (namesym-domain opt) against the freshly-recooked namesym runtime.
# The shared launcher owns the namesym logserver, port selection, process cleanup, and bounded
# run; this wrapper only selects the runtime whose recook should be measured.
#
# Usage: capture.sh <duration_sec> <timing_file_out> [stdout_file]
set -euo pipefail

readonly SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
readonly REPO="$(cd -- "$SCRIPT_DIR/../../.." && pwd -P)"
readonly RUNTIME="$REPO/__cmake/build/linux-clang-namesym-x64/Testing/skinning_culling_benchmark_runtime/opt"

DURATION="${1:-90}"
TIMING_OUT_RAW="${2:-/tmp/timing.txt}"
STDOUT_OUT_RAW="${3:-/tmp/app_stdout.log}"

absolute_output_path(){
    local path="$1"
    local parent
    parent="$(dirname -- "$path")"
    mkdir -p -- "$parent"
    printf '%s/%s\n' "$(cd -- "$parent" && pwd -P)" "$(basename -- "$path")"
}

line_count(){
    local pattern="$1"
    if [[ ! -f "$TIMING_OUT" ]]; then
        printf '0\n'
        return
    fi

    grep -c -- "$pattern" "$TIMING_OUT" || true
}

readonly TIMING_OUT="$(absolute_output_path "$TIMING_OUT_RAW")"
readonly STDOUT_OUT="$(absolute_output_path "$STDOUT_OUT_RAW")"
rm -f -- "$TIMING_OUT" "$STDOUT_OUT"

env \
    DISPLAY="${DISPLAY:-:0}" \
    NWB_LINUX_BACKEND=x11 \
    NWB_RENDER_UNFOCUSED=1 \
    NWB_GPU_TIMING_FILE="$TIMING_OUT" \
    python3 "$REPO/launcher.py" smoke stress-test \
        --backend hw \
        --domain namesym \
        --config opt \
        --configure never \
        --skip-build \
        --working-directory "$RUNTIME" \
        --with-profile \
        --run-seconds "$DURATION" \
    > "$STDOUT_OUT" 2>&1

echo "done. timing intervals: $(line_count '=== interval')"
echo "meshlet_bounds lines: $(line_count 'mesh_skinning.meshlet_bounds')"
echo "render.frame lines: $(line_count 'render.frame')"
