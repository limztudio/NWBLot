#!/usr/bin/env python3
"""Qualify passive image geometry, retained options, tint, clipping and popup scope."""
from __future__ import annotations

import json
import platform
import sys
import time

from guarded_native import GuardedNativeInput
from image_probe import center, observe_image, snapshot_from_logs
from window_smoke import parse_args
from window_capture_smoke import (
    SKIP_EXIT_CODE, STRICT_LOG_FAILURE_MESSAGES, SmokeFailure, SmokeSkip, build_launch_environment,
    collect_log_delta, create_capture_backend, ensure_process_running, launch_logserver, launch_testbed,
    read_bmp_24_rows, require_normal_process_exit, shutdown_logserver_and_collect, terminate_process,
    validate_expected_log_text, wait_for_log_message,
)
from fixture_environment import build_fixture_environment


class ImageRun:
    def __init__(self, args, backend, handle, process, log_directory, log_baseline, log_pattern):
        self.args, self.backend, self.handle, self.process = args, backend, handle, process
        self.log_directory, self.log_baseline, self.log_pattern = log_directory, log_baseline, log_pattern
        self.native = GuardedNativeInput(backend, handle)
        self.deadline = time.monotonic() + args.timeout
        self.snapshot = None
        self.stages = []
        self.failure_report = None
        self.expected = {
            "phase": 0, "focus_code": 0, "before_clicks": 0, "after_clicks": 0,
            "parent": 0, "child": 0, "popup_count": 0, "image_targets": 0,
            "declared_region": 1, "post_declaration_region": 2,
        }

    def logs(self):
        return collect_log_delta(self.log_directory, self.log_baseline, self.log_pattern)

    def point(self, x, y):
        sx, sy = self.snapshot["scale"]
        return round(x * sx), round(y * sy)

    def click(self, name):
        rectangle = self.snapshot["rectangles"][name]
        if rectangle[2] <= 0.0 or rectangle[3] <= 0.0:
            raise SmokeFailure(f"image fixture has no displayed placement for '{name}'")
        self.native.click(*self.point(*center(rectangle)))

    def checkpoint(self, name, *, extent=None, extra=None, **changes):
        self.expected.update(changes)
        self.native.pointer(4, 4)
        self.native.maintain_pointer()
        time.sleep(0.25)
        path = self.args.output_directory / f"{len(self.stages):02d}_{name}.bmp"
        stage_deadline = min(self.deadline, time.monotonic() + 10.0)
        report = None
        while time.monotonic() < stage_deadline:
            ensure_process_running(self.process, "during image UI capture")
            self.native.maintain_pointer()
            snapshot = snapshot_from_logs(self.logs())
            if snapshot is None:
                time.sleep(0.1)
                continue
            if snapshot["logical_extent"][0] < 740.0 or snapshot["logical_extent"][1] < 480.0:
                raise SmokeSkip("the image fixture needs a logical client of at least 740x480")
            if self.native.windows:
                self.backend.capture_prepared_raw_client_window(self.handle, path)
            else:
                self.backend.capture_client_window(self.handle, path)
            report = observe_image(read_bmp_24_rows(path), snapshot, self.expected,
                extent=extent, skin=self.args.skin, extra=extra)
            if report["passed"]:
                self.snapshot = snapshot
                report.update({"stage": name, "capture": str(path), "native_window": self.native.observe_window()})
                self.stages.append(report)
                self.write_report(False)
                return
            time.sleep(0.1)
        self.failure_report = {"stage": name, "capture": str(path), "native_window": self.native.observe_window(),
            "observation": report}
        self.write_report(False)
        raise SmokeFailure(f"image displayed-state gate '{name}' failed: {self.failure_report}")

    def write_report(self, passed):
        report = {
            "passed": passed, "platform": platform.system(), "skin": self.args.skin, "stages": self.stages,
            "failure": self.failure_report,
            "input_path": "Win32 constrained physical cursor with posted pointer/key messages"
                if self.native.windows else "X11 XSendEvent keys/buttons and XSetInputFocus",
            "runtime_limit": "No native Wayland, physical button injection, or live IME qualification.",
        }
        (self.args.output_directory / "image.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")

    def execute(self):
        self.checkpoint("initial_natural_fixed_stretch_tint_transparency_and_clip")
        self.click("before")
        self.checkpoint("before_button_activates_and_takes_focus", before_clicks=1, focus_code=1)
        self.native.tap("Tab")
        self.checkpoint("tab_skips_all_passive_images", focus_code=2)
        self.click("natural_sprite")
        for key in ("Right", "Home", "Return"):
            self.native.tap(key)
        self.checkpoint("passive_image_pointer_and_keyboard_leave_values_unchanged", focus_code=0)
        self.native.tap("F4")
        self.checkpoint("declaration_retains_region_options_and_tint", phase=1, declared_region=2, post_declaration_region=1)
        old_extent = self.snapshot["logical_extent"]
        old_width = self.snapshot["rectangles"]["stretch"][2]
        target_extent = round(old_extent[0]) + 96, round(old_extent[1]) + 64
        physical_extent = self.point(*target_extent)
        self.backend.resize_client(self.handle, *physical_extent)
        self.checkpoint("resize_recomputes_stretch_and_preserves_retained_images", extent=physical_extent,
            extra=lambda snapshot: snapshot["rectangles"]["stretch"][2] != old_width
                and abs(snapshot["logical_extent"][0] - target_extent[0]) < 1.0
                and abs(snapshot["logical_extent"][1] - target_extent[1]) < 1.0)
        self.native.tap("F5")
        self.checkpoint("both_popups_publish_images_before_child_input", parent=1, child=1, popup_count=2, focus_code=4)
        self.native.tap("Tab")
        self.checkpoint("child_tab_stays_on_its_only_button", focus_code=4)
        self.native.tap("Escape")
        self.checkpoint("escape_child_retires_its_image_and_restores_parent", parent=1, child=0, popup_count=1, focus_code=3)
        self.native.tap("Escape")
        self.checkpoint("escape_parent_retires_its_image_and_restores_root", parent=0, child=0, popup_count=0, focus_code=0)


