#!/usr/bin/env python3
"""Strict presentation measurement replay and acquisition failure retention; never launch a renderer."""

import json
from pathlib import Path
import subprocess
import sys
from tempfile import TemporaryDirectory
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "smoke"))
import stress_timing_smoke as smoke

# Shared literals (no inline hardcodes below this block).
LIT_QUARTER = "quarter"
LIT_REFERENCE_THREE = "reference_three"
LIT_AUTOMATIC = "automatic"
LIT_REFERENCE = "reference"
LIT_REFERENCE_GRID9 = "reference_grid9"
LIT_EVERY_FRAME = "every_frame"
LIT_SURFELGIQUALITYSMOKE_REQUESTED_RESOLVE = "SurfelGiQualitySmoke: requested resolve_factor=4"
LIT_RENDERERSYSTEM_DISPATCHED_SURFEL_GI_RE = "RendererSystem: dispatched surfel GI resolve (factor=4, source=1280x900, resolve=320x225)"
LIT_N_96 = "96"
LIT_FPS_16_PRESENTATIONS_480_SECONDS_30_FI = "fps=16 presentations=480 seconds=30 first=80 last=560"
LIT_RENDERER_EXE = "renderer.exe"
LIT_EXECUTABLE = "--executable"
LIT_WORKING_DIRECTORY = "--working-directory"
LIT_NO_LOGSERVER = "--no-logserver"
LIT_SHADOW_RECEIVER_RESOLUTION = "--shadow-receiver-resolution"
LIT_NWB_SHADOW_RECEIVER_RESOLUTION = "NWB_SHADOW_RECEIVER_RESOLUTION"
LIT_HALF = "half"
LIT_ALLOCATIONS = "allocations"
LIT_SYS_STDERR = "sys.stderr"
LIT_SURFEL_GI_RESOLVE_RESOLUTION = "--surfel-gi-resolve-resolution"
LIT_NWB_SURFEL_GI_RESOLVE_RESOLUTION = "NWB_SURFEL_GI_RESOLVE_RESOLUTION"
LIT_VERIFIED = "verified"
LIT_SOFTWARE_SHADOW_CAPTURE_CADENCE = "--software-shadow-capture-cadence"
LIT_REUSE_ONE_FRAME = "reuse_one_frame"
SOFTWARE_SHADOW_ONE_FRAME_REUSE = smoke.SOFTWARE_SHADOW_CAPTURE_REUSE_PREFIX + "(cadence=2)"
LIT_NWB_SOFTWARE_SHADOW_CAPTURE_CADENCE = "NWB_SOFTWARE_SHADOW_CAPTURE_CADENCE"
LIT_ACCEPTED_REUSE_VERIFIED = "accepted_reuse_verified"
LIT_REUSE_TWO_FRAMES = "reuse_two_frames"
LIT_INVALID = "invalid"
LIT_TRACE = "trace"
LIT_LIGHT_SPACE = "light_space"
LIT_SOFTWARE_SHADOW_BACKEND = "--software-shadow-backend"
LIT_SOFTWARE_SHADOW_BUDGET_MIB = "--software-shadow-budget-mib"
LIT_N_256 = "256"
LIT_SOFTWARE_SHADOW_DIRECTIONAL_RESOLUTION = "--software-shadow-directional-resolution"
LIT_N_1024 = "1024"
LIT_SOFTWARE_SHADOW_POINT_RESOLUTION = "--software-shadow-point-resolution"
LIT_N_512 = "512"
LIT_NWB_CAUSTIC_PHOTON_GRID_DIVISOR = "NWB_CAUSTIC_PHOTON_GRID_DIVISOR"
LIT_NWB_SHADOW_TRANSPARENT_SAMPLING = "NWB_SHADOW_TRANSPARENT_SAMPLING"
LIT_NWB_SOFTWARE_SHADOW_COVERAGE = "NWB_SOFTWARE_SHADOW_COVERAGE"
LIT_FITTED_VOLUME = "fitted_volume"
LIT_NWB_SOFTWARE_SHADOW_BLOCKER_SEARCH = "NWB_SOFTWARE_SHADOW_BLOCKER_SEARCH"
LIT_COMPACT_CROSS5 = "compact_cross5"
LIT_N_1 = "-1"
LIT_N_31 = "31"
LIT_N_2049 = "2049"
LIT_N_32_5 = "32.5"
LIT_HARDWARE = "hardware"
LIT_SOFTWARE_SHADOW_COVERAGE = "--software-shadow-coverage"
LIT_SOFTWARE_SHADOW_BLOCKER_SEARCH = "--software-shadow-blocker-search"
LIT_NWB_SOFTWARE_SHADOW_BACKEND = "NWB_SOFTWARE_SHADOW_BACKEND"
LIT_NWB_SOFTWARE_SHADOW_BUDGET_MIB = "NWB_SOFTWARE_SHADOW_BUDGET_MIB"
LIT_NWB_SOFTWARE_SHADOW_DIRECTIONAL_RESOLU = "NWB_SOFTWARE_SHADOW_DIRECTIONAL_RESOLUTION"
LIT_NWB_SOFTWARE_SHADOW_POINT_RESOLUTION = "NWB_SOFTWARE_SHADOW_POINT_RESOLUTION"
LIT_NWB_UNREQUESTED_SETTING = "NWB_UNREQUESTED_SETTING"
LIT_BAD = "bad"
LIT_PATH = "PATH"
LIT_KEPT = "kept"
LIT_EMPTY = "  "
LIT_OBSERVED = "observed"
LIT_FPS = "fps"
LIT_SOFTWARE_SHADOW_SETTINGS_MISMATCH = "software shadow settings mismatch"
LIT_CENTER1 = "center1"
LIT_BLOCKER_SEARCH = "blocker_search"
LIT_SOFTWARE_SHADOW_SETTINGS = "software_shadow_settings"
LIT_LAUNCH_JSON = "launch.json"
LIT_UTF_8 = "utf-8"
LIT_FIXTURE = "fixture"
LIT_ENVIRONMENT = "environment"
LIT_RUNTIME_LOG = "runtime.log"
LIT_SHADOW_TRANSPARENT_SAMPLING = "--shadow-transparent-sampling"
LIT_IDENTITIES = "identities"
LIT_BUILD_LAUNCH_ENVIRONMENT = "build_launch_environment"
LIT_LAUNCH_LOGSERVER = "launch_logserver"
LIT_LOG = "*.log"
LIT_LAUNCH_TESTBED = "launch_testbed"
LIT_TERMINATE_PROCESS = "terminate_process"
LIT_SHUTDOWN_LOGSERVER_AND_COLLECT = "shutdown_logserver_and_collect"
LIT_DEVICE_MATERIAL_SIGNATURE = "device_material_signature"
LIT_OPTICAL_REFLECTION = "optical_reflection"
LIT_STATUS = "status"
LIT_MEASUREMENT = "measurement"
LIT_SAMPLE_COUNT = "sample_count"
LIT_SUMS = "sums"
LIT_UNSUPPORTED_RATIO = "unsupported_ratio"
LIT_QUERIES_PER_HARDWARE_RAY = "queries_per_hardware_ray"
LIT_EXTERIOR_ELIGIBLE_RATIO = "exterior_eligible_ratio"
LIT_RANGES_BY_GENERATION = "ranges_by_generation"
LIT_GENERATION = "generation"
LIT_UNSUPPORTED = "unsupported"
LIT_HARDWARE_READY_SAMPLES = "hardware_ready_samples"
LIT_UNSUPPORTED_PATHS_10 = " unsupported_paths=10"
LIT_EXTRA_0 = " extra=0"
LIT_HARDWARE_QUERIES_0 = "hardware_queries=0"
LIT_REFLECTION_SCREEN_STEPS = "--reflection-screen-steps"
LIT_NWB_REFLECTION_SCREEN_STEPS = "NWB_REFLECTION_SCREEN_STEPS"
LIT_SCREEN_WORK_MEASURED = "screen_work_measured"
LIT_N_48 = "48"
LIT_SCREEN = "screen"
LIT_WORKLOAD = "workload"
LIT_TOTAL = "total"
LIT_TOTAL_20 = "total=20"
LIT_ROW_SPACING_X_0_72 = "row_spacing_x=0.72"
LIT_CHARACTERS_PER_CLASS = "--characters-per-class"
LIT_NWB_STRESS_TEST_SPIN_ANGLE = "NWB_STRESS_TEST_SPIN_ANGLE"
LIT_NWB_RENDERER_BASELINE_FIXED_DELTA_SECO = "NWB_RENDERER_BASELINE_FIXED_DELTA_SECONDS"
LIT_NWB_RENDERER_BASELINE_CAPTURE_FREEZE_F = "NWB_RENDERER_BASELINE_CAPTURE_FREEZE_FRAME"
LIT_NWB_STRESS_CHARACTERS_PER_CLASS = "NWB_STRESS_CHARACTERS_PER_CLASS"
LIT_N_10 = "10"
LIT_ANIMATE = "--animate"
LIT_PACING = "pacing"
LIT_COMPLETE_FPS_16 = "complete fps=16"
LIT_GPUDBG = "--gpudbg"
LIT_VUID_123 = "VUID-123"
LIT_NWB_OTHER = "NWB_OTHER"
LIT_NWB_STRESS_REFLECTION_DIAGNOSTICS = "NWB_STRESS_REFLECTION_DIAGNOSTICS"
LIT_FAILURE_JSON = "failure.json"
LIT_KEEP_ME = "keep me"
LIT_RUNTIME = "runtime"
LIT_FAILED_PROCESS_OUTPUT = "failed process output"
LIT_RAW_TIMEOUT_RUNTIME_LOG = "raw timeout runtime log"
LIT_PROCESS_TAIL_TXT = "process_tail.txt"
LIT_CAPTURE = "capture"
LIT_PROCESS_PRESERVED = "process preserved"
LIT_N_60 = "60"
LIT_MINIMUM_FPS = "--minimum-fps"
LIT_PASSED = "passed"
LIT_CAPTURE_VALIDATED = "capture_validated"
LIT_PERFORMANCE_TARGET = "performance_target"
LIT_NAN = "nan"
LIT_CPU_DIAGNOSTICS = "--cpu-diagnostics"
LIT_REFLECTION_DIAGNOSTICS = "--reflection-diagnostics"
LIT_IDENTITY_BEFORE = "identity_before"
LIT_IDENTITY_AFTER = "identity_after"
LIT_MINIMUM_FPS_2 = "minimum_fps"
LIT_MAIN = "__main__"


