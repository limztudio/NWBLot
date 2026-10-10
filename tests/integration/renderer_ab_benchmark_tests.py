#!/usr/bin/env python3
"""Pure CPU tests for frozen-arm renderer A/B acquisition contracts."""

import contextlib
import copy
import hashlib
import io
import json
from pathlib import Path
import sys
import struct
from types import SimpleNamespace
from unittest.mock import patch
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "smoke"))
import renderer_ab_benchmark as benchmark
from smoke_volume_identity import volume_segment_filename

LIT_TOTAL_MS = "total_ms"
LIT_GPU_SAMPLES = "gpu_samples"
LIT_REPORTS = "reports"
LIT_MEAN_MS = "mean_ms"
LIT_HARDWARE = "hardware"
LIT_RENDERERSYSTEM_DISPATCHED_HARDWARE_TRA = "RendererSystem: dispatched hardware transparent shadow traversal"
LIT_RENDERERSYSTEM_DISPATCHED_SOFTWARE_SHA = "RendererSystem: dispatched software shadow traversal"
LIT_MODIFIED = "modified"
LIT_AVBOITTIMINGPROBE_IN_FLIGHT_RANGES_32 = "AvboitTimingProbe: in-flight ranges 32"
LIT_AVBOITTIMINGPROBE_RENDER_UNFOCUSED_1 = "AvboitTimingProbe: render unfocused 1"
LIT_RENDERERSYSTEM_DEFERRED_RENDERING_TARG = "RendererSystem: deferred rendering targets ready (1280x900, samples=1)"
LIT_GRAPHICS_RUNTIME_CREATED_DEVICE_EXAMPLE_GPU = "GraphicsRuntime: created device 'Example GPU'"
LIT_RENDERERSYSTEM_MATERIAL_GLASS_SELECTED = "RendererSystem: material 'glass' selected CS + PS through compute emulation"
LIT_TRANSPARENTMULTISMOKEPROJECT_SHUTDOWN = "TransparentMultiSmokeProject: shutdown"
LIT_SOURCE = "source"
LIT_REVISION = "revision"
LIT_SOURCE_CPP = "source.cpp"
LIT_UTF_8 = "utf-8"
LIT_SOURCE_JSON = "source.json"
LIT_FILES = "files"
LIT_RUNTIME = "runtime"
LIT_RES = "res"
LIT_AUTHORED_VOL = "authored.vol"
LIT_TRANSPARENT_MULTI = "transparent-multi"
LIT_AVBOIT_REFRACTION_ACCUMULATION = "avboit-refraction-accumulation"
LIT_BASELINE = "baseline"
LIT_CANDIDATE = "candidate"
LIT_ARM = "arm"
LIT_BLOCK = "block"
LIT_POSITION = "position"
LIT_SCOPES = "scopes"
LIT_MISSING_COMPLETED_GPU_SCOPES = "missing completed GPU scopes"
LIT_SAMPLE_RATIO = "sample ratio"
LIT_NWB_SMOKE_FRAMEBUFFER_CAPTURE_PATH = "NWB_SMOKE_FRAMEBUFFER_CAPTURE_PATH"
LIT_OLD_BMP = "old.bmp"
LIT_NWB_REFLECTION_SMOKE_DIAGNOSTICS = "NWB_REFLECTION_SMOKE_DIAGNOSTICS"
LIT_NWB_CAUSTIC_SMOKE_ENABLED = "NWB_CAUSTIC_SMOKE_ENABLED"
LIT_NWB_TRANSPARENT_MULTI_SPIN_ANGLE = "NWB_TRANSPARENT_MULTI_SPIN_ANGLE"
LIT_NWB_RENDERER_BASELINE_CAPTURE_FREEZE_F = "NWB_RENDERER_BASELINE_CAPTURE_FREEZE_FRAME"
LIT_NWB_GPU_TIMING_FILE = "NWB_GPU_TIMING_FILE"
LIT_OLD_TXT = "old.txt"
LIT_PRESERVED = "PRESERVED"
LIT_YES = "yes"
LIT_TIMING_TXT = "timing.txt"
LIT_NWB_AVBOIT_SMOKE_TIMING = "NWB_AVBOIT_SMOKE_TIMING"
LIT_VK_INSTANCE_LAYERS = "VK_INSTANCE_LAYERS"
LIT_VK_LOADER_LAYERS_ENABLE = "VK_LOADER_LAYERS_ENABLE"
LIT_VK_LAYER_KHRONOS_VALIDATION = "VK_LAYER_KHRONOS_validation"
LIT_SHADOW_ROUTE = "shadow_route"
LIT_SOFTWARE = "software"
LIT_N_1280X900 = "1280x900"
LIT_N_960X720 = "960x720"
LIT_IN_FLIGHT_RANGES_32 = "in-flight ranges 32"
LIT_IN_FLIGHT_RANGES_2 = "in-flight ranges 2"
LIT_SELECTED = " selected "
LIT_HYBRID = "hybrid"
LIT_BASELINE_EXECUTABLE = "--baseline-executable"
LIT_BASELINE_RUNTIME = "--baseline-runtime"
LIT_AR = "ar"
LIT_BASELINE_SOURCE_MANIFEST = "--baseline-source-manifest"
LIT_AS_JSON = "as.json"
LIT_CANDIDATE_EXECUTABLE = "--candidate-executable"
LIT_CANDIDATE_RUNTIME = "--candidate-runtime"
LIT_BR = "br"
LIT_CANDIDATE_SOURCE_MANIFEST = "--candidate-source-manifest"
LIT_BS_JSON = "bs.json"
LIT_LOGSERVER_EXECUTABLE = "--logserver-executable"
LIT_LOGGER = "logger"
LIT_OUTPUT_DIRECTORY = "--output-directory"
LIT_OUTPUT = "output"
LIT_BLOCKS = "--blocks"
LIT_NAN = "nan"
LIT_RENDER_OPAQUE_REGULAR = "render.opaque_regular"
LIT_RENDER_DEFERRED_COMPOSITE = "render.deferred_composite"
LIT_RENDER_DEFERRED_PRESENT = "render.deferred_present"
LIT_SECONDARY_SCOPE = "secondary_scope"
LIT_RENDER_SHADOW_VISIBILITY = "render.shadow_visibility"
LIT_STATUS = "status"
LIT_RESOLVED_GPU_TIME_REDUCTION = "resolved_gpu_time_reduction"
LIT_CONTROLS = "controls"
LIT_CONTROL_DRIFT = "control_drift"
LIT_CONTROL_UNCERTAIN = "control_uncertain"
LIT_EVERY_PLANNED_TRIAL = "every planned trial"
LIT_SHADOWTIMINGPROBE_INDIRECT_RESPONSE_HE = "ShadowTimingProbe: indirect response hemi-ambient"
LIT_SOFTSHADOWTESTSMOKEPROJECT_SHUTDOWN = "SoftShadowTestSmokeProject: shutdown"
LIT_SHADOW_ZERO_EXTENT = "shadow-zero-extent"
LIT_SHADOW_FINITE_EXTENT = "shadow-finite-extent"
LIT_NWB_SOFT_SHADOW_TEST_ANGLE = "NWB_SOFT_SHADOW_TEST_ANGLE"
LIT_NWB_SOFT_SHADOW_TEST_SOURCE_RADIUS = "NWB_SOFT_SHADOW_TEST_SOURCE_RADIUS"
LIT_NWB_SOFT_SHADOW_TEST_TIMING = "NWB_SOFT_SHADOW_TEST_TIMING"
LIT_RUNTIME_PIPELINE_CACHE = "runtime_pipeline_cache"
LIT_SCREEN = "screen"
LIT_REFLECTIONSMOKEPROJECT_SHUTDOWN = "ReflectionSmokeProject: shutdown"
LIT_REFLECTION_ROUGH_SPATIAL = "reflection-rough-spatial"
LIT_REFLECTION_MIRROR_SPATIAL = "reflection-mirror-spatial"
LIT_REFLECTION_ROUGH_FILTERED = "reflection-rough-filtered"
LIT_REFLECTION_SCREEN_DEPTH = "reflection-screen-depth"
LIT_REFLECTION_OPTICAL_CLEAR = "reflection-optical-clear"
LIT_REFLECTION_OPTICAL_INSIDE = "reflection-optical-inside"
LIT_CAUSTIC_POPULATED = "caustic-populated"
LIT_NWB_REFLECTION_SMOKE_HISTORY_SAMPLES = "NWB_REFLECTION_SMOKE_HISTORY_SAMPLES"
LIT_NWB_RENDERER_BASELINE_FIXED_DELTA_SECO = "NWB_RENDERER_BASELINE_FIXED_DELTA_SECONDS"
LIT_NEW_TIMING_TXT = "new_timing.txt"
LIT_N_16 = "16"
LIT_INACTIVE_REFLECTION_SCOPES = "inactive reflection scopes"
LIT_WORKLOAD = "--workload"
LIT_CANDIDATE_HARDWARE_DISPATCHES_PER_RANG = "--candidate-hardware-dispatches-per-range"
LIT_POPULATED = "populated"
LIT_DISABLED = "disabled"
LIT_CAUSTIC = "caustic-"
LIT_POSITIVE_PIXELS = "positive_pixels"
LIT_POSITIVE_CHANNEL_GAIN = "positive_channel_gain"
LIT_POSITIVE_TILES = "positive_tiles"
LIT_SPARSE = "sparse"
LIT_QUALIFICATION_JSON = "qualification.json"
LIT_FROZEN = "frozen"
LIT_SCHEMA = "schema"
LIT_POLICY = "policy"
LIT_CAPTURES = "captures"
LIT_FAILURE_JSON = "failure.json"
LIT_FIXTURE_EXE = "fixture.exe"
LIT_LOGGER_EXE = "logger.exe"
LIT_GPUDBG = "--gpudbg"
LIT_WINDOW_CAPTURE_SMOKE_PY = "window_capture_smoke.py"
LIT_METRICS = "metrics"
LIT_QUALIFICATION_TOOL = "qualification_tool"
LIT_BMP = ".bmp"
LIT_COMMAND = "command"
LIT_IMAGE = "image"
LIT_PATH = "path"
LIT_IDENTITY = "identity"
LIT_LOG = "log"
LIT_POPULATED_ON = "populated_on"
LIT_MAIN = "__main__"


