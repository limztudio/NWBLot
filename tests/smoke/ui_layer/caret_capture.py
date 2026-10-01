"""Observe real X11 caret pixels before capturing a full-frame blink-on sample."""
from __future__ import annotations

from pathlib import Path
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from window_capture_smoke import SmokeFailure, ensure_process_running  # noqa: E402


def wait_for_caret_phase(backend, handle, process, samples, deadline):
    off_at = None
    while time.monotonic() < deadline:
        ensure_process_running(process, "while observing caret phase")
        visible = True
        for column, first_row, end_row, color, tolerance in samples:
            rows = backend.sample_client_pixels(handle, column, first_row, 1, end_row - first_row)
            visible &= any(max(abs(actual - expected) for actual, expected in zip(row[0], color)) <= tolerance
                for row in rows)
        now = time.monotonic()
        if not visible:
            off_at = now
        elif off_at is not None and now - off_at < 0.15:
            return
        time.sleep(0.025)
    raise SmokeFailure("caret did not enter its visible phase before the displayed-state deadline")