def shadow_defaults():
    return dict(reflection_screen_steps=96, caustic_photon_grid_divisor=1, surfel_gi_resolve_resolution=LIT_QUARTER,
        shadow_transparent_sampling=LIT_REFERENCE_THREE, shadow_receiver_resolution=LIT_QUARTER,
        software_shadow_backend=LIT_AUTOMATIC, software_shadow_coverage=LIT_REFERENCE, software_shadow_blocker_search=LIT_REFERENCE_GRID9,
        software_shadow_capture_cadence=LIT_EVERY_FRAME, software_shadow_budget_mib=256,
        software_shadow_directional_resolution=512, software_shadow_point_resolution=256)


def shadow_record(backend=0, directional_resolution=512, point_resolution=256, budget_bytes=268435456, coverage=0, blocker_search=0, capture_cadence=0):
    return (smoke.SOFTWARE_SHADOW_SETTINGS + f"backend={backend} directional_resolution={directional_resolution} "
        f"point_resolution={point_resolution} budget_bytes={budget_bytes} coverage={coverage} blocker_search={blocker_search} capture_cadence={capture_cadence}")


def shadow_quality_record(sampling=0, factor=4, width=1280, height=900):
    return (smoke.SHADOW_QUALITY_SETTINGS + f"{sampling} receiver_factor={factor}\n" + smoke.SHADOW_RECEIVER_GRID
        + f"factor={factor} full={width}x{height} receiver={(width+factor-1)//factor}x{(height+factor-1)//factor}")


def workload_record(characters_per_class=10):
    if characters_per_class == 5:
        return (smoke.WORKLOAD + "characters_per_class=5 total=10 transparent=5 opaque=5 layout=zigzag_v1 "
            "rows=2 columns=5 row_spacing_x=1.44 row_stagger_x=0.36 front_z=-0.55 back_z=0.55 body_scale=1 "
            "camera_x=0 camera_y=1.8 camera_z=-4.8 camera_pitch=0.2 vertical_fov=1.0471976 "
            "near_plane=0.001 far_plane=10000 aspect=0")
    return (smoke.WORKLOAD + "characters_per_class=10 total=20 transparent=10 opaque=10 layout=two_rows_v1 "
        "rows=2 columns=10 row_spacing_x=0.72 row_stagger_x=0.18 front_z=-0.55 back_z=0.55 body_scale=1 "
        "camera_x=0 camera_y=2.7 camera_z=-7.2 camera_pitch=0.25 vertical_fov=1.0471976 "
        "near_plane=0.001 far_plane=10000 aspect=0")


def valid_log(characters_per_class=10):
    lines = [marker for marker in smoke.REQUIRED if marker != smoke.SHUTDOWN]
    lines.append(smoke.SPAWN + f"{characters_per_class * 2} spinning characters ({characters_per_class} transparent + "
        f"{characters_per_class} opaque) over ground, directional + point light")
    lines.append(workload_record(characters_per_class))
    lines.append("RendererSystem: deferred rendering targets ready (1280x900, format test)")
    # Synthetic fixed simulation delta is 1/60, while genuine wall/count evidence yields 16 FPS.
    lines.append("Fixture: fixed simulation delta 0.016666667")
    lines.append("CausticQualitySmoke: requested photon_grid_divisor=1")
    lines.append(LIT_SURFELGIQUALITYSMOKE_REQUESTED_RESOLVE)
    lines.append(LIT_RENDERERSYSTEM_DISPATCHED_SURFEL_GI_RE)
    lines.append("RendererSystem: dispatched hardware caustic producer (131072 photons/frame, 2 temporal phases, "
        "262144 full-grid budget, 2 caustic lights, 10 refractive instances)")
    lines.append(shadow_quality_record())
    lines.append(smoke.REFLECTION_QUALITY_SETTINGS + LIT_N_96)
    for index in range(60):
        lines.append(smoke.INTERVAL + f"avg=16 presentations=8 seconds=0.5 first={80+8*index} last={88+8*index}")
    lines.append(smoke.DONE + LIT_FPS_16_PRESENTATIONS_480_SECONDS_30_FI)
    lines.append(smoke.SHUTDOWN)
    return "\n\n".join(lines) + "\n"


def reflection_record(**changes):
    row = dict(sequence=1, generation=1, frame=10, graphics_frame=10, hardware_ready=1, transport_enabled=1,
        candidates=10, hardware_rays=10, exterior_eligible_rays=0, hardware_queries=0, bootstrap_events=0,
        transparent_paths=0, unsupported_paths=10,
        screen_attempts=0, screen_hits=0, screen_returns=0, screen_iterations=0, screen_limit_misses=0)
    row.update(changes)
    return ("StressReflectionStatistics: sequence={sequence} generation={generation} frame={frame} "
        "graphics_frame={graphics_frame} hardware_ready={hardware_ready} transport_enabled={transport_enabled} "
        "candidates={candidates} hardware_rays={hardware_rays} exterior_eligible_rays={exterior_eligible_rays} "
        "hardware_queries={hardware_queries} bootstrap_events={bootstrap_events} "
        "transparent_paths={transparent_paths} unsupported_paths={unsupported_paths} "
        "screen_attempts={screen_attempts} screen_hits={screen_hits} screen_returns={screen_returns} "
        "screen_iterations={screen_iterations} screen_limit_misses={screen_limit_misses}").format(**row)


def reflection_log(*records):
    return valid_log().replace(smoke.START, smoke.REFLECTION_ENABLED + "\n" + smoke.START).replace(
        smoke.SHUTDOWN, "\n".join(records) + "\n" + smoke.SHUTDOWN)


