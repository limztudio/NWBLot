#!/usr/bin/env python3
"""Validate the opt-in frame-lagged async-lighting lifecycle on target Vulkan hardware.

The smoke application starts with frame-lagged lighting enabled. This runner waits for accepted bootstrap and
history-use submissions, uses F1 to request the established current-frame path, then re-enables the feature and
requires the second bootstrap/history-use pair. A host without a dedicated compute-only queue reports the accepted
Graphics fallback and exits with the standard smoke skip code instead of claiming async coverage.
"""

from __future__ import annotations

import argparse
import sys
import time
from pathlib import Path
from types import SimpleNamespace
from typing import Mapping, Sequence


NWB_LAUNCH_COMMAND = "frame-lagged-async-lighting"
REPO = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO / "tests" / "smoke"))

from testbed_window_capture_smoke import (  # noqa: E402
    SKIP_EXIT_CODE,
    SmokeFailure,
    SmokeSkip,
    build_launch_environment,
    collect_log_delta,
    create_capture_backend,
    ensure_process_running,
    launch_logserver,
    launch_testbed,
    require_normal_testbed_exit,
    terminate_process,
)


NO_DEDICATED_ASYNC_COMPUTE = "RendererSystem: frame-lagged async lighting fallback accepted (no dedicated AsyncCompute lane"
BOOTSTRAP_ACCEPTED = "RendererSystem: frame-lagged async lighting bootstrap accepted"
ACTIVE_HISTORY_ACCEPTED = "RendererSystem: frame-lagged async lighting active history accepted"
CURRENT_FRAME_FALLBACK_ACCEPTED = "RendererSystem: frame-lagged async lighting current-frame fallback accepted"
FORBIDDEN_LOG_MESSAGES = (
    "[ERROR]",
    "VUID-",
    "Validation Error",
    "cannot safely continue after an unresolved async Compute recovery join",
    "failed to record lagged lighting-history capture",
    "lagged lighting-history capture submission was rejected",
)


class DedicatedComputeUnavailable(SmokeSkip):
    """The application accepted its mandated Graphics fallback rather than running the opt-in async topology."""


def marker_count(log_text: str, marker: str) -> int:
    return log_text.count(marker)


def reject_forbidden_messages(log_text: str) -> None:
    rejected = [marker for marker in FORBIDDEN_LOG_MESSAGES if marker in log_text]
    if rejected:
        raise SmokeFailure(f"frame-lagged async-lighting smoke found forbidden log messages: {rejected}")


def wait_for_marker(
    process,
    log_directory: Path,
    log_baseline: Mapping[Path, int],
    log_pattern: str,
    marker: str,
    expected_count: int,
    timeout_seconds: float,
    stage: str,
) -> str:
    deadline = time.monotonic() + timeout_seconds
    latest_log = ""
    while time.monotonic() < deadline:
        ensure_process_running(process, stage)
        latest_log = collect_log_delta(log_directory, log_baseline, log_pattern)
        if NO_DEDICATED_ASYNC_COMPUTE in latest_log:
            raise DedicatedComputeUnavailable(
                "frame-lagged async-lighting smoke skipped: the requested feature accepted the Graphics fallback "
                "because this adapter has no dedicated AsyncCompute lane"
            )
        reject_forbidden_messages(latest_log)
        if marker_count(latest_log, marker) >= expected_count:
            return latest_log
        time.sleep(0.1)

    raise SmokeFailure(
        f"timed out {stage}: expected {expected_count} occurrence(s) of '{marker}'\n{latest_log[-4000:]}"
    )


def make_launch_args(args: argparse.Namespace) -> SimpleNamespace:
    return SimpleNamespace(
        no_logserver=args.no_logserver,
        logserver_executable=args.logserver_executable,
        log_port=0,
        working_directory=args.runtime_dir,
        timeout=args.startup_timeout,
        application_arg=["--gpudbg"] if args.gpu_validation else [],
        software_vulkan="off",
    )


def require_positive(parser: argparse.ArgumentParser, option: str, value: float) -> None:
    if value <= 0.0:
        parser.error(f"{option} must be positive")


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--self-test", action="store_true", help="Run parser/state-machine checks without Vulkan.")
    parser.add_argument("--executable", type=Path, help="Path to nwb_frame_lagged_async_lighting_smoke.")
    parser.add_argument("--runtime-dir", type=Path, help="Cooked smoke runtime root used as the process working directory.")
    parser.add_argument("--logserver-executable", help="Optional path to nwb_logserver/logserver.")
    parser.add_argument("--no-logserver", action="store_true", help="Use standalone loader logs rather than a logserver.")
    parser.add_argument(
        "--window-title",
        default="NWB Frame Lagged Async Lighting Smoke",
        help="Native window title used for F1 delivery and graceful exit.",
    )
    parser.add_argument("--startup-timeout", type=float, default=45.0, help="Timeout for device creation and window visibility.")
    parser.add_argument("--transition-timeout", type=float, default=20.0, help="Timeout for each accepted transition.")
    parser.add_argument("--gpu-validation", action="store_true", help="Pass --gpudbg to the loader process.")
    args = parser.parse_args(argv)

    if args.self_test:
        return args

    missing = [option for option in ("executable", "runtime_dir") if getattr(args, option) is None]
    if missing:
        parser.error(f"missing required arguments: {', '.join('--' + option.replace('_', '-') for option in missing)}")
    require_positive(parser, "--startup-timeout", args.startup_timeout)
    require_positive(parser, "--transition-timeout", args.transition_timeout)
    args.executable = args.executable.resolve()
    args.runtime_dir = args.runtime_dir.resolve()
    return args


