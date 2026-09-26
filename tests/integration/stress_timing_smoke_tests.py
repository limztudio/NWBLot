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


def shadow_defaults():
    return dict(reflection_screen_steps=96, caustic_photon_grid_divisor=1, shadow_transparent_sampling="reference_three",
        software_shadow_backend="automatic", software_shadow_coverage="reference", software_shadow_blocker_search="reference_grid9",
        software_shadow_capture_cadence="every_frame", software_shadow_budget_mib=256,
        software_shadow_directional_resolution=512, software_shadow_point_resolution=256)


def shadow_record(backend=0, directional_resolution=512, point_resolution=256, budget_bytes=268435456, coverage=0, blocker_search=0, capture_cadence=0):
    return (smoke.SOFTWARE_SHADOW_SETTINGS + f"backend={backend} directional_resolution={directional_resolution} "
        f"point_resolution={point_resolution} budget_bytes={budget_bytes} coverage={coverage} blocker_search={blocker_search} capture_cadence={capture_cadence}")


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
    lines.append("RendererSystem: dispatched hardware caustic producer (131072 photons/frame, 2 temporal phases, "
        "262144 full-grid budget, 2 caustic lights, 10 refractive instances)")
    lines.append(smoke.SHADOW_QUALITY_SETTINGS + "0")
    lines.append(smoke.REFLECTION_QUALITY_SETTINGS + "96")
    for index in range(60):
        lines.append(smoke.INTERVAL + f"avg=16 presentations=8 seconds=0.5 first={80+8*index} last={88+8*index}")
    lines.append(smoke.DONE + "fps=16 presentations=480 seconds=30 first=80 last=560")
    lines.append(smoke.SHUTDOWN)
    return "\n\n".join(lines) + "\n"


def reflection_record(**changes):
    row = dict(sequence=1, generation=1, frame=10, graphics_frame=10, hardware_ready=1, transport_enabled=1,
        candidates=10, hardware_rays=10, exterior_eligible_rays=0, hardware_queries=0, bootstrap_events=0,
        transparent_paths=0, unsupported_paths=10)
    row.update(changes)
    return ("StressReflectionStatistics: sequence={sequence} generation={generation} frame={frame} "
        "graphics_frame={graphics_frame} hardware_ready={hardware_ready} transport_enabled={transport_enabled} "
        "candidates={candidates} hardware_rays={hardware_rays} exterior_eligible_rays={exterior_eligible_rays} "
        "hardware_queries={hardware_queries} bootstrap_events={bootstrap_events} "
        "transparent_paths={transparent_paths} unsupported_paths={unsupported_paths}").format(**row)


def reflection_log(*records):
    return valid_log().replace(smoke.START, smoke.REFLECTION_ENABLED + "\n" + smoke.START).replace(
        smoke.SHUTDOWN, "\n".join(records) + "\n" + smoke.SHUTDOWN)