class StressSoftwareShadowSettingsTests(unittest.TestCase):
    def setUp(self):
        self.temporary = TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.output = Path(self.temporary.name)
        executable = self.output / LIT_RENDERER_EXE
        executable.write_bytes(LIT_FIXTURE.encode(LIT_UTF_8))
        self.argv = [LIT_EXECUTABLE, str(executable), LIT_WORKING_DIRECTORY, str(self.output), LIT_NO_LOGSERVER]

    def test_shadow_quarter_requires_matching_allocated_grid(self):
        args = smoke.parse_args(self.argv + [LIT_SHADOW_RECEIVER_RESOLUTION, LIT_QUARTER])
        env = smoke.launch_environment({LIT_NWB_SHADOW_RECEIVER_RESOLUTION: LIT_HALF}, args, self.output)
        self.assertEqual(env[LIT_NWB_SHADOW_RECEIVER_RESOLUTION], LIT_QUARTER)
        record = shadow_quality_record(factor=4, width=1001, height=701)
        report = smoke.verify_shadow_quality_settings(record, args, (1001, 701))
        self.assertEqual(report[LIT_ALLOCATIONS][0]["reduced_width"], 251)
        self.assertEqual(report[LIT_ALLOCATIONS][0]["reduced_height"], 176)
        for invalid in (record.splitlines()[0], record.replace("receiver=251x176", "receiver=250x175"),
                record.replace("grid factor=4", "grid factor=2"), record.replace("full=1001x701", "full=1000x701")):
            with self.subTest(record=invalid), self.assertRaises(smoke.SmokeFailure):
                smoke.verify_shadow_quality_settings(invalid, args, (1001, 701))
        with patch(LIT_SYS_STDERR), self.assertRaises(SystemExit):
            smoke.parse_args(self.argv + [LIT_SHADOW_RECEIVER_RESOLUTION, "3"])

    def test_surfel_quarter_override_requires_matching_recorded_dispatch(self):
        args = smoke.parse_args(self.argv + [LIT_SURFEL_GI_RESOLVE_RESOLUTION, LIT_QUARTER])
        env = smoke.launch_environment({LIT_NWB_SURFEL_GI_RESOLVE_RESOLUTION: LIT_HALF}, args, self.output)
        self.assertEqual(env[LIT_NWB_SURFEL_GI_RESOLVE_RESOLUTION], LIT_QUARTER)
        setting = LIT_SURFELGIQUALITYSMOKE_REQUESTED_RESOLVE
        dispatch = LIT_RENDERERSYSTEM_DISPATCHED_SURFEL_GI_RE
        observed = smoke.surfel_gi_quality_smoke.verify_settings(setting + "\n" + dispatch, LIT_QUARTER, (1280, 900))
        self.assertTrue(observed[LIT_VERIFIED])
        for text in (setting, setting + "\n" + dispatch.replace("factor=4", "factor=2"),
                setting + "\n" + dispatch.replace("320x225", "640x450"),
                setting + "\n" + dispatch.replace("1280x900", "1001x701")):
            with self.subTest(text=text), self.assertRaises(smoke.SmokeFailure):
                smoke.surfel_gi_quality_smoke.verify_settings(text, LIT_QUARTER, (1280, 900))

    def test_surfel_resize_checks_each_target_generation_and_odd_extent(self):
        records = ("SurfelGiQualitySmoke: requested resolve_factor=4\n"
            "RendererSystem: dispatched surfel GI resolve (factor=4, source=1280x900, resolve=320x225)\n"
            "RendererSystem: dispatched surfel GI resolve (factor=4, source=1001x701, resolve=251x176)")
        observed = smoke.surfel_gi_quality_smoke.verify_settings(records, LIT_QUARTER)
        self.assertEqual(len(observed["dispatches"]), 2)
        with self.assertRaises(smoke.SmokeFailure):
            smoke.surfel_gi_quality_smoke.verify_settings(records.replace("251x176", "250x175"), LIT_QUARTER)

    def test_capture_cadence_requires_observed_accepted_reuse(self):
        args = smoke.parse_args(self.argv + [LIT_SOFTWARE_SHADOW_CAPTURE_CADENCE, LIT_REUSE_ONE_FRAME])
        env = smoke.launch_environment({LIT_NWB_SOFTWARE_SHADOW_CAPTURE_CADENCE: LIT_EVERY_FRAME}, args, self.output)
        self.assertEqual(env[LIT_NWB_SOFTWARE_SHADOW_CAPTURE_CADENCE], LIT_REUSE_ONE_FRAME)
        record = shadow_record(capture_cadence=1)
        marker = SOFTWARE_SHADOW_ONE_FRAME_REUSE
        observed = smoke.verify_software_shadow_settings(record + "\n" + marker, args)
        self.assertTrue(observed[LIT_ACCEPTED_REUSE_VERIFIED])
        for text in (record, record + "\n" + marker + "\n" + marker, shadow_record() + "\n" + marker):
            with self.subTest(text=text), self.assertRaises(smoke.SmokeFailure):
                smoke.verify_software_shadow_settings(text, args)

    def test_two_frame_cadence_requires_its_exact_runtime_reuse_evidence(self):
        args = smoke.parse_args(self.argv + [LIT_SOFTWARE_SHADOW_CAPTURE_CADENCE, LIT_REUSE_TWO_FRAMES])
        env = smoke.launch_environment({LIT_NWB_SOFTWARE_SHADOW_CAPTURE_CADENCE: LIT_REUSE_ONE_FRAME}, args, self.output)
        self.assertEqual(env[LIT_NWB_SOFTWARE_SHADOW_CAPTURE_CADENCE], LIT_REUSE_TWO_FRAMES)
        record = shadow_record(capture_cadence=2)
        marker = smoke.SOFTWARE_SHADOW_CAPTURE_REUSE_PREFIX + "(cadence=3)"
        self.assertTrue(smoke.verify_software_shadow_settings(record + "\n" + marker, args)[LIT_ACCEPTED_REUSE_VERIFIED])
        for text in (record, record + "\n" + SOFTWARE_SHADOW_ONE_FRAME_REUSE,
            record + "\n" + marker + "\n" + marker, record + "\n" + marker + "\n" + SOFTWARE_SHADOW_ONE_FRAME_REUSE,
            record + "\n" + marker + " malformed", shadow_record(capture_cadence=1) + "\n" + marker):
            with self.subTest(text=text), self.assertRaises(smoke.SmokeFailure):
                smoke.verify_software_shadow_settings(text, args)
        for cadence in (LIT_EVERY_FRAME, LIT_REUSE_ONE_FRAME):
            args.software_shadow_capture_cadence = cadence
            with self.subTest(cadence=cadence), self.assertRaises(smoke.SmokeFailure):
                smoke.verify_software_shadow_settings(shadow_record(capture_cadence=smoke.SOFTWARE_SHADOW_CAPTURE_CADENCE[cadence])
                    + "\n" + marker, args)

    def test_reference_capture_cadence_rejects_unrequested_reuse(self):
        args = smoke.parse_args(self.argv)
        observed = smoke.verify_software_shadow_settings(shadow_record(), args)
        self.assertFalse(observed[LIT_ACCEPTED_REUSE_VERIFIED])
        with self.assertRaises(smoke.SmokeFailure):
            smoke.verify_software_shadow_settings(shadow_record() + "\n" + SOFTWARE_SHADOW_ONE_FRAME_REUSE, args)
        for name in ("2", "REUSE_ONE_FRAME", LIT_INVALID):
            with self.assertRaises(SystemExit):
                smoke.parse_args(self.argv + [LIT_SOFTWARE_SHADOW_CAPTURE_CADENCE, name])


    def test_reference_launch_explicitly_overrides_direct_stress_performance_defaults(self):
        args = smoke.parse_args(self.argv + [LIT_SHADOW_RECEIVER_RESOLUTION, LIT_HALF,
            LIT_SURFEL_GI_RESOLVE_RESOLUTION, LIT_HALF])
        inherited = {
            LIT_NWB_SHADOW_RECEIVER_RESOLUTION: LIT_QUARTER,
            LIT_NWB_SURFEL_GI_RESOLVE_RESOLUTION: LIT_QUARTER,
            LIT_NWB_CAUSTIC_PHOTON_GRID_DIVISOR: "4",
            LIT_NWB_SHADOW_TRANSPARENT_SAMPLING: "temporal_one",
            LIT_NWB_SOFTWARE_SHADOW_COVERAGE: LIT_FITTED_VOLUME,
            LIT_NWB_SOFTWARE_SHADOW_BLOCKER_SEARCH: LIT_COMPACT_CROSS5,
            LIT_NWB_SOFTWARE_SHADOW_CAPTURE_CADENCE: LIT_REUSE_ONE_FRAME,
        }
        env = smoke.launch_environment(inherited, args, self.output)
        expected = {
            LIT_NWB_SHADOW_RECEIVER_RESOLUTION: LIT_HALF,
            LIT_NWB_SURFEL_GI_RESOLVE_RESOLUTION: LIT_HALF,
            LIT_NWB_CAUSTIC_PHOTON_GRID_DIVISOR: "1",
            LIT_NWB_SHADOW_TRANSPARENT_SAMPLING: LIT_REFERENCE_THREE,
            LIT_NWB_SOFTWARE_SHADOW_COVERAGE: LIT_REFERENCE,
            LIT_NWB_SOFTWARE_SHADOW_BLOCKER_SEARCH: LIT_REFERENCE_GRID9,
            LIT_NWB_SOFTWARE_SHADOW_CAPTURE_CADENCE: LIT_EVERY_FRAME,
        }
        for name, value in expected.items():
            with self.subTest(setting=name):
                self.assertEqual(env[name], value)
        with self.assertRaises(smoke.SmokeFailure):
            smoke.verify_shadow_quality_settings(shadow_quality_record(), args)
        with self.assertRaises(smoke.SmokeFailure):
            smoke.surfel_gi_quality_smoke.verify_settings(valid_log(), args.surfel_gi_resolve_resolution, (1280, 900))

    def test_cli_rejects_invalid_ranges_types_and_backend(self):
        cases = ((LIT_SOFTWARE_SHADOW_BUDGET_MIB, ("0", "4096", LIT_N_1, "1.5", LIT_INVALID)),
            (LIT_SOFTWARE_SHADOW_DIRECTIONAL_RESOLUTION, (LIT_N_31, LIT_N_2049, LIT_N_1, LIT_N_32_5, LIT_INVALID)),
            (LIT_SOFTWARE_SHADOW_POINT_RESOLUTION, (LIT_N_31, LIT_N_2049, LIT_N_1, LIT_N_32_5, LIT_INVALID)),
            (LIT_SOFTWARE_SHADOW_BACKEND, (LIT_HARDWARE, "LIGHT_SPACE", LIT_INVALID)),
            (LIT_SOFTWARE_SHADOW_COVERAGE, ("exact", "FITTED_VOLUME", LIT_INVALID)),
            (LIT_SOFTWARE_SHADOW_BLOCKER_SEARCH, ("5", "COMPACT_CROSS5", LIT_INVALID)))
        for option, values in cases:
            for value in values:
                with self.subTest(option=option, value=value), patch(LIT_SYS_STDERR), self.assertRaises(SystemExit):
                    smoke.parse_args(self.argv + [option, value])
        for budget in (1, 4095):
            for resolution in (32, 2048):
                args = smoke.parse_args(self.argv + [LIT_SOFTWARE_SHADOW_BUDGET_MIB, str(budget),
                    LIT_SOFTWARE_SHADOW_DIRECTIONAL_RESOLUTION, str(resolution),
                    LIT_SOFTWARE_SHADOW_POINT_RESOLUTION, str(resolution)])
                self.assertEqual(args.software_shadow_budget_mib, budget)
                self.assertEqual(args.software_shadow_point_resolution, resolution)

    def test_explicit_environment_overrides_inherited_settings_and_strips_other_controls(self):
        args = smoke.parse_args(self.argv + [LIT_SOFTWARE_SHADOW_BACKEND, LIT_LIGHT_SPACE,
            LIT_SOFTWARE_SHADOW_BUDGET_MIB, LIT_N_256, LIT_SOFTWARE_SHADOW_DIRECTIONAL_RESOLUTION, LIT_N_1024,
            LIT_SOFTWARE_SHADOW_POINT_RESOLUTION, LIT_N_512])
        inherited = {LIT_NWB_SOFTWARE_SHADOW_BACKEND: LIT_TRACE, LIT_NWB_SOFTWARE_SHADOW_BUDGET_MIB: "1",
            LIT_NWB_SOFTWARE_SHADOW_DIRECTIONAL_RESOLU: "32", LIT_NWB_SOFTWARE_SHADOW_POINT_RESOLUTION: "64",
            LIT_NWB_UNREQUESTED_SETTING: LIT_BAD, LIT_PATH: LIT_KEPT}
        env = smoke.launch_environment(inherited, args, self.output)
        self.assertEqual(env[LIT_NWB_SOFTWARE_SHADOW_BACKEND], LIT_LIGHT_SPACE)
        self.assertEqual(env[LIT_NWB_SOFTWARE_SHADOW_BUDGET_MIB], LIT_N_256)
        self.assertEqual(env[LIT_NWB_SOFTWARE_SHADOW_DIRECTIONAL_RESOLU], LIT_N_1024)
        self.assertEqual(env[LIT_NWB_SOFTWARE_SHADOW_POINT_RESOLUTION], LIT_N_512)
        self.assertNotIn(LIT_NWB_UNREQUESTED_SETTING, env)
        self.assertEqual(env[LIT_PATH], LIT_KEPT)
        defaults = smoke.launch_environment(inherited, smoke.parse_args(self.argv), self.output)
        self.assertEqual(defaults[LIT_NWB_SOFTWARE_SHADOW_BACKEND], LIT_AUTOMATIC)
        self.assertEqual(defaults[LIT_NWB_SOFTWARE_SHADOW_BUDGET_MIB], LIT_N_256)
        self.assertEqual(defaults[LIT_NWB_SOFTWARE_SHADOW_DIRECTIONAL_RESOLU], LIT_N_512)
        self.assertEqual(defaults[LIT_NWB_SOFTWARE_SHADOW_POINT_RESOLUTION], LIT_N_256)

    def test_report_requires_one_complete_exact_application_record(self):
        args = smoke.parse_args(self.argv)
        report = smoke.verify_software_shadow_settings(LIT_EMPTY + shadow_record() + LIT_EMPTY, args)
        self.assertTrue(report[LIT_VERIFIED])
        for text in ("", shadow_record() + "\n" + shadow_record(), shadow_record().replace("budget_bytes=", "budget="),
            shadow_record() + " unknown=1", shadow_record(budget_bytes=128 * 1024 * 1024),
            shadow_record(backend=1), shadow_record(directional_resolution=1024), shadow_record(point_resolution=512),
            shadow_record(coverage=1), shadow_record(blocker_search=1)):
            with self.subTest(text=text), self.assertRaises(smoke.SmokeFailure):
                smoke.verify_software_shadow_settings(text, args)

    def test_fitted_coverage_is_explicit_forwarded_and_verified(self):
        args = smoke.parse_args(self.argv + [LIT_SOFTWARE_SHADOW_COVERAGE, LIT_FITTED_VOLUME])
        env = smoke.launch_environment({LIT_NWB_SOFTWARE_SHADOW_COVERAGE: LIT_REFERENCE}, args, self.output)
        self.assertEqual(env[LIT_NWB_SOFTWARE_SHADOW_COVERAGE], LIT_FITTED_VOLUME)
        report = smoke.verify_software_shadow_settings(shadow_record(coverage=1), args)
        self.assertTrue(report[LIT_VERIFIED])
        with self.assertRaisesRegex(smoke.SmokeFailure, LIT_SOFTWARE_SHADOW_SETTINGS_MISMATCH):
            smoke.verify_software_shadow_settings(shadow_record(), args)
        defaults = smoke.launch_environment(env, smoke.parse_args(self.argv), self.output)
        self.assertEqual(defaults[LIT_NWB_SOFTWARE_SHADOW_COVERAGE], LIT_REFERENCE)

    def test_blocker_search_is_explicit_forwarded_and_verified(self):
        for name, code in ((LIT_COMPACT_CROSS5, 1), (LIT_CENTER1, 2)):
            with self.subTest(name=name):
                args = smoke.parse_args(self.argv + [LIT_SOFTWARE_SHADOW_BLOCKER_SEARCH, name])
                env = smoke.launch_environment({LIT_NWB_SOFTWARE_SHADOW_BLOCKER_SEARCH: LIT_REFERENCE_GRID9}, args, self.output)
                self.assertEqual(env[LIT_NWB_SOFTWARE_SHADOW_BLOCKER_SEARCH], name)
                report = smoke.verify_software_shadow_settings(shadow_record(blocker_search=code), args)
                self.assertTrue(report[LIT_VERIFIED])
                for record in (shadow_record(), shadow_record(blocker_search=3 - code), shadow_record(blocker_search=3),
                    shadow_record().replace(" blocker_search=0", "")):
                    with self.subTest(record=record), self.assertRaises(smoke.SmokeFailure):
                        smoke.verify_software_shadow_settings(record, args)
                defaults = smoke.launch_environment(env, smoke.parse_args(self.argv), self.output)
                self.assertEqual(defaults[LIT_NWB_SOFTWARE_SHADOW_BLOCKER_SEARCH], LIT_REFERENCE_GRID9)

    def test_blocker_acquisition_records_policy_and_rejects_silent_reference_fallback(self):
        for name, code in ((LIT_COMPACT_CROSS5, 1), (LIT_CENTER1, 2)):
            with self.subTest(name=name):
                args = smoke.parse_args(self.argv + [LIT_SOFTWARE_SHADOW_BLOCKER_SEARCH, name])
                text = shadow_record(blocker_search=code) + "\n" + valid_log()
                result = self.acquire_log(args, text)
                self.assertEqual(result[LIT_SOFTWARE_SHADOW_SETTINGS][LIT_OBSERVED][LIT_BLOCKER_SEARCH], code)
                launch = json.loads((self.output / LIT_LAUNCH_JSON).read_text(encoding=LIT_UTF_8))
                self.assertEqual(launch[LIT_ENVIRONMENT][LIT_NWB_SOFTWARE_SHADOW_BLOCKER_SEARCH], name)
                wrong = shadow_record() + "\n" + valid_log()
                with self.assertRaisesRegex(smoke.SmokeFailure, LIT_SOFTWARE_SHADOW_SETTINGS_MISMATCH):
                    self.acquire_log(args, wrong)
                self.assertEqual((self.output / LIT_RUNTIME_LOG).read_text(encoding=LIT_UTF_8), wrong)

    def test_shadow_sampling_is_explicit_forwarded_and_verified(self):
        for name, code in smoke.SHADOW_TRANSPARENT_SAMPLING.items():
            with self.subTest(name=name):
                args = smoke.parse_args(self.argv + [LIT_SHADOW_TRANSPARENT_SAMPLING, name])
                env = smoke.launch_environment({LIT_NWB_SHADOW_TRANSPARENT_SAMPLING: LIT_INVALID}, args, self.output)
                self.assertEqual(env[LIT_NWB_SHADOW_TRANSPARENT_SAMPLING], name)
                request = shadow_quality_record(code)
                dispatch = smoke.SHADOW_TEMPORAL_ONE_RECORDED + "samples=1 hardware=1"
                record = request + ("\n" + dispatch if code == 1 else "")
                report = smoke.verify_shadow_quality_settings(record, args)
                self.assertTrue(report[LIT_VERIFIED])
                self.assertEqual(report["effective_dispatch_verified"], code == 1)
                if code == 1:
                    software = smoke.verify_shadow_quality_settings(request + "\n" + dispatch.replace("hardware=1", "hardware=0"), args)
                    self.assertFalse(software[LIT_HARDWARE])
                    for invalid in (request, record + "\n" + dispatch, record.replace("samples=1", "samples=3")):
                        with self.assertRaises(smoke.SmokeFailure):
                            smoke.verify_shadow_quality_settings(invalid, args)
                else:
                    with self.assertRaises(smoke.SmokeFailure):
                        smoke.verify_shadow_quality_settings(record + "\n" + dispatch, args)
                for invalid in ("", record + "\n" + record, record + " trailing", smoke.SHADOW_QUALITY_SETTINGS + str(1-code)):
                    with self.assertRaises(smoke.SmokeFailure):
                        smoke.verify_shadow_quality_settings(invalid, args)
        with patch(LIT_SYS_STDERR), self.assertRaises(SystemExit):
            smoke.parse_args(self.argv + [LIT_SHADOW_TRANSPARENT_SAMPLING, "0"])

    def acquire_log(self, args, text):
        with patch.object(smoke, LIT_IDENTITIES, return_value={}), \
            patch.object(smoke, LIT_BUILD_LAUNCH_ENVIRONMENT, return_value={}), \
            patch.object(smoke, LIT_LAUNCH_LOGSERVER, return_value=(None, None, self.output, {}, LIT_LOG)), \
            patch.object(smoke, LIT_LAUNCH_TESTBED, return_value=Mock()), \
            patch.object(smoke, LIT_TERMINATE_PROCESS, return_value=(0, "")), \
            patch.object(smoke, LIT_SHUTDOWN_LOGSERVER_AND_COLLECT, return_value=text), \
            patch.object(smoke.ab, LIT_DEVICE_MATERIAL_SIGNATURE, return_value={}):
            return smoke.acquire(args, self.output)


    def test_acquisition_rejects_smaller_budget_when_256_mib_was_requested_and_preserves_log(self):
        args = smoke.parse_args(self.argv + [LIT_SOFTWARE_SHADOW_BUDGET_MIB, LIT_N_256])
        text = shadow_record(budget_bytes=128 * 1024 * 1024) + "\n" + valid_log()
        with self.assertRaisesRegex(smoke.SmokeFailure, LIT_SOFTWARE_SHADOW_SETTINGS_MISMATCH):
            self.acquire_log(args, text)
        self.assertEqual((self.output / LIT_RUNTIME_LOG).read_text(encoding=LIT_UTF_8), text)


