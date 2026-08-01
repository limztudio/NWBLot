#!/usr/bin/env bash
# Retry the HW-hybrid after-capture until a valid frame lands or attempts exhaust.
# The RADV context-loss is intermittent + "innocent" (environmental), so retrying works.
# Each failure can orphan logserver/app (Xlib hard-exit skips capture.py's finally),
# so we reap by exact PID between attempts.
set -u
cd "$(dirname "$0")"
OUT=after_hw.bmp
MAX=6

reap() {
  for pat in soft_shadow_test_smoke logserver; do
    for pid in $(pgrep -f "$pat" 2>/dev/null); do
      kill "$pid" 2>/dev/null && echo "  reaped $pid ($pat)"
    done
  done
}

reap
for i in $(seq 1 $MAX); do
  echo "=== attempt $i/$MAX ==="
  rm -f "$OUT"
  python3 -u capture.py soft_shadow_test_smoke "$OUT" 2>&1 | sed 's/^/  /'
  if [ -f "$OUT" ] && [ "$(stat -c%s "$OUT")" -gt 100000 ]; then
    echo "=== SUCCESS on attempt $i: $OUT ($(stat -c%s "$OUT") bytes) ==="
    reap
    exit 0
  fi
  echo "  attempt $i produced no valid frame; reaping orphans"
  reap
  sleep 2
done
echo "=== EXHAUSTED $MAX attempts without a valid frame ==="
exit 1