class StressSoftwareShadowSettingsTests(unittest.TestCase):
    def setUp(self):
        self.temporary = TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.output = Path(self.temporary.name)
        executable = self.output / "renderer.exe"
        executable.write_bytes(b"fixture")
        self.argv = ["--executable", str(executable), "--working-directory", str(self.output), "--no-logserver"]

    def test_capture_cadence_requires_observed_accepted_reuse(self):
        args = smoke.parse_args(self.argv + ["--software-shadow-capture-cadence", "reuse_one_frame"])
        env = smoke.launch_environment({"NWB_SOFTWARE_SHADOW_CAPTURE_CADENCE": "every_frame"}, args, self.output)
        self.assertEqual(env["NWB_SOFTWARE_SHADOW_CAPTURE_CADENCE"], "reuse_one_frame")
        record = shadow_record(capture_cadence=1)
        marker = smoke.SOFTWARE_SHADOW_CAPTURE_REUSE
        observed = smoke.verify_software_shadow_settings(record + "\n" + marker, args)
        self.assertTrue(observed["accepted_reuse_verified"])
        for text in (record, record + "\n" + marker + "\n" + marker, shadow_record() + "\n" + marker):
            with self.subTest(text=text), self.assertRaises(smoke.SmokeFailure):
                smoke.verify_software_shadow_settings(text, args)

    def test_reference_capture_cadence_rejects_unrequested_reuse(self):
        args = smoke.parse_args(self.argv)
        observed = smoke.verify_software_shadow_settings(shadow_record(), args)
        self.assertFalse(observed["accepted_reuse_verified"])
        with self.assertRaises(smoke.SmokeFailure):
            smoke.verify_software_shadow_settings(shadow_record() + "\n" + smoke.SOFTWARE_SHADOW_CAPTURE_REUSE, args)
        for name in ("2", "REUSE_ONE_FRAME", "invalid"):
            with self.assertRaises(SystemExit):
                smoke.parse_args(self.argv + ["--software-shadow-capture-cadence", name])

    def test_cli_defaults_and_explicit_modes_match_application_contract(self):
        args = smoke.parse_args(self.argv)
        for key, expected in shadow_defaults().items():
            self.assertEqual(getattr(args, key), expected)
        for backend in ("automatic", "trace", "light_space"):
            with self.subTest(backend=backend):
                args = smoke.parse_args(self.argv + ["--software-shadow-backend", backend,
                    "--software-shadow-budget-mib", "256", "--software-shadow-directional-resolution", "1024",
                    "--software-shadow-point-resolution", "512"])
                self.assertEqual(args.software_shadow_backend, backend)
                self.assertEqual(args.software_shadow_budget_mib, 256)
                self.assertEqual(args.software_shadow_directional_resolution, 1024)
                self.assertEqual(args.software_shadow_point_resolution, 512)

    def test_cli_rejects_invalid_ranges_types_and_backend(self):
        cases = (("--software-shadow-budget-mib", ("0", "4096", "-1", "1.5", "invalid")),
            ("--software-shadow-directional-resolution", ("31", "2049", "-1", "32.5", "invalid")),
            ("--software-shadow-point-resolution", ("31", "2049", "-1", "32.5", "invalid")),
            ("--software-shadow-backend", ("hardware", "LIGHT_SPACE", "invalid")),
            ("--software-shadow-coverage", ("exact", "FITTED_VOLUME", "invalid")),
            ("--software-shadow-blocker-search", ("5", "COMPACT_CROSS5", "invalid")))
        for option, values in cases:
            for value in values:
                with self.subTest(option=option, value=value), patch("sys.stderr"), self.assertRaises(SystemExit):
                    smoke.parse_args(self.argv + [option, value])
        for budget in (1, 4095):
            for resolution in (32, 2048):
                args = smoke.parse_args(self.argv + ["--software-shadow-budget-mib", str(budget),
                    "--software-shadow-directional-resolution", str(resolution),
                    "--software-shadow-point-resolution", str(resolution)])
                self.assertEqual(args.software_shadow_budget_mib, budget)
                self.assertEqual(args.software_shadow_point_resolution, resolution)

    def test_explicit_environment_overrides_inherited_settings_and_strips_other_controls(self):
        args = smoke.parse_args(self.argv + ["--software-shadow-backend", "light_space",
            "--software-shadow-budget-mib", "256", "--software-shadow-directional-resolution", "1024",
            "--software-shadow-point-resolution", "512"])
        inherited = {"NWB_SOFTWARE_SHADOW_BACKEND": "trace", "NWB_SOFTWARE_SHADOW_BUDGET_MIB": "1",
            "NWB_SOFTWARE_SHADOW_DIRECTIONAL_RESOLUTION": "32", "NWB_SOFTWARE_SHADOW_POINT_RESOLUTION": "64",
            "NWB_UNREQUESTED_SETTING": "bad", "PATH": "kept"}
        env = smoke.launch_environment(inherited, args, self.output)
        self.assertEqual(env["NWB_SOFTWARE_SHADOW_BACKEND"], "light_space")
        self.assertEqual(env["NWB_SOFTWARE_SHADOW_BUDGET_MIB"], "256")
        self.assertEqual(env["NWB_SOFTWARE_SHADOW_DIRECTIONAL_RESOLUTION"], "1024")
        self.assertEqual(env["NWB_SOFTWARE_SHADOW_POINT_RESOLUTION"], "512")
        self.assertNotIn("NWB_UNREQUESTED_SETTING", env)
        self.assertEqual(env["PATH"], "kept")
        defaults = smoke.launch_environment(inherited, smoke.parse_args(self.argv), self.output)
        self.assertEqual(defaults["NWB_SOFTWARE_SHADOW_BACKEND"], "automatic")
        self.assertEqual(defaults["NWB_SOFTWARE_SHADOW_BUDGET_MIB"], "256")
        self.assertEqual(defaults["NWB_SOFTWARE_SHADOW_DIRECTIONAL_RESOLUTION"], "512")
        self.assertEqual(defaults["NWB_SOFTWARE_SHADOW_POINT_RESOLUTION"], "256")

    def test_report_requires_one_complete_exact_application_record(self):
        args = smoke.parse_args(self.argv)
        report = smoke.verify_software_shadow_settings("  " + shadow_record() + "  ", args)
        self.assertTrue(report["verified"])
        self.assertEqual(report["requested"], report["observed"])
        self.assertEqual(report["observed"]["budget_bytes"], 256 * 1024 * 1024)
        for text in ("", shadow_record() + "\n" + shadow_record(), shadow_record().replace("budget_bytes=", "budget="),
            shadow_record() + " unknown=1", shadow_record(budget_bytes=128 * 1024 * 1024),
            shadow_record(backend=1), shadow_record(directional_resolution=1024), shadow_record(point_resolution=512),
            shadow_record(coverage=1), shadow_record(blocker_search=1)):
            with self.subTest(text=text), self.assertRaises(smoke.SmokeFailure):
                smoke.verify_software_shadow_settings(text, args)
        # Historical measurement logs remain replayable without the new acquisition evidence.
        self.assertEqual(smoke.parse_measurement(valid_log())["fps"], 16.)

    def test_fitted_coverage_is_explicit_forwarded_and_verified(self):
        args = smoke.parse_args(self.argv + ["--software-shadow-coverage", "fitted_volume"])
        env = smoke.launch_environment({"NWB_SOFTWARE_SHADOW_COVERAGE": "reference"}, args, self.output)
        self.assertEqual(env["NWB_SOFTWARE_SHADOW_COVERAGE"], "fitted_volume")
        report = smoke.verify_software_shadow_settings(shadow_record(coverage=1), args)
        self.assertEqual(report["observed"]["coverage"], 1)
        with self.assertRaisesRegex(smoke.SmokeFailure, "software shadow settings mismatch"):
            smoke.verify_software_shadow_settings(shadow_record(), args)
        defaults = smoke.launch_environment(env, smoke.parse_args(self.argv), self.output)
        self.assertEqual(defaults["NWB_SOFTWARE_SHADOW_COVERAGE"], "reference")

    def test_blocker_search_is_explicit_forwarded_and_verified(self):
        args = smoke.parse_args(self.argv + ["--software-shadow-blocker-search", "compact_cross5"])
        env = smoke.launch_environment({"NWB_SOFTWARE_SHADOW_BLOCKER_SEARCH": "reference_grid9"}, args, self.output)
        self.assertEqual(env["NWB_SOFTWARE_SHADOW_BLOCKER_SEARCH"], "compact_cross5")
        report = smoke.verify_software_shadow_settings(shadow_record(blocker_search=1), args)
        self.assertEqual(report["blocker_search_name"], "compact_cross5")
        self.assertEqual(report["observed"]["blocker_search"], 1)
        for record in (shadow_record(), shadow_record(blocker_search=2), shadow_record().replace(" blocker_search=0", "")):
            with self.subTest(record=record), self.assertRaises(smoke.SmokeFailure):
                smoke.verify_software_shadow_settings(record, args)
        defaults = smoke.launch_environment(env, smoke.parse_args(self.argv), self.output)
        self.assertEqual(defaults["NWB_SOFTWARE_SHADOW_BLOCKER_SEARCH"], "reference_grid9")

    def test_blocker_acquisition_records_policy_and_rejects_silent_reference_fallback(self):
        args = smoke.parse_args(self.argv + ["--software-shadow-blocker-search", "compact_cross5"])
        text = shadow_record(blocker_search=1) + "\n" + valid_log()
        result = self.acquire_log(args, text)
        self.assertEqual(result["software_shadow_settings"]["observed"]["blocker_search"], 1)
        launch = json.loads((self.output / "launch.json").read_text(encoding="utf-8"))
        self.assertEqual(launch["environment"]["NWB_SOFTWARE_SHADOW_BLOCKER_SEARCH"], "compact_cross5")
        wrong = shadow_record() + "\n" + valid_log()
        with self.assertRaisesRegex(smoke.SmokeFailure, "software shadow settings mismatch"):
            self.acquire_log(args, wrong)
        self.assertEqual((self.output / "runtime.log").read_text(encoding="utf-8"), wrong)

    def test_shadow_sampling_is_explicit_forwarded_and_verified(self):
        for name, code in smoke.SHADOW_TRANSPARENT_SAMPLING.items():
            with self.subTest(name=name):
                args = smoke.parse_args(self.argv + ["--shadow-transparent-sampling", name])
                env = smoke.launch_environment({"NWB_SHADOW_TRANSPARENT_SAMPLING": "invalid"}, args, self.output)
                self.assertEqual(env["NWB_SHADOW_TRANSPARENT_SAMPLING"], name)
                request = smoke.SHADOW_QUALITY_SETTINGS + str(code)
                dispatch = smoke.SHADOW_TEMPORAL_ONE_RECORDED + "samples=1 hardware=1"
                record = request + ("\n" + dispatch if code == 1 else "")
                report = smoke.verify_shadow_quality_settings(record, args)
                self.assertTrue(report["verified"])
                self.assertEqual(report["bootstrap_samples"], 3)
                self.assertEqual(report["accepted_history_samples"], 1 if code == 1 else 3)
                self.assertEqual(report["effective_dispatch_verified"], code == 1)
                if code == 1:
                    software = smoke.verify_shadow_quality_settings(request + "\n" + dispatch.replace("hardware=1", "hardware=0"), args)
                    self.assertFalse(software["hardware"])
                    for invalid in (request, record + "\n" + dispatch, record.replace("samples=1", "samples=3")):
                        with self.assertRaises(smoke.SmokeFailure):
                            smoke.verify_shadow_quality_settings(invalid, args)
                else:
                    with self.assertRaises(smoke.SmokeFailure):
                        smoke.verify_shadow_quality_settings(record + "\n" + dispatch, args)
                for invalid in ("", record + "\n" + record, record + " trailing", smoke.SHADOW_QUALITY_SETTINGS + str(1-code)):
                    with self.assertRaises(smoke.SmokeFailure):
                        smoke.verify_shadow_quality_settings(invalid, args)
        defaults = smoke.parse_args(self.argv)
        self.assertEqual(defaults.shadow_transparent_sampling, "reference_three")
        with patch("sys.stderr"), self.assertRaises(SystemExit):
            smoke.parse_args(self.argv + ["--shadow-transparent-sampling", "0"])

    def acquire_log(self, args, text):
        with patch.object(smoke, "identities", return_value={}), \
            patch.object(smoke, "build_launch_environment", return_value={}), \
            patch.object(smoke, "launch_logserver", return_value=(None, None, self.output, {}, "*.log")), \
            patch.object(smoke, "launch_testbed", return_value=Mock()), \
            patch.object(smoke, "terminate_process", return_value=(0, "")), \
            patch.object(smoke, "shutdown_logserver_and_collect", return_value=text), \
            patch.object(smoke.ab, "device_material_signature", return_value={}):
            return smoke.acquire(args, self.output)

    def test_acquisition_records_verified_settings_and_exact_launch_environment(self):
        args = smoke.parse_args(self.argv + ["--software-shadow-budget-mib", "256"])
        result = self.acquire_log(args, shadow_record(budget_bytes=256 * 1024 * 1024) + "\n" + valid_log())
        report = result["software_shadow_settings"]
        self.assertTrue(report["verified"])
        self.assertEqual(report["budget_mib"], 256)
        self.assertEqual(report["observed"]["budget_bytes"], 256 * 1024 * 1024)
        launch = json.loads((self.output / "launch.json").read_text(encoding="utf-8"))
        self.assertEqual(launch["environment"]["NWB_SOFTWARE_SHADOW_BUDGET_MIB"], "256")

    def test_acquisition_rejects_smaller_budget_when_256_mib_was_requested_and_preserves_log(self):
        args = smoke.parse_args(self.argv + ["--software-shadow-budget-mib", "256"])
        text = shadow_record(budget_bytes=128 * 1024 * 1024) + "\n" + valid_log()
        with self.assertRaisesRegex(smoke.SmokeFailure, "software shadow settings mismatch"):
            self.acquire_log(args, text)
        self.assertEqual((self.output / "runtime.log").read_text(encoding="utf-8"), text)