class StressReflectionDiagnosticTests(unittest.TestCase):
    def test_unrequested_reflection_evidence_is_rejected(self):
        with self.assertRaisesRegex(smoke.SmokeFailure, "unrequested reflection"):
            smoke.parse_runtime_log(reflection_log(reflection_record()), 0)

    def test_all_rejected_reflections_are_exposed_without_changing_presentation_result(self):
        text = reflection_log(reflection_record(sequence=3, frame=11, graphics_frame=14),
            reflection_record(sequence=7, frame=15, graphics_frame=18))
        result = smoke.parse_runtime_log(text, 0, reflection_diagnostics=True)
        self.assertEqual(result[LIT_MEASUREMENT][LIT_FPS], 16.)
        optical = result[LIT_OPTICAL_REFLECTION]
        self.assertEqual(optical[LIT_STATUS], "all_rejected")
        self.assertEqual(optical[LIT_SAMPLE_COUNT], 2)
        self.assertEqual(optical[LIT_SUMS]["hardware_rays"], 20)
        self.assertEqual(optical[LIT_SUMS]["unsupported_paths"], 20)
        self.assertEqual(optical[LIT_UNSUPPORTED_RATIO], 1.)
        self.assertEqual(optical[LIT_QUERIES_PER_HARDWARE_RAY], 0.)
        self.assertEqual(optical[LIT_EXTERIOR_ELIGIBLE_RATIO], 0.)

    def test_partial_unsupported_ratios_are_weighted_by_admitted_rays(self):
        text = reflection_log(reflection_record(candidates=1, hardware_rays=1, unsupported_paths=1),
            reflection_record(sequence=2, frame=11, graphics_frame=11, candidates=9, hardware_rays=9,
                hardware_queries=18, bootstrap_events=23, transparent_paths=3, exterior_eligible_rays=6,
                unsupported_paths=0))
        optical = smoke.parse_runtime_log(text, 0, reflection_diagnostics=True)[LIT_OPTICAL_REFLECTION]
        self.assertEqual(optical[LIT_STATUS], LIT_UNSUPPORTED)
        self.assertAlmostEqual(optical[LIT_UNSUPPORTED_RATIO], .1)
        self.assertAlmostEqual(optical[LIT_EXTERIOR_ELIGIBLE_RATIO], .6)
        self.assertAlmostEqual(optical[LIT_QUERIES_PER_HARDWARE_RAY], 1.8)
        self.assertEqual(optical[LIT_SUMS]["bootstrap_events"], 23)

    def test_queries_are_observations_not_a_complete_optical_support_claim(self):
        for unsupported, expected in ((0, "queries_observed"), (10, LIT_UNSUPPORTED)):
            with self.subTest(unsupported=unsupported):
                text = reflection_log(reflection_record(hardware_queries=20, bootstrap_events=4,
                    transparent_paths=2, unsupported_paths=unsupported))
                optical = smoke.parse_runtime_log(text, 0, reflection_diagnostics=True)[LIT_OPTICAL_REFLECTION]
                self.assertEqual(optical[LIT_STATUS], expected)
                self.assertEqual(optical[LIT_QUERIES_PER_HARDWARE_RAY], 2.)

    def test_zero_ray_samples_do_not_produce_false_support_or_zero_ratios(self):
        text = reflection_log(reflection_record(hardware_ready=0, transport_enabled=0,
            candidates=0, hardware_rays=0, unsupported_paths=0))
        optical = smoke.parse_runtime_log(text, 0, reflection_diagnostics=True)[LIT_OPTICAL_REFLECTION]
        self.assertEqual(optical[LIT_STATUS], "no_queries")
        self.assertIsNone(optical[LIT_UNSUPPORTED_RATIO])
        self.assertIsNone(optical[LIT_EXTERIOR_ELIGIBLE_RATIO])
        self.assertIsNone(optical[LIT_QUERIES_PER_HARDWARE_RAY])
        self.assertEqual(optical[LIT_HARDWARE_READY_SAMPLES], 0)

    def test_requested_diagnostics_require_enablement_and_samples_before_shutdown(self):
        valid = reflection_log(reflection_record())
        variants = (valid_log(), reflection_log(), valid.replace(smoke.REFLECTION_ENABLED, ""),
            valid + smoke.REFLECTION_ENABLED, valid.replace(reflection_record(), "") + reflection_record(),
            reflection_record() + "\n" + valid.replace(reflection_record(), ""))
        for text in variants:
            with self.subTest(text=text[-140:]), self.assertRaises(smoke.SmokeFailure):
                smoke.parse_runtime_log(text, 0, reflection_diagnostics=True)

    def test_duplicate_regressing_and_revisited_generations_are_rejected(self):
        first = reflection_record()
        variants = ((first, first), (reflection_record(sequence=4), reflection_record(sequence=3,
            frame=11, graphics_frame=11)), (first, reflection_record(sequence=2)),
            (first, reflection_record(generation=2), reflection_record(sequence=2, frame=11, graphics_frame=11)))
        for rows in variants:
            with self.subTest(rows=rows), self.assertRaises(smoke.SmokeFailure):
                smoke.parse_runtime_log(reflection_log(*rows), 0, reflection_diagnostics=True)
        optical = smoke.parse_runtime_log(reflection_log(first, reflection_record(generation=2)), 0,
            reflection_diagnostics=True)[LIT_OPTICAL_REFLECTION]
        self.assertEqual([row[LIT_GENERATION] for row in optical[LIT_RANGES_BY_GENERATION]], [1, 2])

    def test_numeric_bounds_flags_and_impossible_counter_relationships_are_rejected(self):
        variants = (dict(sequence=0), dict(generation=0), dict(sequence=2 ** 64), dict(frame=2 ** 32),
            dict(graphics_frame=2 ** 64), dict(hardware_queries=2 ** 32), dict(hardware_ready=2),
            dict(transport_enabled=2), dict(hardware_ready=0), dict(transport_enabled=0), dict(candidates=9),
            dict(exterior_eligible_rays=11), dict(transparent_paths=11, hardware_queries=20),
            dict(unsupported_paths=11), dict(bootstrap_events=1), dict(transparent_paths=1),
            dict(hardware_rays=0, unsupported_paths=0, hardware_queries=1))
        for change in variants:
            with self.subTest(change=change), self.assertRaises(smoke.SmokeFailure):
                smoke.parse_runtime_log(reflection_log(reflection_record(**change)), 0, reflection_diagnostics=True)

    def test_missing_duplicate_extra_and_noninteger_fields_are_rejected(self):
        valid = reflection_record()
        variants = (valid.replace(LIT_UNSUPPORTED_PATHS_10, ""), valid + LIT_UNSUPPORTED_PATHS_10,
            valid + LIT_EXTRA_0, valid.replace(LIT_HARDWARE_QUERIES_0, "hardware_queries=-1"),
            valid.replace(LIT_HARDWARE_QUERIES_0, "hardware_queries=nan"))
        for record in variants:
            with self.subTest(record=record), self.assertRaises(smoke.SmokeFailure):
                smoke.parse_runtime_log(reflection_log(record), 0, reflection_diagnostics=True)


