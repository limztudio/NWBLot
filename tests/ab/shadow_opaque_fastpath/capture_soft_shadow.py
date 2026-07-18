#!/usr/bin/env python3
"""Launch the HW soft-shadow-test scene, pin the yaw, and capture its window to a BMP.

Used for the opaque-shadow FORCE_OPAQUE fast-path A/B: the scene stands an OPAQUE and a
GLASS (transparent) body caster side by side on an opaque ground plane under three coloured
soft lights, so one capture exercises both the FORCE_OPAQUE fast path (opaque casters/ground)
and the transparent-skip path (the glass caster's coloured software shadow).
"""

import os
import socket
import subprocess
import sys
import time
from pathlib import Path


REPO = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO / "tests/smoke"))
from testbed_window_capture_smoke import LinuxX11Capture  # noqa: E402


DOMAIN = REPO / "__exec/linux/x64/full/opt"
BIN = DOMAIN / "soft_shadow_test_smoke"
LOGSERVER = DOMAIN / "logserver"
RUNTIME = REPO / "__cmake/build/linux-clang-x64/Testing/skinning_culling_benchmark_runtime/opt"
TITLE = "NWB Soft Shadow Test"
SETTLE = float(os.environ.get("SHADOW_SETTLE", "5.0"))
# Pin the caster yaw so before/after captures line up (the scene auto-spins otherwise).
FROZEN_YAW = os.environ.get("NWB_SOFT_SHADOW_TEST_SPIN_ANGLE", "0.6")


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
    output_bmp = Path(sys.argv[1] if len(sys.argv) == 2 else "/tmp/soft_shadow.bmp").resolve()
    output_bmp.parent.mkdir(parents=True, exist_ok=True)

    environment = os.environ.copy()
    environment.setdefault("DISPLAY", ":0")
    environment["NWB_LINUX_BACKEND"] = "x11"
    environment["NWB_RENDER_UNFOCUSED"] = "1"
    environment["NWB_SOFT_SHADOW_TEST_SPIN_ANGLE"] = FROZEN_YAW

    logserver = app = capture = None
    with (RUNTIME / "shot_logserver.log").open("wb") as logserver_log, (RUNTIME / "shot_app.log").open("wb") as app_log:
        try:
            port = free_port()
            logserver = subprocess.Popen(
                [str(LOGSERVER), "-p", str(port)],
                cwd=RUNTIME, env=environment, stdout=logserver_log, stderr=subprocess.STDOUT,
            )
            if not wait_port(port):
                raise RuntimeError("logserver port not ready")
            print(f"logserver up on {port}")

            app = subprocess.Popen(
                [str(BIN), "-a", "http://localhost", "-p", str(port)],
                cwd=RUNTIME, env=environment, stdout=app_log, stderr=subprocess.STDOUT,
            )
            print(f"app launched pid={app.pid} (yaw={FROZEN_YAW})")

            capture = LinuxX11Capture()
            print("waiting for window...")
            window = capture.wait_for_window(app.pid, 30.0, TITLE)
            if not window:
                raise RuntimeError("window not found")

            print(f"window 0x{window:x}; settling {SETTLE}s...")
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