def scopes(workload, frames=200, frame_ms=5.0):
    return {name: {LIT_TOTAL_MS: (frame_ms if name == benchmark.FRAME else .5) * frames * multiplier,
        LIT_GPU_SAMPLES: frames * multiplier, LIT_REPORTS: 6,
        LIT_MEAN_MS: frame_ms if name == benchmark.FRAME else .5}
        for name, multiplier in workload.scope_multipliers}


def log_text(route=LIT_HARDWARE):
    natural = next(message for message, name in benchmark.SHADOW_ROUTES.items() if name == route)
    return "\n".join((natural, LIT_RENDERERSYSTEM_DISPATCHED_HARDWARE_TRA if route == LIT_HARDWARE
        else LIT_RENDERERSYSTEM_DISPATCHED_SOFTWARE_SHA, LIT_AVBOITTIMINGPROBE_IN_FLIGHT_RANGES_32,
        LIT_AVBOITTIMINGPROBE_RENDER_UNFOCUSED_1,
        "TransparentMultiSmokeProject: shared transparent material with three mutable instance overrides created",
        LIT_RENDERERSYSTEM_DEFERRED_RENDERING_TARG,
        LIT_GRAPHICS_RUNTIME_CREATED_DEVICE_EXAMPLE_GPU,
        LIT_RENDERERSYSTEM_MATERIAL_GLASS_SELECTED,
        LIT_TRANSPARENTMULTISMOKEPROJECT_SHUTDOWN))


def source_manifest(directory, text=LIT_SOURCE, revision=LIT_REVISION):
    directory.mkdir(parents=True, exist_ok=True)
    source = directory / LIT_SOURCE_CPP
    source.write_text(text, encoding=LIT_UTF_8)
    manifest = directory / LIT_SOURCE_JSON
    manifest.write_text(json.dumps({LIT_REVISION: revision,
        LIT_FILES: {source.name: hashlib.sha256(source.read_bytes()).hexdigest()}}), encoding=LIT_UTF_8)
    return manifest


def make_arm(directory, name):
    directory.mkdir(parents=True, exist_ok=True)
    executable = directory / "renderer"
    executable.write_bytes(b"binary")
    runtime = directory / LIT_RUNTIME
    (runtime / LIT_RES).mkdir(parents=True)
    (runtime / LIT_RES / LIT_AUTHORED_VOL).write_bytes(b"authored asset")
    return benchmark.Arm(name, executable, runtime, source_manifest(directory / LIT_SOURCE))


def trial_matrix(frame_delta=-.5):
    workload = benchmark.workloads()[LIT_TRANSPARENT_MULTI]
    arms = (benchmark.Arm(LIT_BASELINE, Path("a"), Path("a_runtime"), Path("a.json")),
        benchmark.Arm(LIT_CANDIDATE, Path("b"), Path("b_runtime"), Path("b.json")))
    orders = [[arm.name for arm in row] for row in benchmark.balanced_orders(arms, 8, 0)]
    trials = []
    for block, row in enumerate(orders):
        for position, arm in enumerate(row):
            current = scopes(workload, frame_ms=5 + block * .01 + (frame_delta if arm == LIT_CANDIDATE else 0))
            trials.append({LIT_ARM: arm, LIT_BLOCK: block, LIT_POSITION: position, LIT_SCOPES: current})
    return workload, orders, trials


class CoverageTests(unittest.TestCase):
    def test_actual_gpu_counts_ignore_cpu_frame_and_displayed_average_fields(self):
        text = ("=== interval: 99999 frames / 0.5s ===\n"
            "  render.frame: window_avg_ms=123 window_min_ms=123 window_max_ms=123 published_windows=1 total_ms=90 gpu_samples=10 sample_avg_ms=9\n"
            "=== interval: 1 frames / 0.5s ===\n"
            "  render.frame: window_avg_ms=456 window_min_ms=456 window_max_ms=456 published_windows=1 total_ms=100 gpu_samples=100 sample_avg_ms=1\n")
        result = benchmark.summarize_intervals(benchmark.parse_intervals(text, finalized=True))
        self.assertEqual(result[benchmark.FRAME][LIT_GPU_SAMPLES], 110)
        self.assertAlmostEqual(result[benchmark.FRAME][LIT_MEAN_MS], 190 / 110)

    def test_partial_live_tail_is_not_counted(self):
        text = ("=== interval: 1 frames / 0.5s ===\n"
            "  render.frame: window_avg_ms=4 window_min_ms=4 window_max_ms=4 published_windows=1 total_ms=4 gpu_samples=1 sample_avg_ms=4\n"
            "=== interval: 1 frames / 0.5s ===\n  render.frame: total_ms=")
        self.assertEqual(len(benchmark.parse_intervals(text)), 1)

    def test_all_avboit_and_control_ranges_are_required(self):
        for workload_name in (LIT_TRANSPARENT_MULTI, LIT_AVBOIT_REFRACTION_ACCUMULATION):
            workload = benchmark.workloads()[workload_name]
            complete = scopes(workload)
            benchmark.validate_coverage(complete, workload, 6, 100)
            for name in workload.scopes:
                with self.subTest(workload=workload_name, scope=name):
                    missing = copy.deepcopy(complete)
                    del missing[name]
                    with self.assertRaisesRegex(benchmark.SmokeFailure, "missing .*GPU scopes"):
                        benchmark.validate_coverage(missing, workload, 6, 100)

    def test_ratio_boundary_and_sparse_reports_are_rejected(self):
        for workload_name in (LIT_TRANSPARENT_MULTI, LIT_AVBOIT_REFRACTION_ACCUMULATION):
            workload = benchmark.workloads()[workload_name]
            for frames, tolerance in ((100, 2), (1000, 20)):
                for scope in (workload.secondary_scope, benchmark.CONTROLS[0]):
                    for sign in (-1, 1):
                        with self.subTest(workload=workload_name, scope=scope, frames=frames, sign=sign):
                            value = scopes(workload, frames)
                            value[scope][LIT_GPU_SAMPLES] += sign * tolerance
                            benchmark.validate_coverage(value, workload, 6, 100)
                            value[scope][LIT_GPU_SAMPLES] += sign
                            with self.assertRaisesRegex(benchmark.SmokeFailure, LIT_SAMPLE_RATIO):
                                benchmark.validate_coverage(value, workload, 6, 100)
            value = scopes(workload)
            value[workload.secondary_scope][LIT_REPORTS] = 5
            benchmark.validate_coverage(value, workload, 6, 100)
            value[workload.secondary_scope][LIT_REPORTS] = 4
            with self.subTest(workload=workload_name), self.assertRaisesRegex(benchmark.SmokeFailure, "publications"):
                benchmark.validate_coverage(value, workload, 6, 100)
            with self.subTest(workload=workload_name), self.assertRaises(benchmark.SmokeFailure):
                benchmark.validate_coverage(scopes(workload, 99), workload, 6, 100)


