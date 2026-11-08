#!/usr/bin/env python3
"""Validate the opt-in frame-lagged async-lighting lifecycle through the selected graphics backend.

The smoke application starts with frame-lagged lighting enabled. This runner waits for accepted bootstrap and
history-use submissions, uses F1 to request the established current-frame path, then re-enables the feature and
requires the second bootstrap/history-use pair. A host without a dedicated compute-only queue reports the accepted
Graphics queue route and exits with the standard smoke skip code instead of claiming async coverage.
"""

from __future__ import annotations

import argparse
import sys
import time
from pathlib import Path
from typing import Mapping, Sequence

LIT_PROCESS_POLL_SECONDS = 0.1
LIT_WINDOW_SETTLE_SECONDS = 0.1


REPO = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO / "tests" / "ab"))
sys.path.insert(0, str(REPO / "tests" / "smoke"))

from gpu_timing_parse import require_positive  # noqa: E402
from window_capture_environment import make_runtime_launch_args  # noqa: E402
from window_capture_termination import CaptureProcessTermination
from window_capture_smoke import (  # noqa: E402
    run_smoke_main, SmokeFailure, build_launch_environment, collect_log_delta, create_capture_backend, ensure_process_running, launch_logserver, launch_testbed,
    shutdown_logserver_and_collect, )
from lifecycle_evidence import LightingLifecycleEvidence, NO_DEDICATED_ASYNC_COMPUTE, LAGGED_LIGHTING_LIFECYCLE
from lifecycle_self_test import LightingLifecycleSelfTest


LIT_SELF_TEST = "--self-test"
LIT_STORE_TRUE = "store_true"
LIT_STARTUP_TIMEOUT = "--startup-timeout"
LIT_TRANSITION_TIMEOUT = "--transition-timeout"
LIT_F1 = "F1"
LIT_LAGGED_LIGHTING_SMOKE = "lagged-lighting smoke"
LIT_LAGGED_LIGHTING_SMOKE_LOGSERVER = "lagged-lighting smoke logserver"
LIT_MAIN = "__main__"


LOG_TAIL_CHAR_LIMIT = 4000


def wait_for_lifecycle_stage(process, log_directory: Path, log_baseline: Mapping[Path, int], log_pattern: str, expected_event_count: int, timeout_seconds: float, stage: str,) -> str:
    deadline = time.monotonic() + timeout_seconds
    latest_log = ""
    while time.monotonic() < deadline:
        ensure_process_running(process, stage)
        latest_log = collect_log_delta(log_directory, log_baseline, log_pattern)
        LightingLifecycleEvidence.reject_forbidden_messages(latest_log)
        if NO_DEDICATED_ASYNC_COMPUTE in latest_log:
            return latest_log
        events = LightingLifecycleEvidence.validate_lifecycle_order(latest_log)
        if len(events) >= expected_event_count:
            return latest_log
        time.sleep(LIT_PROCESS_POLL_SECONDS)

    expected_events = tuple(event_name for event_name, _ in LAGGED_LIGHTING_LIFECYCLE)
    raise SmokeFailure(f"timed out {stage}: expected accepted lifecycle prefix {expected_events[:expected_event_count]}\n" f"{latest_log[-LOG_TAIL_CHAR_LIMIT:]}")


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(LIT_SELF_TEST, action=LIT_STORE_TRUE, help="Run parser/state-machine checks without starting a graphics application.")
    parser.add_argument("--executable", type=Path, help="Path to nwb_frame_lagged_async_lighting_smoke.")
    parser.add_argument("--runtime-dir", type=Path, help="Cooked smoke runtime root used as the process working directory.")
    parser.add_argument("--logserver-executable", help="Optional path to nwb_logserver/logserver.")
    parser.add_argument("--no-logserver", action=LIT_STORE_TRUE, help="Use standalone loader logs rather than a logserver.")
    parser.add_argument("--window-title", default="NWB Frame Lagged Async Lighting Smoke", help="Native window title used for F1 delivery and graceful exit.",)
    parser.add_argument(LIT_STARTUP_TIMEOUT, type=float, default=45.0, help="Timeout for device creation and window visibility.")
    parser.add_argument(LIT_TRANSITION_TIMEOUT, type=float, default=20.0, help="Timeout for each accepted transition.")
    parser.add_argument("--gpu-validation", action=LIT_STORE_TRUE, help="Pass --gpudbg to the loader process.")
    args = parser.parse_args(argv)

    if args.self_test:
        return args

    missing = [option for option in ("executable", "runtime_dir") if getattr(args, option) is None]
    if missing:
        parser.error(f"missing required arguments: {', '.join('--' + option.replace('_', '-') for option in missing)}")
    require_positive(parser, LIT_STARTUP_TIMEOUT, args.startup_timeout)
    require_positive(parser, LIT_TRANSITION_TIMEOUT, args.transition_timeout)
    args.executable = args.executable.resolve()
    args.runtime_dir = args.runtime_dir.resolve()
    return args


