#!/usr/bin/env python3
"""Reject stale, incomplete and internally inconsistent refraction work evidence."""

import copy
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "smoke"))
from refraction_query_diagnostics_smoke import (  # noqa: E402
    COUNTER_FIELDS, Cell, SmokeFailure, parse_completed_statistics, projected_bounds, validate_statistics,
)


class RefractionQueryDiagnosticsTests(unittest.TestCase):
    cell = Cell("960x720", 50, "hardware")

    def profile(self, cell=None):
        cell = cell or self.cell
        left, top, right, bottom = projected_bounds(cell.width, cell.height, cell.density)
        pixels = (right - left) * (bottom - top)
        counters = dict.fromkeys(COUNTER_FIELDS, 0)
        for name in ("hardwarePixels", "entryQueries", "associatedEntries", "exitQueries", "volumeExits", "backgroundQueries", "cachedOpaqueReturns", "cachedHitAttempts"):
            counters[name] = pixels
        counters["physicalQueries"] = counters["queryBudgetUnits"] = 3 * pixels
        return {"metadata": {"sequence": 5, "generation": 2, "graphics_frame": 30, "observed_frame": 32, "target_start_frame": 10,
            "completed_target_frames": 20, "frame_index": 0, "width": cell.width, "height": cell.height, "hardware_requested": 1,
            "hardware_available": 1, "hardware_ready": 1, "csg_enabled": 0, "token_valid": 1, "physical_valid": 1, "token_queue": 0,
            "token_value": 40, "physical_queue": 0, "device_generation": 1}, "counters": counters}

    def text(self, profile=None):
        profile = profile or self.profile()
        metadata = profile["metadata"]
        key = f"sequence={metadata['sequence']} generation={metadata['generation']}"
        lines = ["RefractionQueryDiagnostics: " + " ".join(f"{name}={value}" for name, value in metadata.items())]
        lines += [f"RefractionQueryCounter: {key} name={name} value={value}" for name, value in profile["counters"].items()]
        lines += [f"RefractionQueryDiagnostics: complete {key} counters={len(COUNTER_FIELDS)}", "FramebufferCapture: capture ready",
            f"FramebufferCapture: graphics source frame {metadata['observed_frame'] + 1}"]
        return "\n".join(lines) + "\n"

    def assert_rejected(self, profile, expression):
        validate_statistics(self.profile(), self.cell)
        with self.assertRaisesRegex(SmokeFailure, expression):
            validate_statistics(profile, self.cell)

    def test_missing_counter_and_completion_cannot_publish_partial_frame(self):
        text = self.text()
        parsed = parse_completed_statistics(text)
        validate_statistics(parsed, self.cell)
        with self.assertRaisesRegex(SmokeFailure, "incomplete"):
            parse_completed_statistics(text.replace(next(line for line in text.splitlines(True) if "name=exitQueries " in line), ""))
        with self.assertRaisesRegex(SmokeFailure, "one completed"):
            parse_completed_statistics(text.replace(next(line for line in text.splitlines(True) if ": complete " in line), ""))

    def test_duplicate_counter_and_wrong_reservation_identity_are_rejected(self):
        text = self.text()
        record = next(line for line in text.splitlines(True) if "name=exitQueries " in line)
        with self.assertRaisesRegex(SmokeFailure, "duplicated"):
            parse_completed_statistics(text.replace(record, record + record))
        with self.assertRaisesRegex(SmokeFailure, "does not belong"):
            parse_completed_statistics(text.replace(record, record.replace("sequence=5", "sequence=6")))

    def test_malformed_or_unrecognized_record_cannot_be_silently_ignored(self):
        text = self.text()
        with self.assertRaisesRegex(SmokeFailure, "malformed"):
            parse_completed_statistics(text + "RefractionQueryCounter: corrupt\n")
        with self.assertRaisesRegex(SmokeFailure, "unknown"):
            parse_completed_statistics(text.replace("name=exitQueries", "name=inventedCounter"))
        with self.assertRaisesRegex(SmokeFailure, "metadata"):
            parse_completed_statistics(text.replace("frame_index=0", "frame_index=0 frame_index=0"))

    def test_capture_must_follow_the_complete_counter_block(self):
        text = self.text()
        with self.assertRaisesRegex(SmokeFailure, "before framebuffer"):
            parse_completed_statistics("FramebufferCapture: capture ready\n" + text.replace("FramebufferCapture: capture ready\n", ""))
        with self.assertRaisesRegex(SmokeFailure, "source must follow"):
            parse_completed_statistics(text.replace("graphics source frame 33", "graphics source frame 30"))

    def test_hardware_fallback_and_unidentified_completion_are_rejected(self):
        for field, value in (("hardware_ready", 0), ("token_valid", 0), ("physical_valid", 0), ("token_value", 0),
                ("device_generation", 0), ("physical_queue", (1 << 32) - 1)):
            with self.subTest(field=field):
                profile = self.profile()
                profile["metadata"][field] = value
                self.assert_rejected(profile, "hardware route|invalid")

    def test_stale_wrong_extent_and_insufficient_target_frames_are_rejected(self):
        for changes in ({"width": 967}, {"graphics_frame": 9}, {"graphics_frame": 33}, {"completed_target_frames": 19},
                {"graphics_frame": 25, "completed_target_frames": 15}):
            with self.subTest(changes=changes):
                profile = self.profile()
                profile["metadata"].update(changes)
                self.assert_rejected(profile, "extent|stale")

    def test_overflow_and_out_of_range_counts_cannot_look_like_valid_zero_work(self):
        for field, value in (("overflowFlags", 1), ("exitQueries", -1), ("exitQueries", 1 << 32)):
            with self.subTest(field=field, value=value):
                profile = self.profile()
                profile["counters"][field] = value
                self.assert_rejected(profile, "overflow|unsigned")

    def test_internally_consistent_missing_pixels_still_fail_independent_coverage(self):
        profile = self.profile()
        for name in ("hardwarePixels", "entryQueries", "associatedEntries", "exitQueries", "volumeExits", "backgroundQueries", "cachedOpaqueReturns", "cachedHitAttempts"):
            profile["counters"][name] -= 64
        profile["counters"]["physicalQueries"] -= 192
        profile["counters"]["queryBudgetUnits"] -= 192
        self.assert_rejected(profile, "independent projected rectangle")

    def test_missing_entry_exit_or_background_work_fails_conservation(self):
        for field in ("entryQueries", "associatedEntries", "exitQueries", "volumeExits", "backgroundQueries"):
            with self.subTest(field=field):
                profile = self.profile()
                profile["counters"][field] += 1
                self.assert_rejected(profile, "conservation|bounded")

    def test_tir_and_background_caps_require_three_actual_events(self):
        profile = self.profile()
        c = profile["counters"]
        pixels = c["hardwarePixels"]
        c.update(exitQueries=pixels + 2, tirEvents=3, tirCappedPixels=1, volumeExits=pixels - 1,
            backgroundQueries=pixels + 1, transparentHits=3, backgroundLayerCapPixels=1,
            cachedOpaqueReturns=pixels - 2, cachedHitAttempts=pixels - 2, exitStraightFallbackPixels=1, materialShadeCalls=3)
        c["physicalQueries"] = c["queryBudgetUnits"] = c["entryQueries"] + c["exitQueries"] + c["backgroundQueries"]
        validate_statistics(profile, self.cell)
        for field in ("tirEvents", "transparentHits", "tirCappedPixels", "backgroundLayerCapPixels"):
            with self.subTest(field=field):
                changed = copy.deepcopy(profile)
                changed["counters"][field] -= 1
                self.assert_rejected(changed, "conservation|TIR|layer caps")

    def test_cache_and_material_work_cannot_be_relabelled_as_free_returns(self):
        for field in ("cachedHitAttempts", "cachedHitMisses", "materialShadeCalls", "shadedOpaqueReturns"):
            with self.subTest(field=field):
                profile = self.profile()
                profile["counters"][field] += 1
                self.assert_rejected(profile, "conservation")

    def test_ordinary_queries_cannot_be_relabelled_as_csg_expansion(self):
        for field in ("physicalQueries", "queryBudgetUnits", "entryFailures", "backgroundFailures"):
            with self.subTest(field=field):
                profile = self.profile()
                profile["counters"][field] += 1
                self.assert_rejected(profile, "conservation")
        profile = self.profile()
        profile["metadata"]["csg_enabled"] = 1
        self.assert_rejected(profile, "CSG physical work")

    def test_empty_density_requires_completed_zero_work_instead_of_no_sample(self):
        cell = Cell("960x720", 0, "hardware")
        profile = self.profile(cell)
        validate_statistics(profile, cell)
        profile["counters"]["entryQueries"] = 1
        with self.assertRaisesRegex(SmokeFailure, "conservation"):
            validate_statistics(profile, cell)
        with self.assertRaisesRegex(SmokeFailure, "one completed"):
            parse_completed_statistics("FramebufferCapture: capture ready\n")


if __name__ == "__main__":
    unittest.main()
