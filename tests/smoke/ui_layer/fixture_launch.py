from __future__ import annotations

from dataclasses import dataclass
import json
import platform
import time

from checkpoint_report import CHECKPOINT_POLL_SECONDS, CHECKPOINT_STAGE_TIMEOUT_SECONDS
from fixture_environment import build_fixture_environment
from native_input import NativeInput
from probe_common import center
from window_capture_image_codec import Bmp24Codec
from window_capture_logs import wait_for_log_message
from window_capture_smoke import (STRICT_LOG_FAILURE_MESSAGES, SmokeFailure, SmokeSkip, build_launch_environment, collect_log_delta, create_capture_backend, ensure_process_running, launch_logserver, launch_testbed,
    require_normal_process_exit, shutdown_logserver_and_collect)
from window_capture_termination import CaptureProcessTermination, LIT_GRACEFUL_EXIT_TIMEOUT_SECONDS
from window_capture_validation import validate_expected_log_text

DISPLAY_MARKER_TIMEOUT_SECONDS = 15.0


@dataclass
class FixtureLaunch:
    environment: dict
    backend: object
    logserver: object | None = None
    application: object | None = None
    handle: object | None = None
    log_directory: object | None = None
    log_baseline: object | None = None
    log_pattern: str | None = None


def prepare_fixture_launch(args, fixture, *, build_launch_environment, build_fixture_environment, create_capture_backend, environment_kwargs=None, extra_env=None):
    if not args.executable.is_file():
        raise SmokeFailure(f"executable does not exist: {args.executable}")
    args.output_directory.mkdir(parents=True, exist_ok=True)
    options = {"force_x11": True}
    if environment_kwargs is not None:
        options = dict(environment_kwargs)
    else:
        options["skin"] = args.skin
    environment = build_fixture_environment(build_launch_environment(args), fixture, **options)
    if extra_env:
        environment.update(extra_env)
    return FixtureLaunch(environment, create_capture_backend())


def admit_fixture_processes(args, launch, *, launch_logserver, launch_testbed):
    launch.logserver, port, launch.log_directory, launch.log_baseline, launch.log_pattern = launch_logserver(args, args.executable, launch.environment)
    launch.application = launch_testbed(args, args.executable, launch.environment, port)
    launch.handle = launch.backend.wait_for_window(launch.application.pid, args.timeout, args.window_title)


class UiFixtureRunBase:
    def __init__(self, args, backend, handle, process, log_directory, log_baseline, log_pattern, *, native=None):
        self.args = args
        self.backend = backend
        self.handle = handle
        self.process = process
        self.log_directory = log_directory
        self.log_baseline = log_baseline
        self.log_pattern = log_pattern
        self.native = native if native is not None else NativeInput(backend, handle)
        self.deadline = time.monotonic() + args.timeout
        self.snapshot = None
        self.stages = []
        self.failure_report = None

    def logs(self):
        return collect_log_delta(self.log_directory, self.log_baseline, self.log_pattern)

    def point(self, x, y):
        scale_x, scale_y = self.snapshot["scale"]
        return round(x * scale_x), round(y * scale_y)

    def rectangle_point(self, name, *, error_label=None):
        rectangle = self.snapshot["rectangles"][name]
        if error_label and (rectangle[2] <= 0.0 or rectangle[3] <= 0.0):
            raise SmokeFailure(f"{error_label} '{name}'")
        return self.point(*center(rectangle))

    def click(self, name, *, error_label=None):
        self.native.click(*self.rectangle_point(name, error_label=error_label))

    def capture_window(self, path, *, raw_windows=False):
        if raw_windows and self.native.windows:
            self.backend.capture_prepared_raw_client_window(self.handle, path)
        else:
            self.backend.capture_client_window(self.handle, path)

    def write_json_report(self, filename, payload):
        (self.args.output_directory / filename).write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")

    def write_smoke_report(self, filename, passed, *, input_path, runtime_limit, include_skin=True, include_failure=False, **extras):
        report = {"passed": passed, "platform": platform.system()}
        if include_skin:
            report["skin"] = self.args.skin
        report["stages"] = self.stages
        if include_failure:
            report["failure"] = self.failure_report
        report.update({"input_path": input_path, "runtime_limit": runtime_limit})
        report.update(extras)
        self.write_json_report(filename, report)

    def poll_checkpoint(self, name, *, stage_timeout=CHECKPOINT_STAGE_TIMEOUT_SECONDS, minimum_extent, minimum_extent_message, process_stage, snapshot_reader, evaluate, raw_windows=False, failure_label,
            maintain_pointer=False, before_capture=None, accept_extras=None, dynamic=(), failure_extras=False):
        path = self.args.output_directory / f"{len(self.stages):02d}_{name}.bmp"
        stage_deadline = min(self.deadline, time.monotonic() + stage_timeout)
        report = None
        while time.monotonic() < stage_deadline:
            ensure_process_running(self.process, process_stage)
            if maintain_pointer:
                self.native.maintain_pointer()
            snapshot = snapshot_reader(self.logs())
            if snapshot is None:
                time.sleep(CHECKPOINT_POLL_SECONDS)
                continue
            if snapshot["logical_extent"][0] < minimum_extent[0] or snapshot["logical_extent"][1] < minimum_extent[1]:
                raise SmokeSkip(minimum_extent_message)
            if before_capture is not None and not before_capture(snapshot, report, stage_deadline):
                time.sleep(CHECKPOINT_POLL_SECONDS)
                continue
            self.capture_window(path, raw_windows=raw_windows)
            report = evaluate(Bmp24Codec.read_bmp_24_rows(path), snapshot)
            if report["passed"]:
                self.snapshot = snapshot
                for field in dynamic:
                    self.expected[field] = snapshot[field]
                metadata = {"stage": name, "capture": str(path)}
                if accept_extras is not None:
                    metadata.update(accept_extras(snapshot))
                report.update(metadata)
                self.stages.append(report)
                self.write_report(False)
                return report
            time.sleep(CHECKPOINT_POLL_SECONDS)
        if failure_extras:
            self.failure_report = {"stage": name, "capture": str(path), "native_window": self.native.observe_window(), "observation": report}
            self.write_report(False)
            raise SmokeFailure(f"{failure_label} displayed-state gate '{name}' failed: {self.failure_report}")
        raise SmokeFailure(f"{failure_label} displayed-state gate '{name}' failed: {report}")


