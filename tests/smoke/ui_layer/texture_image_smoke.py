#!/usr/bin/env python3
"""Qualify owned concrete texture bindings, copied inputs, cache lifetimes and Builder popup images."""
from __future__ import annotations

import json
import platform
import sys

from window_smoke import parse_args
from texture_image_probe import center, observe_texture_image, snapshot_from_logs
from window_capture_smoke import run_smoke_main
from passive_run import PassiveImageRun, run_passive_image


class TextureImageRun(PassiveImageRun):
    def __init__(self, args, backend, handle, process, log_directory, log_baseline, log_pattern):
        super().__init__(args, backend, handle, process, log_directory, log_baseline, log_pattern,
            minimum_height=520.0, snapshot_reader=snapshot_from_logs, observer=observe_texture_image, center=center)
        self.expected = { "phase": 0, "focus_code": 0, "before_clicks": 0, "after_clicks": 0, "parent": 0, "child": 0, "popup_count": 0, "image_targets": 0, "declared_source": 1, "post_handle_empty": 1, "replacement_count": 0, "replacement_alternate": 0,
            "eviction_remaining": 0, "eviction_completed": 0, "builder_source": 1, "builder_handle_empty": 1, }

    def checkpoint(self, name, *, extent=None, extra=None, stage_timeout=10.0, **changes):
        return self.capture_checkpoint(name, extent=extent, extra=extra, stage_timeout=stage_timeout, changes=changes)

    def write_report(self, passed):
        report = { "passed": passed, "platform": platform.system(), "skin": self.args.skin, "stages": self.stages, "failure": self.failure_report, "input_path": "Win32 constrained physical cursor with posted pointer/key messages"
                if self.native.windows else "X11 XSendEvent keys/buttons and XSetInputFocus", "runtime_limit": "No native Wayland, physical button injection or live IME coverage.",
            "overlay_contract": ("Actual Builder source widgets paint in the parent/child popup family above " "late base sentinels; primitive UV/crop checks remain separate."),
            "builder_contract": ("A passive Stretch/Fixed image retains copied source/options after the caller " "resets its handle and changes width, height and tint before deferred painting."),
            "cache_contract": ("F7 creates and displays one fresh same-path payload per accepted paint callback " "for 72 versions."), }
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
        self.checkpoint("same_identity_new_payload_preserves_original_source_pixels", replacement_count=1, replacement_alternate=1, extra=lambda snapshot: snapshot["generations"][:2] == original_generations and snapshot["generations"][2] != old_generation)
        old_generation = self.snapshot["generations"][2]
        self.native.tap("F6")
        self.checkpoint("second_fresh_version_restores_default_payload", replacement_count=2, replacement_alternate=0, extra=lambda snapshot: snapshot["generations"][:2] == original_generations and snapshot["generations"][2] != old_generation)
        old_generation = self.snapshot["generations"][2]
        self.native.tap("F7")
        self.checkpoint("72_accepted_versions_preserve_originals_and_final_payload", replacement_count=74, replacement_alternate=0, eviction_remaining=0, eviction_completed=72, stage_timeout=30.0,
            extra=lambda snapshot: snapshot["generations"][:2] == original_generations and snapshot["generations"][2] != old_generation)
        self.native.tap("F4")
        self.checkpoint("paint_declaration_copies_source_handle_rectangle_uv_and_tint", phase=1, declared_source=2, builder_source=2)
        old_extent = self.snapshot["logical_extent"]
        old_width = self.snapshot["rectangles"]["frozen"][2]
        target_extent = round(old_extent[0]) + 96, round(old_extent[1]) + 64
        physical_extent = self.point(*target_extent)
        self.backend.resize_client(self.handle, *physical_extent)
        self.checkpoint("resize_preserves_owned_versions_and_recomputes_image_rectangles", extent=physical_extent, extra=lambda snapshot: snapshot["rectangles"]["frozen"][2] != old_width and snapshot["generations"][:2] == original_generations
                and abs(snapshot["logical_extent"][0] - target_extent[0]) < 1.0 and abs(snapshot["logical_extent"][1] - target_extent[1]) < 1.0)
        self.native.tap("F5")
        self.checkpoint("actual_builder_child_parent_images_cover_late_base_sentinels", parent=1, child=1, popup_count=2, focus_code=4)
        self.native.tap("Tab")
        self.checkpoint("accepted_child_keyboard_scope_remains_active", focus_code=4)
        self.native.tap("Escape")
        self.checkpoint("child_close_retires_builder_source_image_and_restores_parent", parent=1, child=0, popup_count=1, focus_code=3)
        self.native.tap("Escape")
        self.checkpoint("parent_close_retires_builder_source_image_and_restores_root", parent=0, child=0, popup_count=0, focus_code=0)
        self.native.tap("F4")
        self.checkpoint("final_original_and_replacement_versions_remain_independent", phase=0, declared_source=1, builder_source=1)

def run(args):
    return run_passive_image(args, fixture="TEXTURE_IMAGE", display_marker="UiTextureImageSmoke: display", driver_type=TextureImageRun, log_name="texture_image.log", label="UI concrete texture image")


def main(argv):
    return run_smoke_main(argv, parse_args, run, default_timeout=120)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
