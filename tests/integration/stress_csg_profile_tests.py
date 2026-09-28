#!/usr/bin/env python3
"""Verify optional CSG workload admission, actual shadow routing and measurement evidence."""

import json
from pathlib import Path
import sys
from tempfile import TemporaryDirectory
import unittest
from unittest.mock import Mock, patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "smoke"))
import stress_timing_smoke as smoke  # noqa: E402
from stress_timing_smoke_tests import shadow_record, valid_log  # noqa: E402

# Shared literals (no inline hardcodes below this block).
LIT_N = "\n"
LIT_NONE = "none"
LIT_NWB_STRESS_CSG_PROFILE = "NWB_STRESS_CSG_PROFILE"
LIT_WAIST_BANDS = "waist_bands"
LIT_CSG_PROFILE = "--csg-profile"
LIT_VERIFIED = "verified"
LIT_HARDWARE_COMPOSE = "hardware_compose"
LIT_OBSERVED = "observed"
LIT_DISABLED_LOGICAL_DEVICE = "disabled logical-device"
LIT_PRECEDE = "precede"
LIT_ANIMATE = "--animate"
LIT_CSG_PROFILE_2 = "csg_profile"
LIT_MAIN = "__main__"
LIT_UTF_8 = "utf-8"


PROFILE = (smoke.CSG_PROFILE + "profile=waist_bands receivers=20 transparent=10 opaque=10 cutters=2 "
    "half_x=4.5 half_y=0.08 half_z=0.65 center_y=0.9 amplitude_y=0.1 front_z=-0.55 back_z=0.55 motion=crowd_yaw")
DISABLED = "RayQuery=0 RayTracingPipeline=0 RayTracingAccelStruct=0 AccelStructDescriptors=0 AccelStructLayout=0"


def csg_log(hardware=True):
    capability = (smoke.CSG_DEVICE + f"meshlets=0 rayquery={int(hardware)} raypipeline={int(hardware)} "
        f"accelstruct={int(hardware)} wavelanes=64 renderer=fixture")
    dispatch = smoke.CSG_DISPATCH + f"(hardware_compose={int(hardware)}, 25 instances)"
    setup = capability + LIT_N + PROFILE + LIT_N + dispatch
    if not hardware:
        setup += LIT_N + DISABLED
    return shadow_record() + LIT_N + valid_log().replace(smoke.START, smoke.START + LIT_N + setup)