def run(args):
    if not args.executable.is_file():
        raise SmokeFailure(f"executable does not exist: {args.executable}")
    args.output_directory.mkdir(parents=True, exist_ok=True)
    environment = build_fixture_environment(build_launch_environment(args), "IMAGE", skin=args.skin, force_x11=True)
    backend = create_capture_backend()
    logserver = application = image_run = None
    handle = None
    log_directory = log_baseline = log_pattern = None
    try:
        logserver, port, log_directory, log_baseline, log_pattern = launch_logserver(args, args.executable, environment)
        application = launch_testbed(args, args.executable, environment, port)
        handle = backend.wait_for_window(application.pid, args.timeout, args.window_title)
        if not handle:
            ensure_process_running(application, "before the image fixture appeared")
            raise SmokeFailure("custom image fixture did not appear")
        if platform.system() == "Windows":
            backend.prepare_raw_client_window(handle)
        else:
            backend.prepare_window(handle)
        wait_for_log_message(log_directory, log_baseline, log_pattern, "UiImageSmoke: display", min(args.timeout, 15.0))
        image_run = ImageRun(args, backend, handle, application, log_directory, log_baseline, log_pattern)
        image_run.execute()
    finally:
        try:
            if application is not None:
                exit_code, tail = terminate_process(application, "UI image fixture", handle)
                require_normal_process_exit(exit_code, tail, "UI image fixture")
        finally:
            try:
                text = ""
                if log_directory is not None:
                    text = shutdown_logserver_and_collect(logserver, log_directory, log_baseline, log_pattern)
                    logserver = None
                    (args.output_directory / "image.log").write_text(text, encoding="utf-8")
            finally:
                try:
                    if logserver is not None:
                        terminate_process(logserver, "UI image logserver")
                finally:
                    try:
                        if image_run is not None:
                            image_run.native.restore_pointer()
                    finally:
                        backend.close()
    validate_expected_log_text(text, ["Loader: project startup complete", "UiLayerSmokeProject: shutdown"],
        STRICT_LOG_FAILURE_MESSAGES)
    image_run.write_report(True)
    print(f"UI image: {len(image_run.stages)} matching native/GPU gates with {args.skin} atlas", flush=True)
    return 0


def main(argv):
    if not any(argument == "--timeout" or argument.startswith("--timeout=") for argument in argv):
        argv = [*argv, "--timeout", "120"]
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
