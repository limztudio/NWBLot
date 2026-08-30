#!/usr/bin/env python3
"""Validate the opt-in frame-lagged async-lighting lifecycle on target Vulkan hardware.

The smoke application starts with frame-lagged lighting enabled. This runner waits for accepted bootstrap and
history-use submissions, uses F1 to request the established current-frame path, then re-enables the feature and
requires the second bootstrap/history-use pair. A host without a dedicated compute-only queue reports the accepted
Graphics queue route and exits with the standard smoke skip code instead of claiming async coverage.
"""

from __future__ import annotations

import argparse
import sys
import tempfile
import time
from pathlib import Path
from types import SimpleNamespace
from typing import Mapping, Sequence
from unittest import mock


REPO = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO / "tests" / "smoke"))

from window_capture_smoke import (  # noqa: E402
    SKIP_EXIT_CODE,
    STRICT_LOG_FAILURE_MESSAGES,
    SmokeFailure,
    SmokeSkip,
    build_launch_environment,
    collect_log_delta,
    create_capture_backend,
    ensure_process_running,
    launch_logserver,
    launch_testbed,
    require_normal_process_exit,
    shutdown_logserver_and_collect,
    terminate_process,
)


NO_DEDICATED_ASYNC_COMPUTE = "RendererSystem: frame-lagged async lighting Graphics queue route accepted (no dedicated Compute queue"
BOOTSTRAP_ACCEPTED = "RendererSystem: frame-lagged async lighting bootstrap accepted"
ACTIVE_HISTORY_ACCEPTED = "RendererSystem: frame-lagged async lighting active history accepted"
CURRENT_FRAME_ACCEPTED = "RendererSystem: frame-lagged async lighting current-frame path accepted"
FORBIDDEN_LOG_MESSAGES = (
    *STRICT_LOG_FAILURE_MESSAGES,
    "cannot safely continue after an unresolved frame recovery submission",
    "deferred graph build with optional lagged lighting-history capture failed",
    "deferred lagged lighting-history tail was unavailable",
    "lagged lighting-history capture skipped because its source state was unavailable",
    "graph-owned lagged lighting-history capture record/submission was rejected",
)

# A successful target run has exactly this accepted lifecycle.  Keep the sequence as data so the live poller and
# the no-Vulkan self-test use the same verdict rather than independently counting markers.
LAGGED_LIGHTING_LIFECYCLE = (
    ("bootstrap", BOOTSTRAP_ACCEPTED),
    ("active history", ACTIVE_HISTORY_ACCEPTED),
    ("current frame", CURRENT_FRAME_ACCEPTED),
    ("bootstrap", BOOTSTRAP_ACCEPTED),
    ("active history", ACTIVE_HISTORY_ACCEPTED),
)


class DedicatedComputeUnavailable(SmokeSkip):
    """The application uses its Graphics queue route rather than the opt-in async topology."""


def accepted_lifecycle_events(log_text: str) -> tuple[str, ...]:
    """Return accepted lifecycle markers in log order, including repeated transition types."""
    occurrences: list[tuple[int, str]] = []
    for event_name, marker in (
        ("bootstrap", BOOTSTRAP_ACCEPTED),
        ("active history", ACTIVE_HISTORY_ACCEPTED),
        ("current frame", CURRENT_FRAME_ACCEPTED),
    ):
        offset = 0
        while True:
            found = log_text.find(marker, offset)
            if found < 0:
                break
            occurrences.append((found, event_name))
            offset = found + len(marker)

    occurrences.sort(key=lambda occurrence: occurrence[0])
    return tuple(event_name for _, event_name in occurrences)


def validate_lifecycle_order(log_text: str) -> tuple[str, ...]:
    """Reject an out-of-order or duplicated accepted lifecycle event."""
    expected_events = tuple(event_name for event_name, _ in LAGGED_LIGHTING_LIFECYCLE)
    events = accepted_lifecycle_events(log_text)
    allowed_prefix = expected_events[:len(events)]
    if events != allowed_prefix:
        raise SmokeFailure(
            "frame-lagged async-lighting smoke observed an invalid accepted lifecycle: "
            f"expected prefix {expected_events}, observed {events}"
        )
    return events


