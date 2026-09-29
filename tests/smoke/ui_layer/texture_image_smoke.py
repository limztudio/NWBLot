#!/usr/bin/env python3
"""Qualify owned concrete texture bindings, copied inputs, cache lifetimes and Builder popup images."""
from __future__ import annotations

import json
import platform
import sys
import time

from guarded_native import GuardedNativeInput
from texture_image_probe import center, observe_texture_image, snapshot_from_logs
from window_smoke import parse_args
from window_capture_smoke import (
    SKIP_EXIT_CODE, STRICT_LOG_FAILURE_MESSAGES, SmokeFailure, SmokeSkip, build_launch_environment,
    collect_log_delta, create_capture_backend, ensure_process_running, launch_logserver, launch_testbed,
    read_bmp_24_rows, require_normal_process_exit, shutdown_logserver_and_collect, terminate_process,
    validate_expected_log_text, wait_for_log_message,
)


class TextureImageRun:
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
            "declared_source": 1, "post_handle_empty": 1, "replacement_count": 0, "replacement_alternate": 0,
            "eviction_remaining": 0, "eviction_completed": 0, "builder_source": 1, "builder_handle_empty": 1,
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

    def checkpoint(self, name, *, extent=None, extra=None, stage_timeout=10.0, **changes):
        self.expected.update(changes)
        self.native.pointer(4, 4)
        self.native.maintain_pointer()
        time.sleep(0.25)
        path = self.args.output_directory / f"{len(self.stages):02d}_{name}.bmp"
        stage_deadline = min(self.deadline, time.monotonic() + stage_timeout)
        report = None
        while time.monotonic() < stage_deadline:
            ensure_process_running(self.process, "during image UI capture")
            self.native.maintain_pointer()
            snapshot = snapshot_from_logs(self.logs())
            if snapshot is None:
                time.sleep(0.1)
                continue
            if snapshot["logical_extent"][0] < 740.0 or snapshot["logical_extent"][1] < 520.0:
                raise SmokeSkip("the image fixture needs a logical client of at least 740x520")
            if self.native.windows:
                self.backend.capture_prepared_raw_client_window(self.handle, path)
            else:
                self.backend.capture_client_window(self.handle, path)
            report = observe_texture_image(read_bmp_24_rows(path), snapshot, self.expected,
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
            "runtime_limit": "No native Wayland, physical button injection or live IME coverage.",
            "overlay_contract": ("Actual Builder source widgets paint in the parent/child popup family above "
                "late base sentinels; primitive UV/crop checks remain separate."),
            "builder_contract": ("A passive Stretch/Fixed image retains copied source/options after the caller "
                "resets its handle and changes width, height and tint before deferred painting."),
            "cache_contract": ("F7 creates and displays one fresh same-path payload per accepted paint callback "
                "for 72 versions."),
        }
        (self.args.output_directory / "texture_image.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")

    def execute(self):
        self.checkpoint("independent_default_alternate_raw_atlases_uv_tint_and_clip")
        original_generations = self.snapshot["generations"][:2]
        self.click("before")
        self.checkpoint("ordinary_button_activates_alongside_owned_images", before_clicks=1, focus_code=1)
        self.native.tap("Tab")
        self.checkpoint("tab_skips_all_passive_concrete_images", focus_code=2)
        self.click("default_tile")
        for key in ("Right", "Home", "Return"):
            self.native.tap(key)
        self.checkpoint("direct_images_have_no_keyboard_or_pointer_state", focus_code=0)
        self.click("builder_image")
        for key in ("Right", "Home", "Return"):
            self.native.tap(key)
        self.checkpoint("actual_builder_image_stays_passive_after_pointer_and_keys", focus_code=0)
        old_generation = self.snapshot["generations"][2]
        self.native.tap("F6")
        self.checkpoint("same_identity_new_payload_preserves_original_source_pixels", replacement_count=1,
            replacement_alternate=1, extra=lambda snapshot: snapshot["generations"][:2] == original_generations
                and snapshot["generations"][2] != old_generation)
        old_generation = self.snapshot["generations"][2]
        self.native.tap("F6")
        self.checkpoint("second_fresh_version_restores_default_payload", replacement_count=2,
            replacement_alternate=0, extra=lambda snapshot: snapshot["generations"][:2] == original_generations
                and snapshot["generations"][2] != old_generation)
        old_generation = self.snapshot["generations"][2]
        self.native.tap("F7")
        self.checkpoint("72_accepted_versions_preserve_originals_and_final_payload", replacement_count=74,
            replacement_alternate=0, eviction_remaining=0, eviction_completed=72, stage_timeout=30.0,
            extra=lambda snapshot: snapshot["generations"][:2] == original_generations
                and snapshot["generations"][2] != old_generation)
        self.native.tap("F4")
        self.checkpoint("paint_declaration_copies_source_handle_rectangle_uv_and_tint",
            phase=1, declared_source=2, builder_source=2)
        old_extent = self.snapshot["logical_extent"]
        old_width = self.snapshot["rectangles"]["frozen"][2]
        target_extent = round(old_extent[0]) + 96, round(old_extent[1]) + 64
        physical_extent = self.point(*target_extent)
        self.backend.resize_client(self.handle, *physical_extent)
        self.checkpoint("resize_preserves_owned_versions_and_recomputes_image_rectangles", extent=physical_extent,
            extra=lambda snapshot: snapshot["rectangles"]["frozen"][2] != old_width
                and snapshot["generations"][:2] == original_generations
                and abs(snapshot["logical_extent"][0] - target_extent[0]) < 1.0
                and abs(snapshot["logical_extent"][1] - target_extent[1]) < 1.0)
        self.native.tap("F5")
        self.checkpoint("actual_builder_child_parent_images_cover_late_base_sentinels", parent=1, child=1,
            popup_count=2, focus_code=4)
        self.native.tap("Tab")
        self.checkpoint("accepted_child_keyboard_scope_remains_active", focus_code=4)
        self.native.tap("Escape")
        self.checkpoint("child_close_retires_builder_source_image_and_restores_parent", parent=1, child=0,
            popup_count=1, focus_code=3)
        self.native.tap("Escape")
        self.checkpoint("parent_close_retires_builder_source_image_and_restores_root", parent=0, child=0,
            popup_count=0, focus_code=0)
        self.native.tap("F4")
        self.checkpoint("final_original_and_replacement_versions_remain_independent",
            phase=0, declared_source=1, builder_source=1)

