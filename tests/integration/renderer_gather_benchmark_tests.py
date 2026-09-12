#!/usr/bin/env python3
"""Analyzer and temporary-directory generator contracts; these tests never launch a renderer."""

import copy
import hashlib
import io
import json
from pathlib import Path
import sys
from tempfile import TemporaryDirectory
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "smoke"))

import renderer_gather_benchmark as bench
from generate_gather_benchmark_assets import generated_contents, main as generate_assets


def memory_snapshot(frame, count):
    return {scope: {"present": True, "frame": frame, "reserved": 2048, "used": 128,
        "historical_arena_peak": 1024, "allocations": count * 3, "reallocations": count,
        "deallocations": count * 2} for scope in bench.ARENAS}


def rows(workload="shared", mode="timing"):
    configuration = dict(bench.FROZEN_SETTINGS, type="configuration", schema=1, workload=workload, mode=mode,
        successful_frames=384, renderers=65, runtime_renderers=0, runtime_owners=0,
        transparent_renderers=0 if workload == "opaque" else 32 if workload == "hybrid" else 64)
    result = [configuration]
    if mode == "memory":
        result.append({"type": "memory_baseline", "arenas": memory_snapshot(95, 100)})
    for frame in range(96, 352):
        row = {"type": "cpu", "source_frame": frame, "publish_frame": frame, "scopes_ms": {
            "graphics.frame": 8., "graphics.frame_preamble": .05, "graphics.prepare_resources": 2.,
            "graphics.render_passes": 2., "graphics.render": 5., "frame.project_update": .1,
            "graphics.present": .2, "graphics.begin_frame": .4}}
        if mode == "memory":
            row["arenas"] = memory_snapshot(frame, frame - 96 + 101)
        result.append(row)
    scopes = bench.GPU_BASE + (() if workload == "opaque" else bench.GPU_TRANSPARENT)
    for scope in scopes:
        for first, last, mean in ((96, 159, 2.), (160, 351, 4.)):
            count = last - first + 1
            result.append({"type": "gpu", "scope": scope, "first_source_frame": first,
                "last_source_frame": last, "publish_frame": last + 2, "total_ms": count * mean, "samples": count})
    result.append({"type": "complete"})
    return result


def balanced_trials():
    result = bench.summarize_rows(rows(), "shared", "timing")
    orders = [["baseline", "candidate"], ["candidate", "baseline"]] * 4
    trials = []
    for block, order in enumerate(orders):
        for position, arm in enumerate(order):
            value = copy.deepcopy(result)
            if arm == "candidate":
                value["cpu"]["graphics.render"]["mean_ms"] -= .4
                value["cpu"]["graphics.prepare_resources"]["mean_ms"] -= .4
            trials.append(dict(block=block, position=position, arm=arm, result=value))
    return trials, orders