def require_lifecycle_stage(log_text: str, expected_event_count: int) -> tuple[str, ...]:
    """Require a valid accepted lifecycle to have reached a specific stage."""
    if expected_event_count < 0 or expected_event_count > len(LAGGED_LIGHTING_LIFECYCLE):
        raise ValueError(f"invalid expected lifecycle event count: {expected_event_count}")

    expected_events = tuple(event_name for event_name, _ in LAGGED_LIGHTING_LIFECYCLE)
    events = validate_lifecycle_order(log_text)
    if len(events) < expected_event_count:
        raise SmokeFailure(
            "frame-lagged async-lighting smoke has not reached the required accepted lifecycle stage: "
            f"expected {expected_events[:expected_event_count]}, observed {events}"
        )
    return events


def reject_forbidden_messages(log_text: str) -> None:
    rejected = [marker for marker in FORBIDDEN_LOG_MESSAGES if marker in log_text]
    if rejected:
        raise SmokeFailure(f"frame-lagged async-lighting smoke found forbidden log messages: {rejected}")


def require_final_run_verdict(log_text: str, app_exit_code: int | None, app_exit_tail: str) -> None:
    """Validate shutdown and complete diagnostics before classifying an unavailable async topology."""
    require_normal_process_exit(app_exit_code, app_exit_tail, "testbed")
    reject_forbidden_messages(log_text)
    if NO_DEDICATED_ASYNC_COMPUTE in log_text:
        raise DedicatedComputeUnavailable(
            "frame-lagged async-lighting smoke skipped: the requested feature accepted the Graphics queue route "
            "because this adapter has no dedicated Compute queue"
        )
    require_lifecycle_stage(log_text, len(LAGGED_LIGHTING_LIFECYCLE))