class StressReflectionQualityTests(unittest.TestCase):
    def test_explicit_steps_validate_and_replace_inherited_environment(self):
        with TemporaryDirectory() as temporary:
            output = Path(temporary)
            executable = output / LIT_RENDERER_EXE
            executable.write_bytes(LIT_FIXTURE.encode(LIT_UTF_8))
            required = [LIT_EXECUTABLE, str(executable), LIT_WORKING_DIRECTORY, str(output), LIT_NO_LOGSERVER]
            for steps in (8, 256):
                args = smoke.parse_args(required + [LIT_REFLECTION_SCREEN_STEPS, str(steps)])
                env = smoke.launch_environment({LIT_NWB_REFLECTION_SCREEN_STEPS: "1"}, args, output)
                self.assertEqual(env[LIT_NWB_REFLECTION_SCREEN_STEPS], str(steps))
                result = smoke.verify_reflection_quality_settings(smoke.REFLECTION_QUALITY_SETTINGS + str(steps), args, {})
                self.assertTrue(result[LIT_VERIFIED])
            for invalid in ("0", "7", "257", LIT_N_1, "48.5", LIT_BAD):
                with patch(LIT_SYS_STDERR), self.assertRaises(SystemExit):
                    smoke.parse_args(required + [LIT_REFLECTION_SCREEN_STEPS, invalid])

    def test_applied_budget_missing_duplicate_malformed_and_mismatch_fail(self):
        args = SimpleNamespace(reflection_screen_steps=48, reflection_diagnostics=False)
        good = smoke.REFLECTION_QUALITY_SETTINGS + LIT_N_48
        for bad in ("", good + "\n" + good, good + LIT_EXTRA_0, smoke.REFLECTION_QUALITY_SETTINGS + LIT_N_96):
            with self.subTest(bad=bad), self.assertRaises(smoke.SmokeFailure):
                smoke.verify_reflection_quality_settings(bad, args, {})

    def test_actual_software_screen_work_is_independent_of_hardware_counters(self):
        row = reflection_record(hardware_ready=0, transport_enabled=0, candidates=0, hardware_rays=0, unsupported_paths=0,
            screen_attempts=20, screen_hits=2, screen_returns=4, screen_iterations=640, screen_limit_misses=10)
        text = reflection_log(row)
        optical = smoke.parse_runtime_log(text, 0, reflection_diagnostics=True)[LIT_OPTICAL_REFLECTION]
        self.assertEqual(optical[LIT_HARDWARE_READY_SAMPLES], 0)
        self.assertEqual(optical[LIT_SCREEN]["iterations_per_attempt"], 32)
        self.assertEqual(optical[LIT_SCREEN]["limit_miss_ratio"], .5)
        self.assertEqual(optical[LIT_SCREEN]["return_ratio"], .2)
        args = SimpleNamespace(reflection_screen_steps=48, reflection_diagnostics=True)
        self.assertTrue(smoke.verify_reflection_quality_settings(smoke.REFLECTION_QUALITY_SETTINGS + LIT_N_48, args, optical)[LIT_SCREEN_WORK_MEASURED])
        args.reflection_screen_steps = 8
        with self.assertRaisesRegex(smoke.SmokeFailure, "exceeds"):
            smoke.verify_reflection_quality_settings(smoke.REFLECTION_QUALITY_SETTINGS + "8", args, optical)

    def test_missing_screen_counters_reject_and_complete_current_records_recover(self):
        current = reflection_record(screen_attempts=1, screen_returns=1, screen_iterations=96)
        fields = current.split()
        for field in smoke.REFLECTION_SCREEN_FIELDS:
            missing = " ".join(value for value in fields if not value.startswith(field + "="))
            with self.subTest(field=field), self.assertRaisesRegex(smoke.SmokeFailure, "malformed"):
                smoke.parse_runtime_log(reflection_log(missing), 0, reflection_diagnostics=True)
        without_screen = " ".join(value for value in fields
            if not any(value.startswith(field + "=") for field in smoke.REFLECTION_SCREEN_FIELDS))
        with self.assertRaisesRegex(smoke.SmokeFailure, "malformed"):
            smoke.parse_runtime_log(reflection_log(without_screen), 0, reflection_diagnostics=True)
        optical = smoke.parse_runtime_log(reflection_log(current), 0, reflection_diagnostics=True)[LIT_OPTICAL_REFLECTION]
        self.assertEqual(optical[LIT_SCREEN][LIT_SAMPLE_COUNT], 1)
        args = SimpleNamespace(reflection_screen_steps=96, reflection_diagnostics=True)
        self.assertTrue(smoke.verify_reflection_quality_settings(smoke.REFLECTION_QUALITY_SETTINGS + LIT_N_96,
            args, optical)[LIT_SCREEN_WORK_MEASURED])

    def test_malformed_partial_overflow_or_impossible_screen_counters_fail(self):
        prefix = reflection_record().split(" " + smoke.REFLECTION_SCREEN_FIELDS[0] + "=")[0]
        tails = (" screen_attempts=1", " screen_attempts=1 screen_hits=0 screen_returns=0 screen_iterations=0",
            " screen_attempts=1 screen_hits=2 screen_returns=1 screen_iterations=48 screen_limit_misses=0",
            " screen_attempts=1 screen_hits=0 screen_returns=2 screen_iterations=48 screen_limit_misses=0",
            " screen_attempts=1 screen_hits=0 screen_returns=0 screen_iterations=48 screen_limit_misses=2",
            " screen_attempts=0 screen_hits=0 screen_returns=0 screen_iterations=1 screen_limit_misses=0",
            " screen_attempts=1 screen_hits=0 screen_returns=0 screen_iterations=257 screen_limit_misses=0",
            " screen_attempts=4294967296 screen_hits=0 screen_returns=0 screen_iterations=0 screen_limit_misses=0",
            " screen_attempts=1 screen_hits=0 screen_returns=0 screen_iterations=18446744073709551616 screen_limit_misses=0")
        for tail in tails:
            with self.subTest(tail=tail), self.assertRaises(smoke.SmokeFailure):
                smoke.parse_runtime_log(reflection_log(prefix + tail), 0, reflection_diagnostics=True)


