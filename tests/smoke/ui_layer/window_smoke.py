#!/usr/bin/env python3
"""Exercise skinned window behavior through native input and displayed geometry."""
from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
import platform
import sys
import time

from interaction_smoke import NativeInput
from window_probe import ACTION, center, observe_window, snapshot_from_logs
from window_capture_smoke import (
    SKIP_EXIT_CODE, STRICT_LOG_FAILURE_MESSAGES, SmokeFailure, SmokeSkip, build_launch_environment,
    collect_log_delta, create_capture_backend, ensure_process_running, launch_logserver, launch_testbed,
    read_bmp_24_rows, require_normal_process_exit, shutdown_logserver_and_collect, terminate_process,
    validate_expected_log_text, wait_for_log_message,
)


def parse_args(argv):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--working-directory", type=Path, required=True)
    parser.add_argument("--output-directory", type=Path, required=True)
    parser.add_argument("--logserver-executable", type=Path)
    parser.add_argument("--skin", choices=("default", "alternate"), default="default")
    parser.add_argument("--timeout", type=float, default=90.0)
    parser.add_argument("--application-arg", action="append", default=[])
    args = parser.parse_args(argv)
    if not math.isfinite(args.timeout) or args.timeout <= 0.0:
        parser.error("--timeout must be finite and positive")
    args.executable = args.executable.resolve()
    args.working_directory = args.working_directory.resolve()
    args.output_directory = args.output_directory.resolve()
    args.window_title = "NWB UI Layer Smoke"
    args.no_logserver = False
    args.log_port = 0
    args.software_vulkan = "off"
    args.application_arg = ["--gpudbg", *args.application_arg]
    return args


