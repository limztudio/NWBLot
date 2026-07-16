#!/usr/bin/env bash
# Launch stress_test_smoke (namesym-domain opt) against the freshly-recooked namesym RT
# res, with the namesym logserver (name-symbol resolver) so GPU-timing scope names are
# READABLE. Direct-binary capture, mirrors tests/ab/shadow_spp_ab_v3/capture.sh but points
# RT at the namesym build's skinning_culling_benchmark_runtime (where the .vol recook of
# the just-flipped NWB_SKINNED_MESH_BOUNDS_USE_WAVE_REDUCE define lands).
#
# Usage: capture.sh <duration_sec> <timing_file_out> [stdout_file]
set -u
REPO=/home/limstudio/WorkStation/NWBLot
BIN="$REPO/__exec/linux/x64/namesym/opt/stress_test_smoke"
LOGSERVER="$REPO/__exec/linux/x64/namesym/opt/logserver"
RT="$REPO/__cmake/build/linux-clang-namesym-x64/Testing/skinning_culling_benchmark_runtime/opt"

DURATION="${1:-90}"
TIMING_OUT_RAW="${2:-/tmp/timing.txt}"
STDOUT_OUT_RAW="${3:-/tmp/app_stdout.log}"
TIMING_OUT="$(cd "$(dirname "$TIMING_OUT_RAW")" && pwd)/$(basename "$TIMING_OUT_RAW")"
STDOUT_OUT="$(cd "$(dirname "$STDOUT_OUT_RAW")" && pwd)/$(basename "$STDOUT_OUT_RAW")"
LSLOG="$RT/capture_logserver.log"

PORT=$(python3 -c "import socket; s=socket.socket(); s.bind(('localhost',0)); print(s.getsockname()[1]); s.close()")

export DISPLAY=:0
export NWB_LINUX_BACKEND=x11
export NWB_RENDER_UNFOCUSED=1
export NWB_GPU_TIMING_FILE="$TIMING_OUT"

rm -f "$TIMING_OUT" "$STDOUT_OUT" "$LSLOG"

( cd "$RT" && exec "$LOGSERVER" -p "$PORT" ) > "$LSLOG" 2>&1 &
LS_PID=$!

python3 - "$PORT" <<'PY'
import socket, sys, time
port=int(sys.argv[1])
deadline=time.monotonic()+10.0
while time.monotonic()<deadline:
    try:
        s=socket.create_connection(("localhost",port),timeout=0.5); s.close(); sys.exit(0)
    except OSError:
        time.sleep(0.15)
print("logserver port not ready"); sys.exit(1)
PY
if [ $? -ne 0 ]; then echo "FAILED: logserver did not open port"; kill -9 "$LS_PID" 2>/dev/null; exit 1; fi
echo "logserver up on port $PORT (pid $LS_PID)"

( cd "$RT" && exec "$BIN" -a http://localhost -p "$PORT" ) > "$STDOUT_OUT" 2>&1 &
APP_PID=$!
echo "app launched (pid $APP_PID), capturing ${DURATION}s..."

sleep "$DURATION"

kill "$APP_PID" 2>/dev/null
sleep 2
kill -9 "$APP_PID" 2>/dev/null
kill "$LS_PID" 2>/dev/null
sleep 1
kill -9 "$LS_PID" 2>/dev/null

echo "done. timing intervals:" $(grep -c "=== interval" "$TIMING_OUT" 2>/dev/null)
echo "meshlet_bounds lines:" $(grep -c "mesh_skinning.meshlet_bounds" "$TIMING_OUT" 2>/dev/null)
echo "render.frame lines:" $(grep -c "render.frame" "$TIMING_OUT" 2>/dev/null)