def run_ui_fixture(args, *, fixture, display_marker, driver_type, log_name, label, window_label=None, process_label=None, missing_window_message=None, raw_windows=False, restore_pointer=False,
        graceful_exit_timeout=LIT_GRACEFUL_EXIT_TIMEOUT_SECONDS, environment_kwargs=None, extra_env=None, after_display=None, after_collect=None, summary_formatter=None):
    window_name = window_label or label
    process_name = process_label or label
    env_options = {"force_x11": True}
    if hasattr(args, "skin"):
        env_options["skin"] = args.skin
    if environment_kwargs:
        env_options.update(environment_kwargs)
    launch = prepare_fixture_launch(args, fixture, build_launch_environment=build_launch_environment, build_fixture_environment=build_fixture_environment, create_capture_backend=create_capture_backend,
        environment_kwargs=env_options, extra_env=extra_env)
    driver = None
    try:
        admit_fixture_processes(args, launch, launch_logserver=launch_logserver, launch_testbed=launch_testbed)
        if not launch.handle:
            ensure_process_running(launch.application, f"before the {window_name} fixture appeared")
            raise SmokeFailure(missing_window_message or f"custom {process_name} fixture did not appear")
        if raw_windows and platform.system() == "Windows":
            launch.backend.prepare_raw_client_window(launch.handle)
        else:
            launch.backend.prepare_window(launch.handle)
        wait_for_log_message(launch.log_directory, launch.log_baseline, launch.log_pattern, display_marker, min(args.timeout, DISPLAY_MARKER_TIMEOUT_SECONDS))
        if after_display is not None:
            after_display(launch)
        driver = driver_type(args, launch.backend, launch.handle, launch.application, launch.log_directory, launch.log_baseline, launch.log_pattern)
        if hasattr(driver, "on_after_display"):
            driver.on_after_display()
        driver.execute()
    finally:
        try:
            if launch.application is not None:
                exit_code, tail = CaptureProcessTermination.terminate_process(launch.application, f"UI {process_name} fixture", launch.handle, graceful_exit_timeout=graceful_exit_timeout)
                require_normal_process_exit(exit_code, tail, f"UI {process_name} fixture")
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
                        CaptureProcessTermination.terminate_process(launch.logserver, f"UI {process_name} logserver")
                finally:
                    try:
                        if restore_pointer and driver is not None:
                            driver.native.restore_pointer()
                    finally:
                        launch.backend.close()
    validate_expected_log_text(text, ["Loader: project startup complete", "UiLayerSmokeProject: shutdown"], STRICT_LOG_FAILURE_MESSAGES)
    if after_collect is not None:
        after_collect(text, driver)
    driver.write_report(True)
    if summary_formatter is not None:
        print(summary_formatter(driver), flush=True)
    else:
        skin = f" with {args.skin} atlas" if hasattr(args, "skin") else ""
        print(f"{label}: {len(driver.stages)} matching native/GPU gates{skin}", flush=True)
    return 0
