#!/usr/bin/env python3
"""Domain-parametrized variant of stress_timing.py.

Runs <domain>/stress_test_smoke against the skinning_culling_benchmark runtime for
<duration> seconds, collecting per-pass GPU timings (NWB_GPU_TIMING_FILE) and the
FpsProbe whole-frame log via the logserver. Unlike stress_timing.py (hardcoded to
full/opt, hashed scope names) this takes the exec domain so the namesym/opt build can
be used, whose GPU-timing scope names are human-readable and lets the shadow pass be
attributed.

Usage: stress_timing_dom.py <domain_reldir> <timing_out> <logserver_out> <duration_sec>
  e.g. stress_timing_dom.py __exec/linux/x64/namesym/opt after_timing.txt after_ls.txt 30
"""

import os
import socket
import subprocess
import sys
import time
from pathlib import Path


REPO = Path(__file__).resolve().parents[3]
RUNTIME = REPO / "__cmake/build/linux-clang-x64/Testing/skinning_culling_benchmark_runtime/opt"


def free_port():
    with socket.socket() as sock:
        sock.bind(("localhost", 0))
        return sock.getsockname()[1]


def wait_port(port, timeout=10.0):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            with socket.create_connection(("localhost", port), timeout=0.5):
                return True
        except OSError:
            time.sleep(0.15)
    return False


def stop_process(process, timeout):
    if process is None or process.poll() is not None:
        return
    try:
        process.terminate()
        process.wait(timeout=timeout)
    except ProcessLookupError:
        return
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait()


def main():
    domain = REPO / sys.argv[1]
    timing_out = Path(sys.argv[2]).resolve()
    logserver_out = Path(sys.argv[3]).resolve()
    duration = float(sys.argv[4]) if len(sys.argv) > 4 else 30.0
    binary = domain / "stress_test_smoke"
    logserver_bin = domain / "logserver"
    for p in (binary, logserver_bin):
        if not p.is_file():
            raise SystemExit(f"missing: {p}")
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
                [str(logserver_bin), "-p", str(port)],
                cwd=RUNTIME, env=environment, stdout=logserver_log, stderr=subprocess.STDOUT,
            )
            if not wait_port(port):
                raise RuntimeError("logserver port not ready")
            app = subprocess.Popen(
                [str(binary), "-a", "http://localhost", "-p", str(port)],
                cwd=RUNTIME, env=environment, stdout=app_log, stderr=subprocess.STDOUT,
            )
            print(f"stress[{sys.argv[1]}] running pid={app.pid} for {duration}s...")
            time.sleep(duration)
            print("done")
        finally:
            stop_process(app, 3.0)
            stop_process(logserver, 2.0)


if __name__ == "__main__":
    main()
