"""Offline checks for frame-lagged lighting acquisition and evidence."""

import math
import tempfile
from pathlib import Path
from types import SimpleNamespace
from unittest import mock

from gpu_timing_parse import require_positive
from harness_test_support import make_abnormal_exit_terminator
from window_capture_smoke import SmokeFailure
from lifecycle_evidence import LightingLifecycleEvidence, DedicatedComputeUnavailable, NO_DEDICATED_ASYNC_COMPUTE, BOOTSTRAP_ACCEPTED, ACTIVE_HISTORY_ACCEPTED, CURRENT_FRAME_ACCEPTED, LAGGED_LIGHTING_LIFECYCLE

LIT_NON_FINITE_LIFECYCLE_DEADLINE = "non-finite lifecycle deadline"
LIT_LAGGED_LIGHTING_HISTORY_CAPTURE_SKIPPE = "lagged lighting-history capture skipped because its source state was unavailable"
LIT_SELF_TEST = "--self-test"
LIT_STARTUP_TIMEOUT = "--startup-timeout"
LIT_TRANSITION_TIMEOUT = "--transition-timeout"
LIT_LAGGED_LIGHTING_SMOKE = "lagged-lighting smoke"
LIT_LAGGED_LIGHTING_SMOKE_LOGSERVER = "lagged-lighting smoke logserver"
LIT_APP_STOP = "app-stop"
LIT_CLEANUP_NONE = "cleanup-none"
LIT_LOGSERVER_LOG = "logserver_*.log"
LIT_LOGSERVER_HELPER = "logserver-helper"
LIT_TARGET_GENERATION_7 = " (target generation 7)"


