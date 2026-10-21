#!/usr/bin/env python3
"""Strict presentation measurement replay and acquisition failure retention; never launch a renderer."""


import json
from pathlib import Path
import sys
from tempfile import TemporaryDirectory
import unittest
from unittest.mock import Mock, patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "smoke"))
import stress_timing_smoke as smoke
from stress_timing_test_support import shadow_quality_record, shadow_record, stress_arguments, valid_log, workload_record


LIT_QUARTER = "quarter"
LIT_REFERENCE_THREE = "reference_three"
LIT_AUTOMATIC = "automatic"
LIT_REFERENCE = "reference"
LIT_REFERENCE_GRID9 = "reference_grid9"
LIT_EVERY_FRAME = "every_frame"
LIT_SURFELGIQUALITYSMOKE_REQUESTED_RESOLVE = "SurfelGiQualitySmoke: requested resolve_factor=4"
LIT_RENDERERSYSTEM_DISPATCHED_SURFEL_GI_RE = "RendererSystem: dispatched surfel GI resolve (factor=4, source=1280x900, resolve=320x225)"
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
LIT_MAIN = "__main__"


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
        for invalid in (record.splitlines()[0], record.replace("receiver=251x176", "receiver=250x175"), record.replace("grid factor=4", "grid factor=2"), record.replace("full=1001x701", "full=1000x701")):
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
        for text in (setting, setting + "\n" + dispatch.replace("factor=4", "factor=2"), setting + "\n" + dispatch.replace("320x225", "640x450"), setting + "\n" + dispatch.replace("1280x900", "1001x701")):
            with self.subTest(text=text), self.assertRaises(smoke.SmokeFailure):
                smoke.surfel_gi_quality_smoke.verify_settings(text, LIT_QUARTER, (1280, 900))

    def test_surfel_resize_checks_each_target_generation_and_odd_extent(self):
        records = ("SurfelGiQualitySmoke: requested resolve_factor=4\n" "RendererSystem: dispatched surfel GI resolve (factor=4, source=1280x900, resolve=320x225)\n" "RendererSystem: dispatched surfel GI resolve (factor=4, source=1001x701, resolve=251x176)")
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
        for text in (record, record + "\n" + SOFTWARE_SHADOW_ONE_FRAME_REUSE, record + "\n" + marker + "\n" + marker, record + "\n" + marker + "\n" + SOFTWARE_SHADOW_ONE_FRAME_REUSE,
            record + "\n" + marker + " malformed", shadow_record(capture_cadence=1) + "\n" + marker):
            with self.subTest(text=text), self.assertRaises(smoke.SmokeFailure):
                smoke.verify_software_shadow_settings(text, args)
        for cadence in (LIT_EVERY_FRAME, LIT_REUSE_ONE_FRAME):
            args.software_shadow_capture_cadence = cadence
            with self.subTest(cadence=cadence), self.assertRaises(smoke.SmokeFailure):
                smoke.verify_software_shadow_settings(shadow_record(capture_cadence=smoke.SOFTWARE_SHADOW_CAPTURE_CADENCE[cadence]) + "\n" + marker, args)

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
        args = smoke.parse_args(self.argv + [LIT_SHADOW_RECEIVER_RESOLUTION, LIT_HALF, LIT_SURFEL_GI_RESOLVE_RESOLUTION, LIT_HALF])
        inherited = { LIT_NWB_SHADOW_RECEIVER_RESOLUTION: LIT_QUARTER, LIT_NWB_SURFEL_GI_RESOLVE_RESOLUTION: LIT_QUARTER, LIT_NWB_CAUSTIC_PHOTON_GRID_DIVISOR: "4", LIT_NWB_SHADOW_TRANSPARENT_SAMPLING: "temporal_one",
            LIT_NWB_SOFTWARE_SHADOW_COVERAGE: LIT_FITTED_VOLUME, LIT_NWB_SOFTWARE_SHADOW_BLOCKER_SEARCH: LIT_COMPACT_CROSS5, LIT_NWB_SOFTWARE_SHADOW_CAPTURE_CADENCE: LIT_REUSE_ONE_FRAME, }
        env = smoke.launch_environment(inherited, args, self.output)
        expected = { LIT_NWB_SHADOW_RECEIVER_RESOLUTION: LIT_HALF, LIT_NWB_SURFEL_GI_RESOLVE_RESOLUTION: LIT_HALF, LIT_NWB_CAUSTIC_PHOTON_GRID_DIVISOR: "1", LIT_NWB_SHADOW_TRANSPARENT_SAMPLING: LIT_REFERENCE_THREE,
            LIT_NWB_SOFTWARE_SHADOW_COVERAGE: LIT_REFERENCE, LIT_NWB_SOFTWARE_SHADOW_BLOCKER_SEARCH: LIT_REFERENCE_GRID9, LIT_NWB_SOFTWARE_SHADOW_CAPTURE_CADENCE: LIT_EVERY_FRAME, }
        for name, value in expected.items():
            with self.subTest(setting=name):
                self.assertEqual(env[name], value)
        with self.assertRaises(smoke.SmokeFailure):
            smoke.verify_shadow_quality_settings(shadow_quality_record(), args)
        with self.assertRaises(smoke.SmokeFailure):
            smoke.surfel_gi_quality_smoke.verify_settings(valid_log(), args.surfel_gi_resolve_resolution, (1280, 900))

    def test_cli_rejects_invalid_ranges_types_and_backend(self):
        cases = ((LIT_SOFTWARE_SHADOW_BUDGET_MIB, ("0", "4096", LIT_N_1, "1.5", LIT_INVALID)), (LIT_SOFTWARE_SHADOW_DIRECTIONAL_RESOLUTION, (LIT_N_31, LIT_N_2049, LIT_N_1, LIT_N_32_5, LIT_INVALID)),
            (LIT_SOFTWARE_SHADOW_POINT_RESOLUTION, (LIT_N_31, LIT_N_2049, LIT_N_1, LIT_N_32_5, LIT_INVALID)), (LIT_SOFTWARE_SHADOW_BACKEND, (LIT_HARDWARE, "LIGHT_SPACE", LIT_INVALID)), (LIT_SOFTWARE_SHADOW_COVERAGE, ("exact", "FITTED_VOLUME", LIT_INVALID)),
            (LIT_SOFTWARE_SHADOW_BLOCKER_SEARCH, ("5", "COMPACT_CROSS5", LIT_INVALID)))
        for option, values in cases:
            for value in values:
                with self.subTest(option=option, value=value), patch(LIT_SYS_STDERR), self.assertRaises(SystemExit):
                    smoke.parse_args(self.argv + [option, value])
        for budget in (1, 4095):
            for resolution in (32, 2048):
                args = smoke.parse_args(self.argv + [LIT_SOFTWARE_SHADOW_BUDGET_MIB, str(budget), LIT_SOFTWARE_SHADOW_DIRECTIONAL_RESOLUTION, str(resolution), LIT_SOFTWARE_SHADOW_POINT_RESOLUTION, str(resolution)])
                self.assertEqual(args.software_shadow_budget_mib, budget)
                self.assertEqual(args.software_shadow_point_resolution, resolution)

    def test_explicit_environment_overrides_inherited_settings_and_strips_other_controls(self):
        args = smoke.parse_args(self.argv + [LIT_SOFTWARE_SHADOW_BACKEND, LIT_LIGHT_SPACE, LIT_SOFTWARE_SHADOW_BUDGET_MIB, LIT_N_256, LIT_SOFTWARE_SHADOW_DIRECTIONAL_RESOLUTION, LIT_N_1024, LIT_SOFTWARE_SHADOW_POINT_RESOLUTION, LIT_N_512])
        inherited = {LIT_NWB_SOFTWARE_SHADOW_BACKEND: LIT_TRACE, LIT_NWB_SOFTWARE_SHADOW_BUDGET_MIB: "1", LIT_NWB_SOFTWARE_SHADOW_DIRECTIONAL_RESOLU: "32", LIT_NWB_SOFTWARE_SHADOW_POINT_RESOLUTION: "64",
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
        for text in ("", shadow_record() + "\n" + shadow_record(), shadow_record().replace("budget_bytes=", "budget="), shadow_record() + " unknown=1", shadow_record(budget_bytes=128 * 1024 * 1024),
            shadow_record(backend=1), shadow_record(directional_resolution=1024), shadow_record(point_resolution=512), shadow_record(coverage=1), shadow_record(blocker_search=1)):
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
                for record in (shadow_record(), shadow_record(blocker_search=3 - code), shadow_record(blocker_search=3), shadow_record().replace(" blocker_search=0", "")):
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


class StressWorkloadTests(unittest.TestCase):


    def test_requested_profile_and_actual_spawn_counts_must_match(self):
        for observed, requested in ((5, 10), (10, 5), (10, 0), (10, 11)):
            with self.subTest(observed=observed, requested=requested), self.assertRaises(smoke.SmokeFailure):
                smoke.parse_runtime_log(valid_log(observed), 0, characters_per_class=requested)
        with self.assertRaisesRegex(smoke.SmokeFailure, "spawned characters"):
            smoke.parse_runtime_log(valid_log().replace("spawned 20", "spawned 18"), 0)

    def test_layout_camera_counts_and_numeric_validity_are_load_bearing(self):
        changes = ((LIT_TOTAL_20, "total=18"), ("transparent=10", "transparent=9"), ("opaque=10", "opaque=11"), ("characters_per_class=10", "characters_per_class=10.0"), (LIT_TOTAL_20, "total=999"), ("columns=10", "columns=-1"), ("rows=2", "rows=1"),
            ("layout=two_rows_v1", "layout=unknown"), (LIT_ROW_SPACING_X_0_72, "row_spacing_x=1.44"), ("row_stagger_x=0.18", "row_stagger_x=0"), ("front_z=-0.55", "front_z=0.55"), ("body_scale=1", "body_scale=0.5"), ("camera_z=-7.2", "camera_z=-4.8"),
            ("camera_pitch=0.25", "camera_pitch=0.2"), ("camera_y=2.7", "camera_y=nan"), ("vertical_fov=1.0471976", "vertical_fov=1.5"), ("near_plane=0.001", "near_plane=inf"), ("aspect=0", "aspect=1.777"))
        for old, new in changes:
            with self.subTest(new=new), self.assertRaises(smoke.SmokeFailure):
                smoke.parse_runtime_log(valid_log().replace(old, new), 0)
        parsed = smoke.parse_runtime_log(valid_log().replace(LIT_ROW_SPACING_X_0_72, "row_spacing_x=0.72000003"), 0)
        self.assertEqual(parsed[LIT_WORKLOAD][LIT_OBSERVED][LIT_TOTAL], 20)

    def test_fixture_records_must_be_unique_complete_and_before_measurement(self):
        record = workload_record()
        spawn = next(line for line in valid_log().splitlines() if line.startswith(smoke.SPAWN))
        variants = (valid_log().replace(record, ""), valid_log() + record, valid_log().replace(spawn, ""), valid_log() + spawn, valid_log().replace(record, record + " unexpected=1"), valid_log().replace(record, record.replace(" columns=10", "")),
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
        runtime = Path(self.enterContext(TemporaryDirectory()))
        args = stress_arguments(runtime, [LIT_ANIMATE])
        inherited = {LIT_NWB_STRESS_TEST_SPIN_ANGLE: "1.25", LIT_NWB_RENDERER_BASELINE_FIXED_DELTA_SECO: ".25", LIT_NWB_RENDERER_BASELINE_CAPTURE_FREEZE_F: "120", LIT_NWB_STRESS_CHARACTERS_PER_CLASS: "5"}
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


if __name__ == LIT_MAIN:
    unittest.main()
