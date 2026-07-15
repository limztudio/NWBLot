#!/usr/bin/env python3
"""Capture a screenshot of the 'NWB Stress Test Smoke' window for SPP2.

Launches the namesym-domain stress_test_smoke (READABLE names) + namesym logserver against
the benchmark runtime res, waits for the window to appear, settles, and uses the repo's own
ctypes LinuxX11Capture (testbed_window_capture_smoke.py) to grab it to a BMP.
"""
import os, sys, time, socket, subprocess, ctypes
from pathlib import Path

REPO = Path("/home/limstudio/WorkStation/NWBLot")
sys.path.insert(0, str(REPO / "tests" / "smoke"))
from testbed_window_capture_smoke import LinuxX11Capture  # noqa: E402

BIN = REPO / "__exec/linux/x64/namesym/opt/stress_test_smoke"
LOGSERVER = REPO / "__exec/linux/x64/namesym/opt/logserver"
RT = REPO / "__cmake/build/linux-clang-x64/Testing/skinning_culling_benchmark_runtime/opt"
TITLE = "NWB Stress Test Smoke"
SETTLE = float(os.environ.get("SPP_SETTLE", "3.0"))
OUT_BMP = Path(sys.argv[1] if len(sys.argv) > 1 else "/tmp/spp2.bmp")


def free_port():
    s = socket.socket(); s.bind(("localhost", 0)); p = s.getsockname()[1]; s.close(); return p


def wait_port(port, timeout=10.0):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            c = socket.create_connection(("localhost", port), timeout=0.5); c.close(); return True
        except OSError:
            time.sleep(0.15)
    return False


env = os.environ.copy()
env["DISPLAY"] = ":0"
env["NWB_LINUX_BACKEND"] = "x11"
# Keep rendering while the X11 grabber holds focus away; settle needs real frames.
env["NWB_RENDER_UNFOCUSED"] = "1"

port = free_port()
ls = subprocess.Popen([str(LOGSERVER), "-p", str(port)], cwd=str(RT),
                      stdout=open(RT / "shot_logserver.log", "wb"), stderr=subprocess.STDOUT)
if not wait_port(port):
    print("logserver port not ready", file=sys.stderr); ls.kill(); sys.exit(1)
print(f"logserver up on {port}")

app = subprocess.Popen([str(BIN), "-a", "http://localhost", "-p", str(port)],
                       cwd=str(RT), stdout=open(RT / "shot_app.log", "wb"), stderr=subprocess.STDOUT)
print(f"app launched pid={app.pid}")

cap = LinuxX11Capture()
try:
    print("waiting for window...")
    window = cap.wait_for_window(app.pid, 30.0, TITLE)
    if not window:
        print("window not found", file=sys.stderr)
    else:
        print(f"window 0x{window:x}; settling {SETTLE}s for the spinning scene...")
        time.sleep(SETTLE)
        cap.capture_window(window, OUT_BMP.resolve())
        print(f"captured -> {OUT_BMP} ({OUT_BMP.stat().st_size} bytes)")
finally:
    cap.close()
    app.terminate()
    time.sleep(2)
    app.kill()
    ls.terminate()
    time.sleep(1)
    ls.kill()
