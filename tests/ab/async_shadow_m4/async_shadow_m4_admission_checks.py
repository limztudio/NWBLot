from __future__ import annotations

import math
from unittest import mock


LIT_SYNC_EXECUTABLE = "--sync-executable"
LIT_ASYNC_EXECUTABLE = "--async-executable"
LIT_RUNTIME_DIR = "--runtime-dir"
LIT_OUTPUT_DIR = "--output-dir"
LIT_NON_FINITE_BENCHMARK_OPTION = "non-finite benchmark option"
LIT_SYNC = "sync"
LIT_ASYNC = "async"
LIT_WARMUP_SECONDS = "--warmup-seconds"
LIT_STARTUP_TIMEOUT = "--startup-timeout"


class M4AdmissionChecks:
    @staticmethod
    def check_cli(harness):
        numeric_parser = mock.Mock()
        harness.require_non_negative(numeric_parser, LIT_WARMUP_SECONDS, 0.0)
        numeric_parser.error.assert_not_called()
        for validator in (harness.require_positive, harness.require_non_negative):
            for value in (math.nan, math.inf, -math.inf):
                numeric_parser.error.reset_mock()
                numeric_parser.error.side_effect = ValueError(LIT_NON_FINITE_BENCHMARK_OPTION)
                try:
                    validator(numeric_parser, LIT_STARTUP_TIMEOUT, value)
                except ValueError as error:
                    assert str(error) == LIT_NON_FINITE_BENCHMARK_OPTION
                else:
                    raise AssertionError("a non-finite benchmark deadline or tolerance was accepted")
                numeric_parser.error.assert_called_once()

        yaw_arguments = [LIT_SYNC_EXECUTABLE, LIT_SYNC, LIT_ASYNC_EXECUTABLE, LIT_ASYNC, LIT_RUNTIME_DIR, "runtime", LIT_OUTPUT_DIR, "output"]
        for value in (
            "nan", "inf", "-inf", "not-a-number", "1e100", "1e300", "-1e100", "1_0", "+0.6", " 0.6", "0.6 ", "3.4028236e38", "1e-46", "1e-999",
            "340282356779733661637539395458142568448",
            "7.00649232162408535461864791644958065640130970938257885878534141944895541342930300743319094181060791015625E-46",
        ):
            with mock.patch("sys.stderr"):
                try:
                    harness.parse_args(yaw_arguments + [f"--frozen-yaw={value}"])
                except SystemExit as error:
                    assert error.code == 2
                else:
                    raise AssertionError(f"M4 CLI accepted malformed frozen yaw {value}")
        for value in (
            "0", "-1", "0.6000", "-0.0", ".6", "1.", "6e-1", "6E+0", "3.4028235e38", "1e-45", "0e-999", "-0e999",
            "340282356779733661637539395458142568447",
            "7.00649232162408535461864791644958065640130970938257885878534141944895541342930300743319094181060791015626E-46",
            "0e9999999999999999999999999999999", "-0e-9999999999999999999999999999999",
        ):
            assert harness.parse_args(yaw_arguments + [f"--frozen-yaw={value}"]).frozen_yaw == value

    @staticmethod
    def check_lane(harness):
        lane_log = ("StressTestSmokeProject: M4 async compute requested=1 effective=1 graphicsQueue=0:17 computeQueue=1:17 " "computeDedicated=1 computeSupported=1 computeGraphics=0")
        lane = harness.LaneStatus.parse(lane_log)
        assert lane is not None
        lane.validate_for_mode(LIT_ASYNC)
        for invalid_log in (lane_log.replace("graphicsQueue=0:17", "graphicsQueue=65535:17"), lane_log.replace("computeQueue=1:17", "computeQueue=1:18"), lane_log.replace("computeQueue=1:17", "computeQueue=1:0"),
            lane_log.replace("computeQueue=1:17", "computeQueue=65535:0"), lane_log.replace("computeQueue=1:17", "computeQueue=0:17"), lane_log.replace("computeDedicated=1", "computeDedicated=0"), lane_log.replace("computeSupported=1", "computeSupported=0"),
            lane_log.replace("computeGraphics=0", "computeGraphics=1"), lane_log.replace("effective=1", "effective=0"), lane_log.replace("requested=1 effective=1", "requested=0 effective=0"),):
            invalid_lane = harness.LaneStatus.parse(invalid_log)
            assert invalid_lane is not None
            try:
                invalid_lane.validate_for_mode(LIT_ASYNC)
            except harness.DedicatedComputeUnavailable:
                raise AssertionError("M4 contradictory queue evidence was classified as a hardware skip")
            except harness.SmokeFailure:
                pass
            else:
                raise AssertionError("M4 accepted invalid or contradictory selected queue evidence")
        absent_compute_log = ("StressTestSmokeProject: M4 async compute requested=1 effective=0 graphicsQueue=0:17 computeQueue=65535:0 " "computeDedicated=0 computeSupported=0 computeGraphics=0")
        shared_compute_log = ("StressTestSmokeProject: M4 async compute requested=1 effective=0 graphicsQueue=0:17 computeQueue=0:17 " "computeDedicated=0 computeSupported=1 computeGraphics=1")
        for unavailable_log in (absent_compute_log, shared_compute_log):
            unavailable_lane = harness.LaneStatus.parse(unavailable_log)
            assert unavailable_lane is not None
            try:
                unavailable_lane.validate_for_mode(LIT_ASYNC)
            except harness.DedicatedComputeUnavailable:
                pass
            else:
                raise AssertionError("M4 unavailable dedicated compute transport was not a capability skip")
        disabled_lane = harness.LaneStatus.parse(lane_log.replace("requested=1 effective=1", "requested=0 effective=0"))
        assert disabled_lane is not None
        disabled_lane.validate_for_mode(LIT_SYNC)
        for malformed_log in (lane_log.replace("computeGraphics=0", "computeGraphics="), lane_log.replace("computeGraphics=0", "computeGraphics=2"), lane_log.replace("graphicsQueue=0:17", "graphicsQueue=0"),
            lane_log.replace("StressTestSmokeProject:", "OtherProject:"),):
            assert harness.LaneStatus.parse(malformed_log) is None
