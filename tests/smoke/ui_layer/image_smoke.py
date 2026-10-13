#!/usr/bin/env python3
"""Qualify passive image geometry, retained options, tint, clipping and popup scope."""
from __future__ import annotations

import json
import platform
import sys

from window_smoke import parse_args
from image_probe import center, observe_image, snapshot_from_logs
from window_capture_smoke import run_smoke_main
from passive_run import PassiveImageRun, run_passive_image


class ImageRun(PassiveImageRun):
    def __init__(self, args, backend, handle, process, log_directory, log_baseline, log_pattern):
        super().__init__(args, backend, handle, process, log_directory, log_baseline, log_pattern,
            minimum_height=480.0, snapshot_reader=snapshot_from_logs, observer=observe_image, center=center)
        self.expected = { "phase": 0, "focus_code": 0, "before_clicks": 0, "after_clicks": 0, "parent": 0, "child": 0, "popup_count": 0, "image_targets": 0, "declared_region": 1, "post_declaration_region": 2, }

    def checkpoint(self, name, *, extent=None, extra=None, **changes):
        return self.capture_checkpoint(name, extent=extent, extra=extra, stage_timeout=10.0, changes=changes)

    def write_report(self, passed):
        report = { "passed": passed, "platform": platform.system(), "skin": self.args.skin, "stages": self.stages, "failure": self.failure_report, "input_path": "Win32 constrained physical cursor with posted pointer/key messages"
                if self.native.windows else "X11 XSendEvent keys/buttons and XSetInputFocus", "runtime_limit": "No native Wayland, physical button injection, or live IME qualification.", }
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
        self.checkpoint("resize_recomputes_stretch_and_preserves_retained_images", extent=physical_extent, extra=lambda snapshot: snapshot["rectangles"]["stretch"][2] != old_width and abs(snapshot["logical_extent"][0] - target_extent[0]) < 1.0
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
    return run_passive_image(args, fixture="IMAGE", display_marker="UiImageSmoke: display", driver_type=ImageRun, log_name="image.log", label="UI image")


def main(argv):
    return run_smoke_main(argv, parse_args, run, default_timeout=120)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