class StressWorkloadTests(unittest.TestCase):


    def test_requested_profile_and_actual_spawn_counts_must_match(self):
        for observed, requested in ((5, 10), (10, 5), (10, 0), (10, 11)):
            with self.subTest(observed=observed, requested=requested), self.assertRaises(smoke.SmokeFailure):
                smoke.parse_runtime_log(valid_log(observed), 0, characters_per_class=requested)
        with self.assertRaisesRegex(smoke.SmokeFailure, "spawned characters"):
            smoke.parse_runtime_log(valid_log().replace("spawned 20", "spawned 18"), 0)

    def test_layout_camera_counts_and_numeric_validity_are_load_bearing(self):
        changes = ((LIT_TOTAL_20, "total=18"), ("transparent=10", "transparent=9"),
            ("opaque=10", "opaque=11"), ("characters_per_class=10", "characters_per_class=10.0"),
            (LIT_TOTAL_20, "total=999"), ("columns=10", "columns=-1"), ("rows=2", "rows=1"),
            ("layout=two_rows_v1", "layout=unknown"), (LIT_ROW_SPACING_X_0_72, "row_spacing_x=1.44"),
            ("row_stagger_x=0.18", "row_stagger_x=0"), ("front_z=-0.55", "front_z=0.55"),
            ("body_scale=1", "body_scale=0.5"), ("camera_z=-7.2", "camera_z=-4.8"),
            ("camera_pitch=0.25", "camera_pitch=0.2"), ("camera_y=2.7", "camera_y=nan"),
            ("vertical_fov=1.0471976", "vertical_fov=1.5"), ("near_plane=0.001", "near_plane=inf"),
            ("aspect=0", "aspect=1.777"))
        for old, new in changes:
            with self.subTest(new=new), self.assertRaises(smoke.SmokeFailure):
                smoke.parse_runtime_log(valid_log().replace(old, new), 0)
        parsed = smoke.parse_runtime_log(valid_log().replace(LIT_ROW_SPACING_X_0_72, "row_spacing_x=0.72000003"), 0)
        self.assertEqual(parsed[LIT_WORKLOAD][LIT_OBSERVED][LIT_TOTAL], 20)

    def test_fixture_records_must_be_unique_complete_and_before_measurement(self):
        record = workload_record()
        spawn = next(line for line in valid_log().splitlines() if line.startswith(smoke.SPAWN))
        variants = (valid_log().replace(record, ""), valid_log() + record,
            valid_log().replace(spawn, ""), valid_log() + spawn,
            valid_log().replace(record, record + " unexpected=1"),
            valid_log().replace(record, record.replace(" columns=10", "")),
            valid_log().replace(record, "").replace(smoke.SHUTDOWN, record + "\n" + smoke.SHUTDOWN))
        for text in variants:
            with self.subTest(text=text[-100:]), self.assertRaises(smoke.SmokeFailure):
                smoke.parse_runtime_log(text, 0)

    def test_cli_rejects_unsupported_character_profiles(self):
        with TemporaryDirectory() as temporary:
            root = Path(temporary)
            executable = root / LIT_RENDERER_EXE
            executable.write_bytes(LIT_FIXTURE.encode(LIT_UTF_8))
            argv = [LIT_EXECUTABLE, str(executable), LIT_WORKING_DIRECTORY, str(root), LIT_NO_LOGSERVER]
            for value in ("0", "6", "11", "5.5", LIT_INVALID):
                with self.subTest(value=value), patch(LIT_SYS_STDERR), self.assertRaises(SystemExit):
                    smoke.parse_args(argv + [LIT_CHARACTERS_PER_CLASS, value])


