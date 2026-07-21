#!/usr/bin/env python3
"""Capture a soft-shadow-test smoke window to a BMP for the SW-shadow bindless step-4b A/B.

Parameterized twin of ../shadow_opaque_fastpath/capture_soft_shadow.py: the exe basename picks
the HW hybrid (soft_shadow_test_smoke) or the full-software (soft_shadow_test_sw_smoke) path, so
one script captures both sides of the bindless-heap accessor migration. The caster yaw is pinned
(NWB_SOFT_SHADOW_TEST_SPIN_ANGLE) so before/after captures line up pixel-for-pixel.

    python capture.py <exe-basename> <output.bmp>
"""

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


DOMAIN = REPO / "__exec/linux/x64/full/opt"
RUNTIME = REPO / "__cmake/build/linux-clang-x64/Testing/skinning_culling_benchmark_runtime/opt"
TITLE = "NWB Soft Shadow Test"
SETTLE = float(os.environ.get("SHADOW_SETTLE", "6.0"))
FROZEN_YAW = os.environ.get("NWB_SOFT_SHADOW_TEST_SPIN_ANGLE", "0.6")


def main():
    if len(sys.argv) != 3:
        print("usage: capture.py <exe-basename> <output.bmp>", file=sys.stderr)
        return 2
    exe = sys.argv[1]
    output_bmp = Path(sys.argv[2]).resolve()
    output_bmp.parent.mkdir(parents=True, exist_ok=True)
    binary = DOMAIN / exe

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
                [str(DOMAIN / "logserver"), "-p", str(port)],
                cwd=RUNTIME, env=environment, stdout=logserver_log, stderr=subprocess.STDOUT,
            )
            if not wait_port(port):
                raise RuntimeError("logserver port not ready")
            print(f"logserver up on {port}")

            app = subprocess.Popen(
                [str(binary), "-a", "http://localhost", "-p", str(port)],
                cwd=RUNTIME, env=environment, stdout=app_log, stderr=subprocess.STDOUT,
            )
            print(f"app launched pid={app.pid} exe={exe} yaw={FROZEN_YAW}")

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
    return 0


if __name__ == "__main__":
    sys.exit(main())