class WallFrameEvidenceTests(unittest.TestCase):
    @staticmethod
    def reports(intervals):
        fps = []
        gpu = []
        for frames, seconds in intervals:
            average = seconds * 1000 / frames
            fps.append(f"ReflectionSmokeProject: fps avg={frames / seconds:.17g} frame_ms avg={average:.17g} "
                f"min={average / 2:.17g} max={average * 2:.17g} frames={frames} seconds={seconds:.17g}")
            gpu.append(f"=== interval: {frames} frames / {seconds:.4f}s ===")
        return "\n".join(fps), "\n".join(gpu)

    def test_retained_intervals_weight_frames_and_ignore_shutdown_tail(self):
        workload = benchmark.workloads()[LIT_REFLECTION_OPTICAL_CLEAR]
        text, timing = self.reports(((30, .5), (30, .5), (100, .5), (30, .6), (900, .6)))
        value = benchmark.summarize_wall_frames(text.replace("\n", "\r\n"), timing, workload, [2, 4])
        self.assertTrue(value["eligible"])
        self.assertEqual(value["retained_report_range"], [2, 4])
        self.assertEqual(value["frames"], 130)
        self.assertEqual(value["reports"], 2)
        self.assertAlmostEqual(value["seconds"], 1.1)
        self.assertAlmostEqual(value["mean_frame_ms"], 1100 / 130)
        self.assertAlmostEqual(value["mean_fps"], 130 / 1.1)
        self.assertEqual(value["min_frame_ms"], 2.5)
        self.assertEqual(value["max_frame_ms"], 40)

    def test_long_positive_frames_are_retained_and_weighted_without_a_duration_cutoff(self):
        workload = benchmark.workloads()["reflection-optical-csg-cap"]
        text, timing = self.reports(((30, .5), (30, .5), (2, .6), (1, .9), (900, .6)))
        value = benchmark.summarize_wall_frames(text, timing, workload, [2, 4])
        self.assertEqual(value["retained_report_range"], [2, 4])
        self.assertEqual(value["frames"], 3)
        self.assertEqual(value["reports"], 2)
        self.assertAlmostEqual(value["seconds"], 1.5)
        self.assertAlmostEqual(value["mean_frame_ms"], 500)
        self.assertAlmostEqual(value["mean_fps"], 2)
        self.assertAlmostEqual(value["min_frame_ms"], 150)
        self.assertAlmostEqual(value["max_frame_ms"], 1800)

    def test_long_positive_maximum_does_not_discard_an_otherwise_consistent_interval(self):
        workload = benchmark.workloads()[LIT_REFLECTION_OPTICAL_CLEAR]
        text, timing = self.reports(((30, .5), (30, .5), (100, .5), (100, .5)))
        value = benchmark.summarize_wall_frames(text.replace("max=10", "max=251", 1), timing, workload, [2, 4])
        self.assertEqual(value["frames"], 200)
        self.assertAlmostEqual(value["mean_frame_ms"], 5)
        self.assertEqual(value["max_frame_ms"], 251)

    def test_missing_malformed_nonfinite_and_inconsistent_reports_are_refused(self):
        workload = benchmark.workloads()[LIT_REFLECTION_OPTICAL_CLEAR]
        text, timing = self.reports(((30, .5), (30, .5), (100, .5), (100, .5)))
        for changed in ("\n".join(text.splitlines()[:-1]), text.replace("fps avg=200", "fps avg=nan", 1),
            text.replace("frame_ms avg=5", "frame_ms avg=inf", 1), text.replace("frames=100", "frames=0", 1),
            text.replace("fps avg=200", "fps avg=100", 1), text.replace(" min=2.5", "", 1),
            text.replace("max=10", "max=4", 1), text.replace("max=10", "max=0", 1),
            text.replace("min=2.5", "min=-2.5", 1), text.replace("seconds=0.5", "seconds=0", 1)):
            with self.subTest(text=changed), self.assertRaises(benchmark.SmokeFailure):
                benchmark.summarize_wall_frames(changed, timing, workload, [2, 4])

    def test_gpu_header_rounding_is_accepted_but_shifted_windows_are_refused(self):
        workload = benchmark.workloads()[LIT_REFLECTION_OPTICAL_CLEAR]
        text, timing = self.reports(((30, .5), (30, .5), (100, .500049), (100, .5)))
        benchmark.summarize_wall_frames(text, timing, workload, [2, 4])
        for changed in (timing.replace("100 frames", "101 frames", 1),
            timing.replace("100 frames / 0.5000", "100 frames / 0.5002", 1)):
            with self.subTest(timing=changed), self.assertRaisesRegex(benchmark.SmokeFailure, "windows do not match"):
                benchmark.summarize_wall_frames(text, changed, workload, [2, 4])

    def test_fixed_simulation_delta_workloads_explicitly_omit_wall_frame_evidence(self):
        for name in (LIT_TRANSPARENT_MULTI, LIT_SHADOW_ZERO_EXTENT, LIT_CAUSTIC_POPULATED):
            value = benchmark.summarize_wall_frames("", "", benchmark.workloads()[name], [2, 8])
            with self.subTest(workload=name):
                self.assertFalse(value["eligible"])
                self.assertIn("fixed simulation delta", value["reason"])
        workload, orders, trials = trial_matrix()
        for trial in trials:
            trial["wall_frame"] = {"eligible": True, "mean_frame_ms": 1, "mean_fps": 1000}
        self.assertFalse(benchmark.compare_trials(trials, orders, workload)["wall_frame"]["eligible"])

    def test_wall_frame_comparison_uses_complete_paired_blocks_and_correct_units(self):
        workload = benchmark.workloads()[LIT_REFLECTION_OPTICAL_CLEAR]
        _, orders, trials = trial_matrix()
        for trial in trials:
            trial[LIT_SCOPES] = scopes(workload)
            frame_ms = 5 if trial[LIT_ARM] == LIT_BASELINE else 4
            trial["wall_frame"] = {"eligible": True, "mean_frame_ms": frame_ms, "mean_fps": 1000 / frame_ms}
        value = benchmark.compare_trials(trials, orders, workload)["wall_frame"]
        self.assertTrue(value["eligible"])
        self.assertEqual(value["frame_ms_delta"]["blocks"], 8)
        self.assertEqual(value["frame_ms_delta"]["ci95_mean_ms"], [-1, -1])
        self.assertEqual(value["fps_delta"]["ci95_mean_fps"], [50, 50])
        self.assertEqual(value["means"][LIT_CANDIDATE], {"frame_ms": 4, "fps": 250})
        with self.assertRaisesRegex(benchmark.SmokeFailure, LIT_EVERY_PLANNED_TRIAL):
            benchmark.compare_trials(trials[:-1], orders, workload)
        for omitted in (1, len(trials)):
            invalid = copy.deepcopy(trials)
            for trial in invalid[:omitted]:
                del trial["wall_frame"]
            with self.subTest(omitted=omitted), self.assertRaisesRegex(benchmark.SmokeFailure, "every planned trial.*qualified reports"):
                benchmark.compare_trials(invalid, orders, workload)


class WorkloadPolicyTests(unittest.TestCase):
    def test_inherited_smoke_capture_diagnostics_and_pose_are_cleared(self):
        workload = benchmark.workloads()[LIT_TRANSPARENT_MULTI]
        inherited = {LIT_NWB_SMOKE_FRAMEBUFFER_CAPTURE_PATH: LIT_OLD_BMP, LIT_NWB_REFLECTION_SMOKE_DIAGNOSTICS: "1",
            LIT_NWB_CAUSTIC_SMOKE_ENABLED: "1", "NWB_TRANSPARENT_CSG_DISABLE_CUTTER": "1",
            LIT_NWB_TRANSPARENT_MULTI_SPIN_ANGLE: "2", LIT_NWB_RENDERER_BASELINE_CAPTURE_FREEZE_F: "5",
            LIT_NWB_GPU_TIMING_FILE: LIT_OLD_TXT, LIT_PRESERVED: LIT_YES}
        env, overrides = benchmark.configure_environment(inherited, workload, Path(LIT_TIMING_TXT))
        self.assertEqual(env, {LIT_PRESERVED: LIT_YES, **dict(workload.environment_overrides),
            LIT_NWB_GPU_TIMING_FILE: LIT_TIMING_TXT})
        self.assertEqual(overrides[LIT_NWB_TRANSPARENT_MULTI_SPIN_ANGLE], "0")
        self.assertEqual(inherited[LIT_NWB_GPU_TIMING_FILE], LIT_OLD_TXT)

    def test_explicit_validation_is_rejected_not_silently_disabled(self):
        workload = benchmark.workloads()[LIT_TRANSPARENT_MULTI]
        for key in (LIT_VK_INSTANCE_LAYERS, LIT_VK_LOADER_LAYERS_ENABLE):
            with self.subTest(key=key), self.assertRaises(benchmark.SmokeFailure):
                benchmark.configure_environment({key: LIT_VK_LAYER_KHRONOS_VALIDATION}, workload, Path("t"))

    def test_logs_require_actual_route_extent_policy_device_and_shutdown(self):
        self.assertEqual(benchmark.device_material_signature(log_text()),
            benchmark.device_material_signature(log_text().replace("\n", "\r\n")))
        workload = benchmark.workloads()[LIT_TRANSPARENT_MULTI]
        expected = benchmark.transparent_multi_log(log_text(), workload, True)
        self.assertEqual(expected[LIT_SHADOW_ROUTE], LIT_HARDWARE)
        self.assertEqual(expected, benchmark.transparent_multi_log(log_text().replace("\n", "\r\n"), workload, True))
        software = benchmark.transparent_multi_log(log_text(LIT_SOFTWARE), workload, False)
        self.assertEqual(software[LIT_SHADOW_ROUTE], LIT_SOFTWARE)
        with self.assertRaises(benchmark.SmokeFailure):
            benchmark.transparent_multi_log(log_text(LIT_SOFTWARE), workload, True)
        for altered in (log_text().replace(LIT_N_1280X900, LIT_N_960X720),
            log_text().replace(LIT_IN_FLIGHT_RANGES_32, LIT_IN_FLIGHT_RANGES_2),
            log_text().replace("render unfocused 1", "render unfocused 0"),
            log_text().replace(LIT_TRANSPARENTMULTISMOKEPROJECT_SHUTDOWN, ""),
            log_text() + "\nAvboitTimingProbe: in-flight ranges 32", log_text() + "\n[ERROR]: rejected",
            log_text() + "\nFramebufferCapture: capture ready", log_text().replace("GraphicsRuntime: created device", "device")):
            with self.subTest(text=altered), self.assertRaises(benchmark.SmokeFailure):
                benchmark.transparent_multi_log(altered, workload, True)

    def test_material_signature_preserves_mixed_routes_independent_of_creation_order(self):
        indexed = "RendererSystem: material 'glass' selected VertexIndexed + PS from persistent object-space geometry"
        compute = LIT_RENDERERSYSTEM_MATERIAL_GLASS_SELECTED
        device = LIT_GRAPHICS_RUNTIME_CREATED_DEVICE_EXAMPLE_GPU
        mixed = benchmark.device_material_signature("\n".join((device, compute, indexed, compute)))
        reordered = benchmark.device_material_signature("\n".join((device, indexed, compute)))
        self.assertEqual(mixed, reordered)
        self.assertEqual(mixed["material_routes"]["glass"], [compute.split(LIT_SELECTED)[1], indexed.split(LIT_SELECTED)[1]])
        self.assertNotEqual(mixed, benchmark.device_material_signature("\n".join((device, indexed))))
        with self.assertRaises(benchmark.SmokeFailure):
            benchmark.device_material_signature(device)

    def test_current_hardware_route_requires_dispatch_and_rejects_retired_routes(self):
        workload = benchmark.workloads()[LIT_TRANSPARENT_MULTI]
        marker = LIT_RENDERERSYSTEM_DISPATCHED_HARDWARE_TRA
        for altered in (log_text().replace(marker, ""),
            log_text() + "\n" + LIT_RENDERERSYSTEM_DISPATCHED_SOFTWARE_SHA):
            with self.subTest(text=altered), self.assertRaises(benchmark.SmokeFailure):
                benchmark.transparent_multi_log(altered, workload, True)
        retired = log_text().replace("natural hardware shadow route", "natural hybrid shadow route")
        retired_route = next(line for line in retired.splitlines() if "natural hybrid shadow route" in line)
        for altered in (retired, log_text() + "\n" + retired_route):
            for require_hardware in (False, True):
                with self.subTest(hardware=require_hardware), self.assertRaises(benchmark.SmokeFailure):
                    benchmark.transparent_multi_log(altered, workload, require_hardware)

    def test_cli_rejects_lower_coverage_and_unbalanced_plans(self):
        common = [LIT_BASELINE_EXECUTABLE, "a", LIT_BASELINE_RUNTIME, LIT_AR, LIT_BASELINE_SOURCE_MANIFEST, LIT_AS_JSON,
            LIT_CANDIDATE_EXECUTABLE, "b", LIT_CANDIDATE_RUNTIME, LIT_BR, LIT_CANDIDATE_SOURCE_MANIFEST, LIT_BS_JSON,
            LIT_LOGSERVER_EXECUTABLE, LIT_LOGGER, LIT_OUTPUT_DIRECTORY, LIT_OUTPUT]
        for extra in ((LIT_BLOCKS, "7"), (LIT_BLOCKS, "6"), ("--warmup-intervals", "1"),
            ("--sample-intervals", "5"), ("--minimum-frame-samples", "99"), ("--timeout", LIT_NAN),
            ("--application-arg=--gpudbg",)):
            with self.subTest(extra=extra), contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                benchmark.parse_args(common + list(extra))


