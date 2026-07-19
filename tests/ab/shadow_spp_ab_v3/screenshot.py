#!/usr/bin/env python3
"""Capture the stress-test window after cooking a software-shadow SPP variant."""

import os
import subprocess
import sys
import time
from pathlib import Path


REPO = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO / "tests/ab"))
sys.path.insert(0, str(REPO / "tests/smoke"))
from process_helpers import free_port, stop_process, wait_port  # noqa: E402
from testbed_window_capture_smoke import LinuxX11Capture  # noqa: E402


BIN = REPO / "__exec/linux/x64/namesym/opt/stress_test_smoke"
LOGSERVER = REPO / "__exec/linux/x64/namesym/opt/logserver"
RUNTIME = REPO / "__cmake/build/linux-clang-x64/Testing/skinning_culling_benchmark_runtime/opt"
TITLE = "NWB Stress Test Smoke"
SETTLE = float(os.environ.get("SPP_SETTLE", "3.0"))


def main():
    if len(sys.argv) > 2:
        raise SystemExit(f"usage: {sys.argv[0]} [output.bmp]")

    output_bmp = Path(sys.argv[1] if len(sys.argv) == 2 else "/tmp/spp2.bmp").resolve()
    output_bmp.parent.mkdir(parents=True, exist_ok=True)

    os.environ.setdefault("DISPLAY", ":0")
    environment = os.environ.copy()
    environment["NWB_LINUX_BACKEND"] = "x11"
    environment["NWB_RENDER_UNFOCUSED"] = "1"

    logserver = None
    app = None
    capture = None
    with (RUNTIME / "shot_logserver.log").open("wb") as logserver_log, (RUNTIME / "shot_app.log").open("wb") as app_log:
        try:
            port = free_port()
            logserver = subprocess.Popen(
                [str(LOGSERVER), "-p", str(port)],
                cwd=RUNTIME,
                env=environment,
                stdout=logserver_log,
                stderr=subprocess.STDOUT,
            )
            if not wait_port(port):
                raise RuntimeError("logserver port not ready")
            print(f"logserver up on {port}")

            app = subprocess.Popen(
                [str(BIN), "-a", "http://localhost", "-p", str(port)],
                cwd=RUNTIME,
                env=environment,
                stdout=app_log,
                stderr=subprocess.STDOUT,
            )
            print(f"app launched pid={app.pid}")

            capture = LinuxX11Capture()
            print("waiting for window...")
            window = capture.wait_for_window(app.pid, 30.0, TITLE)
            if not window:
                raise RuntimeError("window not found")

            print(f"window 0x{window:x}; settling {SETTLE}s for the spinning scene...")
            time.sleep(SETTLE)
            capture.capture_window(window, output_bmp)
            if not output_bmp.is_file():
                raise RuntimeError(f"capture completed without output: {output_bmp}")
            print(f"captured -> {output_bmp} ({output_bmp.stat().st_size} bytes)")
        finally:
            if capture:
                capture.close()
            stop_process(app, 2.0)
            stop_process(logserver, 1.0)


if __name__ == "__main__":
    main()
