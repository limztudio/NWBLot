#!/usr/bin/env python3
"""Shared smoke-window capture runner for the manual A/B capture scripts.

Boots the engine logserver, launches a smoke binary, waits for its X11 window to appear,
settles for a configured duration (letting temporal accumulators / probe fields converge),
captures the window to a BMP, and tears both processes down. The per-A/B differences --
binary to run, window title, runtime path, settle window, and any frozen spin/yaw env var --
are supplied by each directory's capture.py wrapper.

These scripts run on the KDE-Wayland host under Xwayland; note the documented unattended-
capture stall (a backgrounded/timeout-killed smoke binary is not the active Wayland surface,
so KWin stops sending it frame callbacks and the vsync-locked present loop blocks forever).
Run captures interactively (foreground) on this host, not unattended.
"""

import os
import subprocess
import sys
import time
from pathlib import Path

# This module lives at tests/ab/, so parents[2] is the repo root.
REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "tests/ab"))
sys.path.insert(0, str(REPO / "tests/smoke"))
from process_helpers import free_port, stop_process, wait_port  # noqa: E402
from testbed_window_capture_smoke import LinuxX11Capture  # noqa: E402

DOMAIN = REPO / "__exec/linux/x64/full/opt"
DEFAULT_LOGSERVER = DOMAIN / "logserver"


def capture_smoke_window(
    *,
    runtime,
    title,
    settle,
    output_bmp,
    exe=None,
    binary=None,
    extra_env=None,
    logserver=DEFAULT_LOGSERVER,
    launch_label=None,
):
    """Capture a smoke binary's window to *output_bmp* (a resolved Path).

    Exactly one of *exe* (a bare basename resolved under DOMAIN) or *binary* (an absolute
    Path) selects the launched smoke executable. *runtime* is the working directory the
    app and logserver run in; *title* is the window title to match; *settle* is the seconds
    to wait before the read. *extra_env* pins any frozen spin/yaw env vars so before/after
    captures line up. *launch_label* extends the "app launched" log line.
    """
    if bool(exe) == bool(binary):
        raise ValueError("capture_smoke_window: pass exactly one of exe= or binary=")
    app_path = binary if binary else (DOMAIN / exe)

    environment = os.environ.copy()
    environment.setdefault("DISPLAY", ":0")
    environment["NWB_LINUX_BACKEND"] = "x11"
    environment["NWB_RENDER_UNFOCUSED"] = "1"
    if extra_env:
        environment.update(extra_env)

    output_bmp.parent.mkdir(parents=True, exist_ok=True)

    logserver_proc = app = capture = None
    with (runtime / "shot_logserver.log").open("wb") as logserver_log, (runtime / "shot_app.log").open("wb") as app_log:
        try:
            port = free_port()
            logserver_proc = subprocess.Popen(
                [str(logserver), "-p", str(port)],
                cwd=runtime, env=environment, stdout=logserver_log, stderr=subprocess.STDOUT,
            )
            if not wait_port(port):
                raise RuntimeError("logserver port not ready")
            print(f"logserver up on {port}")

            app = subprocess.Popen(
                [str(app_path), "-a", "http://localhost", "-p", str(port)],
                cwd=runtime, env=environment, stdout=app_log, stderr=subprocess.STDOUT,
            )
            if launch_label:
                print(f"app launched pid={app.pid} {launch_label}")
            else:
                print(f"app launched pid={app.pid}")

            capture = LinuxX11Capture()
            print("waiting for window...")
            window = capture.wait_for_window(app.pid, 30.0, title)
            if not window:
                raise RuntimeError("window not found")

            print(f"window 0x{window:x}; settling {settle}s...")
            time.sleep(settle)
            capture.capture_window(window, output_bmp)
            if not output_bmp.is_file():
                raise RuntimeError(f"capture completed without output: {output_bmp}")
            print(f"captured -> {output_bmp} ({output_bmp.stat().st_size} bytes)")
        finally:
            if capture:
                capture.close()
            stop_process(app, 2.0)
            stop_process(logserver_proc, 1.0)
