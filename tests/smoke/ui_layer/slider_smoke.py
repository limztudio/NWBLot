#!/usr/bin/env python3
"""Qualify continuous sliders through native input and post-end displayed snapshots."""
from __future__ import annotations

import json
import platform
import sys
import time

from slider_native import SliderNativeInput
from slider_probe import bits_value, center, f32, observe_slider, snapshot_from_logs, value_bits
from window_smoke import parse_args
from window_capture_smoke import (
    SKIP_EXIT_CODE, STRICT_LOG_FAILURE_MESSAGES, SmokeFailure, SmokeSkip, build_launch_environment,
    collect_log_delta, create_capture_backend, ensure_process_running, launch_logserver, launch_testbed,
    read_bmp_24_rows, require_normal_process_exit, shutdown_logserver_and_collect, terminate_process,
    validate_expected_log_text, wait_for_log_message,
)


class SliderRun:
    def __init__(self, args, backend, handle, process, log_directory, log_baseline, log_pattern):
        self.args = args
        self.backend = backend
        self.handle = handle
        self.process = process
        self.log_directory = log_directory
        self.log_baseline = log_baseline
        self.log_pattern = log_pattern
        self.native = SliderNativeInput(backend, handle)
        self.deadline = time.monotonic() + args.timeout
        self.snapshot = None
        self.stages = []
        self.failure_report = None
        self.expected = {
            "focus_code": 0, "changes": 0, "popup_changes": 0, "before_clicks": 0, "after_clicks": 0,
            "enabled": 1, "range_code": 0, "step_code": 0, "parent": 0, "popup_count": 0, "focus_scope": 0,
            "main_valid": 1, "main_dragging": 0, "popup_valid": 0, "popup_dragging": 0, "external_intents": 0,
            "main_bits": value_bits(0.25), "disabled_bits": value_bits(0.75),
            "constant_bits": value_bits(0.5), "popup_bits": value_bits(0.5),
        }

    def logs(self):
        return collect_log_delta(self.log_directory, self.log_baseline, self.log_pattern)

    def point(self, x, y):
        scale_x, scale_y = self.snapshot["scale"]
        return round(x * scale_x), round(y * scale_y)

    def rectangle_point(self, name):
        rectangle = self.snapshot["rectangles"][name]
        if rectangle[2] <= 0.0 or rectangle[3] <= 0.0:
            raise SmokeFailure(f"slider fixture has no accepted placement for '{name}'")
        return self.point(*center(rectangle))

    def click(self, name):
        self.native.click(*self.rectangle_point(name))

    def maximum(self, prefix):
        return 0.5 if prefix == "main" and self.expected["range_code"] else 1.0

    def track_point(self, prefix, fraction):
        x, y, width, height = self.snapshot["rectangles"][f"{prefix}_center"]
        return self.point(x + fraction * width, y + height / 2.0)

    def thumb_point(self, prefix, fraction=0.75):
        x, y, width, height = self.snapshot["rectangles"][f"{prefix}_thumb"]
        return self.point(x + fraction * width, y + height / 2.0)

    def logical_x(self, position):
        return f32(position[0] / self.snapshot["scale"][0])

    def seek_value(self, prefix, position):
        centers = self.snapshot["rectangles"][f"{prefix}_center"]
        fraction = (self.logical_x(position) - f32(centers[0])) / f32(centers[2])
        return self.maximum(prefix) * min(1.0, max(0.0, fraction))

    def drag_value(self, prefix, origin, position, baseline, rectangles):
        travel = f32(rectangles[f"{prefix}_travel"][2]) - f32(rectangles[f"{prefix}_thumb"][2])
        delta = self.logical_x(position) - self.logical_x(origin)
        normalized = min(1.0, max(0.0, baseline / self.maximum(prefix))) + delta / travel
        return self.maximum(prefix) * min(1.0, max(0.0, normalized))

    def value_changes(self, prefix, value):
        name = "changes" if prefix == "main" else "popup_changes"
        bits_name = f"{prefix}_bits"
        bits = value_bits(value)
        return {bits_name: bits, name: self.expected[name] + int(self.expected[bits_name] != bits)}

    def navigate(self, key, value, name):
        self.native.tap(key)
        self.checkpoint(name, focus_code=2, **self.value_changes("main", value))

    def checkpoint(self, name, *, extent=None, extra=None, neutral=True, modes=None, dynamic=(), **changes):
        self.native.record("checkpoint_begin", name=name)
        self.expected.update(changes)
        for field in dynamic:
            self.expected.pop(field, None)
        if neutral:
            self.native.pointer(4, 4)
        self.native.maintain_pointer()
        time.sleep(0.25)
        path = self.args.output_directory / f"{len(self.stages):02d}_{name}.bmp"
        stage_deadline = min(self.deadline, time.monotonic() + 10.0)
        report = None
        while time.monotonic() < stage_deadline:
            ensure_process_running(self.process, "during slider UI capture")
            self.native.maintain_pointer()
            self.native.record("capture_before", name=name)
            snapshot = snapshot_from_logs(self.logs())
            if snapshot is None:
                time.sleep(0.1)
                continue
            if snapshot["logical_extent"][0] < 740.0 or snapshot["logical_extent"][1] < 480.0:
                raise SmokeSkip("the slider fixture needs a logical client of at least 740x480")
            if self.native.windows:
                self.backend.capture_prepared_raw_client_window(self.handle, path)
            else:
                self.backend.capture_client_window(self.handle, path)
            report = observe_slider(read_bmp_24_rows(path), snapshot, self.expected,
                extent=extent, skin=self.args.skin, extra=extra, modes=modes)
            self.native.record("capture_after", name=name, sequence=snapshot["sequence"], passed=report["passed"])
            if report["passed"]:
                self.snapshot = snapshot
                for field in dynamic:
                    self.expected[field] = snapshot[field]
                report.update({"stage": name, "capture": str(path), "native_window": self.native.observe_window()})
                self.stages.append(report)
                self.write_report(False)
                return
            time.sleep(0.1)
        self.failure_report = {"stage": name, "capture": str(path), "native_window": self.native.observe_window(),
            "observation": report}
        self.write_report(False)
        raise SmokeFailure(f"slider displayed-state gate '{name}' failed: {self.failure_report}")

    def write_report(self, passed):
        report = {
            "passed": passed, "platform": platform.system(), "skin": self.args.skin, "stages": self.stages,
            "failure": self.failure_report, "native_actions": self.native.actions,
            "input_path": "Win32 ClipCursor-isolated physical cursor positioning with posted pointer/key messages"
                if self.native.windows else "X11 XSendEvent keys/button1 and XSetInputFocus",
            "runtime_limit": "No native Wayland, physical pointer-grab, or live IME qualification.",
        }
        (self.args.output_directory / "slider.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")

    def execute(self):
        self.checkpoint("initial_exact_values_and_atlas")
        self.native.pointer(*self.thumb_point("main", 0.5))
        self.checkpoint("hover_uses_authored_thumb_region_and_tint", neutral=False, modes={"main": "hover"})
        self.click("before")
        self.native.tap("Tab")
        self.checkpoint("tab_enters_one_slider_host", before_clicks=1, focus_code=2)
        self.native.tap("Tab")
        self.checkpoint("next_tab_skips_track_and_thumb_parts", focus_code=3)
        self.native.tap("Tab", shift=True)
        self.navigate("Right", 0.375, "right_adds_exact_key_step")
        self.navigate("Up", 0.5, "up_adds_exact_key_step")
        self.navigate("Down", 0.375, "down_subtracts_exact_key_step")
        self.navigate("Left", 0.25, "left_subtracts_exact_key_step")
        self.navigate("Page_Up", 1.0, "page_up_saturates_at_exact_maximum")
        self.navigate("Page_Down", 0.0, "page_down_saturates_at_exact_minimum")
        self.navigate("End", 1.0, "end_chooses_exact_maximum")
        prior_changes = self.expected["changes"]
        self.native.tap("Home")
        self.native.tap("Right", repeat_count=3)
        self.checkpoint("home_then_accepted_arrow_repeats", main_bits=value_bits(0.5), dynamic=("changes",),
            extra=lambda snapshot: prior_changes + 1 <= snapshot["changes"] <= prior_changes + 5)
        seek = self.track_point("main", 0.75)
        sought = self.seek_value("main", seek)
        self.native.button(True, *seek)
        self.checkpoint("track_press_seeks_absolute_value_while_held", neutral=False, modes={"main": "pressed"},
            main_dragging=1, **self.value_changes("main", sought))
        self.native.button(False, *seek)
        origin = self.thumb_point("main")
        baseline = bits_value(self.expected["main_bits"])
        original_rectangles = dict(self.snapshot["rectangles"])
        self.native.button(True, *origin)
        self.checkpoint("offcenter_thumb_press_preserves_exact_value", neutral=False, modes={"main": "pressed"},
            main_dragging=1)
        travel = self.snapshot["rectangles"]["main_center"][2]
        scale = self.snapshot["scale"][0]
        moved = origin[0] + round(travel * 0.125 * scale), origin[1]
        self.native.pointer(*moved)
        value = self.drag_value("main", origin, moved, baseline, original_rectangles)
        self.checkpoint("held_thumb_move_preserves_grab_offset", neutral=False, modes={"main": "pressed"},
            **self.value_changes("main", value))
        moved_again = origin[0] + round(travel * 0.375 * scale), origin[1]
        self.native.pointer(*moved_again)
        value = self.drag_value("main", origin, moved_again, baseline, original_rectangles)
        self.checkpoint("later_move_uses_original_baseline_and_clamps_maximum", neutral=False, modes={"main": "pressed"},
            **self.value_changes("main", value))
        self.native.pointer(*self.point(4, 4))
        time.sleep(0.15)
        self.native.button(False, *self.point(4, 4))
        self.checkpoint("outside_capture_clamps_minimum_and_release_finishes", main_dragging=0,
            **self.value_changes("main", 0.0))
        self.click("disabled_thumb")
        self.native.tap("End")
        self.click("constant_thumb")
        self.native.tap("Right")
        self.checkpoint("disabled_and_constant_controls_cannot_change_or_focus", focus_code=0)
        self.click("main_thumb")
        self.native.key("Right", True)
        self.checkpoint("held_key_first_step_is_presented", focus_code=2, **self.value_changes("main", 0.125))
        self.native.command("fence_value")
        self.native.key("Right", True, repeat=True)
        self.native.key("Right", False)
        self.checkpoint("same_value_external_setter_retires_held_key", external_intents=1)
        self.native.key("Right", True)
        self.checkpoint("fresh_key_press_after_external_fence", **self.value_changes("main", 0.25))
        self.native.command("toggle_step")
        self.native.key("Right", True, repeat=True)
        self.native.key("Right", False)
        self.checkpoint("changed_step_retires_old_repeat", step_code=1)
        prior_changes = self.expected["changes"]
        self.native.key("Right", True)
        time.sleep(0.15)
        self.native.key("Right", True, repeat=True)
        self.checkpoint("fresh_coarse_step_and_repeat_use_new_policy", main_bits=value_bits(0.75), dynamic=("changes",),
            extra=lambda snapshot: prior_changes + 1 <= snapshot["changes"] <= prior_changes + 2)
        self.native.command("toggle_range")
        self.native.key("Right", True, repeat=True)
        self.native.key("Right", False)
        self.checkpoint("changed_range_retires_repeat_and_preserves_authoritative_value", range_code=1)
        old_thumb = self.thumb_point("main")
        self.native.button(True, *old_thumb)
        self.native.command("toggle_enabled")
        self.native.pointer(old_thumb[0] - 40, old_thumb[1])
        self.native.button(False, old_thumb[0] - 40, old_thumb[1])
        self.native.tap("Home")
        self.click("main_track")
        self.checkpoint("disable_retires_capture_and_blocks_fresh_input", enabled=0, focus_code=0, main_dragging=0)
        self.native.command("toggle_enabled")
        self.native.command("set_quarter")
        self.checkpoint("reenable_and_external_value_publish_fresh_state", enabled=1, external_intents=2,
            main_bits=value_bits(0.25))
        old_thumb = self.thumb_point("main")
        self.native.button(True, *old_thumb)
        self.checkpoint("fresh_thumb_press_is_published_before_external_fence", neutral=False,
            modes={"main": "pressed"}, focus_code=2, main_dragging=1)
        self.native.command("fence_value")
        self.native.pointer(old_thumb[0] - 40, old_thumb[1])
        self.native.button(False, old_thumb[0] - 40, old_thumb[1])
        self.checkpoint("same_value_external_setter_retires_thumb_capture", external_intents=3, main_dragging=0)
        old_thumb = self.thumb_point("main")
        old_width = self.snapshot["rectangles"]["main_center"][2]
        old_extent = self.snapshot["logical_extent"]
        target_extent = round(old_extent[0]) + 96, round(old_extent[1]) + 64
        physical_extent = self.point(*target_extent)
        self.native.button(True, *old_thumb)
        self.backend.resize_client(self.handle, *physical_extent)
        time.sleep(0.3)
        self.native.pointer(old_thumb[0] + 40, old_thumb[1])
        self.native.button(False, old_thumb[0] + 40, old_thumb[1])
        self.checkpoint("resize_changes_admission_and_retires_old_drag", extent=physical_extent, main_dragging=0,
            dynamic=("focus_code",), extra=lambda snapshot: snapshot["focus_code"] in (0, 2)
                and snapshot["rectangles"]["main_center"][2] != old_width
                and abs(snapshot["logical_extent"][0] - target_extent[0]) < 1.0
                and abs(snapshot["logical_extent"][1] - target_extent[1]) < 1.0)
        fresh = self.track_point("main", 0.75)
        value = self.seek_value("main", fresh)
        self.native.click(*fresh)
        self.checkpoint("fresh_seek_uses_resized_geometry_and_current_range", focus_code=2,
            **self.value_changes("main", value))
        old_thumb = self.thumb_point("main")
        self.native.button(True, *old_thumb)
        self.native.focus_loss_and_gain()
        self.native.pointer(old_thumb[0] - 40, old_thumb[1])
        self.native.button(False, old_thumb[0] - 40, old_thumb[1])
        self.checkpoint("native_focus_loss_retires_drag_without_value_change", focus_code=0, main_dragging=0)
        self.click("open")
        self.checkpoint("parent_popup_publishes_slider_focus_and_geometry", parent=1, popup_count=1,
            focus_code=5, focus_scope=1, popup_valid=1)
        popup_seek = self.track_point("popup", 0.75)
        value = self.seek_value("popup", popup_seek)
        self.native.button(True, *popup_seek)
        self.checkpoint("popup_track_seek_publishes_held_value", neutral=False, modes={"popup": "pressed"},
            popup_dragging=1, **self.value_changes("popup", value))
        self.native.command("close_parent")
        self.checkpoint("ancestor_close_retires_popup_drag_and_hidden_diagnostics", neutral=False, parent=0, popup_count=0,
            focus_code=4, focus_scope=0, popup_valid=0, popup_dragging=0)
        self.native.pointer(popup_seek[0] - 60, popup_seek[1])
        self.native.button(False, popup_seek[0] - 60, popup_seek[1])
        self.checkpoint("stale_popup_motion_and_release_preserve_closed_value")
        self.click("open")
        self.checkpoint("reopened_popup_publishes_before_fresh_thumb_input", parent=1, popup_count=1,
            focus_code=5, focus_scope=1, popup_valid=1)
        origin = self.thumb_point("popup")
        baseline = bits_value(self.expected["popup_bits"])
        original_rectangles = dict(self.snapshot["rectangles"])
        delta = round(self.snapshot["rectangles"]["popup_center"][2] * -0.25 * self.snapshot["scale"][0])
        destination = origin[0] + delta, origin[1]
        value = self.drag_value("popup", origin, destination, baseline, original_rectangles)
        self.native.drag(origin, destination)
        self.checkpoint("fresh_popup_thumb_drag_after_ancestor_reopen", **self.value_changes("popup", value))
        self.click("popup_close")
        self.checkpoint("popup_close_button_returns_root_focus", parent=0, popup_count=0, focus_code=4, focus_scope=0,
            popup_valid=0, popup_dragging=0)


