#!/usr/bin/env python3
"""Run the full/opt stress_test_smoke scene for N seconds, collecting GPU-pass timings
(NWB_GPU_TIMING_FILE) and the FpsProbe whole-frame log (via the logserver).

Usage: stress_timing.py <timing_out> <logserver_out> <duration_sec>
"""

import os
import subprocess
import sys
import time
from pathlib import Path


REPO = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO / "tests/ab"))
DOMAIN = REPO / "__exec/linux/x64/full/opt"
BIN = DOMAIN / "stress_test_smoke"
LOGSERVER = DOMAIN / "logserver"
RUNTIME = REPO / "__cmake/build/linux-clang-x64/Testing/skinning_culling_benchmark_runtime/opt"

from process_helpers import free_port, stop_process, wait_port  # noqa: E402


def main():
    timing_out = Path(sys.argv[1]).resolve()
    logserver_out = Path(sys.argv[2]).resolve()
    duration = float(sys.argv[3]) if len(sys.argv) > 3 else 40.0
    for p in (timing_out, logserver_out):
        p.parent.mkdir(parents=True, exist_ok=True)
        if p.exists():
            p.unlink()

    environment = os.environ.copy()
    environment.setdefault("DISPLAY", ":0")
    environment["NWB_LINUX_BACKEND"] = "x11"
    environment["NWB_RENDER_UNFOCUSED"] = "1"
    environment["NWB_GPU_TIMING_FILE"] = str(timing_out)

    logserver = app = None
    with logserver_out.open("wb") as logserver_log, (RUNTIME / "stress_app.log").open("wb") as app_log:
        try:
            port = free_port()
            logserver = subprocess.Popen(
                [str(LOGSERVER), "-p", str(port)],
                cwd=RUNTIME, env=environment, stdout=logserver_log, stderr=subprocess.STDOUT,
            )
            if not wait_port(port):
                raise RuntimeError("logserver port not ready")
            app = subprocess.Popen(
                [str(BIN), "-a", "http://localhost", "-p", str(port)],
                cwd=RUNTIME, env=environment, stdout=app_log, stderr=subprocess.STDOUT,
            )
            print(f"stress running pid={app.pid} for {duration}s...")
            time.sleep(duration)
            print("done")
        finally:
            stop_process(app, 3.0)
            stop_process(logserver, 2.0)


if __name__ == "__main__":
    main()
