#!/usr/bin/env python3
"""Capture a gi-test smoke window to a BMP for the GI bindless step-4 A/B.

GI twin of ../caustic_bindless_step4/capture.py: the exe basename picks the HW producer
(gi_test_smoke) or the forced-software-emulation sibling (gi_test_sw_smoke), so one script captures
both sides of the surfel-GI bindless-heap accessor migration. The GI test scene is static (an
open-top box with a red +X wall and a blue -X wall under a fixed directional light -> indirect
red/blue bleed onto the shadowed floor), so before/after captures line up frame-for-frame; the DDGI
probe field is left to converge across the settle window before the read. Two identical runs still
differ by a small temporal-blend (EMA) floor concentrated in the bounced-lighting features.

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
RUNTIME = REPO / "__cmake/build/linux-clang-x64/Testing/smoke_runtime/opt"
TITLE = "NWB GI Test"
# DDGI probes converge over a temporal blend, so give the field time to settle before the read;
# two identical runs still differ by a small EMA floor in the bounced-lighting features.
SETTLE = float(os.environ.get("GI_SETTLE", "6.0"))


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
            print(f"app launched pid={app.pid} exe={exe}")

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