def run(args):
    if not args.executable.is_file():
        raise SmokeFailure(f"executable does not exist: {args.executable}")
    args.output_directory.mkdir(parents=True, exist_ok=True)
    environment = build_launch_environment(args)
    for fixture in ("WINDOW", "POPUP", "POPUP_TOOLS", "NESTED_POPUP", "LIST", "COMBO", "SEARCH_COMBO",
            "NUMERIC_EDIT", "TEXT_AREA", "RADIO_GROUP", "SLIDER", "PROGRESS", "IMAGE"):
        environment[f"NWB_UI_LAYER_{fixture}"] = "0"
        environment[f"NWB_UI_LAYER_{fixture}_SKIN"] = "0"
    environment.update({"NWB_UI_LAYER_TEXTURE_IMAGE": "1",
        "NWB_UI_LAYER_TEXTURE_IMAGE_SKIN": "1" if args.skin == "alternate" else "0",
        "NWB_UI_LAYER_EDIT": "0", "NWB_UI_LAYER_INTERACTIVE": "0"})
    if platform.system() == "Linux":
        environment["NWB_LINUX_BACKEND"] = "x11"
    for variable in ("NWB_SMOKE_FRAMEBUFFER_CAPTURE_PATH", "NWB_SMOKE_FRAMEBUFFER_CAPTURE_FRAME_COUNT",
            "NWB_RENDERER_BASELINE_CAPTURE_FREEZE_FRAME", "NWB_RENDERER_BASELINE_FIXED_DELTA_SECONDS", "NWB_GPU_TIMING_FILE"):
        environment.pop(variable, None)
    backend = create_capture_backend()
    logserver = application = texture_image_run = None
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
        wait_for_log_message(log_directory, log_baseline, log_pattern, "UiTextureImageSmoke: display", min(args.timeout, 15.0))
        texture_image_run = TextureImageRun(args, backend, handle, application, log_directory, log_baseline, log_pattern)
        texture_image_run.execute()
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
                    (args.output_directory / "texture_image.log").write_text(text, encoding="utf-8")
            finally:
                try:
                    if logserver is not None:
                        terminate_process(logserver, "UI image logserver")
                finally:
                    try:
                        if texture_image_run is not None:
                            texture_image_run.native.restore_pointer()
                    finally:
                        backend.close()
    validate_expected_log_text(text, ["Loader: project startup complete", "UiLayerSmokeProject: shutdown"],
        STRICT_LOG_FAILURE_MESSAGES)
    texture_image_run.write_report(True)
    print(f"UI concrete texture image: {len(texture_image_run.stages)} matching native/GPU gates "
        f"with {args.skin} atlas", flush=True)
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