class WorkloadControlSelectionTests(unittest.TestCase):
    SHADOW_CONTROLS = (LIT_RENDER_OPAQUE_REGULAR, "render.deferred_lighting",
        LIT_RENDER_DEFERRED_COMPOSITE, LIT_RENDER_DEFERRED_PRESENT)

    def shadow_workload(self, **changes):
        from dataclasses import replace
        values = {"name": "test-shadow-controls", LIT_SECONDARY_SCOPE: LIT_RENDER_SHADOW_VISIBILITY,
            "control_scopes": self.SHADOW_CONTROLS}
        values.update(changes)
        return replace(benchmark.workloads()[LIT_TRANSPARENT_MULTI], **values)

    def test_shadow_target_change_is_not_misclassified_as_control_drift(self):
        _, orders, trials = trial_matrix()
        workload = self.shadow_workload()
        for trial in trials:
            if trial[LIT_ARM] == LIT_CANDIDATE:
                trial[LIT_SCOPES][LIT_RENDER_SHADOW_VISIBILITY][LIT_MEAN_MS] = .2
                trial[LIT_SCOPES][LIT_RENDER_SHADOW_VISIBILITY][LIT_TOTAL_MS] = .2 * trial[LIT_SCOPES][LIT_RENDER_SHADOW_VISIBILITY][LIT_GPU_SAMPLES]
        result = benchmark.compare_trials(trials, orders, workload)
        self.assertEqual(result[LIT_STATUS], LIT_RESOLVED_GPU_TIME_REDUCTION)

    def test_composite_and_present_drift_remain_controls(self):
        for changed_scope in (LIT_RENDER_DEFERRED_COMPOSITE, LIT_RENDER_DEFERRED_PRESENT):
            _, orders, trials = trial_matrix()
            for trial in trials:
                if trial[LIT_ARM] == LIT_CANDIDATE:
                    trial[LIT_SCOPES][changed_scope][LIT_MEAN_MS] = .8
            with self.subTest(scope=changed_scope):
                result = benchmark.compare_trials(trials, orders, self.shadow_workload())
                self.assertEqual(result[LIT_STATUS], LIT_CONTROL_DRIFT)
                self.assertTrue(result[LIT_CONTROLS][changed_scope]["material_drift"])

    def test_uncertain_present_control_cannot_claim_reduction(self):
        _, orders, trials = trial_matrix()
        for trial in trials:
            if trial[LIT_ARM] == LIT_CANDIDATE:
                trial[LIT_SCOPES][LIT_RENDER_DEFERRED_PRESENT][LIT_MEAN_MS] += .2 if trial[LIT_BLOCK] % 2 else -.2
        result = benchmark.compare_trials(trials, orders, self.shadow_workload())
        self.assertEqual(result[LIT_STATUS], LIT_CONTROL_UNCERTAIN)

    def test_empty_duplicate_or_mutable_controls_are_rejected(self):
        for controls in ((), (LIT_RENDER_OPAQUE_REGULAR, LIT_RENDER_OPAQUE_REGULAR), [LIT_RENDER_OPAQUE_REGULAR]):
            with self.subTest(controls=controls), self.assertRaisesRegex(benchmark.SmokeFailure, "nonempty unique tuple"):
                self.shadow_workload(control_scopes=controls)

    def test_unobserved_control_is_rejected(self):
        with self.assertRaisesRegex(benchmark.SmokeFailure, "not an observed scope"):
            self.shadow_workload(control_scopes=("render.not_measured",))

    def test_primary_and_secondary_targets_cannot_be_controls(self):
        for control in (benchmark.FRAME, LIT_RENDER_SHADOW_VISIBILITY):
            with self.subTest(control=control), self.assertRaisesRegex(benchmark.SmokeFailure, "target cannot also be a control"):
                self.shadow_workload(control_scopes=(control,))

    def test_shadow_controls_still_require_complete_scope_coverage(self):
        workload = self.shadow_workload()
        for control in self.SHADOW_CONTROLS:
            values = scopes(workload)
            del values[control]
            with self.subTest(control=control), self.assertRaisesRegex(benchmark.SmokeFailure, LIT_MISSING_COMPLETED_GPU_SCOPES):
                benchmark.validate_coverage(values, workload, 6, 100)

    def test_per_workload_controls_do_not_relax_complete_trial_requirement(self):
        _, orders, trials = trial_matrix()
        with self.assertRaisesRegex(benchmark.SmokeFailure, LIT_EVERY_PLANNED_TRIAL):
            benchmark.compare_trials(trials[:-1], orders, self.shadow_workload())


def soft_shadow_log_text(workload, route=LIT_HARDWARE):
    values = dict(workload.environment_overrides)
    return "\n".join(("ShadowTimingProbe: in-flight ranges 32", "ShadowTimingProbe: render unfocused 1",
        "ShadowTimingProbe: caustic emission 0", LIT_SHADOWTIMINGPROBE_INDIRECT_RESPONSE_HE,
        f"ShadowTimingProbe: natural shadow route {route}",
        LIT_RENDERERSYSTEM_DISPATCHED_HARDWARE_TRA if route == LIT_HARDWARE else LIT_RENDERERSYSTEM_DISPATCHED_SOFTWARE_SHA,
        f"ShadowTimingProbe: source extents angular={values['NWB_SOFT_SHADOW_TEST_ANGLE']} radius={values['NWB_SOFT_SHADOW_TEST_SOURCE_RADIUS']}",
        "SoftShadowTestSmokeProject: opaque + glass characters on a ground plane, 3 coloured lights, angularRadius=0 rad",
        LIT_RENDERERSYSTEM_DEFERRED_RENDERING_TARG,
        LIT_GRAPHICS_RUNTIME_CREATED_DEVICE_EXAMPLE_GPU, LIT_RENDERERSYSTEM_MATERIAL_GLASS_SELECTED,
        LIT_SOFTSHADOWTESTSMOKEPROJECT_SHUTDOWN))


class ShadowWorkloadPolicyTests(unittest.TestCase):
    def test_shadow_environment_clears_inherited_capture_and_extent_policy(self):
        workload = benchmark.workloads()[LIT_SHADOW_ZERO_EXTENT]
        env, overrides = benchmark.configure_environment({LIT_NWB_SOFT_SHADOW_TEST_ANGLE: "0.2",
            LIT_NWB_SOFT_SHADOW_TEST_SOURCE_RADIUS: "1", LIT_NWB_SOFT_SHADOW_TEST_TIMING: "0",
            LIT_NWB_RENDERER_BASELINE_CAPTURE_FREEZE_F: "360", LIT_NWB_SMOKE_FRAMEBUFFER_CAPTURE_PATH: LIT_OLD_BMP,
            LIT_NWB_REFLECTION_SMOKE_DIAGNOSTICS: "1", LIT_PRESERVED: LIT_YES}, workload, Path(LIT_TIMING_TXT))
        self.assertEqual(env, {LIT_PRESERVED: LIT_YES, **dict(workload.environment_overrides), LIT_NWB_GPU_TIMING_FILE: LIT_TIMING_TXT})

    def test_shadow_logs_require_actual_policy_route_extent_and_lifecycle(self):
        for name in (LIT_SHADOW_ZERO_EXTENT, LIT_SHADOW_FINITE_EXTENT):
            workload = benchmark.workloads()[name]
            text = soft_shadow_log_text(workload)
            expected = benchmark.soft_shadow_log(text, workload, True)
            self.assertEqual(expected, benchmark.soft_shadow_log(text.replace("\n", "\r\n"), workload, True))
            for altered in (text.replace(LIT_IN_FLIGHT_RANGES_32, LIT_IN_FLIGHT_RANGES_2),
                text.replace("caustic emission 0", "caustic emission 1"), text.replace(LIT_N_1280X900, LIT_N_960X720),
                text.replace(LIT_SOFTSHADOWTESTSMOKEPROJECT_SHUTDOWN, ""), text + "\nShadowTimingProbe: render unfocused 0",
                text.replace("natural shadow route hardware", "natural shadow route software")):
                with self.subTest(name=name, text=altered), self.assertRaises(benchmark.SmokeFailure):
                    benchmark.soft_shadow_log(altered, workload, True)

    def test_shadow_hardware_dispatch_evidence_is_required_and_retired_routes_are_rejected(self):
        workload = benchmark.workloads()[LIT_SHADOW_ZERO_EXTENT]
        marker = LIT_RENDERERSYSTEM_DISPATCHED_HARDWARE_TRA
        text = soft_shadow_log_text(workload)
        for altered in (text.replace(marker, ""), text + "\n" + LIT_RENDERERSYSTEM_DISPATCHED_SOFTWARE_SHA):
            with self.subTest(text=altered), self.assertRaises(benchmark.SmokeFailure):
                benchmark.soft_shadow_log(altered, workload, True)
        retired = soft_shadow_log_text(workload, LIT_HYBRID)
        retired_route = f"ShadowTimingProbe: natural shadow route {LIT_HYBRID}"
        for altered in (retired, text + "\n" + retired_route):
            for require_hardware in (False, True):
                with self.subTest(text=altered, require_hardware=require_hardware), self.assertRaises(benchmark.SmokeFailure):
                    benchmark.soft_shadow_log(altered, workload, require_hardware)
        software_text = soft_shadow_log_text(workload, LIT_SOFTWARE)
        software = benchmark.soft_shadow_log(software_text, workload, False)
        self.assertEqual(software[LIT_SHADOW_ROUTE], LIT_SOFTWARE)
        with self.assertRaises(benchmark.SmokeFailure):
            benchmark.soft_shadow_log(software_text, workload, True)

    def test_shadow_logs_require_exact_material_indirect_response(self):
        workload = benchmark.workloads()[LIT_SHADOW_ZERO_EXTENT]
        text = soft_shadow_log_text(workload)
        marker = LIT_SHADOWTIMINGPROBE_INDIRECT_RESPONSE_HE
        self.assertEqual(benchmark.soft_shadow_log(text, workload, True)["indirect_response"], "hemi-ambient")
        for altered in (text.replace(marker, ""), text.replace(marker, "ShadowTimingProbe: indirect response surfel"),
            text + "\n" + marker, text + "\nShadowTimingProbe: indirect response surfel"):
            with self.subTest(text=altered), self.assertRaises(benchmark.SmokeFailure):
                benchmark.soft_shadow_log(altered, workload, True)

    def test_shadow_zero_policy_does_not_accept_tiny_nonzero_or_nonfinite(self):
        workload = benchmark.workloads()[LIT_SHADOW_ZERO_EXTENT]
        text = soft_shadow_log_text(workload)
        for value in ("0.000000001", LIT_NAN, "inf", "-inf"):
            with self.subTest(value=value), self.assertRaises(benchmark.SmokeFailure):
                benchmark.soft_shadow_log(text.replace("angular=0 radius=0", f"angular={value} radius=0"), workload, True)

    def test_shadow_logs_reject_validation_capture_and_fallback_emission(self):
        workload = benchmark.workloads()[LIT_SHADOW_ZERO_EXTENT]
        for marker in ("FramebufferCapture: ready", "Vulkan: enabled validation layer", LIT_VK_LAYER_KHRONOS_VALIDATION,
            "render submission suspended", "retaining all-lit visibility", "preserving opaque visibility"):
            with self.subTest(marker=marker), self.assertRaises(benchmark.SmokeFailure):
                benchmark.soft_shadow_log(soft_shadow_log_text(workload) + "\n" + marker, workload, True)

    def test_shadow_coverage_requires_each_phase_and_rejects_caustic_work(self):
        workload = benchmark.workloads()[LIT_SHADOW_ZERO_EXTENT]
        values = scopes(workload)
        benchmark.validate_coverage(values, workload, 6, 100)
        for name in benchmark.SHADOW_PHASES:
            missing = copy.deepcopy(values)
            del missing[name]
            with self.subTest(scope=name), self.assertRaises(benchmark.SmokeFailure):
                benchmark.validate_coverage(missing, workload, 6, 100)
        for name in benchmark.SHADOW_INACTIVE:
            unexpected = copy.deepcopy(values)
            unexpected[name] = dict(values[benchmark.FRAME])
            with self.subTest(scope=name), self.assertRaises(benchmark.SmokeFailure):
                benchmark.validate_inactive_scopes(unexpected, workload)


