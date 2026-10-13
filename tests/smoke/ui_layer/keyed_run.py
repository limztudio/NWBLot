"""Common native-input and coordinate state for keyed list UI smoke runs."""
from __future__ import annotations

import time

from probe_common import center
from window_capture_smoke import collect_log_delta


class KeyedRun:
    def __init__(self, args, backend, handle, process, log_directory, log_baseline, log_pattern, *, native_type):
        self.args = args
        self.backend = backend
        self.handle = handle
        self.process = process
        self.log_directory = log_directory
        self.log_baseline = log_baseline
        self.log_pattern = log_pattern
        self.native = native_type(backend, handle)
        self.deadline = time.monotonic() + args.timeout
        self.snapshot = None
        self.stages = []

    def logs(self):
        return collect_log_delta(self.log_directory, self.log_baseline, self.log_pattern)

    def point(self, x, y):
        scale_x, scale_y = self.snapshot["scale"]
        return round(x * scale_x), round(y * scale_y)

    def click(self, name):
        self.native.click(*self.point(*center(self.snapshot["rectangles"][name])))

    def row(self, offset):
        x, y, width, height = self.snapshot["rectangles"]["viewport"]
        return self.point(x + min(width / 2.0, 120.0), y + (offset + 0.5) * 24.0)