class StressReflectionDiagnosticTests(unittest.TestCase):
    def test_default_run_explicitly_has_unmeasured_optical_support(self):
        result = smoke.parse_runtime_log(valid_log(), 0)
        self.assertEqual(result["optical_reflection"], {"requested": False, "status": "not_measured"})
        with self.assertRaisesRegex(smoke.SmokeFailure, "unrequested reflection"):
            smoke.parse_runtime_log(reflection_log(reflection_record()), 0)

    def test_all_rejected_reflections_are_exposed_without_changing_presentation_result(self):
        text = reflection_log(reflection_record(sequence=3, frame=11, graphics_frame=14),
            reflection_record(sequence=7, frame=15, graphics_frame=18))
        result = smoke.parse_runtime_log(text, 0, reflection_diagnostics=True)
        optical = result["optical_reflection"]
        self.assertEqual(result["measurement"]["fps"], 16.)
        self.assertEqual(optical["status"], "all_rejected")
        self.assertEqual(optical["sample_count"], 2)
        self.assertEqual(optical["sums"]["hardware_rays"], 20)
        self.assertEqual(optical["sums"]["unsupported_paths"], 20)
        self.assertEqual(optical["unsupported_ratio"], 1.)
        self.assertEqual(optical["queries_per_hardware_ray"], 0.)
        self.assertEqual(optical["exterior_eligible_ratio"], 0.)
        self.assertEqual(optical["ranges_by_generation"], [{"generation": 1, "sample_count": 2,
            "sequence_range": [3, 7], "frame_range": [11, 15], "graphics_frame_range": [14, 18]}])

    def test_partial_unsupported_ratios_are_weighted_by_admitted_rays(self):
        text = reflection_log(reflection_record(candidates=1, hardware_rays=1, unsupported_paths=1),
            reflection_record(sequence=2, frame=11, graphics_frame=11, candidates=9, hardware_rays=9,
                hardware_queries=18, bootstrap_events=23, transparent_paths=3, exterior_eligible_rays=6,
                unsupported_paths=0))
        optical = smoke.parse_runtime_log(text, 0, reflection_diagnostics=True)["optical_reflection"]
        self.assertEqual(optical["status"], "unsupported")
        self.assertAlmostEqual(optical["unsupported_ratio"], .1)
        self.assertAlmostEqual(optical["exterior_eligible_ratio"], .6)
        self.assertAlmostEqual(optical["queries_per_hardware_ray"], 1.8)
        self.assertEqual(optical["sums"]["bootstrap_events"], 23)

    def test_queries_are_observations_not_a_complete_optical_support_claim(self):
        for unsupported, expected in ((0, "queries_observed"), (10, "unsupported")):
            with self.subTest(unsupported=unsupported):
                text = reflection_log(reflection_record(hardware_queries=20, bootstrap_events=4,
                    transparent_paths=2, unsupported_paths=unsupported))
                optical = smoke.parse_runtime_log(text, 0, reflection_diagnostics=True)["optical_reflection"]
                self.assertEqual(optical["status"], expected)
                self.assertEqual(optical["queries_per_hardware_ray"], 2.)

    def test_zero_ray_samples_do_not_produce_false_support_or_zero_ratios(self):
        text = reflection_log(reflection_record(hardware_ready=0, transport_enabled=0,
            candidates=0, hardware_rays=0, unsupported_paths=0))
        optical = smoke.parse_runtime_log(text, 0, reflection_diagnostics=True)["optical_reflection"]
        self.assertEqual(optical["status"], "no_queries")
        self.assertIsNone(optical["unsupported_ratio"])
        self.assertIsNone(optical["exterior_eligible_ratio"])
        self.assertIsNone(optical["queries_per_hardware_ray"])
        self.assertEqual(optical["hardware_ready_samples"], 0)

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
            reflection_diagnostics=True)["optical_reflection"]
        self.assertEqual([row["generation"] for row in optical["ranges_by_generation"]], [1, 2])

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
        variants = (valid.replace(" unsupported_paths=10", ""), valid + " unsupported_paths=10",
            valid + " extra=0", valid.replace("hardware_queries=0", "hardware_queries=-1"),
            valid.replace("hardware_queries=0", "hardware_queries=nan"))
        for record in variants:
            with self.subTest(record=record), self.assertRaises(smoke.SmokeFailure):
                smoke.parse_runtime_log(reflection_log(record), 0, reflection_diagnostics=True)