class RendererGatherBenchmarkAnalysisTests(unittest.TestCase):
    def test_weights_gpu_totals_by_actual_completed_ranges(self):
        result = bench.summarize_rows(rows(), "shared", "timing")
        self.assertEqual(result["source_window"], [96, 351])
        self.assertEqual(result["gpu"]["render.frame"]["mean_ms"], 3.5)
        self.assertEqual(result["cpu"]["graphics.prepare_resources"]["mean_ms"], 2.)
        self.assertEqual(result["cpu"]["graphics.render"]["total_ms"], 1280.)

    def test_rejects_duplicate_cpu_source_frame(self):
        values = rows()
        values[2]["source_frame"] = 96
        with self.assertRaisesRegex(bench.SmokeFailure, "duplicate, skipped"):
            bench.summarize_rows(values, "shared", "timing")

    def test_rejects_missing_real_preparation_scope(self):
        values = rows()
        del values[1]["scopes_ms"]["graphics.prepare_resources"]
        with self.assertRaisesRegex(bench.SmokeFailure, "totals or parent"):
            bench.summarize_rows(values, "shared", "timing")

    def test_rejects_failed_preparation_marker(self):
        values = rows()
        values[1]["scopes_ms"]["graphics.prepare_resources_failed"] = .4
        with self.assertRaisesRegex(bench.SmokeFailure, "totals or parent"):
            bench.summarize_rows(values, "shared", "timing")

    def test_rejects_parent_double_counting(self):
        values = rows()
        values[1]["scopes_ms"]["graphics.prepare_resources"] = 4.
        with self.assertRaisesRegex(bench.SmokeFailure, "subtotals exceed"):
            bench.summarize_rows(values, "shared", "timing")

    def test_rejects_missing_completed_frame_control(self):
        values = [row for row in rows() if row.get("scope") != "render.frame"]
        with self.assertRaisesRegex(bench.SmokeFailure, "coverage is missing"):
            bench.summarize_rows(values, "shared", "timing")

    def test_rejects_excessive_mixed_warmup_gpu_publication(self):
        values = rows()
        for row in values:
            if row.get("type") == "gpu" and row["first_source_frame"] == 96:
                row["first_source_frame"] = 95
                row["samples"] += 1
        with self.assertRaisesRegex(bench.SmokeFailure, "below 90%"):
            bench.summarize_rows(values, "shared", "timing")

    def test_rejects_duplicated_gpu_publication(self):
        values = rows()
        values.insert(-1, copy.deepcopy(next(row for row in values if row.get("type") == "gpu")))
        with self.assertRaisesRegex(bench.SmokeFailure, "overlapping GPU"):
            bench.summarize_rows(values, "shared", "timing")

    def test_allows_only_actual_initial_opaque_target_clear(self):
        values = rows("opaque")
        values.insert(-1, {"type": "gpu", "scope": "render.avboit_clear", "first_source_frame": 0,
            "last_source_frame": 0, "publish_frame": 2, "samples": 1, "total_ms": .01})
        self.assertEqual(bench.summarize_rows(values, "opaque", "timing")["gpu"]["render.frame"]["gpu_samples"], 256)
        values[-2]["last_source_frame"] = 96
        with self.assertRaisesRegex(bench.SmokeFailure, "inactive measured"):
            bench.summarize_rows(values, "opaque", "timing")

    def test_rejects_enabled_history_even_outside_measured_window(self):
        values = rows()
        values.insert(-1, {"type": "gpu", "scope": "render.reflection_temporal", "first_source_frame": 0,
            "last_source_frame": 0, "publish_frame": 2, "samples": 1, "total_ms": .01})
        with self.assertRaisesRegex(bench.SmokeFailure, "inactive GPU"):
            bench.summarize_rows(values, "shared", "timing")

    def test_memory_counters_use_warmup_baseline_not_lifetime_totals(self):
        result = bench.summarize_rows(rows(mode="memory"), "shared", "memory")
        scope = result["memory"][bench.ARENAS[0]]
        self.assertEqual(scope["allocation_deltas"], 3.)
        self.assertEqual(scope["reallocation_deltas"], 1.)
        self.assertEqual(scope["historical_arena_peak"], 1024)
        self.assertEqual(scope["available_frames"], 256)

    def test_timing_campaign_rejects_memory_capture(self):
        values = rows()
        values[1]["arenas"] = memory_snapshot(96, 10)
        with self.assertRaisesRegex(bench.SmokeFailure, "contaminated"):
            bench.summarize_rows(values, "shared", "timing")

    def test_unavailable_optional_arena_is_null_not_zero_savings(self):
        values = rows(mode="memory")
        optional = "impl/ecs_render/avboit_transparent_csg"
        for row in values:
            if "arenas" in row:
                row["arenas"][optional]["present"] = False
        result = bench.summarize_rows(values, "shared", "memory")["memory"][optional]
        self.assertIsNone(result["allocation_deltas"])
        self.assertFalse(result["complete"])
        self.assertEqual(result["available_frames"], 0)

    def test_required_scratch_owner_must_exist(self):
        values = rows(mode="memory")
        values[2]["arenas"][bench.REQUIRED_ARENAS[0]]["present"] = False
        with self.assertRaisesRegex(bench.SmokeFailure, "scratch owner is unavailable"):
            bench.summarize_rows(values, "shared", "memory")

    def test_counter_reset_is_rejected(self):
        values = rows(mode="memory")
        values[3]["arenas"][bench.ARENAS[0]]["allocations"] = 0
        with self.assertRaisesRegex(bench.SmokeFailure, "cumulative counters reset"):
            bench.summarize_rows(values, "shared", "memory")

    def test_missing_runtime_owner_does_not_qualify(self):
        values = rows()
        values[0].update(workload="runtime", transparent_renderers=56)
        with self.assertRaisesRegex(bench.SmokeFailure, "runtime workload"):
            bench.summarize_rows(values, "runtime", "timing")

    def test_real_rendering_settings_must_match(self):
        values = rows()
        values[0]["hardware_budget"] = 0
        with self.assertRaisesRegex(bench.SmokeFailure, "fixture controls changed"):
            bench.summarize_rows(values, "shared", "timing")

    def test_incomplete_footer_does_not_qualify(self):
        with self.assertRaisesRegex(bench.SmokeFailure, "complete result footer"):
            bench.summarize_rows(rows()[:-1], "shared", "timing")

    def test_memory_only_comparison_never_performs_timing_inference(self):
        result = bench.summarize_rows(rows(mode="memory"), "shared", "memory")
        orders = [["baseline", "candidate"], ["candidate", "baseline"]] * 4
        trials = [dict(block=block, position=position, arm=arm, result=result)
            for block, order in enumerate(orders) for position, arm in enumerate(order)]
        with patch.object(bench, "paired_statistics", side_effect=AssertionError("timing inference forbidden")):
            self.assertEqual(bench.compare(trials, orders, "memory")["status"], "memory_observation_only")

    def test_balanced_comparison_rejects_partial_trial_matrix(self):
        with self.assertRaisesRegex(bench.SmokeFailure, "all balanced"):
            bench.compare([], [["baseline", "candidate"]] * 8, "timing")

    def test_cpu_reduction_requires_stable_gpu_and_complete_cpu_controls(self):
        trials, orders = balanced_trials()
        result = bench.compare(trials, orders, "timing")
        self.assertEqual(result["status"], "resolved_cpu_render_reduction")
        self.assertEqual(set(result["gpu_controls"]), set(bench.GPU_CONTROLS))
        self.assertAlmostEqual(result["cpu"]["graphics.prepare_resources"]["mean_ms"], -.4)

    def test_gpu_control_drift_blocks_cpu_benefit_claim(self):
        trials, orders = balanced_trials()
        for trial in trials:
            if trial["arm"] == "candidate":
                trial["result"]["gpu"]["render.shadow_visibility"]["mean_ms"] += .5
        self.assertEqual(bench.compare(trials, orders, "timing")["status"], "gpu_control_drift")

    def test_total_cpu_frame_regression_cannot_hide_behind_callback_improvement(self):
        trials, orders = balanced_trials()
        for trial in trials:
            if trial["arm"] == "candidate":
                trial["result"]["cpu"]["graphics.frame"]["mean_ms"] += 1.
        self.assertEqual(bench.compare(trials, orders, "timing")["status"], "whole_cpu_frame_regression_or_uncertain")

    def test_generator_has_bounded_distinct_keys_without_changing_geometry(self):
        contents = generated_contents("mesh asset;\n", "// shader\n")
        meshes = [data for path, data in contents.items() if "/meshes/" in path]
        materials = [data for path, data in contents.items() if "/materials/" in path]
        self.assertEqual(len(contents), 129)
        self.assertEqual(len(meshes), 64)
        self.assertEqual(len(materials), 64)
        self.assertEqual(set(meshes), {b"mesh asset;\r\n"})
        self.assertEqual(len(set(materials)), 1)

    def test_generator_rejects_stale_files_before_overwriting_and_keeps_source_root_separate(self):
        with TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "authored"
            source.mkdir()
            mesh = source / "mesh.nwb"
            mesh.write_bytes(b"mesh asset;\r\n")
            surface = root / "template.surface"
            surface.write_bytes(b"// surface\r\n")
            output = root / "generated"
            arguments = ["--source-root", str(source), "--mesh-template", str(mesh),
                "--surface-template", str(surface), "--output-root", str(output)]
            self.assertEqual(generate_assets(arguments), 0)
            identity = json.loads((output / "generation_identity.json").read_text(encoding="utf-8"))
            actual = {path.relative_to(output).as_posix(): path.read_bytes()
                for path in output.rglob("*") if path.is_file()}
            self.assertEqual(set(actual), set(identity["files"]) | {"generation_identity.json"})
            self.assertEqual({path: hashlib.sha256(data).hexdigest()
                for path, data in actual.items() if path != "generation_identity.json"}, identity["files"])
            stale = output / "retired.nwb"
            stale.write_bytes(b"retired asset;\r\n")
            mesh.write_bytes(b"mesh changed;\r\n")
            before = {path.relative_to(output).as_posix(): path.read_bytes()
                for path in output.rglob("*") if path.is_file()}
            with patch("sys.stderr", new_callable=io.StringIO) as errors:
                with self.assertRaises(SystemExit) as failure:
                    generate_assets(arguments)
                self.assertEqual(failure.exception.code, 2)
                self.assertIn("unexpected existing generated files: retired.nwb", errors.getvalue())
            self.assertEqual({path.relative_to(output).as_posix(): path.read_bytes()
                for path in output.rglob("*") if path.is_file()}, before)
            nested_output = source / "generated"
            with patch("sys.stderr", new_callable=io.StringIO) as errors:
                with self.assertRaises(SystemExit) as failure:
                    generate_assets(arguments[:-1] + [str(nested_output)])
                self.assertEqual(failure.exception.code, 2)
                self.assertIn("generated root must be separate", errors.getvalue())
            self.assertFalse(nested_output.exists())
            self.assertEqual(mesh.read_bytes(), b"mesh changed;\r\n")

    def test_explicit_vulkan_layers_are_rejected_before_timing_or_memory_acquisition(self):
        for key in ("VK_INSTANCE_LAYERS", "VK_LOADER_LAYERS_ENABLE"):
            for mode in ("timing", "memory"):
                with self.subTest(key=key, mode=mode):
                    with self.assertRaisesRegex(bench.SmokeFailure, "explicit Vulkan layer override"):
                        bench.environment({key: "VK_LAYER_KHRONOS_validation"}, "shared", mode, "capture.jsonl")
                    self.assertEqual(bench.environment({key: ""}, "shared", mode, "capture.jsonl")[key], "")

    def test_inherited_smoke_and_diagnostic_controls_are_sanitized(self):
        env = bench.environment({"NWB_REFLECTION_SMOKE_DIAGNOSTICS": "1", "NWB_GPU_TIMING_FILE": "stale",
            "NWB_RENDERER_BASELINE_CAPTURE_FREEZE_FRAME": "1", "NWB_LINUX_BACKEND": "x11", "PATH": "keep"},
            "unique", "timing", "capture.jsonl")
        self.assertNotIn("NWB_GPU_TIMING_FILE", env)
        self.assertNotIn("NWB_REFLECTION_SMOKE_DIAGNOSTICS", env)
        self.assertEqual(env["NWB_RENDERER_BASELINE_CAPTURE_FREEZE_FRAME"], "0")
        self.assertEqual(env["NWB_GATHER_BENCHMARK_WORKLOAD"], "unique")


if __name__ == "__main__":
    unittest.main()
