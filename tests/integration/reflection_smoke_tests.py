#!/usr/bin/env python3
"""Exercise reflection image oracles against correct geometry and false-positive failures."""

import os
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "smoke"))
from reflection_smoke import (  # noqa: E402
    BASELINE_CAPTURES, BUDGET_CAPTURES, CAPTURES, DEFAULT_RAY_BUDGET,
    FOREGROUND_REGION, GLASS_REGION, OPAQUE_REGION, SCREEN_CAPTURES, STATISTICS_FIELDS,
    SmokeFailure, analyze_markers, analyze_panels, capture, capture_environment, compare_marker_motion,
    compare_hybrid_statistics, compare_opaque_glass, compare_panel_motion, parse_statistics,
    region_pixels, validate_statistics,
)


class ReflectionSmokeAnalysisTests(unittest.TestCase):
    width = 400
    height = 300

    def marker_frame(self, moved=False):
        rows = [[(20, 20, 20) for _ in range(self.width)] for _ in range(self.height)]
        # Independent projected positions for a 60-degree vertical FOV, 14-unit virtual-source distance.
        shift = 17 if moved else 0
        for cx, cy, color in ((170 + shift, 157, (180, 5, 5)), (230 + shift, 139, (5, 180, 5))):
            for y in range(cy - 9, cy + 10):
                for x in range(cx - 9, cx + 10):
                    if (x - cx) ** 2 + (y - cy) ** 2 <= 81:
                        rows[y][x] = color
        return self.width, self.height, rows

    def glass_frames(self):
        rows = [[(160, 160, 160) if x % 20 < 10 else (10, 10, 10)
            for x in range(self.width)] for _ in range(self.height)]
        for x, y in region_pixels(self.width, self.height, FOREGROUND_REGION):
            rows[y][x] = (10, 20, 220)
        reflected = [list(row) for row in rows]
        for region in (OPAQUE_REGION, GLASS_REGION):
            left, top = int(region[0] * self.width) + 8, int(region[1] * self.height) + 8
            for y in range(top, top + 15):
                for x in range(left, left + 20):
                    reflected[y][x] = (180, 5, 5)
        return (self.width, self.height, rows), (self.width, self.height, reflected)

    def panel_frame(self, case="onscreen", mode="screen", reflection_shift=None):
        rows = [[(20, 20, 20) for _ in range(self.width)] for _ in range(self.height)]
        # Independently rounded projections at 400x300, computed from authored world-space rectangles.
        direct = {"red": (53, 185, 26, 22), "green": (347, 98, 26, 30)}
        reflected = {"red": (151, 162, 9, 7), "green": (249, 133, 9, 10)}
        if case == "onscreen_moved":
            direct = {"red": (74, 185, 26, 22), "green": (326, 98, 26, 30)}
            reflected = {"red": (158, 162, 9, 7), "green": (242, 133, 9, 10)}
        elif case == "boundary":
            direct.pop("green")
            reflected["green"] = (287, 133, 9, 10)
            if mode == "screen":
                reflected.pop("green")
        elif case == "floor":
            direct = {"red": (151, 133, 9, 7), "green": (249, 133, 9, 10)}
            reflected = {"red": (151, 248, 9, 7), "green": (249, 248, 9, 10)}
        if mode == "disabled":
            reflected.clear()
        for label, regions in (("direct", direct), ("reflection", reflected)):
            for color, (cx, cy, half_width, half_height) in regions.items():
                if label == "reflection" and reflection_shift:
                    cx += reflection_shift[0]
                    cy += reflection_shift[1]
                # Half-strength provisional screen radiance still has to retain visible, localized marker color.
                value = 90 if mode == "screen" and label == "reflection" else 180
                rgb = (value, 5, 5) if color == "red" else (5, value, 5)
                for y in range(cy - half_height, cy + half_height + 1):
                    for x in range(cx - half_width, cx + half_width + 1):
                        rows[y][x] = rgb
        return self.width, self.height, rows

    def test_projected_two_color_markers_and_known_motion_pass(self):
        result = compare_marker_motion(self.marker_frame(), self.marker_frame(moved=True))
        self.assertEqual(result["moved"]["red"]["measured_motion"], [17.0, 0.0])

    def test_flat_tint_cannot_pass_as_localized_reflection(self):
        frame = self.marker_frame()
        frame[2][:] = [[(180, 5, 5)] * self.width for _ in range(self.height)]
        with self.assertRaisesRegex(SmokeFailure, "predicted marker region"):
            analyze_markers(frame)

    def test_missing_or_stationary_marker_fails(self):
        with self.assertRaises(SmokeFailure):
            compare_marker_motion(self.marker_frame(), self.marker_frame())
        frame = self.marker_frame()
        for row in frame[2]:
            row[:] = [(20, 20, 20) if pixel[1] > 100 else pixel for pixel in row]
        with self.assertRaisesRegex(SmokeFailure, "missing.*green"):
            analyze_markers(frame)

    def test_correctly_located_sparse_marker_pixels_fail_area_requirement(self):
        frame = self.marker_frame()
        kept = 0
        for row in frame[2]:
            for x, pixel in enumerate(row):
                if pixel[0] > 100:
                    kept += 1
                    if kept > 90:
                        row[x] = (20, 20, 20)
        with self.assertRaisesRegex(SmokeFailure, "projected area"):
            analyze_markers(frame)

    def test_offscreen_marker_cannot_exist_in_disabled_or_screen_frame(self):
        with self.assertRaisesRegex(SmokeFailure, "without a hardware"):
            analyze_markers(self.marker_frame(), required=False)
        blank = (self.width, self.height, [[(20, 20, 20)] * self.width for _ in range(self.height)])
        self.assertEqual(analyze_markers(blank, required=False)["red"]["pixels"], 0)

    def test_both_receiver_types_preserve_foreground_and_exterior(self):
        result = compare_opaque_glass(*self.glass_frames())
        self.assertEqual(result["opaque_new_reflected_pixels"], 300)
        self.assertEqual(result["glass_new_reflected_pixels"], 300)
        self.assertEqual(result["foreground_pixels"], result["foreground_retained"])
        self.assertEqual(result["exterior_changed"], 0)

    def test_missing_glass_reflection_fails(self):
        before, after = self.glass_frames()
        for x, y in region_pixels(self.width, self.height, GLASS_REGION):
            after[2][y][x] = before[2][y][x]
        with self.assertRaisesRegex(SmokeFailure, "glass sphere"):
            compare_opaque_glass(before, after)

    def test_sparse_receiver_reflection_fails_region_coverage(self):
        before, after = self.glass_frames()
        kept = 0
        for x, y in region_pixels(self.width, self.height, GLASS_REGION):
            if after[2][y][x] != before[2][y][x]:
                kept += 1
                if kept > 55:
                    after[2][y][x] = before[2][y][x]
        with self.assertRaisesRegex(SmokeFailure, "glass sphere"):
            compare_opaque_glass(before, after)

    def test_expanded_foreground_fails_even_when_original_strip_is_retained(self):
        before, after = self.glass_frames()
        top = int(FOREGROUND_REGION[1] * self.height)
        for x, y in region_pixels(self.width, self.height, FOREGROUND_REGION):
            if y < top + 4:
                before[2][y][x] = (20, 20, 20)
        with self.assertRaisesRegex(SmokeFailure, "expanded"):
            compare_opaque_glass(before, after)

    def test_foreground_and_exterior_corruption_fail(self):
        before, after = self.glass_frames()
        for x, y in region_pixels(self.width, self.height, FOREGROUND_REGION):
            after[2][y][x] = (180, 5, 5)
        with self.assertRaisesRegex(SmokeFailure, "foreground AVBOIT"):
            compare_opaque_glass(before, after)
        before, after = self.glass_frames()
        for y in range(40, 80):
            for x in range(15, 55):
                after[2][y][x] = (180, 5, 5)
        with self.assertRaisesRegex(SmokeFailure, "outside"):
            compare_opaque_glass(before, after)

    def test_malformed_rows_fail(self):
        frame = self.marker_frame()
        frame[2].pop()
        with self.assertRaisesRegex(SmokeFailure, "malformed"):
            analyze_markers(frame)

    def test_all_panel_route_geometries_pass(self):
        for case, mode in SCREEN_CAPTURES:
            with self.subTest(case=case, mode=mode):
                result = analyze_panels(self.panel_frame(case, mode), case, mode)
                self.assertEqual(result["red"]["outside_predicted_regions"], 0)
                self.assertEqual(result["green"]["outside_predicted_regions"], 0)

    def test_direct_image_cannot_pass_as_screen_reflection(self):
        for case in ("onscreen", "floor"):
            with self.subTest(case=case), self.assertRaisesRegex(SmokeFailure, "missing red reflection"):
                analyze_panels(self.panel_frame(case, "disabled"), case, "screen")

    def test_floor_reflection_must_appear_below_its_direct_source(self):
        with self.assertRaisesRegex(SmokeFailure, "missing red reflection"):
            analyze_panels(self.panel_frame("floor", reflection_shift=(0, -115)), "floor", "screen")

    def test_displaced_reflection_cannot_pass_the_panel_projection(self):
        with self.assertRaisesRegex(SmokeFailure, "centroid|missing red reflection"):
            analyze_panels(self.panel_frame(reflection_shift=(8, 0)), "onscreen", "screen")

    def test_sparse_correctly_located_panel_fails_projected_area(self):
        frame = self.panel_frame()
        for y in range(150, 174):
            for x in range(138, 164):
                if not (149 <= x <= 153 and 160 <= y <= 164):
                    frame[2][y][x] = (20, 20, 20)
        with self.assertRaisesRegex(SmokeFailure, "projected area"):
            analyze_panels(frame, "onscreen", "screen")

    def test_boundary_screen_must_lose_offscreen_green_and_hybrid_must_retain_it(self):
        with self.assertRaisesRegex(SmokeFailure, "unexpected green reflection"):
            analyze_panels(self.panel_frame("boundary", "hybrid"), "boundary", "screen")
        with self.assertRaisesRegex(SmokeFailure, "missing green reflection"):
            analyze_panels(self.panel_frame("boundary", "screen"), "boundary", "hybrid")

    def test_panel_colors_outside_both_predicted_regions_fail(self):
        frame = self.panel_frame()
        for y in range(230, 250):
            for x in range(10, 30):
                frame[2][y][x] = (180, 5, 5)
        with self.assertRaisesRegex(SmokeFailure, "outside direct/reflected"):
            analyze_panels(frame, "onscreen", "screen")

    def test_inward_panel_motion_matches_distinct_direct_and_reflected_scales(self):
        result = compare_panel_motion(self.panel_frame(), self.panel_frame("onscreen_moved"))
        self.assertEqual(result["moved"]["red"]["direct"]["measured_motion"], [21.0, 0.0])
        self.assertEqual(result["moved"]["red"]["reflection"]["measured_motion"], [7.0, 0.0])
        self.assertEqual(result["moved"]["green"]["direct"]["measured_motion"], [-21.0, 0.0])
        self.assertEqual(result["moved"]["green"]["reflection"]["measured_motion"], [-7.0, 0.0])

    def test_wrong_reflected_motion_fails_even_within_static_centroid_tolerance(self):
        moved = self.panel_frame("onscreen_moved", reflection_shift=(-3, 0))
        analyze_panels(moved, "onscreen_moved", "screen")
        with self.assertRaisesRegex(SmokeFailure, "authored inward movement"):
            compare_panel_motion(self.panel_frame(), moved)

    def test_capture_matrix_keeps_baseline_and_four_floor_routes(self):
        self.assertEqual(len(CAPTURES), 20)
        self.assertEqual(len(set(CAPTURES)), len(CAPTURES))
        self.assertEqual(set(CAPTURES), set(BASELINE_CAPTURES) | set(SCREEN_CAPTURES))
        self.assertEqual({mode for case, mode in CAPTURES if case == "floor"},
            {"disabled", "screen", "hardware", "hybrid"})
        self.assertIn(("offscreen", "hybrid"), CAPTURES)

    def test_capture_environment_removes_inherited_fixture_controls(self):
        with patch.dict(os.environ, {"NWB_REFLECTION_SMOKE_TIMING": "1", "NWB_REFRACTION_SMOKE_ENABLED": "0",
            "NWB_REFLECTION_SMOKE_DEBUG": "source",
            "NWB_GPU_TIMING_FILE": "unexpected.txt", "NWB_RENDERER_BASELINE_CAPTURE_FREEZE_FRAME": "1"}):
            result = capture_environment("moved", "hardware")
        self.assertEqual(result["NWB_REFLECTION_SMOKE_CASE"], "moved")
        self.assertEqual(result["NWB_REFLECTION_SMOKE_MODE"], "hardware")
        self.assertEqual(result["NWB_RENDERER_BASELINE_FIXED_DELTA_SECONDS"], "0.016666667")
        self.assertEqual(result["NWB_REFLECTION_SMOKE_RAY_BUDGET"], str(DEFAULT_RAY_BUDGET))
        self.assertNotIn("NWB_REFLECTION_SMOKE_TIMING", result)
        self.assertNotIn("NWB_REFLECTION_SMOKE_DEBUG", result)
        self.assertNotIn("NWB_REFRACTION_SMOKE_ENABLED", result)
        self.assertNotIn("NWB_GPU_TIMING_FILE", result)
        self.assertNotIn("NWB_RENDERER_BASELINE_CAPTURE_FREEZE_FRAME", result)


