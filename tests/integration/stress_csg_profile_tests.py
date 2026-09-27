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


PROFILE = (smoke.CSG_PROFILE + "profile=waist_bands receivers=20 transparent=10 opaque=10 cutters=2 "
    "half_x=4.5 half_y=0.08 half_z=0.65 center_y=0.9 amplitude_y=0.1 front_z=-0.55 back_z=0.55 motion=crowd_yaw")
DISABLED = "RayQuery=0 RayTracingPipeline=0 RayTracingAccelStruct=0 AccelStructDescriptors=0 AccelStructLayout=0"


def csg_log(hardware=True):
    capability = (smoke.CSG_DEVICE + f"meshlets=0 rayquery={int(hardware)} raypipeline={int(hardware)} "
        f"accelstruct={int(hardware)} wavelanes=64 renderer=fixture")
    dispatch = smoke.CSG_DISPATCH + f"(hardware_compose={int(hardware)}, 25 instances)"
    setup = capability + "\n" + PROFILE + "\n" + dispatch
    if not hardware:
        setup += "\n" + DISABLED
    return shadow_record() + "\n" + valid_log().replace(smoke.START, smoke.START + "\n" + setup)


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
        self.assertEqual(args.csg_profile, "none")
        environment = smoke.launch_environment({"NWB_STRESS_CSG_PROFILE": "waist_bands"}, args, self.output)
        self.assertEqual(environment["NWB_STRESS_CSG_PROFILE"], "none")
        self.assertFalse(smoke.verify_csg_profile(valid_log(), args)["enabled"])
        with self.assertRaisesRegex(smoke.SmokeFailure, "unrequested CSG"):
            smoke.verify_csg_profile(csg_log(), args)

    def test_cli_requires_complete_twenty_body_map_profile(self):
        arguments = self.argv + ["--csg-profile", "waist_bands"]
        args = smoke.parse_args(arguments)
        environment = smoke.launch_environment({"NWB_STRESS_CSG_PROFILE": "none"}, args, self.output)
        self.assertEqual(environment["NWB_STRESS_CSG_PROFILE"], "waist_bands")
        for extra in (["--characters-per-class", "5"], ["--software-shadow-backend", "trace"],
            ["--software-shadow-capture-cadence", "reuse_one_frame"], ["--csg-profile", "unknown"]):
            with self.subTest(extra=extra), patch("sys.stderr"), self.assertRaises(SystemExit):
                smoke.parse_args(arguments + extra)

    def test_hardware_and_disabled_device_routes_are_verified(self):
        args = smoke.parse_args(self.argv + ["--csg-profile", "waist_bands"])
        hardware = smoke.verify_csg_profile(csg_log(), args)
        self.assertTrue(hardware["hardware_compose"])
        self.assertEqual(hardware["observed"]["receivers"], 20)
        self.assertEqual(hardware["map_instances"], 25)
        args.application_arg.append("--disable-hardware-ray-tracing")
        software = smoke.verify_csg_profile(csg_log(False), args)
        self.assertFalse(software["hardware_compose"])
        with self.assertRaisesRegex(smoke.SmokeFailure, "disabled logical-device"):
            smoke.verify_csg_profile(csg_log(), args)
        with self.assertRaisesRegex(smoke.SmokeFailure, "disabled logical-device"):
            smoke.verify_csg_profile(csg_log(False).replace(DISABLED, ""), args)

    def test_missing_or_partial_csg_work_cannot_qualify(self):
        args = smoke.parse_args(self.argv + ["--csg-profile", "waist_bands"])
        original = csg_log()
        for text in (valid_log(), original.replace(PROFILE, ""), original.replace(PROFILE, PROFILE + "\n" + PROFILE),
            original.replace("receivers=20", "receivers=10"), original.replace("transparent=10 opaque=10 cutters=2", "transparent=9 opaque=10 cutters=2"),
            original.replace("cutters=2", "cutters=1"), original.replace("half_y=0.08", "half_y=0"),
            original.replace("amplitude_y=0.1", "amplitude_y=nan"), original.replace("25 instances", "19 instances"),
            original.replace("hardware_compose=1", "hardware_compose=0"), original.replace(smoke.CSG_DISPATCH, "NotDispatched: "),
            original.replace(smoke.CSG_DEVICE, "NotCapabilities: ")):
            with self.subTest(text=text[:100]), self.assertRaises(smoke.SmokeFailure):
                smoke.verify_csg_profile(text, args)

    def test_profile_must_be_installed_before_measuring(self):
        args = smoke.parse_args(self.argv + ["--csg-profile", "waist_bands"])
        original = csg_log()
        late = original.replace(PROFILE, "").replace(smoke.SHUTDOWN, PROFILE + "\n" + smoke.SHUTDOWN)
        with self.assertRaisesRegex(smoke.SmokeFailure, "precede"):
            smoke.verify_csg_profile(late, args)
        dispatch = smoke.CSG_DISPATCH + "(hardware_compose=1, 25 instances)"
        late_dispatch = original.replace(dispatch, "").replace(smoke.DONE, dispatch + "\n" + smoke.DONE)
        with self.assertRaisesRegex(smoke.SmokeFailure, "precede"):
            smoke.verify_csg_profile(late_dispatch, args)

    def test_fixed_and_moving_modes_keep_the_same_profile(self):
        for moving in (False, True):
            args = smoke.parse_args(self.argv + ["--csg-profile", "waist_bands"] + (["--animate"] if moving else []))
            environment = smoke.launch_environment({}, args, self.output)
            self.assertEqual(environment["NWB_STRESS_CSG_PROFILE"], "waist_bands")
            self.assertEqual("NWB_STRESS_TEST_SPIN_ANGLE" not in environment, moving)
            self.assertEqual("NWB_RENDERER_BASELINE_FIXED_DELTA_SECONDS" not in environment, moving)

    def test_acquisition_retains_verified_csg_evidence(self):
        args = smoke.parse_args(self.argv + ["--csg-profile", "waist_bands", "--animate"])
        with patch.object(smoke, "identities", return_value={}), \
            patch.object(smoke, "build_launch_environment", return_value={}), \
            patch.object(smoke, "launch_logserver", return_value=(None, None, self.output, {}, "*.log")), \
            patch.object(smoke, "launch_testbed", return_value=Mock()), \
            patch.object(smoke, "terminate_process", return_value=(0, "")), \
            patch.object(smoke, "shutdown_logserver_and_collect", return_value=csg_log()), \
            patch.object(smoke.ab, "device_material_signature", return_value={}):
            result = smoke.acquire(args, self.output)
        self.assertTrue(result["csg_profile"]["verified"])
        self.assertEqual(result["csg_profile"]["signature"], PROFILE)
        self.assertEqual(result["workload"]["observed"]["total"], 20)
        self.assertEqual(result["motion"]["mode"], "rotating")
        launch = json.loads((self.output / "launch.json").read_text(encoding="utf-8"))
        self.assertEqual(launch["environment"]["NWB_STRESS_CSG_PROFILE"], "waist_bands")


if __name__ == "__main__":
    unittest.main()
