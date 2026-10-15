"""Shared capture gates and process lifetime for passive image smoke fixtures."""
from __future__ import annotations

import platform
import time

from guarded_native import GuardedNativeInput
from fixture_environment import build_fixture_environment
from fixture_launch import admit_fixture_processes, prepare_fixture_launch
from window_capture_smoke import (STRICT_LOG_FAILURE_MESSAGES, SmokeFailure, SmokeSkip, build_launch_environment, collect_log_delta, create_capture_backend, ensure_process_running, launch_logserver, launch_testbed,
    read_bmp_24_rows, require_normal_process_exit, shutdown_logserver_and_collect, terminate_process, validate_expected_log_text, wait_for_log_message,)


class PassiveImageRun:
    def __init__(self, args, backend, handle, process, log_directory, log_baseline, log_pattern, *, minimum_height, snapshot_reader, observer, center, fixture_label="image"):
        self.args, self.backend, self.handle, self.process = args, backend, handle, process
        self.log_directory, self.log_baseline, self.log_pattern = log_directory, log_baseline, log_pattern
        self.native = GuardedNativeInput(backend, handle)
        self.deadline = time.monotonic() + args.timeout
        self.snapshot = None
        self.stages = []
        self.failure_report = None
        self.minimum_height = minimum_height
        self.snapshot_reader = snapshot_reader
        self.observer = observer
        self.center = center
        self.fixture_label = fixture_label

    def logs(self):
        return collect_log_delta(self.log_directory, self.log_baseline, self.log_pattern)

    def point(self, x, y):
        sx, sy = self.snapshot["scale"]
        return round(x * sx), round(y * sy)

    def click(self, name):
        rectangle = self.snapshot["rectangles"][name]
        if rectangle[2] <= 0.0 or rectangle[3] <= 0.0:
            raise SmokeFailure(f"{self.fixture_label} fixture has no displayed placement for '{name}'")
        self.native.click(*self.point(*self.center(rectangle)))

    def capture_checkpoint(self, name, *, extent, extra, stage_timeout, changes):
        self.expected.update(changes)
        self.native.pointer(4, 4)
        self.native.maintain_pointer()
        time.sleep(0.25)
        path = self.args.output_directory / f"{len(self.stages):02d}_{name}.bmp"
        stage_deadline = min(self.deadline, time.monotonic() + stage_timeout)
        report = None
        while time.monotonic() < stage_deadline:
            ensure_process_running(self.process, f"during {self.fixture_label} UI capture")
            self.native.maintain_pointer()
            snapshot = self.snapshot_reader(self.logs())
            if snapshot is None:
                time.sleep(0.1)
                continue
            if snapshot["logical_extent"][0] < 740.0 or snapshot["logical_extent"][1] < self.minimum_height:
                raise SmokeSkip(f"the {self.fixture_label} fixture needs a logical client of at least 740x{self.minimum_height:g}")
            if self.native.windows:
                self.backend.capture_prepared_raw_client_window(self.handle, path)
            else:
                self.backend.capture_client_window(self.handle, path)
            report = self.observer(read_bmp_24_rows(path), snapshot, self.expected, extent=extent, skin=self.args.skin, extra=extra)
            if report["passed"]:
                self.snapshot = snapshot
                report.update({"stage": name, "capture": str(path), "native_window": self.native.observe_window()})
                self.stages.append(report)
                self.write_report(False)
                return
            time.sleep(0.1)
        self.failure_report = {"stage": name, "capture": str(path), "native_window": self.native.observe_window(), "observation": report}
        self.write_report(False)
        raise SmokeFailure(f"{self.fixture_label} displayed-state gate '{name}' failed: {self.failure_report}")


def run_passive_image(args, *, fixture, display_marker, driver_type, log_name, label, fixture_label="image"):
    launch = prepare_fixture_launch(args, fixture, build_launch_environment=build_launch_environment, build_fixture_environment=build_fixture_environment, create_capture_backend=create_capture_backend)
    driver = None
    try:
        admit_fixture_processes(args, launch, launch_logserver=launch_logserver, launch_testbed=launch_testbed)
        if not launch.handle:
            ensure_process_running(launch.application, f"before the {fixture_label} fixture appeared")
            raise SmokeFailure(f"custom {fixture_label} fixture did not appear")
        if platform.system() == "Windows":
            launch.backend.prepare_raw_client_window(launch.handle)
        else:
            launch.backend.prepare_window(launch.handle)
        wait_for_log_message(launch.log_directory, launch.log_baseline, launch.log_pattern, display_marker, min(args.timeout, 15.0))
        driver = driver_type(args, launch.backend, launch.handle, launch.application, launch.log_directory, launch.log_baseline, launch.log_pattern)
        driver.execute()
    finally:
        try:
            if launch.application is not None:
                exit_code, tail = terminate_process(launch.application, f"UI {fixture_label} fixture", launch.handle)
                require_normal_process_exit(exit_code, tail, f"UI {fixture_label} fixture")
        finally:
            try:
                text = ""
                if launch.log_directory is not None:
                    text = shutdown_logserver_and_collect(launch.logserver, launch.log_directory, launch.log_baseline, launch.log_pattern)
                    launch.logserver = None
                    (args.output_directory / log_name).write_text(text, encoding="utf-8")
            finally:
                try:
                    if launch.logserver is not None:
                        terminate_process(launch.logserver, f"UI {fixture_label} logserver")
                finally:
                    try:
                        if driver is not None:
                            driver.native.restore_pointer()
                    finally:
                        launch.backend.close()
    validate_expected_log_text(text, ["Loader: project startup complete", "UiLayerSmokeProject: shutdown"], STRICT_LOG_FAILURE_MESSAGES)
    driver.write_report(True)
    print(f"{label}: {len(driver.stages)} matching native/GPU gates with {args.skin} atlas", flush=True)
    return 0