class WindowRun:
    def __init__(self, args, backend, handle, process, log_directory, log_baseline, log_pattern):
        self.args = args
        self.backend = backend
        self.handle = handle
        self.process = process
        self.log_directory = log_directory
        self.log_baseline = log_baseline
        self.log_pattern = log_pattern
        self.native = NativeInput(backend, handle)
        self.deadline = time.monotonic() + args.timeout
        self.snapshot = None
        self.stages = []

    def logs(self):
        return collect_log_delta(self.log_directory, self.log_baseline, self.log_pattern)

    def point(self, x, y):
        scale_x, scale_y = self.snapshot["scale"]
        return round(x * scale_x), round(y * scale_y)

    def click(self, rectangle):
        self.native.click(*self.point(*center(self.snapshot["rectangles"][rectangle])))

    def title_point(self):
        x, y, width, height = self.snapshot["rectangles"]["title"]
        return x + width * 0.6, y + height / 2.0

    def quick_drag(self, origin, dx, dy, *, leave=False):
        start = self.point(*origin)
        finish = self.point(origin[0] + dx, origin[1] + dy)
        self.native.button(True, *start)
        if leave:
            self.native._post(0x02A3)  # WM_MOUSELEAVE must retain the active title gesture.
        self.native.pointer(*finish)
        self.native.button(False, *finish)
        # No sleep separates this complete native event batch. Release must retain the accumulated gesture.
        scale_x, scale_y = self.snapshot["scale"]
        return (finish[0] - start[0]) / scale_x, (finish[1] - start[1]) / scale_y

    def checkpoint(self, name, bounds, count, collapsed=False, locked=False, *, extent=None):
        time.sleep(0.25)
        path = self.args.output_directory / f"{len(self.stages):02d}_{name}.bmp"
        stage_deadline = min(self.deadline, time.monotonic() + 10.0)
        report = None
        while time.monotonic() < stage_deadline:
            ensure_process_running(self.process, "during window UI capture")
            snapshot = snapshot_from_logs(self.logs())
            if snapshot is None:
                time.sleep(0.1)
                continue
            if snapshot["logical_extent"][0] < 430.0 or snapshot["logical_extent"][1] < 270.0:
                raise SmokeSkip("the window fixture needs a logical client of at least 430x270 for separate model markers")
            self.backend.capture_client_window(self.handle, path)
            report = observe_window(read_bmp_24_rows(path), snapshot, bounds, count, collapsed, locked, extent=extent, skin=self.args.skin)
            if report["passed"]:
                self.snapshot = snapshot
                report.update({"stage": name, "capture": str(path)})
                self.stages.append(report)
                self.write_report(False)
                return
            time.sleep(0.1)
        raise SmokeFailure(f"window displayed-state gate '{name}' failed: {report}")

    def write_report(self, passed):
        report = {"passed": passed, "platform": platform.system(), "skin": self.args.skin, "stages": self.stages,
            "input_path": "Win32 posted button/motion/leave/capture-change/activation messages"
                if self.native.windows else "X11 XSendEvent and XSetInputFocus",
            "runtime_limit": "No physical pointer grab or native Wayland input qualification",
            "omitted_platform_stages": [] if self.native.windows else ["capture_change_cancel", "capture_change_recovery", "pointer_leave_drag"]}
        (self.args.output_directory / "window.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")

    def execute(self):
        bounds = (40.0, 40.0, 300.0, 180.0)
        self.checkpoint("initial", bounds, 0)
        if tuple(self.snapshot["minimum"]) != (180.0, 110.0):
            raise SmokeFailure(f"window fixture minimum differs from declared 180x110: {self.snapshot['minimum']}")
        self.click("button")
        self.checkpoint("first_button", bounds, 1)
        dx, dy = self.quick_drag(self.title_point(), 30.0, 10.0)
        bounds = (bounds[0] + dx, bounds[1] + dy, bounds[2], bounds[3])
        self.checkpoint("complete_title_drag", bounds, 1)
        dx, dy = self.quick_drag(center(self.snapshot["rectangles"]["resize"]), 40.0, 10.0)
        bounds = (bounds[0], bounds[1], bounds[2] + dx, bounds[3] + dy)
        self.checkpoint("complete_corner_resize", bounds, 1)
        self.quick_drag(center(self.snapshot["rectangles"]["resize"]), -240.0, -180.0)
        bounds = (bounds[0], bounds[1], 180.0, 110.0)
        self.checkpoint("minimum_resize", bounds, 1)
        self.click("collapse")
        self.checkpoint("collapsed", bounds, 1, collapsed=True)
        self.click("button")
        self.checkpoint("collapsed_body_inactive", bounds, 1, collapsed=True)
        self.click("collapse")
        self.checkpoint("expanded", bounds, 1)
        self.click("lock")
        self.checkpoint("locked", bounds, 1, locked=True)
        self.quick_drag(self.title_point(), 24.0, 12.0)
        self.checkpoint("locked_move_barrier", bounds, 1, locked=True)
        self.quick_drag(center(self.snapshot["rectangles"]["resize"]), 60.0, 40.0)
        self.checkpoint("locked_resize_barrier", bounds, 1, locked=True)
        self.click("lock")
        self.checkpoint("unlocked", bounds, 1)
        origin = self.title_point()
        self.native.button(True, *self.point(*origin))
        self.native.focus_loss_and_gain()
        self.native.button(False, *self.point(origin[0] + 25.0, origin[1] + 10.0))
        self.checkpoint("focus_loss_cancels_drag", bounds, 1)
        self.click("button")
        self.checkpoint("focus_loss_recovery_click", bounds, 2)
        count = 2
        if self.native.windows:
            origin = self.title_point()
            self.native.button(True, *self.point(*origin))
            self.native._post(0x0215)  # WM_CAPTURECHANGED through the native loss callback.
            self.native.button(False, *self.point(origin[0] + 25.0, origin[1] + 10.0))
            self.checkpoint("capture_change_cancel", bounds, count)
            self.click("button")
            count += 1
            self.checkpoint("capture_change_recovery", bounds, count)
            dx, dy = self.quick_drag(self.title_point(), 18.0, 8.0, leave=True)
            bounds = (bounds[0] + dx, bounds[1] + dy, bounds[2], bounds[3])
            self.checkpoint("pointer_leave_drag", bounds, count)
        self.backend.resize_client(self.handle, 800, 600)
        self.checkpoint("client_resized", bounds, count, extent=(800, 600))
        dx, dy = self.quick_drag(self.title_point(), 24.0, 16.0)
        bounds = (bounds[0] + dx, bounds[1] + dy, bounds[2], bounds[3])
        self.checkpoint("resized_first_drag", bounds, count, extent=(800, 600))
        self.click("button")
        self.checkpoint("resized_button", bounds, count + 1, extent=(800, 600))