class StressMotionTests(unittest.TestCase):
    def test_rotating_launch_removes_inherited_freezes_and_fixed_simulation_time(self):
        args = SimpleNamespace(spin_angle=.6, fixed_delta_seconds=None, reflection_diagnostics=False,
            characters_per_class=10, animate=True, **shadow_defaults())
        inherited = {LIT_NWB_STRESS_TEST_SPIN_ANGLE: "1.25", LIT_NWB_RENDERER_BASELINE_FIXED_DELTA_SECO: ".25",
            LIT_NWB_RENDERER_BASELINE_CAPTURE_FREEZE_F: "120", LIT_NWB_STRESS_CHARACTERS_PER_CLASS: "5"}
        env = smoke.launch_environment(inherited, args, Path("moving"))
        self.assertNotIn(LIT_NWB_STRESS_TEST_SPIN_ANGLE, env)
        self.assertNotIn(LIT_NWB_RENDERER_BASELINE_FIXED_DELTA_SECO, env)
        self.assertNotIn(LIT_NWB_RENDERER_BASELINE_CAPTURE_FREEZE_F, env)
        self.assertEqual(env[LIT_NWB_STRESS_CHARACTERS_PER_CLASS], LIT_N_10)

    def test_rotating_cli_rejects_conflicting_yaw_and_simulation_controls(self):
        with TemporaryDirectory() as temporary:
            root = Path(temporary)
            executable = root / LIT_RENDERER_EXE
            executable.write_bytes(LIT_FIXTURE.encode(LIT_UTF_8))
            argv = [LIT_EXECUTABLE, str(executable), LIT_WORKING_DIRECTORY, str(root), LIT_NO_LOGSERVER]
            moving = smoke.parse_args(argv + [LIT_ANIMATE])
            self.assertIsNone(moving.fixed_delta_seconds)
            for extra in (["--spin-angle", ".6"], ["--fixed-delta-seconds", ".016666667"]):
                with self.subTest(extra=extra), patch(LIT_SYS_STDERR), self.assertRaises(SystemExit):
                    smoke.parse_args(argv + [LIT_ANIMATE] + extra)


class StressMeasurementTests(unittest.TestCase):
    def test_complete_rate_uses_presentations_and_wall_not_fixed_delta_or_queries(self):
        result = smoke.parse_measurement(valid_log())
        self.assertEqual(result[LIT_FPS], 16.)
        self.assertEqual(result["frame_ms"], 62.5)

    def test_optional_pacing_summary_parses_and_rejects_disorder(self):
        paced = valid_log() + "StressTestSmokeProject: presentation pacing samples=480 p50ms=62.5 p95ms=70.0 maxms=120.0 stalls50ms=3\n"
        self.assertIsNotNone(smoke.parse_runtime_log(paced, 0)[LIT_PACING])
        self.assertIsNone(smoke.parse_runtime_log(valid_log(), 0)[LIT_PACING])
        with self.assertRaises(smoke.SmokeFailure):
            smoke.parse_measurement(paced.replace("p50ms=62.5 p95ms=70.0", "p50ms=80.0 p95ms=70.0"))
        with self.assertRaises(smoke.SmokeFailure):
            smoke.parse_measurement(paced + "StressTestSmokeProject: presentation pacing samples=1 p50ms=1 p95ms=1 maxms=1 stalls50ms=0")

    def test_bad_exit_rejected_even_with_complete_log(self):
        with self.assertRaises(smoke.SmokeFailure):
            smoke.parse_runtime_log(valid_log(), 1)

    def test_rejects_timing_sample_rate_in_place_of_presentation_rate(self):
        with self.assertRaisesRegex(smoke.SmokeFailure, "count divided"):
            smoke.parse_measurement(valid_log().replace(LIT_COMPLETE_FPS_16, "complete fps=10.7"))

    def test_counts_positive_exact_bounded_and_rates_finite(self):
        for old, new in (("presentations=480", "presentations=479"),
            (LIT_FPS_16_PRESENTATIONS_480_SECONDS_30_FI, "fps=0 presentations=0 seconds=30 first=80 last=80"),
            (LIT_COMPLETE_FPS_16, "complete fps=nan"),
            ("seconds=30", "seconds=inf"),
            ("last=560", "last=18446744073709551616")):
            with self.subTest(new=new), self.assertRaises(smoke.SmokeFailure):
                smoke.parse_measurement(valid_log().replace(old, new))

    def test_requires_full_wall_window(self):
        with self.assertRaises(smoke.SmokeFailure):
            smoke.parse_measurement(valid_log().replace("fps=16 presentations=480 seconds=30", "fps=16.551724137931034 presentations=480 seconds=29"))

    def test_duplicate_missing_or_out_of_order_markers_rejected(self):
        for text in (valid_log() + smoke.START, valid_log().replace(smoke.START, ""),
            valid_log() + smoke.DONE + LIT_FPS_16_PRESENTATIONS_480_SECONDS_30_FI,
            valid_log().replace(smoke.SHUTDOWN, ""), smoke.SHUTDOWN + "\n" + valid_log().replace(smoke.SHUTDOWN, "")):
            with self.subTest(text=text[-100:]), self.assertRaises(smoke.SmokeFailure):
                smoke.parse_measurement(text)

    def test_interval_count_chain_and_wall_sum_are_load_bearing(self):
        for old, new in (("first=88 last=96", "first=89 last=97"),
            ("avg=16 presentations=8 seconds=0.5 first=80", "avg=8 presentations=8 seconds=1 first=80")):
            with self.subTest(new=new), self.assertRaises(smoke.SmokeFailure):
                smoke.parse_measurement(valid_log().replace(old, new))

    def test_actual_gpu_debug_markers_required_only_when_requested(self):
        with self.assertRaises(smoke.SmokeFailure):
            smoke.parse_runtime_log(valid_log(), 0, [LIT_GPUDBG])
        text = "\n".join("    " + marker + "   " for marker in smoke.GPU_DEBUG) + "\n" + valid_log()
        self.assertEqual(smoke.parse_runtime_log(text, 0, [LIT_GPUDBG])[LIT_MEASUREMENT][LIT_FPS], 16.)

    def test_errors_and_incomplete_or_suspended_measurements_rejected(self):
        for marker in ("[ERROR] GPU broke", LIT_VUID_123, "presentation measurement incomplete", "render submission suspended"):
            with self.subTest(marker=marker), self.assertRaises(smoke.SmokeFailure):
                smoke.parse_measurement(valid_log() + marker)

    def test_environment_replaces_inherited_capture_controls(self):
        args = SimpleNamespace(spin_angle=.6, fixed_delta_seconds=.016666667, reflection_diagnostics=False, characters_per_class=10, animate=False, **shadow_defaults())
        env = smoke.launch_environment({LIT_NWB_RENDERER_BASELINE_CAPTURE_FREEZE_F: LIT_N_96,
            LIT_NWB_STRESS_TEST_SPIN_ANGLE: "2", LIT_NWB_OTHER: LIT_BAD,
            LIT_NWB_STRESS_REFLECTION_DIAGNOSTICS: "1", LIT_NWB_STRESS_CHARACTERS_PER_CLASS: "5", LIT_PATH: LIT_KEPT}, args, Path("trial"))
        self.assertNotIn(LIT_NWB_RENDERER_BASELINE_CAPTURE_FREEZE_F, env)
        self.assertNotIn(LIT_NWB_OTHER, env)
        self.assertEqual(env[LIT_NWB_STRESS_TEST_SPIN_ANGLE], "0.6")
        self.assertEqual(env[LIT_NWB_STRESS_CHARACTERS_PER_CLASS], LIT_N_10)
        self.assertEqual(env[LIT_PATH], LIT_KEPT)
        self.assertNotIn(LIT_NWB_STRESS_REFLECTION_DIAGNOSTICS, env)

    def test_output_guard_preserves_prior_evidence_and_rejects_input_overlap(self):
        with TemporaryDirectory() as temporary:
            root = Path(temporary)
            old = root / "old"
            old.mkdir()
            evidence = old / LIT_FAILURE_JSON
            evidence.write_text(LIT_KEEP_ME, encoding=LIT_UTF_8)
            with self.assertRaises(smoke.SmokeFailure):
                smoke.reserve_output(old, [])
            self.assertEqual(evidence.read_text(encoding=LIT_UTF_8), LIT_KEEP_ME)
            protected = root / LIT_RUNTIME
            protected.mkdir()
            with self.assertRaises(smoke.SmokeFailure):
                smoke.reserve_output(protected / "new", [protected])

    def test_timeout_preserves_raw_failure_log_and_original_failure(self):
        with TemporaryDirectory() as temporary:
            output = Path(temporary)
            args = SimpleNamespace(executable=output / "app.exe", working_directory=output,
                no_logserver=True, logserver_executable=None, application_arg=[], timeout=90,
                spin_angle=.6, fixed_delta_seconds=.016666667, reflection_diagnostics=False, characters_per_class=10, animate=False, **shadow_defaults())
            process = Mock()
            process.wait.side_effect = subprocess.TimeoutExpired("app", 90)
            with patch.object(smoke, LIT_IDENTITIES, return_value={}), \
                patch.object(smoke, LIT_BUILD_LAUNCH_ENVIRONMENT, return_value={}), \
                patch.object(smoke, LIT_LAUNCH_LOGSERVER, return_value=(None, None, output, {}, LIT_LOG)), \
                patch.object(smoke, LIT_LAUNCH_TESTBED, return_value=process), \
                patch.object(smoke, LIT_TERMINATE_PROCESS, return_value=(1, LIT_FAILED_PROCESS_OUTPUT)), \
                patch.object(smoke, LIT_SHUTDOWN_LOGSERVER_AND_COLLECT, return_value=LIT_RAW_TIMEOUT_RUNTIME_LOG):
                with self.assertRaisesRegex(smoke.SmokeFailure, "self-exit"):
                    smoke.acquire(args, output)
            self.assertEqual((output / LIT_RUNTIME_LOG).read_text(encoding=LIT_UTF_8), LIT_RAW_TIMEOUT_RUNTIME_LOG)
            self.assertEqual((output / LIT_PROCESS_TAIL_TXT).read_text(encoding=LIT_UTF_8), LIT_FAILED_PROCESS_OUTPUT)