class ReflectionCompletedStatisticsTests(unittest.TestCase):
    @staticmethod
    def sample(mode="hybrid", budget=DEFAULT_RAY_BUDGET, sequence=1, frame=3):
        requested = mode in ("hardware", "hybrid")
        value = dict(sequence=sequence, generation=1, frame=frame,
            mode=("disabled", "screen", "hardware", "hybrid").index(mode), width=960, height=720,
            requested_budget=budget, effective_budget=budget, queue_capacity=DEFAULT_RAY_BUDGET,
            hardware_requested=int(requested), hardware_available=1, hardware_ready=int(requested),
            token_queue=0, token_value=10 + sequence, physical_queue=0, device_generation=1,
            candidates=0, hardware_rays=0, hardware_hits=0, opaque_pixels=0, glass_pixels=0,
            fallback_pixels=0, screen_attempts=0, screen_hits=0)
        if mode != "disabled":
            value["opaque_pixels"] = 1000
            value["screen_attempts"] = 1000 if mode in ("screen", "hybrid") else 0
            value["screen_hits"] = 600 if mode in ("screen", "hybrid") else 0
            value["candidates"] = 1000 - value["screen_hits"] if requested else 0
            value["hardware_rays"] = min(value["candidates"], budget)
            value["hardware_hits"] = value["hardware_rays"] // 2
            value["fallback_pixels"] = 1000 - value["screen_hits"] - value["hardware_hits"]
        return value

    @staticmethod
    def log_line(sample):
        return "ReflectionSmokeStatistics: " + " ".join(f"{name}={sample[name]}" for name in STATISTICS_FIELDS)

    def test_parse_exact_completed_samples_and_ignore_unrelated_logs(self):
        first, second = self.sample(), self.sample(sequence=3, frame=5)
        text = "ordinary launch\n" + self.log_line(first) + "\nordinary shutdown\n" + self.log_line(second)
        samples = parse_statistics(text)
        self.assertEqual(samples, [first, second])
        self.assertEqual(validate_statistics(samples, "floor", "hybrid"), samples)

    def test_parser_rejects_missing_duplicate_and_noninteger_fields(self):
        line = self.log_line(self.sample())
        for malformed in (line.rsplit(" ", 1)[0], line + " sequence=1",
            line.replace("screen_hits=600", "screen_hits=nan"), line.replace("screen_hits=600", "unknown=600")):
            with self.subTest(malformed=malformed), self.assertRaises(SmokeFailure):
                parse_statistics(malformed)
        with self.assertRaisesRegex(SmokeFailure, "no completed"):
            parse_statistics("Reflection resolve: hardware")

    def test_every_route_and_zero_limited_budgets_have_valid_completed_partitions(self):
        for mode in ("disabled", "screen", "hardware", "hybrid"):
            with self.subTest(mode=mode):
                self.assertEqual(len(validate_statistics([self.sample(mode)], "floor", mode)), 1)
        for case, mode, budget in BUDGET_CAPTURES:
            sample = self.sample(mode, budget)
            self.assertEqual(sample["hardware_rays"], budget)
            self.assertGreater(sample["fallback_pixels"], 0)
            self.assertEqual(validate_statistics([sample], case, mode, budget), [sample])

    def test_static_capture_rejects_wrong_mode_dimensions_budget_and_generation(self):
        for field, value in (("mode", 1), ("width", 1920), ("requested_budget", 64),
            ("effective_budget", 64), ("queue_capacity", 0), ("generation", 2), ("device_generation", 2)):
            second = self.sample(sequence=2, frame=4)
            second[field] = value
            with self.subTest(field=field), self.assertRaises(SmokeFailure):
                validate_statistics([self.sample(), second], "floor", "hybrid")

    def test_rejected_token_stale_sample_and_uncompleted_warmup_fail(self):
        for field, value in (("token_value", 0), ("device_generation", 0), ("physical_queue", 65535)):
            sample = self.sample()
            sample[field] = value
            with self.subTest(field=field), self.assertRaisesRegex(SmokeFailure, "accepted token"):
                validate_statistics([sample], "floor", "hybrid")
        for second in (self.sample(), self.sample(sequence=2, frame=3)):
            with self.assertRaisesRegex(SmokeFailure, "stale or out of order"):
                validate_statistics([self.sample(), second], "floor", "hybrid")
        with self.assertRaisesRegex(SmokeFailure, "stable frame 3"):
            validate_statistics([self.sample(frame=2)], "floor", "hybrid")

    def test_every_sample_must_obey_budget_including_warmup(self):
        bad = self.sample(budget=64, frame=1)
        bad["hardware_rays"] = 65
        with self.assertRaisesRegex(SmokeFailure, "exceeded.*budget"):
            validate_statistics([bad, self.sample(budget=64, sequence=2)], "floor", "hybrid", 64)
        bad = self.sample(budget=0)
        bad["hardware_rays"] = 1
        with self.assertRaisesRegex(SmokeFailure, "exceeded.*budget"):
            validate_statistics([bad], "floor", "hybrid", 0)

    def test_missing_outcome_duplicate_count_and_impossible_population_fail(self):
        for field, value in (("fallback_pixels", 199), ("hardware_hits", 201),
            ("screen_hits", 1001), ("opaque_pixels", 960 * 720 + 1)):
            sample = self.sample()
            sample[field] = value
            with self.subTest(field=field), self.assertRaises(SmokeFailure):
                validate_statistics([sample], "floor", "hybrid")

    def test_screen_route_cannot_issue_hardware_work(self):
        sample = self.sample("screen")
        sample.update(candidates=1, hardware_rays=1, hardware_ready=1)
        with self.assertRaisesRegex(SmokeFailure, "non-hardware"):
            validate_statistics([sample], "floor", "screen")

    def test_disabled_nonzero_counter_and_missing_glass_are_rejected(self):
        disabled = self.sample("disabled")
        disabled.update(opaque_pixels=1, fallback_pixels=1)
        with self.assertRaisesRegex(SmokeFailure, "disabled reflection"):
            validate_statistics([disabled], "floor", "disabled")
        with self.assertRaisesRegex(SmokeFailure, "missing primary glass"):
            validate_statistics([self.sample("hardware")], "opaque_glass", "hardware")

    def test_provisional_screen_result_cannot_pass_as_an_accepted_floor_hit(self):
        sample = self.sample("screen")
        sample.update(screen_hits=0, fallback_pixels=1000)
        with self.assertRaisesRegex(SmokeFailure, "accepted screen hits"):
            validate_statistics([sample], "floor", "screen")

    def test_floor_ray_savings_require_accepted_hits_and_separated_completed_ranges(self):
        hardware = [self.sample("hardware")]
        hybrid = [self.sample()]
        result = compare_hybrid_statistics(hardware, hybrid)
        self.assertEqual(result["hardware_ray_range"], [1000, 1000])
        self.assertEqual(result["hybrid_hardware_ray_range"], [400, 400])
        equal = self.sample()
        equal["hardware_rays"] = 1000
        with self.assertRaisesRegex(SmokeFailure, "reduce completed hardware rays"):
            compare_hybrid_statistics(hardware, [equal])
        provisional = self.sample()
        provisional["screen_hits"] = 0
        with self.assertRaisesRegex(SmokeFailure, "accepting screen hits"):
            compare_hybrid_statistics(hardware, [provisional])

    def test_budget_environment_retains_explicit_zero(self):
        with patch.dict(os.environ, {"NWB_REFLECTION_SMOKE_RAY_BUDGET": "999"}):
            result = capture_environment("floor", "hybrid", 0)
        self.assertEqual(result["NWB_REFLECTION_SMOKE_RAY_BUDGET"], "0")

    def test_reduced_population_cannot_pass_as_hybrid_ray_savings(self):
        hybrid = self.sample()
        hybrid["opaque_pixels"] = 500
        with self.assertRaisesRegex(SmokeFailure, "matched eligible populations"):
            compare_hybrid_statistics([self.sample("hardware")], [hybrid])

    def test_zero_budget_capture_expects_fallback_without_masking_real_hardware_capability(self):
        for mode, route in (("hybrid", "screen-space"), ("hardware", "environment")):
            with self.subTest(mode=mode), tempfile.TemporaryDirectory() as directory:
                args = SimpleNamespace(output_directory=Path(directory), executable=Path("reflection.exe"),
                    working_directory=Path(directory), frames=16, timeout=60, logserver_executable=None,
                    require_hardware=True, application_arg=["--gpudbg"])
                sample = self.sample(mode, budget=0)
                frame = 960, 720, [[(20, 20, 20)] * 960] * 720

                def run_capture(command, **kwargs):
                    required = [command[i + 1] for i, arg in enumerate(command) if arg == "--expect-log-message"]
                    rejected = [command[i + 1] for i, arg in enumerate(command) if arg == "--reject-log-message"]
                    self.assertIn("Reflection resolve: " + route, required)
                    self.assertNotIn("Reflection resolve: hardware", required)
                    self.assertIn("Reflection resolve: hardware", rejected)
                    self.assertIn("ReflectionSmokeProject: hardware available", required)
                    self.assertEqual(kwargs["env"]["NWB_REFLECTION_SMOKE_RAY_BUDGET"], "0")
                    Path(command[command.index("--log-output") + 1]).write_text(self.log_line(sample), encoding="utf-8")
                    return SimpleNamespace(returncode=0)

                with patch("reflection_smoke.subprocess.run", side_effect=run_capture), \
                    patch("reflection_smoke.read_bmp_24_rows", return_value=frame), patch("builtins.print"):
                    self.assertEqual(capture(args, "floor", mode, 0), (frame, [sample]))


if __name__ == "__main__":
    unittest.main()