def run(args: argparse.Namespace) -> int:
    if not args.executable.is_file():
        raise SmokeFailure(f"lagged-lighting executable does not exist: {args.executable}")
    if not args.runtime_dir.is_dir():
        raise SmokeFailure(f"lagged-lighting runtime directory does not exist: {args.runtime_dir}")

    launch_args = make_runtime_launch_args(args)
    environment = build_launch_environment(launch_args)
    capture_backend = None
    logserver_process = None
    app_process = None
    log_directory = None
    log_baseline: Mapping[Path, int] = {}
    log_pattern = ""
    app_exit_code = None
    app_exit_tail = ""
    final_log = ""
    window = None
    try:
        capture_backend = create_capture_backend()
        logserver_process, log_port, log_directory, log_baseline, log_pattern = launch_logserver(launch_args, args.executable, environment)
        if not log_directory:
            raise SmokeFailure("frame-lagged async-lighting smoke could not select a runtime-log directory")
        app_process = launch_testbed(launch_args, args.executable, environment, log_port)
        window = capture_backend.wait_for_window(app_process.pid, args.startup_timeout, args.window_title)
        if not window:
            raise SmokeFailure(f"lagged-lighting smoke did not expose the expected window '{args.window_title}'")
        capture_backend.prepare_window(window)
        time.sleep(LIT_WINDOW_SETTLE_SECONDS)

        final_log = wait_for_lifecycle_stage(app_process, log_directory, log_baseline, log_pattern, 1, args.startup_timeout, "while waiting for the first accepted bootstrap",)
        if NO_DEDICATED_ASYNC_COMPUTE not in final_log:
            final_log = wait_for_lifecycle_stage(app_process, log_directory, log_baseline, log_pattern, 2, args.transition_timeout, "while waiting for the first accepted history use",)
        if NO_DEDICATED_ASYNC_COMPUTE not in final_log:
            capture_backend.send_named_key(window, LIT_F1)
            final_log = wait_for_lifecycle_stage(app_process, log_directory, log_baseline, log_pattern, 3, args.transition_timeout, "while waiting for the accepted current-frame path",)
        if NO_DEDICATED_ASYNC_COMPUTE not in final_log:
            capture_backend.send_named_key(window, LIT_F1)
            final_log = wait_for_lifecycle_stage(app_process, log_directory, log_baseline, log_pattern, 4, args.transition_timeout, "while waiting for the second accepted bootstrap",)
        if NO_DEDICATED_ASYNC_COMPUTE not in final_log:
            final_log = wait_for_lifecycle_stage(app_process, log_directory, log_baseline, log_pattern, 5, args.transition_timeout, "while waiting for the second accepted history use",)
        app_exit_code, app_exit_tail = CaptureProcessTermination.terminate_process(app_process, LIT_LAGGED_LIGHTING_SMOKE, window)
        app_process = None
        final_log = shutdown_logserver_and_collect(logserver_process, log_directory, log_baseline, log_pattern, LIT_LAGGED_LIGHTING_SMOKE_LOGSERVER,)
        logserver_process = None
    finally:
        CaptureProcessTermination.terminate_process(app_process, LIT_LAGGED_LIGHTING_SMOKE, window)
        CaptureProcessTermination.terminate_process(logserver_process, LIT_LAGGED_LIGHTING_SMOKE_LOGSERVER)
        if capture_backend:
            capture_backend.close()

    LightingLifecycleEvidence.require_final_run_verdict(final_log, app_exit_code, app_exit_tail)
    print("frame-lagged async-lighting smoke passed: bootstrap -> active history -> current frame -> bootstrap -> active history")
    return 0


def main(argv: Sequence[str]) -> int:
    return run_smoke_main(argv, parse_args, run, self_test=lambda: LightingLifecycleSelfTest.run(sys.modules[__name__]), output_stream=sys.stderr, failure_types=(SmokeFailure,))


if __name__ == LIT_MAIN:
    sys.exit(main(sys.argv[1:]))