class FrozenIdentityTests(unittest.TestCase):
    def test_source_content_and_manifest_are_both_frozen(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            manifest = source_manifest(root)
            benchmark.source_identity(manifest)
            (root / LIT_SOURCE_CPP).write_text("changed", encoding=LIT_UTF_8)
            with self.assertRaisesRegex(benchmark.SmokeFailure, "source bytes"):
                benchmark.source_identity(manifest)
            for document in ([], {LIT_REVISION: "r", LIT_FILES: {}}, {LIT_REVISION: "r", LIT_FILES: {"x": "invalid"}}):
                manifest.write_text(json.dumps(document), encoding=LIT_UTF_8)
                with self.assertRaises(benchmark.SmokeFailure):
                    benchmark.source_identity(manifest)

    def test_only_exact_contiguous_pipeline_cache_segments_can_mutate(self):
        with tempfile.TemporaryDirectory() as temporary:
            arm = make_arm(Path(temporary), LIT_BASELINE)
            before = benchmark.freeze_arm(arm)
            cache = arm.runtime / LIT_RES / volume_segment_filename(LIT_RUNTIME_PIPELINE_CACHE, 0)
            cache.write_bytes(b"runtime cache")
            self.assertEqual(before, benchmark.freeze_arm(arm))
            cache.write_bytes(b"updated runtime cache")
            self.assertEqual(before, benchmark.freeze_arm(arm))
            unrelated = arm.runtime / LIT_RES / volume_segment_filename(LIT_RUNTIME_PIPELINE_CACHE, 2)
            unrelated.write_bytes(b"gap means authored/unknown")
            self.assertNotEqual(before, benchmark.freeze_arm(arm))

    def test_binary_authored_source_and_shared_tool_changes_abort(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            arm = make_arm(root / LIT_ARM, LIT_BASELINE)
            other = make_arm(root / "other", LIT_CANDIDATE)
            tool = root / "tool.py"
            tool.write_bytes(b"tool")
            identities = {item.name: benchmark.freeze_arm(item) for item in (arm, other)}
            shared = {str(tool): benchmark.file_identity(tool)}
            benchmark.verify_frozen((arm, other), identities, shared)
            for path in (arm.executable, arm.runtime / LIT_RES / LIT_AUTHORED_VOL, tool):
                original = path.read_bytes()
                path.write_bytes(b"changed")
                with self.subTest(path=path), self.assertRaises(benchmark.SmokeFailure):
                    benchmark.verify_frozen((arm, other), identities, shared)
                path.write_bytes(original)
            dependency = arm.executable.parent / "runtime.dll"
            dependency.write_bytes(b"new loader dependency")
            with self.assertRaises(benchmark.SmokeFailure):
                benchmark.verify_frozen((arm, other), identities, shared)

    def test_shared_or_hard_linked_caches_are_refused(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            first = make_arm(root / "a", LIT_BASELINE)
            second = make_arm(root / "b", LIT_CANDIDATE)
            with self.assertRaisesRegex(benchmark.SmokeFailure, "distinct"):
                benchmark.validate_arm_separation((first, first))
            benchmark.validate_arm_separation((first, second))
            filename = volume_segment_filename(LIT_RUNTIME_PIPELINE_CACHE, 0)
            first_cache = first.runtime / LIT_RES / filename
            second_cache = second.runtime / LIT_RES / filename
            first_cache.write_bytes(b"shared cache")
            second_cache.hardlink_to(first_cache)
            with self.assertRaisesRegex(benchmark.SmokeFailure, "alias"):
                benchmark.validate_arm_separation((first, second))

    def test_missing_windows_crash_helper_fails_before_launch(self):
        with tempfile.TemporaryDirectory() as temporary:
            executable = Path(temporary) / "smoke.exe"
            executable.write_bytes(b"exe")
            with self.assertRaisesRegex(benchmark.SmokeFailure, "crash_handler"):
                benchmark.binary_identity(executable)


class PairedInferenceTests(unittest.TestCase):

    def test_missing_duplicate_and_wrong_order_trials_are_not_silently_dropped(self):
        workload, orders, trials = trial_matrix()
        changed = copy.deepcopy(trials)
        changed[0][LIT_POSITION] = 1
        for invalid in (trials[:-1], trials + [trials[0]], changed):
            with self.assertRaisesRegex(benchmark.SmokeFailure, LIT_EVERY_PLANNED_TRIAL):
                benchmark.compare_trials(invalid, orders, workload)

    def test_small_effect_and_uncertain_controls_cannot_claim_speedup(self):
        workload, orders, trials = trial_matrix(-.01)
        self.assertEqual(benchmark.compare_trials(trials, orders, workload)[LIT_STATUS], "unresolved")
        workload, orders, trials = trial_matrix(-.5)
        for trial in trials:
            if trial[LIT_ARM] == LIT_CANDIDATE:
                trial[LIT_SCOPES][benchmark.CONTROLS[0]][LIT_MEAN_MS] += .2 if trial[LIT_BLOCK] % 2 else -.2
        result = benchmark.compare_trials(trials, orders, workload)
        self.assertEqual(result[LIT_STATUS], LIT_CONTROL_UNCERTAIN)
        for trial in trials:
            if trial[LIT_ARM] == LIT_CANDIDATE:
                trial[LIT_SCOPES][benchmark.CONTROLS[0]][LIT_MEAN_MS] = .3
        self.assertEqual(benchmark.compare_trials(trials, orders, workload)[LIT_STATUS], LIT_CONTROL_DRIFT)


def reflection_log_text(workload):
    policy = workload.reflection_policy
    route = "screen-space" if policy.variant.mode == LIT_SCREEN else LIT_HARDWARE
    lines = [f"ReflectionSmokeProject: case {policy.family} created",
        f"ReflectionSmokeProject: reflection mode {policy.variant.mode}",
        f"ReflectionSmokeProject: hardware ray budget {policy.ray_budget}",
        "ReflectionSmokeProject: screen feedback 0",
        f"ReflectionSmokeProject: screen steps {policy.screen_steps}",
        "ReflectionSmokeProject: timing render unfocused 1",
        "ReflectionSmokeProject: timing in-flight ranges 32",
        "ReflectionSmokeProject: timing depth mip count 10",
        "ReflectionSmokeProject: hardware available", LIT_REFLECTIONSMOKEPROJECT_SHUTDOWN,
        f"Reflection resolve: {route}", LIT_GRAPHICS_RUNTIME_CREATED_DEVICE_EXAMPLE_GPU,
        "RendererSystem: material 'receiver' selected CS + PS through compute emulation",
        "RendererSystem: deferred rendering targets ready (960x720, samples=1)"]
    if policy.family.startswith("optical_"):
        lines.append(f"ReflectionSmokeProject: optical query limit {policy.optical_queries}")
    return "\n".join(lines)


class ReflectionWorkloadTests(unittest.TestCase):
    def test_reflection_environment_reuses_fixed_production_controls_after_clearing_inheritance(self):
        inherited = {LIT_NWB_REFLECTION_SMOKE_DIAGNOSTICS: "1", LIT_NWB_REFLECTION_SMOKE_HISTORY_SAMPLES: "1",
            LIT_NWB_AVBOIT_SMOKE_TIMING: "1", LIT_NWB_SMOKE_FRAMEBUFFER_CAPTURE_PATH: LIT_OLD_BMP,
            LIT_NWB_RENDERER_BASELINE_FIXED_DELTA_SECO: "5", LIT_PRESERVED: LIT_YES}
        for workload in benchmark.workloads().values():
            if workload.reflection_policy is None:
                continue
            with self.subTest(workload=workload.name):
                env, overrides = benchmark.configure_environment(inherited, workload, Path(LIT_NEW_TIMING_TXT))
                self.assertEqual(env, {LIT_PRESERVED: LIT_YES, **overrides})
                self.assertEqual(overrides[LIT_NWB_REFLECTION_SMOKE_DIAGNOSTICS], "0")
                self.assertEqual(overrides[LIT_NWB_REFLECTION_SMOKE_HISTORY_SAMPLES], LIT_N_16)
                self.assertEqual(overrides[LIT_NWB_RENDERER_BASELINE_FIXED_DELTA_SECO], "0.016666667")
        self.assertEqual(inherited[LIT_NWB_REFLECTION_SMOKE_DIAGNOSTICS], "1")

    def test_reflection_cannot_override_explicit_vulkan_validation(self):
        workload = benchmark.workloads()[LIT_REFLECTION_ROUGH_SPATIAL]
        for key in (LIT_VK_INSTANCE_LAYERS, LIT_VK_LOADER_LAYERS_ENABLE):
            with self.subTest(key=key), self.assertRaises(benchmark.SmokeFailure):
                benchmark.configure_environment({key: "validation"}, workload, Path("timing"))

    def test_screen_depth_rejects_incomplete_or_excess_mip_coverage(self):
        workload = benchmark.workloads()[LIT_REFLECTION_SCREEN_DEPTH]
        for multiplier in (1, 9, 11):
            changed = scopes(workload)
            changed[benchmark.reflection.DEPTH][LIT_GPU_SAMPLES] = 200 * multiplier
            with self.subTest(multiplier=multiplier), self.assertRaises(benchmark.SmokeFailure):
                benchmark.validate_coverage(changed, workload, 6, 100)

    def test_mirror_rejects_missing_spatial_range(self):
        workload = benchmark.workloads()[LIT_REFLECTION_MIRROR_SPATIAL]
        changed = scopes(workload)
        del changed[benchmark.reflection.SPATIAL]
        with self.assertRaises(benchmark.SmokeFailure):
            benchmark.validate_coverage(changed, workload, 6, 100)

    def test_filtered_retains_temporal_one_per_frame_and_both_controls(self):
        workload = benchmark.workloads()[LIT_REFLECTION_ROUGH_FILTERED]
        for missing in (benchmark.reflection.TEMPORAL, benchmark.reflection.SPATIAL, benchmark.CONTROLS[1]):
            changed = scopes(workload)
            del changed[missing]
            with self.subTest(scope=missing), self.assertRaises(benchmark.SmokeFailure):
                benchmark.validate_coverage(changed, workload, 6, 100)

    def test_inactive_ranges_are_rejected_even_outside_the_retained_window(self):
        for workload in benchmark.workloads().values():
            for inactive in workload.inactive_scopes:
                with self.subTest(workload=workload.name, scope=inactive), self.assertRaisesRegex(
                    benchmark.SmokeFailure, LIT_INACTIVE_REFLECTION_SCOPES):
                    benchmark.validate_inactive_scopes({inactive: {LIT_GPU_SAMPLES: 1}}, workload)

    def test_inactive_hashed_scope_is_decoded_and_rejected(self):
        workload = benchmark.workloads()[LIT_REFLECTION_SCREEN_DEPTH]
        symbols = benchmark.load_name_symbols(None, workload.observed_scopes)
        token = next(token for token, name in symbols.items() if name == benchmark.reflection.HARDWARE)
        text = f"=== interval: 1 frames / 0.5s ===\n  {token}: window_avg_ms=1 window_min_ms=1 window_max_ms=1 published_windows=1 total_ms=1 gpu_samples=1 sample_avg_ms=1\n"
        parsed = benchmark.summarize_intervals(benchmark.parse_intervals(text, symbols, finalized=True))
        with self.assertRaisesRegex(benchmark.SmokeFailure, LIT_INACTIVE_REFLECTION_SCOPES):
            benchmark.validate_inactive_scopes(parsed, workload)

    def test_reflection_runtime_signature_uses_production_log_validation(self):
        for workload in benchmark.workloads().values():
            if workload.reflection_policy is None:
                continue
            with self.subTest(workload=workload.name):
                text = reflection_log_text(workload)
                signature = workload.validate_log(text, workload, True)
                self.assertEqual(signature["reflection_route"], workload.reflection_policy.variant.mode)
                self.assertEqual(signature["reflection_policy"]["roughness"], workload.reflection_policy.roughness)
                self.assertEqual(signature, workload.validate_log(text.replace("\n", "\r\n"), workload, True))
                for altered in (text.replace(LIT_N_960X720, LIT_N_1280X900),
                    text.replace("timing in-flight ranges 32", "timing in-flight ranges 2"),
                    text + "\nReflectionSmokeProject: screen steps 16", text + "\nVUID-rejected",
                    text + "\nFramebufferCapture: capture ready", text.replace("hardware available", "hardware unavailable"),
                    text.replace(LIT_REFLECTIONSMOKEPROJECT_SHUTDOWN, "")):
                    with self.assertRaises(benchmark.SmokeFailure):
                        workload.validate_log(altered, workload, True)

    def test_inside_query_cap_is_strict_despite_shared_benchmark_only_checking_clear(self):
        workload = benchmark.workloads()[LIT_REFLECTION_OPTICAL_INSIDE]
        text = reflection_log_text(workload)
        for changed in (text.replace("optical query limit 16", "optical query limit 8"),
            text + "\nReflectionSmokeProject: optical query limit 16"):
            with self.assertRaisesRegex(benchmark.SmokeFailure, "optical query limit"):
                workload.validate_log(changed, workload, True)

    def test_declared_native_dispatch_count_never_changes_range_coverage(self):
        common = [LIT_BASELINE_EXECUTABLE, "a", LIT_BASELINE_RUNTIME, LIT_AR, LIT_BASELINE_SOURCE_MANIFEST, LIT_AS_JSON,
            LIT_CANDIDATE_EXECUTABLE, "b", LIT_CANDIDATE_RUNTIME, LIT_BR, LIT_CANDIDATE_SOURCE_MANIFEST, LIT_BS_JSON,
            LIT_LOGSERVER_EXECUTABLE, LIT_LOGGER, LIT_OUTPUT_DIRECTORY, LIT_OUTPUT]
        for count in (2, 169):
            args = benchmark.parse_args(common + [LIT_WORKLOAD, "reflection-optical-csg-cap",
                LIT_CANDIDATE_HARDWARE_DISPATCHES_PER_RANG, str(count)])
            workload = benchmark.workloads()[args.workload]
            value = scopes(workload)
            benchmark.validate_coverage(value, workload, 6, 100)
            value[benchmark.reflection.HARDWARE][LIT_GPU_SAMPLES] *= count
            with self.subTest(native_calls=count), self.assertRaises(benchmark.SmokeFailure):
                benchmark.validate_coverage(value, workload, 6, 100)
        for invalid in ("0", "-1", "1382401"):
            with self.subTest(native_calls=invalid), contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                benchmark.parse_args(common + [LIT_WORKLOAD, "reflection-optical-csg-cap",
                    LIT_CANDIDATE_HARDWARE_DISPATCHES_PER_RANG, invalid])
        for name in (LIT_TRANSPARENT_MULTI, LIT_REFLECTION_SCREEN_DEPTH, LIT_REFLECTION_ROUGH_FILTERED):
            with self.subTest(workload=name), contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                benchmark.parse_args(common + [LIT_WORKLOAD, name, LIT_CANDIDATE_HARDWARE_DISPATCHES_PER_RANG, "2"])

    def test_sliced_long_hardware_range_retains_all_work_in_per_frame_and_paired_times(self):
        workload = benchmark.workloads()["reflection-optical-csg-cap"]
        _, orders, trials = trial_matrix()
        for trial in trials:
            candidate = trial[LIT_ARM] == LIT_CANDIDATE
            trial["native_hardware_dispatches_per_range"] = 169 if candidate else 1
            frame_ms = 2400 if candidate else 100
            trial[LIT_SCOPES] = scopes(workload, frame_ms=frame_ms)
            trial["wall_frame"] = {"eligible": True, "mean_frame_ms": frame_ms, "mean_fps": 1000 / frame_ms}
            hardware = trial[LIT_SCOPES][benchmark.reflection.HARDWARE]
            hardware[LIT_MEAN_MS] = 2351 if candidate else 50
            hardware[LIT_TOTAL_MS] = hardware[LIT_MEAN_MS] * hardware[LIT_GPU_SAMPLES]
            benchmark.validate_coverage(trial[LIT_SCOPES], workload, 6, 100)
        result = benchmark.compare_trials(trials, orders, workload)
        self.assertEqual(result[benchmark.LIT_MEANS_MS][LIT_CANDIDATE][benchmark.reflection.HARDWARE], 2351)
        self.assertEqual(result["secondary_per_frame_work"][benchmark.LIT_MEANS_MS][LIT_CANDIDATE], 2351)
        self.assertEqual(result["secondary"][LIT_MEAN_MS], 2301)
        self.assertEqual(result["frame"][LIT_MEAN_MS], 2300)

    def test_depth_secondary_rejects_an_incomplete_trial_matrix(self):
        workload = benchmark.workloads()[LIT_REFLECTION_SCREEN_DEPTH]
        _, orders, trials = trial_matrix()
        for trial in trials:
            frame_ms = 5 if trial[LIT_ARM] == LIT_BASELINE else 4.5
            trial[LIT_SCOPES] = scopes(workload, frame_ms=frame_ms)
            trial["wall_frame"] = {"eligible": True, "mean_frame_ms": frame_ms, "mean_fps": 1000 / frame_ms}
            if trial[LIT_ARM] == LIT_CANDIDATE:
                trial[LIT_SCOPES][benchmark.reflection.DEPTH][LIT_MEAN_MS] = .4
                trial[LIT_SCOPES][benchmark.reflection.DEPTH][LIT_TOTAL_MS] = .4 * 200 * 10
        benchmark.compare_trials(trials, orders, workload)
        with self.assertRaisesRegex(benchmark.SmokeFailure, LIT_EVERY_PLANNED_TRIAL):
            benchmark.compare_trials(trials[:-1], orders, workload)


def caustic_log_text(preset=LIT_POPULATED, enabled=True, capture=False):
    distance = benchmark.caustic.PRESETS[preset]
    text = "\n".join((
        LIT_GRAPHICS_RUNTIME_CREATED_DEVICE_EXAMPLE_GPU,
        LIT_RENDERERSYSTEM_MATERIAL_GLASS_SELECTED,
        "TransparentMultiSmokeProject: natural hardware shadow route selected on RayQuery-capable hardware",
        LIT_RENDERERSYSTEM_DISPATCHED_HARDWARE_TRA,
        LIT_AVBOITTIMINGPROBE_IN_FLIGHT_RANGES_32, LIT_AVBOITTIMINGPROBE_RENDER_UNFOCUSED_1,
        "AvboitTimingProbe: caustic in-flight ranges 32",
        "CausticSphereSmokeProject: reflection mode 0", "CausticSphereSmokeProject: camera refraction disabled",
        "CausticSphereSmokeProject: caustics " + ("enabled" if enabled else LIT_DISABLED),
        "CausticTimingProbe: reflection diagnostics false temporal false spatial false feedback false",
        "CausticTimingProbe: scene single-static-sphere-ground-v1",
        f"CausticTimingProbe: camera {preset} distance {distance} height 0.85",
        "CausticTimingProbe: fixed delta 0.016666667 yaw 0 sphere scale 0.7",
        "CausticTimingProbe: directional pitch 0.9 yaw 0.65 intensity 2",
        "CausticTimingProbe: vertical FOV radians 1.0471976",
        "CausticTimingProbe: photon phases bootstrap 2 converged 4 warmup 8",
        LIT_RENDERERSYSTEM_DEFERRED_RENDERING_TARG,
        LIT_TRANSPARENTMULTISMOKEPROJECT_SHUTDOWN))
    if enabled:
        text += "\nRendererSystem: dispatched hardware caustic producer (131072 photons/frame, 2 temporal phases, 262144 full-grid budget, 1 caustic lights, 1 refractive instances)"
    if capture:
        text += "\nFramebufferCapture: capture ready\nFramebufferCapture: graphics source frame 359"
    return text


class CausticMeasurementTests(unittest.TestCase):
    def test_completed_resolve_and_photon_counts_cannot_be_divided_or_missing(self):
        workload = benchmark.workloads()[LIT_CAUSTIC_POPULATED]
        benchmark.validate_coverage(scopes(workload), workload, 6, 100)
        for name in (benchmark.caustic.RESOLVE, benchmark.caustic.PHOTONS):
            missing = scopes(workload)
            del missing[name]
            with self.subTest(scope=name), self.assertRaises(benchmark.SmokeFailure):
                benchmark.validate_coverage(missing, workload, 6, 100)
            wrong = scopes(workload)
            wrong[name][LIT_GPU_SAMPLES] *= 2
            with self.assertRaisesRegex(benchmark.SmokeFailure, LIT_SAMPLE_RATIO):
                benchmark.validate_coverage(wrong, workload, 6, 100)

    def test_warmup_is_actual_completed_gpu_work_not_publication_count(self):
        values = {name: {LIT_GPU_SAMPLES: 12, LIT_TOTAL_MS: 1, LIT_REPORTS: 2}
            for name in (benchmark.FRAME, benchmark.caustic.PHOTONS)}
        benchmark.caustic.validate_warmup(values)
        for name in values:
            changed = copy.deepcopy(values)
            changed[name][LIT_GPU_SAMPLES] = 11
            changed[name][LIT_REPORTS] = 1000
            with self.assertRaises(benchmark.SmokeFailure):
                benchmark.caustic.validate_warmup(changed)

    def test_actual_native_route_geometry_and_photon_budget_are_load_bearing(self):
        for preset in benchmark.caustic.PRESETS:
            workload = benchmark.workloads()[LIT_CAUSTIC + preset]
            text = caustic_log_text(preset)
            workload.validate_log(text, workload, True)
            for changed in (text.replace("131072 photons", "65536 photons"),
                text.replace("262144 full-grid", "131072 full-grid"), text.replace("1 refractive", "2 refractive"),
                text.replace("height 0.85", "height 0.7"), text.replace("sphere scale 0.7", "sphere scale 0.35"),
                text.replace("intensity 2", "intensity 4"), text.replace("radians 1.0471976", "radians 0.7"),
                text.replace("warmup 8", "warmup 16"), text.replace("yaw 0", "yaw 1"),
                text.replace("hardware caustic producer", "software caustic producer"),
                text + "\nCausticSphereSmokeProject: reflection mode 2",
                text + "\nAvboitTimingProbe: caustic in-flight ranges 2"):
                with self.subTest(preset=preset, changed=changed[-120:]), self.assertRaises(benchmark.SmokeFailure):
                    workload.validate_log(changed, workload, True)

    def test_caustic_current_route_rejects_retired_and_contradictory_routes(self):
        workload = benchmark.workloads()[LIT_CAUSTIC_POPULATED]
        settings = dict(workload.environment_overrides)
        text = caustic_log_text()
        hardware_marker = LIT_RENDERERSYSTEM_DISPATCHED_HARDWARE_TRA
        legacy = text.replace("natural hardware shadow route", "natural hybrid shadow route").replace(
            hardware_marker, LIT_RENDERERSYSTEM_DISPATCHED_SOFTWARE_SHA)
        with self.assertRaises(benchmark.SmokeFailure):
            benchmark.caustic.validate_log(legacy, settings)
        retired_route = next(line for line in legacy.splitlines() if "natural hybrid shadow route" in line)
        for altered in (legacy, text + "\n" + retired_route):
            with self.subTest(text=altered), self.assertRaises(benchmark.SmokeFailure):
                workload.validate_log(altered, workload, True)
        for altered in (text.replace(hardware_marker, ""),
            text + "\n" + LIT_RENDERERSYSTEM_DISPATCHED_SOFTWARE_SHA):
            with self.subTest(text=altered), self.assertRaises(benchmark.SmokeFailure):
                benchmark.caustic.validate_log(altered, settings)

    def test_capture_evidence_is_required_for_qualification_and_forbidden_for_timing(self):
        workload = benchmark.workloads()[LIT_CAUSTIC_POPULATED]
        text = caustic_log_text(capture=True)
        settings = dict(workload.environment_overrides)
        actual = benchmark.caustic.validate_log(text, settings, capture=True)
        self.assertEqual(actual["graphics_source_frame"], 359)
        for value in (text, caustic_log_text() + "\nVK_LAYER_KHRONOS_validation"):
            with self.assertRaises(benchmark.SmokeFailure):
                workload.validate_log(value, workload, True)
        for changed in (caustic_log_text(), text.replace("source frame 359", "source frame 7")):
            with self.assertRaises(benchmark.SmokeFailure):
                benchmark.caustic.validate_log(changed, settings, capture=True)
        off = benchmark.caustic.environment(LIT_POPULATED, False)
        benchmark.caustic.validate_log(caustic_log_text(enabled=False, capture=True), off, capture=True)
        with self.assertRaises(benchmark.SmokeFailure):
            benchmark.caustic.validate_log(text, off, capture=True)

    def test_receiver_mask_excludes_sphere_and_image_background(self):
        width, height = 160, 120
        points = list(benchmark.caustic.receiver_pixels(width, height, 2.2))
        self.assertGreater(len(points), 100)
        self.assertNotIn((width // 2, height // 2), points)
        self.assertTrue(all(y > height // 2 for _, y in points))
        off = (width, height, [[(50, 50, 50) for _ in range(width)] for _ in range(height)])
        rows = [list(row) for row in off[2]]
        for x, y in points[:20]:
            rows[y][x] = (70, 70, 70)
        for x in range(width):
            rows[0][x] = (255, 255, 255)
        result = benchmark.caustic.footprint((width, height, rows), off, 2.2)
        self.assertEqual(result[LIT_POSITIVE_PIXELS], 20)
        self.assertEqual(benchmark.caustic.footprint(off, off, 2.2)[LIT_POSITIVE_PIXELS], 0)

    def test_visible_pixel_and_tile_reduction_are_independent_qualification_gates(self):
        good = {LIT_POPULATED: {LIT_POSITIVE_PIXELS: 1000, LIT_POSITIVE_TILES: 100, LIT_POSITIVE_CHANNEL_GAIN: 60000},
            LIT_SPARSE: {LIT_POSITIVE_PIXELS: 250, LIT_POSITIVE_TILES: 30, LIT_POSITIVE_CHANNEL_GAIN: 15000}}
        benchmark.caustic.validate_metrics(good)
        for key, value in ((LIT_POSITIVE_PIXELS, 99), (LIT_POSITIVE_CHANNEL_GAIN, 1499), (LIT_POSITIVE_TILES, 81)):
            changed = copy.deepcopy(good)
            changed[LIT_SPARSE][key] = value
            with self.subTest(key=key), self.assertRaises(benchmark.SmokeFailure):
                benchmark.caustic.validate_metrics(changed)
        with self.assertRaises(benchmark.SmokeFailure):
            benchmark.caustic.validate_metrics({LIT_POPULATED: good[LIT_POPULATED], LIT_SPARSE: good[LIT_POPULATED]})

    def test_caustic_cli_requires_both_frozen_arm_qualification_reports(self):
        common = [LIT_BASELINE_EXECUTABLE, "a", LIT_BASELINE_RUNTIME, LIT_AR, LIT_BASELINE_SOURCE_MANIFEST, LIT_AS_JSON,
            LIT_CANDIDATE_EXECUTABLE, "b", LIT_CANDIDATE_RUNTIME, LIT_BR, LIT_CANDIDATE_SOURCE_MANIFEST, LIT_BS_JSON,
            LIT_LOGSERVER_EXECUTABLE, LIT_LOGGER, LIT_OUTPUT_DIRECTORY, LIT_OUTPUT]
        proofs = ["--baseline-caustic-qualification", "aqual.json", "--candidate-caustic-qualification", "bqual.json"]
        for partial in ([], proofs[:2], proofs[2:]):
            with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                benchmark.parse_args(common + [LIT_WORKLOAD, LIT_CAUSTIC_POPULATED] + partial)
        with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
            benchmark.parse_args(common + proofs)

    def test_qualification_replay_requires_exact_identity_policy_and_all_four_captures(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / LIT_QUALIFICATION_JSON
            identity = {LIT_FROZEN: "actual-arm"}
            document = {LIT_SCHEMA: benchmark.caustic.SCHEMA, LIT_POLICY: benchmark.caustic.POLICY,
                LIT_ARM: identity, LIT_CAPTURES: {}}
            for changed in ({**document, LIT_ARM: {LIT_FROZEN: "other-arm"}},
                {**document, LIT_POLICY: {}}, document):
                path.write_text(json.dumps(changed), encoding=LIT_UTF_8)
                with self.assertRaises(benchmark.SmokeFailure):
                    benchmark.caustic.validate_report(path, identity)

    def test_caustic_rejected_output_preserves_existing_evidence_without_launch(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            output = directory / "old_evidence"
            output.mkdir()
            saved = output / LIT_FAILURE_JSON
            saved.write_bytes(b"original failure evidence")
            argv = ["--executable", str(directory / "bin" / LIT_FIXTURE_EXE),
                "--runtime", str(directory / LIT_RUNTIME), "--source-manifest", str(directory / LIT_SOURCE / LIT_SOURCE_JSON),
                LIT_LOGSERVER_EXECUTABLE, str(directory / LIT_LOGGER / LIT_LOGGER_EXE), LIT_OUTPUT_DIRECTORY, str(output)]
            with patch.object(benchmark.caustic.subprocess, "run") as launch, contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(benchmark.caustic.main(argv), 1)
            launch.assert_not_called()
            self.assertEqual(saved.read_bytes(), b"original failure evidence")
            self.assertEqual(sorted(path.name for path in output.iterdir()), [LIT_FAILURE_JSON])

    def test_caustic_output_overlap_rejected_in_both_directions(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            protected = root / "inputs"
            for output in (protected, protected / LIT_OUTPUT, root):
                with self.subTest(output=output), self.assertRaises(benchmark.SmokeFailure):
                    benchmark.caustic.validate_output_path(output, (protected,))
            benchmark.caustic.validate_output_path(root / "evidence", (protected,))

    def test_caustic_requested_gpu_debug_needs_actual_loader_activation(self):
        utility = benchmark.caustic
        text = "\n".join(utility.GPU_DEBUG_MARKERS)
        utility.validate_gpu_debug(text, [LIT_GPUDBG])
        utility.validate_gpu_debug("ordinary launch", [])
        for marker in utility.GPU_DEBUG_MARKERS:
            with self.subTest(marker=marker), self.assertRaises(benchmark.SmokeFailure):
                utility.validate_gpu_debug(text.replace(marker, "requested only"), [LIT_GPUDBG])

    def test_synthetic_distinct_frozen_roots_replay_and_tampering_checks(self):
        # Small synthetic parser evidence only; this does not qualify a real framebuffer or GPU arm.
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            policy = {**benchmark.caustic.POLICY, "width": 160, "height": 120}
            with patch.multiple(benchmark.caustic, WIDTH=160, HEIGHT=120, POLICY=policy):
                utility = benchmark.caustic
                width, height = 160, 120
                launchers, sources = {}, {}
                launcher_bytes = Path(utility.__file__).with_name(LIT_WINDOW_CAPTURE_SMOKE_PY).read_bytes()
                for name in (LIT_BASELINE, LIT_CANDIDATE):
                    arm_root = directory / name
                    launcher = arm_root / "source/tests/smoke/window_capture_smoke.py"
                    launcher.parent.mkdir(parents=True)
                    launcher.write_bytes(launcher_bytes)
                    manifest = arm_root / LIT_SOURCE_JSON
                    manifest.write_text(json.dumps({LIT_REVISION: name, LIT_FILES: {
                        launcher.relative_to(arm_root).as_posix(): hashlib.sha256(launcher_bytes).hexdigest()}}), encoding=LIT_UTF_8)
                    launchers[name] = launcher
                    sources[name] = benchmark.source_identity(manifest)
                identity = {"executable": str(directory / LIT_FIXTURE_EXE), LIT_RUNTIME: str(directory / LIT_RUNTIME),
                    LIT_SOURCE: sources[LIT_CANDIDATE]}
                logger = directory / LIT_LOGGER_EXE
                logger.write_bytes(b"synthetic logger identity")
                (directory / "crash_handler.exe").write_bytes(b"synthetic crash helper identity")
                logger_dependency = directory / "logger_dependency.dll"
                logger_dependency.write_bytes(b"synthetic logger dependency identity")
                args = SimpleNamespace(**identity, logserver_executable=logger, timeout=90.0, application_arg=[])
                document = {LIT_SCHEMA: utility.SCHEMA, LIT_POLICY: policy, LIT_ARM: identity, LIT_CAPTURES: {},
                    LIT_METRICS: {}, LIT_QUALIFICATION_TOOL: benchmark.file_identity(Path(utility.__file__)),
                    "launcher": benchmark.file_identity(Path(utility.__file__).with_name(LIT_WINDOW_CAPTURE_SMOKE_PY)),
                    "logserver": benchmark.file_identity(logger), "logserver_path": str(logger),
                    "logserver_binaries": benchmark.binary_identity(logger),
                    "timeout_seconds": 90.0, "application_args": []}
                for preset, distance in utility.PRESETS.items():
                    frames = {}
                    for enabled in (True, False):
                        key = preset + ("_on" if enabled else "_off")
                        image, log = directory / (key + LIT_BMP), directory / (key + ".log")
                        selected = set(utility.receiver_pixels(width, height, distance)) if enabled else set()
                        pixels = bytearray()
                        for y in reversed(range(height)):
                            for x in range(width):
                                pixels.extend(bytes((70, 70, 70) if (x, y) in selected else (50, 50, 50)))
                        header = struct.pack("<2sIHHI", b"BM", 54 + len(pixels), 0, 0, 54)
                        header += struct.pack("<IiiHHIIiiII", 40, width, height, 1, 24, 0, len(pixels), 0, 0, 0, 0)
                        image.write_bytes(header + pixels)
                        log.write_text(caustic_log_text(preset, enabled, capture=True).replace(LIT_N_1280X900, "160x120"), encoding=LIT_UTF_8)
                        settings = utility.environment(preset, enabled)
                        document[LIT_CAPTURES][key] = {"settings": settings,
                            LIT_RUNTIME: utility.validate_log(log.read_text(encoding=LIT_UTF_8), settings, capture=True),
                            LIT_COMMAND: utility.capture_command(args, image, launcher=launchers[LIT_CANDIDATE]),
                            LIT_IMAGE: {LIT_PATH: image.name, LIT_IDENTITY: benchmark.file_identity(image)},
                            LIT_LOG: {LIT_PATH: log.name, LIT_IDENTITY: benchmark.file_identity(log)}}
                        frames[enabled] = utility.read_bmp_24_rows(image)
                    document[LIT_METRICS][preset] = utility.footprint(frames[True], frames[False], distance)
                report = directory / LIT_QUALIFICATION_JSON
                report.write_text(json.dumps(document), encoding=LIT_UTF_8)
                result, paths = utility.validate_report(report, identity)
                self.assertEqual(result[LIT_METRICS], document[LIT_METRICS])
                self.assertEqual(len(paths), 12)
                # Both physical frozen roots contain identical launchers; replay must bind the selected arm's path.
                baseline_identity = {**identity, LIT_SOURCE: sources[LIT_BASELINE]}
                baseline_document = copy.deepcopy(document)
                baseline_document[LIT_ARM] = baseline_identity
                for key, record in baseline_document[LIT_CAPTURES].items():
                    record[LIT_COMMAND] = utility.capture_command(args, directory / (key + LIT_BMP),
                        launcher=launchers[LIT_BASELINE])
                report.write_text(json.dumps(baseline_document), encoding=LIT_UTF_8)
                baseline_result, _ = utility.validate_report(report, baseline_identity)
                self.assertEqual(baseline_result[LIT_METRICS], document[LIT_METRICS])
                wrong_root = copy.deepcopy(document)
                wrong_root[LIT_CAPTURES][LIT_POPULATED_ON][LIT_COMMAND] = baseline_document[LIT_CAPTURES][LIT_POPULATED_ON][LIT_COMMAND]
                report.write_text(json.dumps(wrong_root), encoding=LIT_UTF_8)
                with self.assertRaisesRegex(benchmark.SmokeFailure, "launch command changed"):
                    utility.validate_report(report, identity)
                unpinned_identity = copy.deepcopy(identity)
                unpinned_identity[LIT_SOURCE][LIT_FILES].pop(str(launchers[LIT_CANDIDATE].resolve()))
                unpinned = copy.deepcopy(document)
                unpinned[LIT_ARM] = unpinned_identity
                report.write_text(json.dumps(unpinned), encoding=LIT_UTF_8)
                with self.assertRaisesRegex(benchmark.SmokeFailure, "launcher is unpinned"):
                    utility.validate_report(report, unpinned_identity)
                report.write_text(json.dumps(document), encoding=LIT_UTF_8)
                launchers[LIT_CANDIDATE].write_bytes(launcher_bytes + LIT_MODIFIED.encode(LIT_UTF_8))
                with self.assertRaisesRegex(benchmark.SmokeFailure, "launcher is unpinned or its bytes changed"):
                    utility.validate_report(report, identity)
                launchers[LIT_CANDIDATE].write_bytes(launcher_bytes)
                saved_dependency = logger_dependency.read_bytes()
                logger_dependency.write_bytes(saved_dependency + LIT_MODIFIED.encode(LIT_UTF_8))
                with self.assertRaisesRegex(benchmark.SmokeFailure, "dependency inventory changed"):
                    utility.validate_report(report, identity)
                logger_dependency.write_bytes(saved_dependency)
                for kind in (LIT_IMAGE, LIT_LOG):
                    raw = directory / document[LIT_CAPTURES][LIT_POPULATED_ON][kind][LIT_PATH]
                    saved = raw.read_bytes()
                    raw.write_bytes(saved + LIT_MODIFIED.encode(LIT_UTF_8))
                    with self.subTest(kind=kind), self.assertRaisesRegex(benchmark.SmokeFailure, "raw .* changed"):
                        utility.validate_report(report, identity)
                    raw.write_bytes(saved)
                for field in (LIT_METRICS, LIT_QUALIFICATION_TOOL, LIT_COMMAND):
                    changed = copy.deepcopy(document)
                    if field == LIT_METRICS:
                        changed[field][LIT_POPULATED][LIT_POSITIVE_PIXELS] += 1
                    elif field == LIT_QUALIFICATION_TOOL:
                        changed[field]["sha256"] = "0" * 64
                    else:
                        command = changed[LIT_CAPTURES][LIT_POPULATED_ON][LIT_COMMAND]
                        command[command.index("--application-capture-frame-count") + 1] = LIT_N_16
                    report.write_text(json.dumps(changed), encoding=LIT_UTF_8)
                    with self.subTest(field=field), self.assertRaises(benchmark.SmokeFailure):
                        utility.validate_report(report, identity)


if __name__ == LIT_MAIN:
    unittest.main()