def run(args: argparse.Namespace) -> int:
    if not args.executable.is_file():
        raise SmokeFailure(f"lagged-lighting executable does not exist: {args.executable}")
    if not args.runtime_dir.is_dir():
        raise SmokeFailure(f"lagged-lighting runtime directory does not exist: {args.runtime_dir}")

    launch_args = make_launch_args(args)
    environment = build_launch_environment(launch_args)
    environment["NWB_RENDER_UNFOCUSED"] = "1"
    capture_backend = None
    logserver_process = None
    app_process = None
    log_directory = None
    log_baseline: Mapping[Path, int] = {}
    log_pattern = ""
    app_exit_code = None
    app_exit_tail = ""
    final_log = ""
    try:
        capture_backend = create_capture_backend()
        logserver_process, log_port, log_directory, log_baseline, log_pattern = launch_logserver(
            launch_args, args.executable, environment
        )
        if not log_directory:
            raise SmokeFailure("frame-lagged async-lighting smoke could not select a runtime-log directory")
        app_process = launch_testbed(launch_args, args.executable, environment, log_port)
        window = capture_backend.wait_for_window(app_process.pid, args.startup_timeout, args.window_title)
        if not window:
            raise SmokeFailure(f"lagged-lighting smoke did not expose the expected window '{args.window_title}'")

        final_log = wait_for_marker(
            app_process,
            log_directory,
            log_baseline,
            log_pattern,
            ACTIVE_HISTORY_ACCEPTED,
            1,
            args.startup_timeout,
            "while waiting for the first accepted history use",
        )

        capture_backend.send_named_key(window, "F1")
        final_log = wait_for_marker(
            app_process,
            log_directory,
            log_baseline,
            log_pattern,
            CURRENT_FRAME_FALLBACK_ACCEPTED,
            1,
            args.transition_timeout,
            "while waiting for the accepted current-frame fallback",
        )

        capture_backend.send_named_key(window, "F1")
        final_log = wait_for_marker(
            app_process,
            log_directory,
            log_baseline,
            log_pattern,
            BOOTSTRAP_ACCEPTED,
            2,
            args.transition_timeout,
            "while waiting for the second accepted bootstrap",
        )
        final_log = wait_for_marker(
            app_process,
            log_directory,
            log_baseline,
            log_pattern,
            ACTIVE_HISTORY_ACCEPTED,
            2,
            args.transition_timeout,
            "while waiting for the second accepted history use",
        )
    finally:
        if app_process:
            app_exit_code, app_exit_tail = terminate_process(app_process, "lagged-lighting smoke", args.window_title)
        # Let the graceful window close send its last logger records before observing the delta one final time.
        time.sleep(0.25)
        if log_directory:
            final_log = collect_log_delta(log_directory, log_baseline, log_pattern)
        terminate_process(logserver_process, "lagged-lighting smoke logserver")
        if capture_backend:
            capture_backend.close()

    require_normal_testbed_exit(app_exit_code, app_exit_tail)
    if NO_DEDICATED_ASYNC_COMPUTE in final_log:
        raise DedicatedComputeUnavailable(
            "frame-lagged async-lighting smoke skipped: the requested feature accepted the Graphics fallback "
            "because this adapter has no dedicated AsyncCompute lane"
        )
    reject_forbidden_messages(final_log)
    for marker, count in ((BOOTSTRAP_ACCEPTED, 2), (ACTIVE_HISTORY_ACCEPTED, 2), (CURRENT_FRAME_FALLBACK_ACCEPTED, 1)):
        if marker_count(final_log, marker) < count:
            raise SmokeFailure(f"lagged-lighting smoke completed without {count} occurrence(s) of '{marker}'")
    print("frame-lagged async-lighting smoke passed: bootstrap -> active history -> fallback -> bootstrap -> active history")
    return 0


def run_self_test() -> int:
    log = "\n".join((
        f"{BOOTSTRAP_ACCEPTED} (target generation 7)",
        f"{ACTIVE_HISTORY_ACCEPTED} (target generation 7)",
        f"{CURRENT_FRAME_FALLBACK_ACCEPTED} (target generation 7)",
        f"{BOOTSTRAP_ACCEPTED} (target generation 7)",
        f"{ACTIVE_HISTORY_ACCEPTED} (target generation 7)",
    ))
    assert marker_count(log, BOOTSTRAP_ACCEPTED) == 2
    assert marker_count(log, ACTIVE_HISTORY_ACCEPTED) == 2
    assert marker_count(log, CURRENT_FRAME_FALLBACK_ACCEPTED) == 1
    reject_forbidden_messages(log)
    assert NO_DEDICATED_ASYNC_COMPUTE not in log
    fallback_log = f"{NO_DEDICATED_ASYNC_COMPUTE}, target generation 3)"
    assert NO_DEDICATED_ASYNC_COMPUTE in fallback_log
    print("frame-lagged async-lighting harness self-test passed")
    return 0


def main(argv: Sequence[str]) -> int:
    args = parse_args(argv)
    if args.self_test:
        return run_self_test()
    try:
        return run(args)
    except DedicatedComputeUnavailable as error:
        print(f"SKIP: {error}", file=sys.stderr)
        return SKIP_EXIT_CODE
    except SmokeSkip as error:
        print(f"SKIP: {error}", file=sys.stderr)
        return SKIP_EXIT_CODE
    except SmokeFailure as error:
        print(f"FAIL: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