def expected_actions(windows):
    actions = [("increase", 1, 0, 1), ("locked", 1, 1, 2), ("locked", 1, 0, 3), ("increase", 2, 0, 4)]
    if windows:
        actions += [("increase", 3, 0, 5), ("increase", 4, 0, 6)]
    else:
        actions += [("increase", 3, 0, 5)]
    return actions


def run(args):
    if not args.executable.is_file():
        raise SmokeFailure(f"executable does not exist: {args.executable}")
    args.output_directory.mkdir(parents=True, exist_ok=True)
    environment = build_launch_environment(args)
    environment["NWB_UI_LAYER_WINDOW"] = "1"
    environment["NWB_UI_LAYER_INTERACTIVE"] = "0"
    environment["NWB_UI_LAYER_WINDOW_SKIN"] = "1" if args.skin == "alternate" else "0"
    environment["NWB_UI_LAYER_EDIT"] = "0"
    environment["NWB_UI_LAYER_POPUP"] = "0"
    environment["NWB_UI_LAYER_POPUP_SKIN"] = "0"
    if platform.system() == "Linux":
        environment["NWB_LINUX_BACKEND"] = "x11"
    for variable in ("NWB_SMOKE_FRAMEBUFFER_CAPTURE_PATH", "NWB_SMOKE_FRAMEBUFFER_CAPTURE_FRAME_COUNT",
        "NWB_RENDERER_BASELINE_CAPTURE_FREEZE_FRAME", "NWB_RENDERER_BASELINE_FIXED_DELTA_SECONDS", "NWB_GPU_TIMING_FILE"):
        environment.pop(variable, None)
    backend = create_capture_backend()
    logserver = application = window_run = None
    handle = None
    log_directory = log_baseline = log_pattern = None
    try:
        logserver, port, log_directory, log_baseline, log_pattern = launch_logserver(args, args.executable, environment)
        application = launch_testbed(args, args.executable, environment, port)
        handle = backend.wait_for_window(application.pid, args.timeout, args.window_title)
        if not handle:
            ensure_process_running(application, "before the window fixture appeared")
            raise SmokeFailure("custom window fixture did not appear")
        backend.prepare_window(handle)
        wait_for_log_message(log_directory, log_baseline, log_pattern, "UiWindowSmoke: display", min(args.timeout, 15.0))
        window_run = WindowRun(args, backend, handle, application, log_directory, log_baseline, log_pattern)
        window_run.execute()
    finally:
        try:
            if application is not None:
                exit_code, tail = terminate_process(application, "UI window fixture", handle)
                require_normal_process_exit(exit_code, tail, "UI window fixture")
        finally:
            try:
                text = ""
                if log_directory is not None:
                    text = shutdown_logserver_and_collect(logserver, log_directory, log_baseline, log_pattern)
                    logserver = None
                    (args.output_directory / "window.log").write_text(text, encoding="utf-8")
            finally:
                if logserver is not None:
                    terminate_process(logserver, "UI window logserver")
                backend.close()
    validate_expected_log_text(text, ["Loader: project startup complete", "UiLayerSmokeProject: shutdown"], STRICT_LOG_FAILURE_MESSAGES)
    observed = [(action, int(count), int(locked), int(sequence)) for action, count, locked, sequence in ACTION.findall(text)]
    expected = expected_actions(window_run.native.windows)
    if observed != expected:
        raise SmokeFailure(f"unexpected window action sequence: {observed}; expected {expected}")
    window_run.write_report(True)
    print(f"UI window: {len(window_run.stages)} displayed gates, {len(expected)} exact actions", flush=True)
    return 0


def main(argv):
    args = parse_args(argv)
    try:
        return run(args)
    except SmokeSkip as error:
        print(f"SKIP: {error}", flush=True)
        return SKIP_EXIT_CODE
    except (SmokeFailure, OSError, ValueError, TimeoutError) as error:
        print(f"FAIL: {error}", flush=True)
        return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