class StressReflectionQualityTests(unittest.TestCase):
    def test_explicit_steps_validate_and_replace_inherited_environment(self):
        with TemporaryDirectory() as temporary:
            output = Path(temporary)
            executable = output / "renderer.exe"
            executable.write_bytes(b"fixture")
            required = ["--executable", str(executable), "--working-directory", str(output), "--no-logserver"]
            self.assertEqual(smoke.parse_args(required).reflection_screen_steps, 96)
            for steps in (8, 32, 48, 96, 256):
                args = smoke.parse_args(required + ["--reflection-screen-steps", str(steps)])
                env = smoke.launch_environment({"NWB_REFLECTION_SCREEN_STEPS": "1"}, args, output)
                self.assertEqual(env["NWB_REFLECTION_SCREEN_STEPS"], str(steps))
                result = smoke.verify_reflection_quality_settings(smoke.REFLECTION_QUALITY_SETTINGS + str(steps), args, {})
                self.assertEqual(result, {"screen_max_steps": steps, "verified": True, "screen_work_measured": False})
            for invalid in ("0", "7", "257", "-1", "48.5", "bad"):
                with patch("sys.stderr"), self.assertRaises(SystemExit):
                    smoke.parse_args(required + ["--reflection-screen-steps", invalid])

    def test_applied_budget_missing_duplicate_malformed_and_mismatch_fail(self):
        args = SimpleNamespace(reflection_screen_steps=48, reflection_diagnostics=False)
        good = smoke.REFLECTION_QUALITY_SETTINGS + "48"
        for bad in ("", good + "\n" + good, good + " extra=0", smoke.REFLECTION_QUALITY_SETTINGS + "96"):
            with self.subTest(bad=bad), self.assertRaises(smoke.SmokeFailure):
                smoke.verify_reflection_quality_settings(bad, args, {})

    def test_actual_software_screen_work_is_independent_of_hardware_counters(self):
        row = reflection_record(hardware_ready=0, transport_enabled=0, candidates=0, hardware_rays=0, unsupported_paths=0)
        row += " screen_attempts=20 screen_hits=2 screen_returns=4 screen_iterations=640 screen_limit_misses=10"
        text = reflection_log(row)
        optical = smoke.parse_runtime_log(text, 0, reflection_diagnostics=True)["optical_reflection"]
        self.assertEqual(optical["hardware_ready_samples"], 0)
        self.assertEqual(optical["screen"]["iterations_per_attempt"], 32)
        self.assertEqual(optical["screen"]["limit_miss_ratio"], .5)
        self.assertEqual(optical["screen"]["return_ratio"], .2)
        args = SimpleNamespace(reflection_screen_steps=48, reflection_diagnostics=True)
        self.assertTrue(smoke.verify_reflection_quality_settings(smoke.REFLECTION_QUALITY_SETTINGS + "48", args, optical)["screen_work_measured"])
        args.reflection_screen_steps = 8
        with self.assertRaisesRegex(smoke.SmokeFailure, "exceeds"):
            smoke.verify_reflection_quality_settings(smoke.REFLECTION_QUALITY_SETTINGS + "8", args, optical)

    def test_legacy_replay_remains_valid_but_new_diagnostic_acquisition_needs_screen_evidence(self):
        optical = smoke.parse_runtime_log(reflection_log(reflection_record()), 0, reflection_diagnostics=True)["optical_reflection"]
        self.assertEqual(optical["screen"]["sample_count"], 0)
        args = SimpleNamespace(reflection_screen_steps=96, reflection_diagnostics=True)
        with self.assertRaisesRegex(smoke.SmokeFailure, "screen-work"):
            smoke.verify_reflection_quality_settings(smoke.REFLECTION_QUALITY_SETTINGS + "96", args, optical)

    def test_malformed_partial_overflow_or_impossible_screen_counters_fail(self):
        prefix = reflection_record()
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
    def test_default_target_reports_twenty_bodies_and_full_camera_signature(self):
        workload = smoke.parse_runtime_log(valid_log(), 0)["workload"]
        self.assertEqual(workload["requested_characters_per_class"], 10)
        self.assertEqual(workload["observed"]["total"], 20)
        self.assertEqual(workload["observed"]["transparent"], 10)
        self.assertEqual(workload["observed"]["opaque"], 10)
        self.assertEqual(workload["observed"]["layout"], "two_rows_v1")
        self.assertEqual(workload["observed"]["camera_z"], -7.2)
        self.assertEqual(workload["signature"], workload_record())

    def test_explicit_comparison_preserves_ten_body_layout_and_camera(self):
        workload = smoke.parse_runtime_log(valid_log(5), 0, characters_per_class=5)["workload"]
        self.assertEqual(workload["observed"]["total"], 10)
        self.assertEqual(workload["observed"]["layout"], "zigzag_v1")
        self.assertEqual(workload["observed"]["row_spacing_x"], 1.44)
        self.assertEqual(workload["observed"]["camera_z"], -4.8)
        self.assertEqual(smoke.parse_measurement(valid_log(5), 5)["fps"], 16.)

    def test_requested_profile_and_actual_spawn_counts_must_match(self):
        for observed, requested in ((5, 10), (10, 5), (10, 0), (10, 11)):
            with self.subTest(observed=observed, requested=requested), self.assertRaises(smoke.SmokeFailure):
                smoke.parse_runtime_log(valid_log(observed), 0, characters_per_class=requested)
        with self.assertRaisesRegex(smoke.SmokeFailure, "spawned characters"):
            smoke.parse_runtime_log(valid_log().replace("spawned 20", "spawned 18"), 0)

    def test_layout_camera_counts_and_numeric_validity_are_load_bearing(self):
        changes = (("total=20", "total=18"), ("transparent=10", "transparent=9"),
            ("opaque=10", "opaque=11"), ("characters_per_class=10", "characters_per_class=10.0"),
            ("total=20", "total=999"), ("columns=10", "columns=-1"), ("rows=2", "rows=1"),
            ("layout=two_rows_v1", "layout=unknown"), ("row_spacing_x=0.72", "row_spacing_x=1.44"),
            ("row_stagger_x=0.18", "row_stagger_x=0"), ("front_z=-0.55", "front_z=0.55"),
            ("body_scale=1", "body_scale=0.5"), ("camera_z=-7.2", "camera_z=-4.8"),
            ("camera_pitch=0.25", "camera_pitch=0.2"), ("camera_y=2.7", "camera_y=nan"),
            ("vertical_fov=1.0471976", "vertical_fov=1.5"), ("near_plane=0.001", "near_plane=inf"),
            ("aspect=0", "aspect=1.777"))
        for old, new in changes:
            with self.subTest(new=new), self.assertRaises(smoke.SmokeFailure):
                smoke.parse_runtime_log(valid_log().replace(old, new), 0)
        parsed = smoke.parse_runtime_log(valid_log().replace("row_spacing_x=0.72", "row_spacing_x=0.72000003"), 0)
        self.assertEqual(parsed["workload"]["observed"]["total"], 20)

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

    def test_cli_defaults_to_target_and_only_accepts_fixed_profiles(self):
        with TemporaryDirectory() as temporary:
            root = Path(temporary)
            executable = root / "renderer.exe"
            executable.write_bytes(b"fixture")
            argv = ["--executable", str(executable), "--working-directory", str(root), "--no-logserver"]
            self.assertEqual(smoke.parse_args(argv).characters_per_class, 10)
            self.assertEqual(smoke.parse_args(argv + ["--characters-per-class", "5"]).characters_per_class, 5)
            for value in ("0", "6", "11", "5.5", "invalid"):
                with self.subTest(value=value), patch("sys.stderr"), self.assertRaises(SystemExit):
                    smoke.parse_args(argv + ["--characters-per-class", value])