def wait_for_lifecycle_stage(
    process,
    log_directory: Path,
    log_baseline: Mapping[Path, int],
    log_pattern: str,
    expected_event_count: int,
    timeout_seconds: float,
    stage: str,
) -> str:
    deadline = time.monotonic() + timeout_seconds
    latest_log = ""
    while time.monotonic() < deadline:
        ensure_process_running(process, stage)
        latest_log = collect_log_delta(log_directory, log_baseline, log_pattern)
        reject_forbidden_messages(latest_log)
        if NO_DEDICATED_ASYNC_COMPUTE in latest_log:
            return latest_log
        events = validate_lifecycle_order(latest_log)
        if len(events) >= expected_event_count:
            return latest_log
        time.sleep(0.1)

    expected_events = tuple(event_name for event_name, _ in LAGGED_LIGHTING_LIFECYCLE)
    raise SmokeFailure(
        f"timed out {stage}: expected accepted lifecycle prefix {expected_events[:expected_event_count]}\n"
        f"{latest_log[-4000:]}"
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
    window = None
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
        capture_backend.prepare_window(window)
        time.sleep(0.1)

        final_log = wait_for_lifecycle_stage(
            app_process,
            log_directory,
            log_baseline,
            log_pattern,
            1,
            args.startup_timeout,
            "while waiting for the first accepted bootstrap",
        )
        if NO_DEDICATED_ASYNC_COMPUTE not in final_log:
            final_log = wait_for_lifecycle_stage(
                app_process,
                log_directory,
                log_baseline,
                log_pattern,
                2,
                args.transition_timeout,
                "while waiting for the first accepted history use",
            )
        if NO_DEDICATED_ASYNC_COMPUTE not in final_log:
            capture_backend.send_named_key(window, "F1")
            final_log = wait_for_lifecycle_stage(
                app_process,
                log_directory,
                log_baseline,
                log_pattern,
                3,
                args.transition_timeout,
                "while waiting for the accepted current-frame path",
            )
        if NO_DEDICATED_ASYNC_COMPUTE not in final_log:
            capture_backend.send_named_key(window, "F1")
            final_log = wait_for_lifecycle_stage(
                app_process,
                log_directory,
                log_baseline,
                log_pattern,
                4,
                args.transition_timeout,
                "while waiting for the second accepted bootstrap",
            )
        if NO_DEDICATED_ASYNC_COMPUTE not in final_log:
            final_log = wait_for_lifecycle_stage(
                app_process,
                log_directory,
                log_baseline,
                log_pattern,
                5,
                args.transition_timeout,
                "while waiting for the second accepted history use",
            )
        app_exit_code, app_exit_tail = terminate_process(app_process, "lagged-lighting smoke", window)
        app_process = None
        final_log = shutdown_logserver_and_collect(
            logserver_process,
            log_directory,
            log_baseline,
            log_pattern,
            "lagged-lighting smoke logserver",
        )
        logserver_process = None
    finally:
        terminate_process(app_process, "lagged-lighting smoke", window)
        terminate_process(logserver_process, "lagged-lighting smoke logserver")
        if capture_backend:
            capture_backend.close()

    require_final_run_verdict(final_log, app_exit_code, app_exit_tail)
    print("frame-lagged async-lighting smoke passed: bootstrap -> active history -> current frame -> bootstrap -> active history")
    return 0


def run_self_test() -> int:
    assert FORBIDDEN_LOG_MESSAGES[:len(STRICT_LOG_FAILURE_MESSAGES)] == STRICT_LOG_FAILURE_MESSAGES
    log = "\n".join((
        f"{BOOTSTRAP_ACCEPTED} (target generation 7)",
        f"{ACTIVE_HISTORY_ACCEPTED} (target generation 7)",
        f"{CURRENT_FRAME_ACCEPTED} (target generation 7)",
        f"{BOOTSTRAP_ACCEPTED} (target generation 7)",
        f"{ACTIVE_HISTORY_ACCEPTED} (target generation 7)",
    ))
    assert accepted_lifecycle_events(log) == tuple(event_name for event_name, _ in LAGGED_LIGHTING_LIFECYCLE)
    assert validate_lifecycle_order(log) == accepted_lifecycle_events(log)
    assert require_lifecycle_stage(log, len(LAGGED_LIGHTING_LIFECYCLE)) == accepted_lifecycle_events(log)
    for invalid_log in (
        "\n".join((
            f"{ACTIVE_HISTORY_ACCEPTED} (target generation 7)",
            f"{BOOTSTRAP_ACCEPTED} (target generation 7)",
        )),
        "\n".join((
            f"{BOOTSTRAP_ACCEPTED} (target generation 7)",
            f"{BOOTSTRAP_ACCEPTED} (target generation 7)",
        )),
    ):
        try:
            validate_lifecycle_order(invalid_log)
        except SmokeFailure:
            pass
        else:
            raise AssertionError("invalid accepted lifecycle was not rejected")
    try:
        require_lifecycle_stage(log.splitlines()[0], len(LAGGED_LIGHTING_LIFECYCLE))
    except SmokeFailure:
        pass
    else:
        raise AssertionError("incomplete accepted lifecycle was not rejected")
    try:
        require_lifecycle_stage(log, len(LAGGED_LIGHTING_LIFECYCLE) + 1)
    except ValueError:
        pass
    else:
        raise AssertionError("invalid lifecycle stage count was not rejected")
    reject_forbidden_messages(log)
    assert NO_DEDICATED_ASYNC_COMPUTE not in log
    graphics_route_log = f"{NO_DEDICATED_ASYNC_COMPUTE}, target generation 3)"
    assert NO_DEDICATED_ASYNC_COMPUTE in graphics_route_log
    try:
        require_final_run_verdict(f"{graphics_route_log}\n[ERROR] simulated validation failure", 0, "")
    except SmokeFailure:
        pass
    else:
        raise AssertionError("a forbidden error accompanying the Graphics queue route was incorrectly skipped")
    current_history_warnings = (
        "deferred graph build with optional lagged lighting-history capture failed; retrying without the tail",
        "deferred lagged lighting-history tail was unavailable; reverting to current-frame lighting",
        "lagged lighting-history capture skipped because its source state was unavailable",
        "graph-owned lagged lighting-history capture record/submission was rejected; reverting to current-frame lighting",
    )
    for warning in current_history_warnings:
        try:
            require_final_run_verdict(f"{log}\n{warning}", 0, "")
        except SmokeFailure:
            pass
        else:
            raise AssertionError(f"current lagged lighting-history warning was not rejected: {warning}")
    try:
        require_final_run_verdict(graphics_route_log, 0, "")
    except DedicatedComputeUnavailable:
        pass
    else:
        raise AssertionError("a clean Graphics queue route was not classified as a topology skip")
    try:
        require_final_run_verdict(graphics_route_log, 9, "simulated abnormal exit after the topology marker")
    except SmokeFailure:
        pass
    else:
        raise AssertionError("an abnormal exit after the Graphics queue route was incorrectly skipped")

    module = sys.modules[__name__]
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        executable = root / "orchestration.exe"
        executable.write_bytes(b"exe")
        args = parse_args(["--self-test"])
        args.executable = executable
        args.runtime_dir = root
        app = SimpleNamespace(pid=4321, poll=lambda: None)
        logserver = object()
        baseline = {root / "old.log": 7}
        backend = mock.Mock()
        backend.wait_for_window.return_value = 17
        events = []

        def terminate(process, name, window_handle=None):
            if process is app:
                events.append(("app-stop", name, window_handle))
                return 7, "simulated abnormal exit"
            assert process is None
            events.append(("cleanup-none", name, window_handle))
            return None, ""

        def shutdown(process, log_directory, received_baseline, pattern, shutdown_name="logserver"):
            assert process is logserver
            assert log_directory == root
            assert received_baseline == baseline
            assert pattern == "logserver_*.log"
            assert events == [("app-stop", "lagged-lighting smoke", 17)]
            events.append(("logserver-helper", shutdown_name))
            return NO_DEDICATED_ASYNC_COMPUTE

        with mock.patch.object(module, "build_launch_environment", return_value={}), \
             mock.patch.object(module, "create_capture_backend", return_value=backend), \
             mock.patch.object(module, "launch_logserver", return_value=(logserver, 49152, root, baseline, "logserver_*.log")), \
             mock.patch.object(module, "launch_testbed", return_value=app), \
             mock.patch.object(module, "wait_for_lifecycle_stage", return_value=NO_DEDICATED_ASYNC_COMPUTE), \
             mock.patch.object(module.time, "sleep"), \
             mock.patch.object(module, "terminate_process", side_effect=terminate) as terminate_mock, \
             mock.patch.object(module, "shutdown_logserver_and_collect", side_effect=shutdown) as shutdown_mock:
            try:
                run(args)
            except SmokeFailure as error:
                assert "exit 7" in str(error)
            else:
                raise AssertionError("lagged-lighting orchestration accepted an abnormal Testbed exit")

        assert events == [
            ("app-stop", "lagged-lighting smoke", 17),
            ("logserver-helper", "lagged-lighting smoke logserver"),
            ("cleanup-none", "lagged-lighting smoke", 17),
            ("cleanup-none", "lagged-lighting smoke logserver", None),
        ]
        assert terminate_mock.mock_calls == [
            mock.call(app, "lagged-lighting smoke", 17),
            mock.call(None, "lagged-lighting smoke", 17),
            mock.call(None, "lagged-lighting smoke logserver"),
        ]
        shutdown_mock.assert_called_once_with(
            logserver, root, baseline, "logserver_*.log", "lagged-lighting smoke logserver"
        )
        backend.close.assert_called_once_with()

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