def run(args):
    if not args.executable.is_file():
        raise SmokeFailure(f"executable does not exist: {args.executable}")
    args.output_directory.mkdir(parents=True, exist_ok=True)
    environment = build_launch_environment(args)
    for fixture in ("WINDOW", "POPUP", "POPUP_TOOLS", "NESTED_POPUP", "LIST", "COMBO", "SEARCH_COMBO",
            "NUMERIC_EDIT", "TEXT_AREA", "RADIO_GROUP"):
        environment[f"NWB_UI_LAYER_{fixture}"] = "0"
        environment[f"NWB_UI_LAYER_{fixture}_SKIN"] = "0"
    environment.update({
        "NWB_UI_LAYER_SLIDER": "1",
        "NWB_UI_LAYER_SLIDER_SKIN": "1" if args.skin == "alternate" else "0",
        "NWB_UI_LAYER_EDIT": "0", "NWB_UI_LAYER_INTERACTIVE": "0",
    })
    if platform.system() == "Linux":
        environment["NWB_LINUX_BACKEND"] = "x11"
    for variable in ("NWB_SMOKE_FRAMEBUFFER_CAPTURE_PATH", "NWB_SMOKE_FRAMEBUFFER_CAPTURE_FRAME_COUNT",
        "NWB_RENDERER_BASELINE_CAPTURE_FREEZE_FRAME", "NWB_RENDERER_BASELINE_FIXED_DELTA_SECONDS", "NWB_GPU_TIMING_FILE"):
        environment.pop(variable, None)
    backend = create_capture_backend()
    logserver = application = slider_run = None
    handle = None
    log_directory = log_baseline = log_pattern = None
    try:
        logserver, port, log_directory, log_baseline, log_pattern = launch_logserver(args, args.executable, environment)
        application = launch_testbed(args, args.executable, environment, port)
        handle = backend.wait_for_window(application.pid, args.timeout, args.window_title)
        if not handle:
            ensure_process_running(application, "before the slider fixture appeared")
            raise SmokeFailure("custom slider fixture did not appear")
        if platform.system() == "Windows":
            backend.prepare_raw_client_window(handle)
        else:
            backend.prepare_window(handle)
        wait_for_log_message(log_directory, log_baseline, log_pattern, "UiSliderSmoke: display", min(args.timeout, 15.0))
        slider_run = SliderRun(args, backend, handle, application, log_directory, log_baseline, log_pattern)
        slider_run.execute()
    finally:
        try:
            if application is not None:
                exit_code, tail = terminate_process(application, "UI slider fixture", handle)
                require_normal_process_exit(exit_code, tail, "UI slider fixture")
        finally:
            try:
                text = ""
                if log_directory is not None:
                    text = shutdown_logserver_and_collect(logserver, log_directory, log_baseline, log_pattern)
                    logserver = None
                    (args.output_directory / "slider.log").write_text(text, encoding="utf-8")
            finally:
                try:
                    if logserver is not None:
                        terminate_process(logserver, "UI slider logserver")
                finally:
                    try:
                        if slider_run is not None:
                            slider_run.native.restore_pointer()
                    finally:
                        backend.close()
    validate_expected_log_text(text, ["Loader: project startup complete", "UiLayerSmokeProject: shutdown"],
        STRICT_LOG_FAILURE_MESSAGES)
    slider_run.write_report(True)
    print(f"UI slider: {len(slider_run.stages)} matching native/GPU gates with {args.skin} atlas", flush=True)
    return 0


def main(argv):
    if not any(argument == "--timeout" or argument.startswith("--timeout=") for argument in argv):
        argv = [*argv, "--timeout", "180"]
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