class LightingLifecycleSelfTest:
    @staticmethod
    def run(harness) -> int:
        numeric_parser = mock.Mock()
        require_positive(numeric_parser, LIT_STARTUP_TIMEOUT, 1.0)
        numeric_parser.error.assert_not_called()
        for value in (math.nan, math.inf, -math.inf):
            numeric_parser.error.reset_mock()
            numeric_parser.error.side_effect = ValueError(LIT_NON_FINITE_LIFECYCLE_DEADLINE)
            try:
                require_positive(numeric_parser, LIT_TRANSITION_TIMEOUT, value)
            except ValueError as error:
                assert str(error) == LIT_NON_FINITE_LIFECYCLE_DEADLINE
            else:
                raise AssertionError("a non-finite lifecycle deadline was accepted")
            numeric_parser.error.assert_called_once()

        log = "\n".join((f"{BOOTSTRAP_ACCEPTED}{LIT_TARGET_GENERATION_7}", f"{ACTIVE_HISTORY_ACCEPTED}{LIT_TARGET_GENERATION_7}", f"{CURRENT_FRAME_ACCEPTED}{LIT_TARGET_GENERATION_7}", f"{BOOTSTRAP_ACCEPTED}{LIT_TARGET_GENERATION_7}",
            f"{ACTIVE_HISTORY_ACCEPTED}{LIT_TARGET_GENERATION_7}",))
        assert LightingLifecycleEvidence.validate_lifecycle_order(log) == LightingLifecycleEvidence.accepted_lifecycle_events(log)
        LightingLifecycleEvidence.require_lifecycle_stage(log, len(LAGGED_LIGHTING_LIFECYCLE))
        for invalid_log in ("\n".join((f"{ACTIVE_HISTORY_ACCEPTED}{LIT_TARGET_GENERATION_7}", f"{BOOTSTRAP_ACCEPTED}{LIT_TARGET_GENERATION_7}",)), "\n".join((f"{BOOTSTRAP_ACCEPTED}{LIT_TARGET_GENERATION_7}", f"{BOOTSTRAP_ACCEPTED}{LIT_TARGET_GENERATION_7}",)),):
            try:
                LightingLifecycleEvidence.validate_lifecycle_order(invalid_log)
            except SmokeFailure:
                pass
            else:
                raise AssertionError("invalid accepted lifecycle was not rejected")
        try:
            LightingLifecycleEvidence.require_lifecycle_stage(log.splitlines()[0], len(LAGGED_LIGHTING_LIFECYCLE))
        except SmokeFailure:
            pass
        else:
            raise AssertionError("incomplete accepted lifecycle was not rejected")
        try:
            LightingLifecycleEvidence.require_lifecycle_stage(log, len(LAGGED_LIGHTING_LIFECYCLE) + 1)
        except ValueError:
            pass
        else:
            raise AssertionError("invalid lifecycle stage count was not rejected")
        graphics_route_log = f"{NO_DEDICATED_ASYNC_COMPUTE}, target generation 3)"
        try:
            LightingLifecycleEvidence.require_final_run_verdict(f"{graphics_route_log}\n[ERROR] simulated validation failure", 0, "")
        except SmokeFailure:
            pass
        else:
            raise AssertionError("a forbidden error accompanying the Graphics queue route was incorrectly skipped")
        current_history_warnings = ("deferred graph build with optional lagged lighting-history capture failed; retrying without the tail", "deferred lagged lighting-history tail was unavailable; reverting to current-frame lighting",
            LIT_LAGGED_LIGHTING_HISTORY_CAPTURE_SKIPPE, "graph-owned lagged lighting-history capture record/submission was rejected; reverting to current-frame lighting",)
        for warning in current_history_warnings:
            try:
                LightingLifecycleEvidence.require_final_run_verdict(f"{log}\n{warning}", 0, "")
            except SmokeFailure:
                pass
            else:
                raise AssertionError(f"current lagged lighting-history warning was not rejected: {warning}")
        try:
            LightingLifecycleEvidence.require_final_run_verdict(graphics_route_log, 0, "")
        except DedicatedComputeUnavailable:
            pass
        else:
            raise AssertionError("a clean Graphics queue route was not classified as a topology skip")
        try:
            LightingLifecycleEvidence.require_final_run_verdict(graphics_route_log, 9, "simulated abnormal exit after the topology marker")
        except SmokeFailure:
            pass
        else:
            raise AssertionError("an abnormal exit after the Graphics queue route was incorrectly skipped")

        module = harness
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            executable = root / "orchestration.exe"
            executable.write_bytes(b"exe")
            args = harness.parse_args([LIT_SELF_TEST])
            args.executable = executable
            args.runtime_dir = root
            app = SimpleNamespace(pid=4321, poll=lambda: None)
            logserver = object()
            baseline = {root / "old.log": 7}
            backend = mock.Mock()
            backend.wait_for_window.return_value = 17
            events = []

            terminate = make_abnormal_exit_terminator(app, events)

            def shutdown(process, log_directory, received_baseline, pattern, shutdown_name="logserver"):
                assert process is logserver
                assert events == [(LIT_APP_STOP, LIT_LAGGED_LIGHTING_SMOKE, 17)]
                events.append((LIT_LOGSERVER_HELPER, shutdown_name))
                return NO_DEDICATED_ASYNC_COMPUTE

            with mock.patch.object(module, "build_launch_environment", return_value={}), \
                 mock.patch.object(module, "create_capture_backend", return_value=backend), \
                 mock.patch.object(module, "launch_logserver", return_value=(logserver, 49152, root, baseline, LIT_LOGSERVER_LOG)), \
                 mock.patch.object(module, "launch_testbed", return_value=app), \
                 mock.patch.object(module, "wait_for_lifecycle_stage", return_value=NO_DEDICATED_ASYNC_COMPUTE), \
                 mock.patch.object(module.time, "sleep"), \
                 mock.patch.object(module.CaptureProcessTermination, "terminate_process", side_effect=terminate) as terminate_mock, \
                 mock.patch.object(module, "shutdown_logserver_and_collect", side_effect=shutdown):
                try:
                    harness.run(args)
                except SmokeFailure as error:
                    assert "exit 7" in str(error)
                else:
                    raise AssertionError("lagged-lighting orchestration accepted an abnormal Testbed exit")

            assert events == [(LIT_APP_STOP, LIT_LAGGED_LIGHTING_SMOKE, 17), (LIT_LOGSERVER_HELPER, LIT_LAGGED_LIGHTING_SMOKE_LOGSERVER), (LIT_CLEANUP_NONE, LIT_LAGGED_LIGHTING_SMOKE, 17), (LIT_CLEANUP_NONE, LIT_LAGGED_LIGHTING_SMOKE_LOGSERVER, None),]
            assert terminate_mock.mock_calls == [mock.call(app, LIT_LAGGED_LIGHTING_SMOKE, 17), mock.call(None, LIT_LAGGED_LIGHTING_SMOKE, 17), mock.call(None, LIT_LAGGED_LIGHTING_SMOKE_LOGSERVER),]
            backend.close.assert_called_once_with()

        print("frame-lagged async-lighting harness self-test passed")
        return 0