class StressMotionTests(unittest.TestCase):
    def test_rotating_launch_removes_inherited_freezes_and_fixed_simulation_time(self):
        args = SimpleNamespace(spin_angle=.6, fixed_delta_seconds=None, reflection_diagnostics=False,
            characters_per_class=10, animate=True, **shadow_defaults())
        inherited = {"NWB_STRESS_TEST_SPIN_ANGLE": "1.25", "NWB_RENDERER_BASELINE_FIXED_DELTA_SECONDS": ".25",
            "NWB_RENDERER_BASELINE_CAPTURE_FREEZE_FRAME": "120", "NWB_STRESS_CHARACTERS_PER_CLASS": "5"}
        env = smoke.launch_environment(inherited, args, Path("moving"))
        self.assertNotIn("NWB_STRESS_TEST_SPIN_ANGLE", env)
        self.assertNotIn("NWB_RENDERER_BASELINE_FIXED_DELTA_SECONDS", env)
        self.assertNotIn("NWB_RENDERER_BASELINE_CAPTURE_FREEZE_FRAME", env)
        self.assertEqual(env["NWB_STRESS_CHARACTERS_PER_CLASS"], "10")
        self.assertEqual(env["NWB_STRESS_SMOKE_TIMING"], "1")

    def test_rotating_cli_rejects_conflicting_yaw_and_simulation_controls(self):
        with TemporaryDirectory() as temporary:
            root = Path(temporary)
            executable = root / "renderer.exe"
            executable.write_bytes(b"fixture")
            argv = ["--executable", str(executable), "--working-directory", str(root), "--no-logserver"]
            fixed = smoke.parse_args(argv)
            self.assertFalse(fixed.animate)
            self.assertEqual(fixed.fixed_delta_seconds, .016666667)
            moving = smoke.parse_args(argv + ["--animate"])
            self.assertTrue(moving.animate)
            self.assertIsNone(moving.fixed_delta_seconds)
            for extra in (["--spin-angle", ".6"], ["--fixed-delta-seconds", ".016666667"]):
                with self.subTest(extra=extra), patch("sys.stderr"), self.assertRaises(SystemExit):
                    smoke.parse_args(argv + ["--animate"] + extra)


