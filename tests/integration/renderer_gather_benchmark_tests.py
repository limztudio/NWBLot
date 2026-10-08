#!/usr/bin/env python3
"""Analyzer and temporary-directory generator contracts; these tests never launch a renderer."""

import copy
import io
from pathlib import Path
import sys
from tempfile import TemporaryDirectory
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "smoke"))

import renderer_gather_benchmark as bench
from generate_gather_benchmark_assets import main as generate_assets

LIT_PRESENT = "present"
LIT_HISTORICAL_ARENA_PEAK = "historical_arena_peak"
LIT_ALLOCATIONS = "allocations"
LIT_SHARED = "shared"
LIT_TIMING = "timing"
LIT_OPAQUE = "opaque"
LIT_MEMORY = "memory"
LIT_TYPE = "type"
LIT_ARENAS = "arenas"
LIT_CPU = "cpu"
LIT_SOURCE_FRAME = "source_frame"
LIT_PUBLISH_FRAME = "publish_frame"
LIT_SCOPES_MS = "scopes_ms"
LIT_GRAPHICS_FRAME = "graphics.frame"
LIT_GRAPHICS_PREPARE_RESOURCES = "graphics.prepare_resources"
LIT_GRAPHICS_RENDER = "graphics.render"
LIT_GPU = "gpu"
LIT_SCOPE = "scope"
LIT_FIRST_SOURCE_FRAME = "first_source_frame"
LIT_LAST_SOURCE_FRAME = "last_source_frame"
LIT_TOTAL_MS = "total_ms"
LIT_SAMPLES = "samples"
LIT_COMPLETE = "complete"
LIT_BASELINE = "baseline"
LIT_CANDIDATE = "candidate"
LIT_MEAN_MS = "mean_ms"
LIT_RENDER_FRAME = "render.frame"
LIT_TOTALS_OR_PARENT = "totals or parent"
LIT_ALLOCATION_DELTAS = "allocation_deltas"
LIT_AVAILABLE_FRAMES = "available_frames"
LIT_RUNTIME = "runtime"
LIT_STATUS = "status"
LIT_ARM = "arm"
LIT_RESULT = "result"
LIT_MESH_ASSET_R_N = b"mesh asset;\r\n"
LIT_GENERATED = "generated"
LIT_MESH_CHANGED_R_N = b"mesh changed;\r\n"
LIT_SYS_STDERR = "sys.stderr"
LIT_CAPTURE_JSONL = "capture.jsonl"
LIT_NWB_REFLECTION_SMOKE_DIAGNOSTICS = "NWB_REFLECTION_SMOKE_DIAGNOSTICS"
LIT_NWB_GPU_TIMING_FILE = "NWB_GPU_TIMING_FILE"
LIT_NWB_RENDERER_BASELINE_CAPTURE_FREEZE_F = "NWB_RENDERER_BASELINE_CAPTURE_FREEZE_FRAME"
LIT_UNIQUE = "unique"
LIT_MAIN = "__main__"


def memory_snapshot(frame, count):
    return {scope: {LIT_PRESENT: True, "frame": frame, "reserved": 2048, "used": 128,
        LIT_HISTORICAL_ARENA_PEAK: 1024, LIT_ALLOCATIONS: count * 3, "reallocations": count,
        "deallocations": count * 2} for scope in bench.ARENAS}


def rows(workload=LIT_SHARED, mode=LIT_TIMING):
    configuration = dict(bench.FROZEN_SETTINGS, type="configuration", schema=1, workload=workload, mode=mode,
        successful_frames=384, renderers=65, runtime_renderers=0, runtime_owners=0,
        transparent_renderers=0 if workload == LIT_OPAQUE else 32 if workload == "hybrid" else 64)
    result = [configuration]
    if mode == LIT_MEMORY:
        result.append({LIT_TYPE: "memory_baseline", LIT_ARENAS: memory_snapshot(95, 100)})
    for frame in range(96, 352):
        row = {LIT_TYPE: LIT_CPU, LIT_SOURCE_FRAME: frame, LIT_PUBLISH_FRAME: frame, LIT_SCOPES_MS: {
            LIT_GRAPHICS_FRAME: 8., "graphics.frame_preamble": .05, LIT_GRAPHICS_PREPARE_RESOURCES: 2.,
            "graphics.render_passes": 2., LIT_GRAPHICS_RENDER: 5., "frame.project_update": .1,
            "graphics.present": .2, "graphics.begin_frame": .4}}
        if mode == LIT_MEMORY:
            row[LIT_ARENAS] = memory_snapshot(frame, frame - 96 + 101)
        result.append(row)
    scopes = bench.GPU_BASE + (() if workload == LIT_OPAQUE else bench.GPU_TRANSPARENT)
    for scope in scopes:
        for first, last, mean in ((96, 159, 2.), (160, 351, 4.)):
            count = last - first + 1
            result.append({LIT_TYPE: LIT_GPU, LIT_SCOPE: scope, LIT_FIRST_SOURCE_FRAME: first,
                LIT_LAST_SOURCE_FRAME: last, LIT_PUBLISH_FRAME: last + 2, LIT_TOTAL_MS: count * mean, LIT_SAMPLES: count})
    result.append({LIT_TYPE: LIT_COMPLETE})
    return result


