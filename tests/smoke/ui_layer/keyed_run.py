"""Common native-input state and displayed capture gates for keyed list UI smoke runs."""
from __future__ import annotations

import time

from probe_common import center
from fixture_environment import build_fixture_environment
from fixture_launch import admit_fixture_processes, prepare_fixture_launch
from window_capture_smoke import (STRICT_LOG_FAILURE_MESSAGES, SmokeFailure, SmokeSkip, build_launch_environment, collect_log_delta, create_capture_backend, ensure_process_running, launch_logserver, launch_testbed,
    read_bmp_24_rows, require_normal_process_exit, shutdown_logserver_and_collect, terminate_process, validate_expected_log_text, wait_for_log_message)


class KeyedRun:
    def __init__(self, args, backend, handle, process, log_directory, log_baseline, log_pattern, *, native_type, snapshot_reader, observer, minimum_height, fixture, capture_stage):
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
        self.snapshot_reader = snapshot_reader
        self.observer = observer
        self.minimum_height = minimum_height
        self.fixture = fixture
        self.capture_stage = capture_stage

    def logs(self):
        return collect_log_delta(self.log_directory, self.log_baseline, self.log_pattern)

    def point(self, x, y):
        scale_x, scale_y = self.snapshot["scale"]
        return round(x * scale_x), round(y * scale_y)

    def click(self, name):
        self.native.click(*self.point(*center(self.snapshot["rectangles"][name])))

    def row(self, offset):
        x, y, width, height = self.snapshot["rectangles"]["first_row"]
        vx, vy, vw, vh = self.snapshot["rectangles"]["viewport"]
        left, right = max(vx, x), min(vx + vw, x + width)
        top, bottom = max(vy, y + offset * height), min(vy + vh, y + (offset + 1) * height)
        if height <= 0.0 or right <= left or not 0 <= offset < self.snapshot["past"] - self.snapshot["first"] or bottom <= top:
            raise SmokeFailure(f"{self.fixture} requested a row outside its accepted visible interval")
        return self.point(left + min((right - left) / 2.0, 120.0), (top + bottom) / 2.0)

    def checkpoint(self, name, *, extent=None, extra=None, dynamic=(), **changes):
        self.expected.update(changes)
        for field in dynamic:
            self.expected.pop(field, None)
        if self.snapshot is not None:
            self.native.pointer(*self.point(*center(self.snapshot["rectangles"]["counter"])))
        time.sleep(0.25)
        path = self.args.output_directory / f"{len(self.stages):02d}_{name}.bmp"
        stage_deadline = min(self.deadline, time.monotonic() + 10.0)
        report = None
        while time.monotonic() < stage_deadline:
            ensure_process_running(self.process, self.capture_stage)
            snapshot = self.snapshot_reader(self.logs())
            if snapshot is None:
                time.sleep(0.1)
                continue
            if snapshot["logical_extent"][0] < 600.0 or snapshot["logical_extent"][1] < self.minimum_height:
                raise SmokeSkip(f"the {self.fixture} fixture needs a logical client of at least 600x{self.minimum_height:g}")
            self.backend.capture_client_window(self.handle, path)
            report = self.observer(read_bmp_24_rows(path), snapshot, self.expected, extent=extent, skin=self.args.skin, extra=extra)
            if report["passed"]:
                self.snapshot = snapshot
                for field in dynamic:
                    self.expected[field] = snapshot[field]
                report.update({"stage": name, "capture": str(path)})
                self.stages.append(report)
                self.write_report(False)
                return
            time.sleep(0.1)
        raise SmokeFailure(f"{self.fixture} displayed-state gate '{name}' failed: {report}")


def run_keyed_fixture(args, *, fixture, display_marker, driver_type, log_name, label, window_label, process_label):
    launch = prepare_fixture_launch(args, fixture, build_launch_environment=build_launch_environment, build_fixture_environment=build_fixture_environment, create_capture_backend=create_capture_backend)
    driver = None
    try:
        admit_fixture_processes(args, launch, launch_logserver=launch_logserver, launch_testbed=launch_testbed)
        if not launch.handle:
            ensure_process_running(launch.application, f"before the {window_label} fixture appeared")
            raise SmokeFailure(f"custom {process_label} fixture did not appear")
        launch.backend.prepare_window(launch.handle)
        wait_for_log_message(launch.log_directory, launch.log_baseline, launch.log_pattern, display_marker, min(args.timeout, 15.0))
        driver = driver_type(args, launch.backend, launch.handle, launch.application, launch.log_directory, launch.log_baseline, launch.log_pattern)
        driver.execute()
    finally:
        try:
            if launch.application is not None:
                exit_code, tail = terminate_process(launch.application, f"UI {process_label} fixture", launch.handle)
                require_normal_process_exit(exit_code, tail, f"UI {process_label} fixture")
        finally:
            try:
                text = ""
                if launch.log_directory is not None:
                    text = shutdown_logserver_and_collect(launch.logserver, launch.log_directory, launch.log_baseline, launch.log_pattern)
                    launch.logserver = None
                    (args.output_directory / log_name).write_text(text, encoding="utf-8")
            finally:
                if launch.logserver is not None:
                    terminate_process(launch.logserver, f"UI {process_label} logserver")
                launch.backend.close()
    validate_expected_log_text(text, ["Loader: project startup complete", "UiLayerSmokeProject: shutdown"], STRICT_LOG_FAILURE_MESSAGES)
    driver.write_report(True)
    print(f"{label}: {len(driver.stages)} matching native/GPU gates with {args.skin} atlas", flush=True)
    return 0