class StressPerformanceTargetTests(unittest.TestCase):
    def setUp(self):
        self.temporary = TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.runtime = self.root / LIT_RUNTIME
        self.runtime.mkdir()
        executable = self.runtime / LIT_RENDERER_EXE
        executable.write_bytes(b"synthetic")
        self.argv = [LIT_EXECUTABLE, str(executable), LIT_WORKING_DIRECTORY, str(self.runtime), LIT_NO_LOGSERVER]

    def rate_log(self, fps):
        count = int(fps / 2)
        lines = []
        interval = 0
        for line in valid_log().splitlines():
            if line.startswith(smoke.INTERVAL):
                first = 80 + count * interval
                line = smoke.INTERVAL + f"avg={fps} presentations={count} seconds=0.5 first={first} last={first+count}"
                interval += 1
            elif line.startswith(smoke.DONE):
                line = smoke.DONE + f"fps={fps} presentations={count*60} seconds=30 first=80 last={80+count*60}"
            lines.append(line)
        return shadow_record() + "\n" + "\n".join(lines) + "\n"

    def acquire_or_main(self, extra, text, use_main=False, identities=None):
        output = self.root / LIT_CAPTURE
        argv = self.argv + ["--output-directory", str(output)] + extra
        args = smoke.parse_args(argv)
        if not use_main:
            output.mkdir(exist_ok=True)
        with patch.object(smoke, LIT_IDENTITIES, side_effect=identities, return_value={LIT_VERIFIED: True}), \
            patch.object(smoke, LIT_BUILD_LAUNCH_ENVIRONMENT, return_value={}), \
            patch.object(smoke, LIT_LAUNCH_LOGSERVER, return_value=(None, None, output, {}, LIT_LOG)), \
            patch.object(smoke, LIT_LAUNCH_TESTBED, return_value=Mock()), \
            patch.object(smoke, LIT_TERMINATE_PROCESS, return_value=(0, LIT_PROCESS_PRESERVED)), \
            patch.object(smoke, LIT_SHUTDOWN_LOGSERVER_AND_COLLECT, return_value=text), \
            patch.object(smoke.ab, LIT_DEVICE_MATERIAL_SIGNATURE, return_value={}), patch.object(smoke, "write_status"):
            return smoke.main(argv) if use_main else smoke.acquire(args, output)

    def test_positive_finite_threshold_extremes_are_accepted(self):
        for threshold in ("5e-324", "1e308"):
            self.assertEqual(smoke.parse_args(self.argv + [LIT_MINIMUM_FPS, threshold]).minimum_fps, float(threshold))

    def test_invalid_or_diagnostic_threshold_requests_are_rejected(self):
        for value in ("0", LIT_N_1, LIT_NAN, "inf", "-inf", "1e309", "1e-999", LIT_INVALID):
            with self.subTest(value=value), patch(LIT_SYS_STDERR), self.assertRaises(SystemExit):
                smoke.parse_args(self.argv + ["--minimum-fps=" + value])
        for flags in ([LIT_CPU_DIAGNOSTICS], [LIT_REFLECTION_DIAGNOSTICS], [LIT_CPU_DIAGNOSTICS, LIT_REFLECTION_DIAGNOSTICS]):
            with self.subTest(flags=flags), patch(LIT_SYS_STDERR), self.assertRaises(SystemExit):
                smoke.parse_args(self.argv + [LIT_MINIMUM_FPS, LIT_N_60] + flags)
            self.assertIsNone(smoke.parse_args(self.argv + flags).minimum_fps)

    def test_acquisition_strict_boundary_and_validated_provenance(self):
        for fps, expected in ((58, False), (60, False), (62, True)):
            with self.subTest(fps=fps):
                result = self.acquire_or_main([LIT_MINIMUM_FPS, LIT_N_60], self.rate_log(fps))
                self.assertEqual(result[LIT_PASSED], expected)
                self.assertTrue(result[LIT_CAPTURE_VALIDATED])

    def test_rounded_logged_rate_cannot_turn_equal_count_rate_into_pass(self):
        text = self.rate_log(60).replace("complete fps=60 ", "complete fps=60.00000003 ")
        result = self.acquire_or_main([LIT_MINIMUM_FPS, LIT_N_60], text)
        self.assertGreater(result[LIT_MEASUREMENT][LIT_FPS], 60)
        self.assertEqual(result[LIT_PERFORMANCE_TARGET]["observed_fps"], 60)
        self.assertFalse(result[LIT_PASSED])

    def test_main_retains_fully_validated_result_and_separate_target_failure(self):
        text = self.rate_log(60)
        self.assertEqual(self.acquire_or_main([LIT_MINIMUM_FPS, LIT_N_60], text, use_main=True), 1)
        output = self.root / LIT_CAPTURE
        result = json.loads((output / "result.json").read_text())
        failure = json.loads((output / LIT_FAILURE_JSON).read_text())
        self.assertTrue(result[LIT_CAPTURE_VALIDATED])
        self.assertFalse(result[LIT_PASSED])
        self.assertFalse(result[LIT_PERFORMANCE_TARGET][LIT_PASSED])
        self.assertIn("must exceed 60", failure["error"])
        self.assertEqual((output / LIT_RUNTIME_LOG).read_text(), text)
        self.assertEqual((output / LIT_PROCESS_TAIL_TXT).read_text(), LIT_PROCESS_PRESERVED)
        self.assertEqual(result[LIT_IDENTITY_BEFORE], {LIT_VERIFIED: True})
        self.assertEqual(result[LIT_IDENTITY_BEFORE], result[LIT_IDENTITY_AFTER])


    def test_target_does_not_bypass_validation_or_identity_checks(self):
        for text in (self.rate_log(62) + LIT_VUID_123, self.rate_log(62).replace("blocker_search=0", "blocker_search=1")):
            with self.subTest(text=text[-50:]), self.assertRaises(smoke.SmokeFailure):
                self.acquire_or_main([LIT_MINIMUM_FPS, LIT_N_60], text)
        with self.assertRaisesRegex(smoke.SmokeFailure, "identity changed"):
            self.acquire_or_main([LIT_MINIMUM_FPS, LIT_N_60], self.rate_log(62), identities=[{LIT_GENERATION: 1}, {LIT_GENERATION: 2}])

    def test_direct_acquisition_rejects_diagnostic_or_invalid_threshold_before_launch(self):
        for field, value in (("cpu_diagnostics", True), ("reflection_diagnostics", True), (LIT_MINIMUM_FPS_2, float(LIT_NAN))):
            args = smoke.parse_args(self.argv + [LIT_MINIMUM_FPS, LIT_N_60])
            setattr(args, field, value)
            with patch.object(smoke, LIT_LAUNCH_TESTBED) as launch, self.assertRaises(smoke.SmokeFailure):
                smoke.acquire(args, self.root / "unused")
            launch.assert_not_called()


if __name__ == LIT_MAIN:
    unittest.main()