class StressCsgProfileTests(unittest.TestCase):
    def setUp(self):
        self.temporary = TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.output = Path(self.temporary.name)
        executable = self.output / "renderer.exe"
        executable.write_bytes(b"fixture")
        self.argv = ["--executable", str(executable), "--working-directory", str(self.output), "--no-logserver"]

    def test_default_strips_inherited_csg_and_rejects_unrequested_work(self):
        args = smoke.parse_args(self.argv)
        self.assertEqual(args.csg_profile, LIT_NONE)
        environment = smoke.launch_environment({LIT_NWB_STRESS_CSG_PROFILE: LIT_WAIST_BANDS}, args, self.output)
        self.assertEqual(environment[LIT_NWB_STRESS_CSG_PROFILE], LIT_NONE)
        self.assertFalse(smoke.verify_csg_profile(valid_log(), args)["enabled"])
        with self.assertRaisesRegex(smoke.SmokeFailure, "unrequested CSG"):
            smoke.verify_csg_profile(csg_log(), args)

    def test_cli_requires_complete_twenty_body_map_profile(self):
        arguments = self.argv + [LIT_CSG_PROFILE, LIT_WAIST_BANDS]
        args = smoke.parse_args(arguments)
        environment = smoke.launch_environment({LIT_NWB_STRESS_CSG_PROFILE: LIT_NONE}, args, self.output)
        self.assertEqual(environment[LIT_NWB_STRESS_CSG_PROFILE], LIT_WAIST_BANDS)
        for extra in (["--characters-per-class", "5"], ["--software-shadow-backend", "trace"],
            [LIT_CSG_PROFILE, "unknown"]):
            with self.subTest(extra=extra), patch("sys.stderr"), self.assertRaises(SystemExit):
                smoke.parse_args(arguments + extra)

    def test_csg_reuse_requires_accepted_runtime_evidence(self):
        for name, requested, effective in (("reuse_one_frame", 1, 2), ("reuse_two_frames", 2, 3)):
            args = smoke.parse_args(self.argv + [LIT_CSG_PROFILE, LIT_WAIST_BANDS, "--software-shadow-capture-cadence", name])
            environment = smoke.launch_environment({}, args, self.output)
            self.assertEqual(environment["NWB_SOFTWARE_SHADOW_CAPTURE_CADENCE"], name)
            text = csg_log().replace("capture_cadence=0", f"capture_cadence={requested}")
            with self.assertRaisesRegex(smoke.SmokeFailure, "accepted light-space capture reuse"):
                smoke.verify_software_shadow_settings(text, args)
            marker = smoke.SOFTWARE_SHADOW_CAPTURE_REUSE_PREFIX + f"(cadence={effective})"
            verified = smoke.verify_software_shadow_settings(text + LIT_N + marker, args)
            self.assertTrue(verified["accepted_reuse_verified"])
            self.assertTrue(smoke.verify_csg_profile(text, args)[LIT_VERIFIED])

    def test_hardware_and_disabled_device_routes_are_verified(self):
        args = smoke.parse_args(self.argv + [LIT_CSG_PROFILE, LIT_WAIST_BANDS])
        hardware = smoke.verify_csg_profile(csg_log(), args)
        self.assertTrue(hardware[LIT_HARDWARE_COMPOSE])
        self.assertEqual(hardware[LIT_OBSERVED]["receivers"], 20)
        self.assertEqual(hardware["map_instances"], 25)
        args.application_arg.append("--disable-hardware-ray-tracing")
        software = smoke.verify_csg_profile(csg_log(False), args)
        self.assertFalse(software[LIT_HARDWARE_COMPOSE])
        with self.assertRaisesRegex(smoke.SmokeFailure, LIT_DISABLED_LOGICAL_DEVICE):
            smoke.verify_csg_profile(csg_log(), args)
        with self.assertRaisesRegex(smoke.SmokeFailure, LIT_DISABLED_LOGICAL_DEVICE):
            smoke.verify_csg_profile(csg_log(False).replace(DISABLED, ""), args)

    def test_missing_or_partial_csg_work_cannot_qualify(self):
        args = smoke.parse_args(self.argv + [LIT_CSG_PROFILE, LIT_WAIST_BANDS])
        original = csg_log()
        for text in (valid_log(), original.replace(PROFILE, ""), original.replace(PROFILE, PROFILE + LIT_N + PROFILE),
            original.replace("receivers=20", "receivers=10"), original.replace("transparent=10 opaque=10 cutters=2", "transparent=9 opaque=10 cutters=2"),
            original.replace("cutters=2", "cutters=1"), original.replace("half_y=0.08", "half_y=0"),
            original.replace("amplitude_y=0.1", "amplitude_y=nan"), original.replace("25 instances", "19 instances"),
            original.replace("hardware_compose=1", "hardware_compose=0"), original.replace(smoke.CSG_DISPATCH, "NotDispatched: "),
            original.replace(smoke.CSG_DEVICE, "NotCapabilities: ")):
            with self.subTest(text=text[:100]), self.assertRaises(smoke.SmokeFailure):
                smoke.verify_csg_profile(text, args)

    def test_profile_must_be_installed_before_measuring(self):
        args = smoke.parse_args(self.argv + [LIT_CSG_PROFILE, LIT_WAIST_BANDS])
        original = csg_log()
        late = original.replace(PROFILE, "").replace(smoke.SHUTDOWN, PROFILE + LIT_N + smoke.SHUTDOWN)
        with self.assertRaisesRegex(smoke.SmokeFailure, LIT_PRECEDE):
            smoke.verify_csg_profile(late, args)
        dispatch = smoke.CSG_DISPATCH + "(hardware_compose=1, 25 instances)"
        late_dispatch = original.replace(dispatch, "").replace(smoke.DONE, dispatch + LIT_N + smoke.DONE)
        with self.assertRaisesRegex(smoke.SmokeFailure, LIT_PRECEDE):
            smoke.verify_csg_profile(late_dispatch, args)

    def test_fixed_and_moving_modes_keep_the_same_profile(self):
        for moving in (False, True):
            args = smoke.parse_args(self.argv + [LIT_CSG_PROFILE, LIT_WAIST_BANDS] + ([LIT_ANIMATE] if moving else []))
            environment = smoke.launch_environment({}, args, self.output)
            self.assertEqual(environment[LIT_NWB_STRESS_CSG_PROFILE], LIT_WAIST_BANDS)
            self.assertEqual("NWB_STRESS_TEST_SPIN_ANGLE" not in environment, moving)
            self.assertEqual("NWB_RENDERER_BASELINE_FIXED_DELTA_SECONDS" not in environment, moving)

    def test_acquisition_retains_verified_csg_evidence(self):
        args = smoke.parse_args(self.argv + [LIT_CSG_PROFILE, LIT_WAIST_BANDS, LIT_ANIMATE])
        with patch.object(smoke, "identities", return_value={}), \
            patch.object(smoke, "build_launch_environment", return_value={}), \
            patch.object(smoke, "launch_logserver", return_value=(None, None, self.output, {}, "*.log")), \
            patch.object(smoke, "launch_testbed", return_value=Mock()), \
            patch.object(smoke, "terminate_process", return_value=(0, "")), \
            patch.object(smoke, "shutdown_logserver_and_collect", return_value=csg_log()), \
            patch.object(smoke.ab, "device_material_signature", return_value={}):
            result = smoke.acquire(args, self.output)
        self.assertTrue(result[LIT_CSG_PROFILE_2][LIT_VERIFIED])
        self.assertEqual(result[LIT_CSG_PROFILE_2]["signature"], PROFILE)
        self.assertEqual(result["workload"][LIT_OBSERVED]["total"], 20)
        self.assertEqual(result["motion"]["mode"], "rotating")
        launch = json.loads((self.output / "launch.json").read_text(encoding=LIT_UTF_8))
        self.assertEqual(launch["environment"][LIT_NWB_STRESS_CSG_PROFILE], LIT_WAIST_BANDS)


if __name__ == LIT_MAIN:
    unittest.main()