def balanced_trials():
    result = bench.summarize_rows(rows(), LIT_SHARED, LIT_TIMING)
    orders = [[LIT_BASELINE, LIT_CANDIDATE], [LIT_CANDIDATE, LIT_BASELINE]] * 4
    trials = []
    for block, order in enumerate(orders):
        for position, arm in enumerate(order):
            value = copy.deepcopy(result)
            if arm == LIT_CANDIDATE:
                value[LIT_CPU][LIT_GRAPHICS_RENDER][LIT_MEAN_MS] -= .4
                value[LIT_CPU][LIT_GRAPHICS_PREPARE_RESOURCES][LIT_MEAN_MS] -= .4
            trials.append(dict(block=block, position=position, arm=arm, result=value))
    return trials, orders


class RendererGatherBenchmarkAnalysisTests(unittest.TestCase):
    def test_rejects_duplicate_cpu_source_frame(self):
        values = rows()
        values[2][LIT_SOURCE_FRAME] = 96
        with self.assertRaisesRegex(bench.SmokeFailure, "duplicate, skipped"):
            bench.summarize_rows(values, LIT_SHARED, LIT_TIMING)

    def test_rejects_missing_real_preparation_scope(self):
        values = rows()
        del values[1][LIT_SCOPES_MS][LIT_GRAPHICS_PREPARE_RESOURCES]
        with self.assertRaisesRegex(bench.SmokeFailure, LIT_TOTALS_OR_PARENT):
            bench.summarize_rows(values, LIT_SHARED, LIT_TIMING)

    def test_rejects_failed_preparation_marker(self):
        values = rows()
        values[1][LIT_SCOPES_MS]["graphics.prepare_resources_failed"] = .4
        with self.assertRaisesRegex(bench.SmokeFailure, LIT_TOTALS_OR_PARENT):
            bench.summarize_rows(values, LIT_SHARED, LIT_TIMING)

    def test_rejects_parent_double_counting(self):
        values = rows()
        values[1][LIT_SCOPES_MS][LIT_GRAPHICS_PREPARE_RESOURCES] = 4.
        with self.assertRaisesRegex(bench.SmokeFailure, "subtotals exceed"):
            bench.summarize_rows(values, LIT_SHARED, LIT_TIMING)

    def test_rejects_missing_completed_frame_control(self):
        values = [row for row in rows() if row.get(LIT_SCOPE) != LIT_RENDER_FRAME]
        with self.assertRaisesRegex(bench.SmokeFailure, "coverage is missing"):
            bench.summarize_rows(values, LIT_SHARED, LIT_TIMING)

    def test_rejects_excessive_mixed_warmup_gpu_publication(self):
        values = rows()
        for row in values:
            if row.get(LIT_TYPE) == LIT_GPU and row[LIT_FIRST_SOURCE_FRAME] == 96:
                row[LIT_FIRST_SOURCE_FRAME] = 95
                row[LIT_SAMPLES] += 1
        with self.assertRaisesRegex(bench.SmokeFailure, "below 90%"):
            bench.summarize_rows(values, LIT_SHARED, LIT_TIMING)

    def test_rejects_duplicated_gpu_publication(self):
        values = rows()
        values.insert(-1, copy.deepcopy(next(row for row in values if row.get(LIT_TYPE) == LIT_GPU)))
        with self.assertRaisesRegex(bench.SmokeFailure, "overlapping GPU"):
            bench.summarize_rows(values, LIT_SHARED, LIT_TIMING)

    def test_allows_only_actual_initial_opaque_target_clear(self):
        values = rows(LIT_OPAQUE)
        values.insert(-1, {LIT_TYPE: LIT_GPU, LIT_SCOPE: "render.avboit_clear", LIT_FIRST_SOURCE_FRAME: 0,
            LIT_LAST_SOURCE_FRAME: 0, LIT_PUBLISH_FRAME: 2, LIT_SAMPLES: 1, LIT_TOTAL_MS: .01})
        self.assertEqual(bench.summarize_rows(values, LIT_OPAQUE, LIT_TIMING)[LIT_GPU][LIT_RENDER_FRAME]["gpu_samples"], 256)
        values[-2][LIT_LAST_SOURCE_FRAME] = 96
        with self.assertRaisesRegex(bench.SmokeFailure, "inactive measured"):
            bench.summarize_rows(values, LIT_OPAQUE, LIT_TIMING)

    def test_rejects_enabled_history_even_outside_measured_window(self):
        values = rows()
        values.insert(-1, {LIT_TYPE: LIT_GPU, LIT_SCOPE: "render.reflection_temporal", LIT_FIRST_SOURCE_FRAME: 0,
            LIT_LAST_SOURCE_FRAME: 0, LIT_PUBLISH_FRAME: 2, LIT_SAMPLES: 1, LIT_TOTAL_MS: .01})
        with self.assertRaisesRegex(bench.SmokeFailure, "inactive GPU"):
            bench.summarize_rows(values, LIT_SHARED, LIT_TIMING)

    def test_memory_counters_use_warmup_baseline_not_lifetime_totals(self):
        result = bench.summarize_rows(rows(mode=LIT_MEMORY), LIT_SHARED, LIT_MEMORY)
        scope = result[LIT_MEMORY][bench.ARENAS[0]]
        self.assertEqual(scope[LIT_ALLOCATION_DELTAS], 3.)
        self.assertEqual(scope["reallocation_deltas"], 1.)

    def test_timing_campaign_rejects_memory_capture(self):
        values = rows()
        values[1][LIT_ARENAS] = memory_snapshot(96, 10)
        with self.assertRaisesRegex(bench.SmokeFailure, "contaminated"):
            bench.summarize_rows(values, LIT_SHARED, LIT_TIMING)

    def test_unavailable_optional_arena_is_null_not_zero_savings(self):
        values = rows(mode=LIT_MEMORY)
        optional = "impl/ecs_render/avboit_transparent_csg"
        for row in values:
            if LIT_ARENAS in row:
                row[LIT_ARENAS][optional][LIT_PRESENT] = False
        result = bench.summarize_rows(values, LIT_SHARED, LIT_MEMORY)[LIT_MEMORY][optional]
        self.assertIsNone(result[LIT_ALLOCATION_DELTAS])
        self.assertFalse(result[LIT_COMPLETE])
        self.assertEqual(result[LIT_AVAILABLE_FRAMES], 0)

    def test_required_scratch_owner_must_exist(self):
        values = rows(mode=LIT_MEMORY)
        values[2][LIT_ARENAS][bench.REQUIRED_ARENAS[0]][LIT_PRESENT] = False
        with self.assertRaisesRegex(bench.SmokeFailure, "scratch owner is unavailable"):
            bench.summarize_rows(values, LIT_SHARED, LIT_MEMORY)

    def test_counter_reset_is_rejected(self):
        values = rows(mode=LIT_MEMORY)
        values[3][LIT_ARENAS][bench.ARENAS[0]][LIT_ALLOCATIONS] = 0
        with self.assertRaisesRegex(bench.SmokeFailure, "cumulative counters reset"):
            bench.summarize_rows(values, LIT_SHARED, LIT_MEMORY)

    def test_missing_runtime_owner_does_not_qualify(self):
        values = rows()
        values[0].update(workload=LIT_RUNTIME, transparent_renderers=56)
        with self.assertRaisesRegex(bench.SmokeFailure, "runtime workload"):
            bench.summarize_rows(values, LIT_RUNTIME, LIT_TIMING)

    def test_real_rendering_settings_must_match(self):
        values = rows()
        values[0]["hardware_budget"] = 0
        with self.assertRaisesRegex(bench.SmokeFailure, "fixture controls changed"):
            bench.summarize_rows(values, LIT_SHARED, LIT_TIMING)

    def test_incomplete_footer_does_not_qualify(self):
        with self.assertRaisesRegex(bench.SmokeFailure, "complete result footer"):
            bench.summarize_rows(rows()[:-1], LIT_SHARED, LIT_TIMING)

    def test_memory_only_comparison_never_performs_timing_inference(self):
        result = bench.summarize_rows(rows(mode=LIT_MEMORY), LIT_SHARED, LIT_MEMORY)
        orders = [[LIT_BASELINE, LIT_CANDIDATE], [LIT_CANDIDATE, LIT_BASELINE]] * 4
        trials = [dict(block=block, position=position, arm=arm, result=result)
            for block, order in enumerate(orders) for position, arm in enumerate(order)]
        with patch.object(bench, "paired_statistics", side_effect=AssertionError("timing inference forbidden")):
            self.assertEqual(bench.compare(trials, orders, LIT_MEMORY)[LIT_STATUS], "memory_observation_only")

    def test_balanced_comparison_rejects_partial_trial_matrix(self):
        with self.assertRaisesRegex(bench.SmokeFailure, "all balanced"):
            bench.compare([], [[LIT_BASELINE, LIT_CANDIDATE]] * 8, LIT_TIMING)


    def test_gpu_control_drift_blocks_cpu_benefit_claim(self):
        trials, orders = balanced_trials()
        for trial in trials:
            if trial[LIT_ARM] == LIT_CANDIDATE:
                trial[LIT_RESULT][LIT_GPU]["render.shadow_visibility"][LIT_MEAN_MS] += .5
        self.assertEqual(bench.compare(trials, orders, LIT_TIMING)[LIT_STATUS], "gpu_control_drift")

    def test_total_cpu_frame_regression_cannot_hide_behind_callback_improvement(self):
        trials, orders = balanced_trials()
        for trial in trials:
            if trial[LIT_ARM] == LIT_CANDIDATE:
                trial[LIT_RESULT][LIT_CPU][LIT_GRAPHICS_FRAME][LIT_MEAN_MS] += 1.
        self.assertEqual(bench.compare(trials, orders, LIT_TIMING)[LIT_STATUS], "whole_cpu_frame_regression_or_uncertain")

    def test_generator_rejects_stale_files_before_overwriting_and_keeps_source_root_separate(self):
        with TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "authored"
            source.mkdir()
            mesh = source / "mesh.nwb"
            mesh.write_bytes(LIT_MESH_ASSET_R_N)
            surface = root / "template.surface"
            surface.write_bytes(b"// surface\r\n")
            output = root / LIT_GENERATED
            arguments = ["--source-root", str(source), "--mesh-template", str(mesh),
                "--surface-template", str(surface), "--output-root", str(output)]
            self.assertEqual(generate_assets(arguments), 0)
            stale = output / "retired.nwb"
            stale.write_bytes(b"retired asset;\r\n")
            mesh.write_bytes(LIT_MESH_CHANGED_R_N)
            before = {path.relative_to(output).as_posix(): path.read_bytes()
                for path in output.rglob("*") if path.is_file()}
            with patch(LIT_SYS_STDERR, new_callable=io.StringIO) as errors:
                with self.assertRaises(SystemExit) as failure:
                    generate_assets(arguments)
                self.assertEqual(failure.exception.code, 2)
                self.assertIn("unexpected existing generated files: retired.nwb", errors.getvalue())
            self.assertEqual({path.relative_to(output).as_posix(): path.read_bytes()
                for path in output.rglob("*") if path.is_file()}, before)
            nested_output = source / LIT_GENERATED
            with patch(LIT_SYS_STDERR, new_callable=io.StringIO) as errors:
                with self.assertRaises(SystemExit) as failure:
                    generate_assets(arguments[:-1] + [str(nested_output)])
                self.assertEqual(failure.exception.code, 2)
                self.assertIn("generated root must be separate", errors.getvalue())
            self.assertFalse(nested_output.exists())
            self.assertEqual(mesh.read_bytes(), LIT_MESH_CHANGED_R_N)

    def test_explicit_vulkan_layers_are_rejected_before_timing_or_memory_acquisition(self):
        for key in ("VK_INSTANCE_LAYERS", "VK_LOADER_LAYERS_ENABLE"):
            for mode in (LIT_TIMING, LIT_MEMORY):
                with self.subTest(key=key, mode=mode):
                    with self.assertRaisesRegex(bench.SmokeFailure, "explicit Vulkan layer override"):
                        bench.environment({key: "VK_LAYER_KHRONOS_validation"}, LIT_SHARED, mode, LIT_CAPTURE_JSONL)
                    self.assertEqual(bench.environment({key: ""}, LIT_SHARED, mode, LIT_CAPTURE_JSONL)[key], "")

    def test_inherited_smoke_and_diagnostic_controls_are_sanitized(self):
        env = bench.environment({LIT_NWB_REFLECTION_SMOKE_DIAGNOSTICS: "1", LIT_NWB_GPU_TIMING_FILE: "stale",
            LIT_NWB_RENDERER_BASELINE_CAPTURE_FREEZE_F: "1", "NWB_LINUX_BACKEND": "x11", "PATH": "keep"},
            LIT_UNIQUE, LIT_TIMING, LIT_CAPTURE_JSONL)
        self.assertNotIn(LIT_NWB_GPU_TIMING_FILE, env)
        self.assertNotIn(LIT_NWB_REFLECTION_SMOKE_DIAGNOSTICS, env)
        self.assertEqual(env[LIT_NWB_RENDERER_BASELINE_CAPTURE_FREEZE_F], "0")


if __name__ == LIT_MAIN:
    unittest.main()