class StressMeasurementTests(unittest.TestCase):
    def test_complete_rate_uses_presentations_and_wall_not_fixed_delta_or_queries(self):
        result = smoke.parse_measurement(valid_log())
        self.assertEqual(result["fps"], 16.)
        self.assertEqual(result["frame_ms"], 62.5)
        self.assertEqual(result["presentations"], 480)
        self.assertEqual(result["last"] - result["first"], 480)
        self.assertEqual(len(result["intervals"]), 60)

    def test_optional_pacing_summary_parses_and_rejects_disorder(self):
        paced = valid_log() + "StressTestSmokeProject: presentation pacing samples=480 p50ms=62.5 p95ms=70.0 maxms=120.0 stalls50ms=3\n"
        parsed = smoke.parse_runtime_log(paced, 0)
        self.assertEqual(parsed["pacing"]["samples"], 480)
        self.assertEqual(parsed["pacing"]["stalls50ms"], 3)
        self.assertLessEqual(parsed["pacing"]["p50ms"], parsed["pacing"]["p95ms"])
        self.assertLessEqual(parsed["pacing"]["p95ms"], parsed["pacing"]["maxms"])
        self.assertIsNone(smoke.parse_runtime_log(valid_log(), 0)["pacing"])
        with self.assertRaises(smoke.SmokeFailure):
            smoke.parse_measurement(paced.replace("p50ms=62.5 p95ms=70.0", "p50ms=80.0 p95ms=70.0"))
        with self.assertRaises(smoke.SmokeFailure):
            smoke.parse_measurement(paced + "StressTestSmokeProject: presentation pacing samples=1 p50ms=1 p95ms=1 maxms=1 stalls50ms=0")

    def test_bad_exit_rejected_even_with_complete_log(self):
        with self.assertRaises(smoke.SmokeFailure):
            smoke.parse_runtime_log(valid_log(), 1)

    def test_rejects_timing_sample_rate_in_place_of_presentation_rate(self):
        with self.assertRaisesRegex(smoke.SmokeFailure, "count divided"):
            smoke.parse_measurement(valid_log().replace("complete fps=16", "complete fps=10.7"))

    def test_counts_positive_exact_bounded_and_rates_finite(self):
        for old, new in (("presentations=480", "presentations=479"),
            ("fps=16 presentations=480 seconds=30 first=80 last=560", "fps=0 presentations=0 seconds=30 first=80 last=80"),
            ("complete fps=16", "complete fps=nan"),
            ("seconds=30", "seconds=inf"),
            ("last=560", "last=18446744073709551616")):
            with self.subTest(new=new), self.assertRaises(smoke.SmokeFailure):
                smoke.parse_measurement(valid_log().replace(old, new))

    def test_requires_full_wall_window(self):
        with self.assertRaises(smoke.SmokeFailure):
            smoke.parse_measurement(valid_log().replace("fps=16 presentations=480 seconds=30", "fps=16.551724137931034 presentations=480 seconds=29"))

    def test_duplicate_missing_or_out_of_order_markers_rejected(self):
        for text in (valid_log() + smoke.START, valid_log().replace(smoke.START, ""),
            valid_log() + smoke.DONE + "fps=16 presentations=480 seconds=30 first=80 last=560",
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
            smoke.parse_runtime_log(valid_log(), 0, ["--gpudbg"])
        text = "\n".join("    " + marker + "   " for marker in smoke.GPU_DEBUG) + "\n" + valid_log()
        self.assertEqual(smoke.parse_runtime_log(text, 0, ["--gpudbg"])["measurement"]["fps"], 16.)

    def test_errors_and_incomplete_or_suspended_measurements_rejected(self):
        for marker in ("[ERROR] GPU broke", "VUID-123", "presentation measurement incomplete", "render submission suspended"):
            with self.subTest(marker=marker), self.assertRaises(smoke.SmokeFailure):
                smoke.parse_measurement(valid_log() + marker)

    def test_environment_replaces_inherited_capture_controls(self):
        args = SimpleNamespace(spin_angle=.6, fixed_delta_seconds=.016666667, reflection_diagnostics=False, characters_per_class=10, animate=False, **shadow_defaults())
        env = smoke.launch_environment({"NWB_RENDERER_BASELINE_CAPTURE_FREEZE_FRAME": "96",
            "NWB_STRESS_TEST_SPIN_ANGLE": "2", "NWB_OTHER": "bad",
            "NWB_STRESS_REFLECTION_DIAGNOSTICS": "1", "NWB_STRESS_CHARACTERS_PER_CLASS": "5", "PATH": "kept"}, args, Path("trial"))
        self.assertNotIn("NWB_RENDERER_BASELINE_CAPTURE_FREEZE_FRAME", env)
        self.assertNotIn("NWB_OTHER", env)
        self.assertEqual(env["NWB_STRESS_TEST_SPIN_ANGLE"], "0.6")
        self.assertEqual(env["NWB_STRESS_SMOKE_TIMING"], "1")
        self.assertEqual(env["NWB_STRESS_CHARACTERS_PER_CLASS"], "10")
        args.characters_per_class = 5
        self.assertEqual(smoke.launch_environment({}, args, Path("comparison"))["NWB_STRESS_CHARACTERS_PER_CLASS"], "5")
        self.assertEqual(env["PATH"], "kept")
        self.assertNotIn("NWB_STRESS_REFLECTION_DIAGNOSTICS", env)
        args.reflection_diagnostics = True
        diagnostic_env = smoke.launch_environment({}, args, Path("diagnostic"))
        self.assertEqual(diagnostic_env["NWB_STRESS_REFLECTION_DIAGNOSTICS"], "1")

    def test_output_guard_preserves_prior_evidence_and_rejects_input_overlap(self):
        with TemporaryDirectory() as temporary:
            root = Path(temporary)
            old = root / "old"
            old.mkdir()
            evidence = old / "failure.json"
            evidence.write_text("keep me", encoding="utf-8")
            with self.assertRaises(smoke.SmokeFailure):
                smoke.reserve_output(old, [])
            self.assertEqual(evidence.read_text(encoding="utf-8"), "keep me")
            protected = root / "runtime"
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
            with patch.object(smoke, "identities", return_value={}), \
                patch.object(smoke, "build_launch_environment", return_value={}), \
                patch.object(smoke, "launch_logserver", return_value=(None, None, output, {}, "*.log")), \
                patch.object(smoke, "launch_testbed", return_value=process), \
                patch.object(smoke, "terminate_process", return_value=(1, "failed process output")), \
                patch.object(smoke, "shutdown_logserver_and_collect", return_value="raw timeout runtime log"):
                with self.assertRaisesRegex(smoke.SmokeFailure, "self-exit"):
                    smoke.acquire(args, output)
            self.assertEqual((output / "runtime.log").read_text(encoding="utf-8"), "raw timeout runtime log")
            self.assertEqual((output / "process_tail.txt").read_text(encoding="utf-8"), "failed process output")


class StressPerformanceTargetTests(unittest.TestCase):
    def setUp(self):
        self.temporary = TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.runtime = self.root / "runtime"
        self.runtime.mkdir()
        executable = self.runtime / "renderer.exe"
        executable.write_bytes(b"synthetic")
        self.argv = ["--executable", str(executable), "--working-directory", str(self.runtime), "--no-logserver"]

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
        output = self.root / "capture"
        argv = self.argv + ["--output-directory", str(output)] + extra
        args = smoke.parse_args(argv)
        if not use_main:
            output.mkdir(exist_ok=True)
        with patch.object(smoke, "identities", side_effect=identities, return_value={"verified": True}), \
            patch.object(smoke, "build_launch_environment", return_value={}), \
            patch.object(smoke, "launch_logserver", return_value=(None, None, output, {}, "*.log")), \
            patch.object(smoke, "launch_testbed", return_value=Mock()), \
            patch.object(smoke, "terminate_process", return_value=(0, "process preserved")), \
            patch.object(smoke, "shutdown_logserver_and_collect", return_value=text), \
            patch.object(smoke.ab, "device_material_signature", return_value={}), patch.object(smoke, "write_status"):
            return smoke.main(argv) if use_main else smoke.acquire(args, output)

    def test_default_capture_only_and_positive_finite_thresholds(self):
        self.assertIsNone(smoke.parse_args(self.argv).minimum_fps)
        for threshold in ("60", "0.001", "5e-324", "1e308"):
            self.assertEqual(smoke.parse_args(self.argv + ["--minimum-fps", threshold]).minimum_fps, float(threshold))
        result = self.acquire_or_main([], self.rate_log(16))
        self.assertTrue(result["passed"])
        self.assertTrue(result["capture_validated"])
        self.assertFalse(result["performance_target"]["requested"])
        self.assertIsNone(result["performance_target"]["passed"])

    def test_invalid_or_diagnostic_threshold_requests_are_rejected(self):
        for value in ("0", "-1", "nan", "inf", "-inf", "1e309", "1e-999", "invalid"):
            with self.subTest(value=value), patch("sys.stderr"), self.assertRaises(SystemExit):
                smoke.parse_args(self.argv + ["--minimum-fps=" + value])
        for flags in (["--cpu-diagnostics"], ["--reflection-diagnostics"], ["--cpu-diagnostics", "--reflection-diagnostics"]):
            with self.subTest(flags=flags), patch("sys.stderr"), self.assertRaises(SystemExit):
                smoke.parse_args(self.argv + ["--minimum-fps", "60"] + flags)
            self.assertIsNone(smoke.parse_args(self.argv + flags).minimum_fps)

    def test_acquisition_strict_boundary_and_validated_provenance(self):
        for fps, expected in ((58, False), (60, False), (62, True)):
            with self.subTest(fps=fps):
                result = self.acquire_or_main(["--minimum-fps", "60"], self.rate_log(fps))
                self.assertEqual(result["passed"], expected)
                self.assertTrue(result["capture_validated"])
                self.assertEqual(result["performance_target"], dict(requested=True, minimum_fps=60, comparison=">",
                    observed_fps=fps, source="accepted_native_presentations / steady_clock_seconds", passed=expected))
                self.assertEqual(result["identity_before"], result["identity_after"])
                self.assertIn("runtime.log", result["raw_files"])
                launch = json.loads((self.root / "capture/launch.json").read_text())
                self.assertEqual(launch["minimum_fps"], 60)

    def test_rounded_logged_rate_cannot_turn_equal_count_rate_into_pass(self):
        text = self.rate_log(60).replace("complete fps=60 ", "complete fps=60.00000003 ")
        result = self.acquire_or_main(["--minimum-fps", "60"], text)
        self.assertGreater(result["measurement"]["fps"], 60)
        self.assertEqual(result["performance_target"]["observed_fps"], 60)
        self.assertFalse(result["passed"])

    def test_main_retains_fully_validated_result_and_separate_target_failure(self):
        text = self.rate_log(60)
        self.assertEqual(self.acquire_or_main(["--minimum-fps", "60"], text, use_main=True), 1)
        output = self.root / "capture"
        result = json.loads((output / "result.json").read_text())
        failure = json.loads((output / "failure.json").read_text())
        self.assertTrue(result["capture_validated"])
        self.assertFalse(result["passed"])
        self.assertFalse(result["performance_target"]["passed"])
        self.assertIn("must exceed 60", failure["error"])
        self.assertEqual((output / "runtime.log").read_text(), text)
        self.assertEqual((output / "process_tail.txt").read_text(), "process preserved")
        self.assertEqual(result["identity_before"], {"verified": True})
        self.assertEqual(result["identity_before"], result["identity_after"])

    def test_main_above_target_passes_without_failure_artifact(self):
        self.assertEqual(self.acquire_or_main(["--minimum-fps", "60"], self.rate_log(62), use_main=True), 0)
        result = json.loads((self.root / "capture/result.json").read_text())
        self.assertTrue(result["performance_target"]["passed"])
        self.assertFalse((self.root / "capture/failure.json").exists())

    def test_target_does_not_bypass_validation_or_identity_checks(self):
        for text in (self.rate_log(62) + "VUID-123", self.rate_log(62).replace("blocker_search=0", "blocker_search=1")):
            with self.subTest(text=text[-50:]), self.assertRaises(smoke.SmokeFailure):
                self.acquire_or_main(["--minimum-fps", "60"], text)
        with self.assertRaisesRegex(smoke.SmokeFailure, "identity changed"):
            self.acquire_or_main(["--minimum-fps", "60"], self.rate_log(62), identities=[{"generation": 1}, {"generation": 2}])

    def test_direct_acquisition_rejects_diagnostic_or_invalid_threshold_before_launch(self):
        for field, value in (("cpu_diagnostics", True), ("reflection_diagnostics", True), ("minimum_fps", float("nan"))):
            args = smoke.parse_args(self.argv + ["--minimum-fps", "60"])
            setattr(args, field, value)
            with patch.object(smoke, "launch_testbed") as launch, self.assertRaises(smoke.SmokeFailure):
                smoke.acquire(args, self.root / "unused")
            launch.assert_not_called()


if __name__ == "__main__":
    unittest.main()
