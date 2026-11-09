#!/usr/bin/env python3
import fnmatch
import os
from pathlib import Path
from types import SimpleNamespace
import platform
import secrets
import subprocess
import sys
import time
from smoke_failure_reporting import write_failure_report
from window_capture_arguments import WindowCaptureArguments
from window_capture_process_closer import WindowProcessCloser
from window_capture_output_reader import CaptureOutputReader
from window_capture_process_output import CapturedProcessOutput
from window_capture_termination import CaptureProcessTermination
from window_capture_frame import WindowFrameCapture
from window_capture_outcome import SmokeFailure, SmokeSkip, write_status  # noqa: E402
from window_capture_image import TRANSPARENT_CSG_REGIONS, read_bmp_24, validate_surfel_coverage_probe  # noqa: E402
from window_capture_x11 import LinuxX11Capture  # noqa: E402
from window_capture_win32 import WindowsCapture  # noqa: E402
from window_capture_logs import READINESS_OPTION, ScopedLogBaseline, choose_free_port, collect_log_delta, full_log_scope, snapshot_log_files, standalone_log_pattern, wait_for_log_drain, wait_for_log_message, wait_for_tcp_port  # noqa: E402
from window_capture_environment import application_capture_partial_path, build_launch_environment, executable_sibling, prepare_application_capture_environment  # noqa: E402
from window_capture_validation import validate_application_capture_log_text, validate_capture_for_args, validate_expected_log_messages  # noqa: E402


LIT_SHORT_POLL_SECONDS = 0.05
LIT_LOGSERVER_START_TIMEOUT_SECONDS = 10.0

LIT_FRONT = "front"
LIT_SIDE = "side"

LIT_UTF_8 = "utf-8"
LIT_WINDOWS = "Windows"
LIT_LINUX = "Linux"
LIT_NWB_OUTPUT_CAPTURE = "_nwb_output_capture"
LIT_RESIZE_CLIENT = "resize_client"
LIT_LOGSERVER = "logserver"
LIT_P = "-p"
LIT_A = "-a"
LIT_TESTBED = "testbed"
LIT_BEFORE_LOG_COLLECTION = "before log collection"
LIT_TIMEOUT = "--timeout"
LIT_MAIN = "__main__"


SKIP_EXIT_CODE = 77
STRICT_LOG_FAILURE_MESSAGES = ("[WARNING]", "[CRITICAL WARNING]", "[ASSERT]", "[ERROR]", "[FATAL]", "VUID-", "Validation Error", "failed to resolve shader",)
FRAMEBUFFER_CAPTURE_PATH_ENV = "NWB_SMOKE_FRAMEBUFFER_CAPTURE_PATH"
FRAMEBUFFER_CAPTURE_READY_MESSAGE = "FramebufferCapture: capture ready"
FRAMEBUFFER_CAPTURE_SKIP_MESSAGE = "FramebufferCapture: skipped because swap-chain transfer-source usage is unavailable"


def run_smoke_main(argv, parse_args, run, *, default_timeout=None, self_test=None, output_stream=None, failure_types=(SmokeFailure, OSError, ValueError, TimeoutError)):
    """Apply common smoke CLI timeout and exit-status handling."""
    if default_timeout is not None and not any(argument == LIT_TIMEOUT or argument.startswith("--timeout=") for argument in argv):
        argv = [*argv, LIT_TIMEOUT, str(default_timeout)]
    args = parse_args(argv)
    if self_test is not None and args.self_test:
        return self_test()
    try:
        return run(args)
    except SmokeSkip as error:
        print(f"SKIP: {error}", file=output_stream, flush=True)
        return SKIP_EXIT_CODE
    except failure_types as error:
        print(f"FAIL: {error}", file=output_stream, flush=True)
        return 1





















def finish_logserver_cleanup(logserver, output, errors, primary_failure, terminate, write_report):
    try:
        terminate(logserver, LIT_LOGSERVER)
    except Exception as error:
        errors.append(f"logger cleanup: {error}")
    if errors:
        write_status("; ".join(errors))
        write_failure_report(write_report, output / "cleanup_error.json", {"errors": errors})
        if not primary_failure:
            raise SmokeFailure("; ".join(errors))


def cleanup_capture_processes(testbed, handle, logserver, primary_error):
    failures = []
    retained = []
    for process, name, window in ((testbed, LIT_TESTBED, handle), (logserver, LIT_LOGSERVER, None)):
        if process is None:
            continue
        capture = getattr(process, LIT_NWB_OUTPUT_CAPTURE, None)
        if capture is not None:
            retained.append((name, capture))
        try:
            CaptureProcessTermination.terminate_process(process, name, window, preserve_capture=True)
        except Exception as error:
            failures.append((name, error))
    if primary_error is not None or failures:
        for name, capture in retained:
            CapturedProcessOutput.report_secondary_failure(f"{name} output retained at '{capture.path}'")
        for name, error in failures if primary_error is not None else failures[1:]:
            CapturedProcessOutput.report_secondary_failure(f"{name} cleanup also failed: {error}")
        if primary_error is None:
            raise failures[0][1]
        return
    for _, capture in retained:
        CapturedProcessOutput.dispose_process_output_capture(capture)









def finish_observed_output(skip_reason, observed):
    if skip_reason and observed:
        message, path = observed[0]
        raise SmokeFailure(f"capability skip also contained blocking process output '{message}' in '{path}'")
    for path in {path for _, path in observed}:
        try:
            path.unlink()
        except OSError as error:
            raise SmokeFailure(f"failed to remove process output capture '{path}': {error}") from error








def window_capture_source_paths():
    directory = Path(__file__).resolve().parent
    with os.scandir(directory) as entries:
        paths = [directory / entry.name for entry in entries if fnmatch.fnmatch(entry.name, "window_capture_*.py")]
    paths.append(directory / "smoke_failure_reporting.py")
    paths.append(directory.parents[1] / "launcher" / "readiness.py")
    return tuple(sorted(paths))


def create_capture_backend():
    system = platform.system()
    if system == LIT_LINUX:
        return LinuxX11Capture()
    if system == LIT_WINDOWS:
        return WindowsCapture()
    raise SmokeSkip(f"window handle capture is not implemented for {system}")


def launch_logserver(args, executable, env):
    if args.no_logserver:
        log_directory = executable.resolve().parent
        log_pattern = standalone_log_pattern(executable)
        return None, None, log_directory, snapshot_log_files(log_directory, log_pattern), log_pattern

    # Resolve relative executable paths before Popen changes to the runtime directory.
    logserver = Path(args.logserver_executable).resolve() if args.logserver_executable else executable_sibling(executable, LIT_LOGSERVER)
    if not logserver:
        write_status("INFO: logserver executable was not found; using standalone loader logs")
        log_directory = executable.resolve().parent
        log_pattern = standalone_log_pattern(executable)
        return None, None, log_directory, snapshot_log_files(log_directory, log_pattern), log_pattern

    log_directory = logserver.resolve().parent
    log_pattern = "logserver_*.log"
    log_baseline = snapshot_log_files(log_directory, log_pattern)
    port = args.log_port if args.log_port else choose_free_port()
    token = secrets.token_hex(16)
    process = CapturedProcessOutput.launch_captured_process([str(logserver), LIT_P, str(port), READINESS_OPTION, token], args.working_directory, env, LIT_LOGSERVER,)
    try:
        wait_for_tcp_port(port, min(args.timeout, LIT_LOGSERVER_START_TIMEOUT_SECONDS), process, token)
    except BaseException as error:
        capture = getattr(process, LIT_NWB_OUTPUT_CAPTURE, None)
        try:
            _, tail = CaptureProcessTermination.terminate_process(process, LIT_LOGSERVER, preserve_capture=True)
            if capture is not None:
                CapturedProcessOutput.report_secondary_failure(f"logger output retained at '{capture.path}'")
        except (SmokeFailure, OSError, subprocess.SubprocessError) as cleanup_error:
            if not isinstance(error, Exception):
                CapturedProcessOutput.report_secondary_failure(f"logger cleanup after cancellation failed: {cleanup_error}")
                raise error from cleanup_error
            raise SmokeFailure(f"{error}\nlogger cleanup: {cleanup_error}") from error
        if not isinstance(error, Exception):
            raise
        if tail:
            raise SmokeFailure(f"{error}\n{tail}") from error
        raise

    # Poll the owned server; shutdown still checks the full log baseline.
    log_baseline = ScopedLogBaseline(log_baseline, log_pattern, process.pid)
    return process, port, log_directory, log_baseline, log_baseline.live_pattern


def launch_testbed(args, executable, env, log_port):
    command = [str(executable)]
    if log_port:
        command.extend([LIT_A, "http://localhost", LIT_P, str(log_port)])
    else:
        command.extend([LIT_A, "", LIT_P, "0"])
    command.extend(args.application_arg)

    return CapturedProcessOutput.launch_captured_process(command, args.working_directory, env, LIT_TESTBED,)


def wait_for_application_capture_exit(process, output_path, timeout_seconds):
    deadline = time.monotonic() + timeout_seconds
    artifact_observed = False
    while time.monotonic() < deadline:
        artifact_observed = artifact_observed or output_path.is_file()
        exit_code = process.poll()
        if exit_code is not None:
            return exit_code, artifact_observed or output_path.is_file()
        time.sleep(LIT_SHORT_POLL_SECONDS)

    artifact_state = "was written" if artifact_observed or output_path.is_file() else "was not written"
    raise SmokeFailure(f"timed out waiting for testbed to self-exit after application capture; artifact {artifact_state}")


def ensure_process_running(process, stage, process_name=LIT_TESTBED):
    if process is None or process.poll() is None:
        return

    tail = CaptureOutputReader.read_process_tail(process)
    raise SmokeFailure(f"{process_name} exited {stage} (exit {process.returncode})\n{tail}")


def require_normal_process_exit(exit_code, tail, process_name):
    if exit_code == 0:
        return

    if exit_code is None:
        raise SmokeFailure(f"{process_name} did not exit during shutdown")

    detail = f"\n{tail}" if tail else ""
    raise SmokeFailure(f"{process_name} exited during graceful shutdown (exit {exit_code}){detail}")


def shutdown_logserver_and_collect(process, directory, baseline, pattern, shutdown_name=LIT_LOGSERVER, *, rejected_output=(), observed_output=None, observed_needles=()):
    if process is not None:
        ensure_process_running(process, LIT_BEFORE_LOG_COLLECTION, LIT_LOGSERVER)
        if rejected_output or observed_output is not None:
            exit_code, tail = CaptureProcessTermination.terminate_process(process, shutdown_name, rejected_output=rejected_output, observed_output=observed_output, observed_needles=observed_needles)
        else:
            exit_code, tail = CaptureProcessTermination.terminate_process(process, shutdown_name)
        require_normal_process_exit(exit_code, tail, LIT_LOGSERVER)
    wait_for_log_drain(directory, baseline, pattern)
    baseline, pattern = full_log_scope(baseline, pattern)
    return collect_log_delta(directory, baseline, pattern) if directory else ""










def launch_and_capture(args, backend):
    executable = Path(args.executable).resolve()
    if not executable.exists():
        raise SmokeFailure(f"executable does not exist: {executable}")

    env = build_launch_environment(args)
    rejected_output = tuple(getattr(args, "reject_log_message", ()))
    observed_needles = tuple(getattr(args, "skip_blocking_log_message", ()))
    observed_output = []
    logserver_process = None
    testbed_process = None
    result = None
    handle = None
    testbed_exit_code = None
    testbed_exit_tail = ""
    skip_reason = None
    try:
        logserver_process, log_port, log_directory, log_baseline, log_pattern = launch_logserver(args, executable, env)
        testbed_process = launch_testbed(args, executable, env, log_port)
        handle = backend.wait_for_window(testbed_process.pid, args.timeout, args.window_title)
        if not handle:
            exit_code = testbed_process.poll()
            if exit_code is not None:
                tail = CaptureOutputReader.read_process_tail(testbed_process)
                raise SmokeFailure(f"testbed exited before a window was visible (exit {exit_code})\n{tail}")
            raise SmokeFailure("timed out waiting for a visible testbed window")

        if getattr(args, LIT_RESIZE_CLIENT, None):
            result = WindowFrameCapture(ensure_process_running).capture_resized_window(args, backend, handle, testbed_process, log_directory, log_baseline, log_pattern,)
        else:
            time.sleep(args.settle_seconds)
            ensure_process_running(testbed_process, "before checked capture")
            result = WindowFrameCapture(ensure_process_running).capture_render_ready_window(args, backend, handle, testbed_process)
        testbed_exit_code, testbed_exit_tail = CaptureProcessTermination.terminate_process(testbed_process, LIT_TESTBED, handle, rejected_output=rejected_output, observed_output=observed_output, observed_needles=observed_needles, graceful_exit_timeout=args.graceful_exit_timeout)
        testbed_process = None
        require_normal_process_exit(testbed_exit_code, testbed_exit_tail, LIT_TESTBED)
        if logserver_process is not None:
            ensure_process_running(logserver_process, LIT_BEFORE_LOG_COLLECTION, LIT_LOGSERVER)
            logserver_exit_code, logserver_exit_tail = CaptureProcessTermination.terminate_process(logserver_process, LIT_LOGSERVER, rejected_output=rejected_output, observed_output=observed_output, observed_needles=observed_needles)
            logserver_process = None
            require_normal_process_exit(logserver_exit_code, logserver_exit_tail, LIT_LOGSERVER)
        wait_for_log_drain(log_directory, log_baseline, log_pattern)
        skip_reason = validate_expected_log_messages(log_directory, log_baseline, log_pattern, args.expect_log_message, args.reject_log_message, args.skip_log_message, args.skip_blocking_log_message,)
    finally:
        cleanup_capture_processes(testbed_process, handle, logserver_process, sys.exc_info()[1])

    finish_observed_output(skip_reason, observed_output)
    if skip_reason:
        raise SmokeSkip(skip_reason)
    return result


def launch_and_capture_application(args):
    executable = Path(args.executable).resolve()
    if not executable.exists():
        raise SmokeFailure(f"executable does not exist: {executable}")

    env = prepare_application_capture_environment(args, build_launch_environment(args))
    rejected_output = tuple(getattr(args, "reject_log_message", ()))
    observed_needles = tuple(getattr(args, "skip_blocking_log_message", ()))
    observed_output = []
    partial_path = application_capture_partial_path(args.output)
    logserver_process = None
    testbed_process = None
    testbed_exit_code = None
    testbed_exit_tail = ""
    wait_failure = None
    artifact_observed = False
    log_text = ""
    try:
        logserver_process, log_port, log_directory, log_baseline, log_pattern = launch_logserver(args, executable, env,)
        testbed_process = launch_testbed(args, executable, env, log_port)
        try:
            testbed_exit_code, artifact_observed = wait_for_application_capture_exit(testbed_process, args.output, args.timeout,)
        except SmokeFailure as error:
            wait_failure = error

        try:
            testbed_exit_code, testbed_exit_tail = CaptureProcessTermination.terminate_process(testbed_process, LIT_TESTBED, rejected_output=rejected_output, observed_output=observed_output, observed_needles=observed_needles, graceful_exit_timeout=args.graceful_exit_timeout)
            testbed_process = None
            log_text = shutdown_logserver_and_collect(logserver_process, log_directory, log_baseline, log_pattern, rejected_output=rejected_output, observed_output=observed_output, observed_needles=observed_needles)
            logserver_process = None
        except Exception as cleanup_error:
            if wait_failure is None:
                raise
            CapturedProcessOutput.report_secondary_failure(f"capture cleanup failed while handling {wait_failure}: {cleanup_error}")
            raise wait_failure from cleanup_error
    finally:
        cleanup_capture_processes(testbed_process, None, logserver_process, sys.exc_info()[1] or wait_failure)

    log_output = getattr(args, "log_output", None)
    if log_output:
        log_output.parent.mkdir(parents=True, exist_ok=True)
        log_output.write_bytes(log_text.encode(LIT_UTF_8))
    if wait_failure:
        raise wait_failure
    if testbed_exit_code != 0:
        detail = f"\n{testbed_exit_tail}" if testbed_exit_tail else ""
        raise SmokeFailure(f"testbed self-exited during application capture (exit {testbed_exit_code}){detail}")

    skip_reason = validate_application_capture_log_text(log_text, args)
    finish_observed_output(skip_reason, observed_output)
    if skip_reason:
        raise SmokeSkip(skip_reason)
    if not args.output.is_file():
        observation = " after being observed" if artifact_observed else ""
        raise SmokeFailure(f"application capture artifact '{args.output}' is missing{observation}")
    if partial_path.exists():
        raise SmokeFailure(f"application capture left incomplete artifact '{partial_path}'")

    result = read_bmp_24(args.output)
    validate_capture_for_args(args, result)
    return result




def main(argv):
    args = WindowCaptureArguments(STRICT_LOG_FAILURE_MESSAGES).parse_args(argv)
    backend = None
    try:
        if args.application_capture:
            result = launch_and_capture_application(args)
        else:
            backend = create_capture_backend()
            if args.window_handle is not None:
                result = WindowFrameCapture(ensure_process_running).capture_existing_handle(args, backend)
            else:
                result = launch_and_capture(args, backend)

        if args.application_capture:
            status = f"captured application framebuffer ({result.width}x{result.height}) -> {args.output}"
        else:
            status = f"captured window 0x{result.handle:x} ({result.width}x{result.height}) -> {args.output}"
        if args.expect_transparent_multi:
            analysis = result.transparent_multi
            status = (f"{status}; transparent regions " f"left={analysis.left_pixels}, center={analysis.center_pixels}, right={analysis.right_pixels}")
        if args.expect_transparent_csg:
            analysis = result.transparent_csg[args.transparent_csg_pose]
            status = (f"{status}; transparent CSG " f"cut_void={analysis.cut_void_pixels}/{analysis.cut_region_pixels}, " f"remaining_center={analysis.remaining_center_pixels}/{analysis.remaining_region_pixels}")
        if args.expect_texture_smoke:
            analysis = result.texture_smoke
            status = (f"{status}; sampled texture hues " f"red={analysis.red_pixels}, green={analysis.green_pixels}, blue={analysis.blue_pixels}; " f"receiver red GI={analysis.receiver_red_pixels}/{analysis.receiver_pixel_count}")
        if args.expect_surfel_coverage_probe:
            probe = validate_surfel_coverage_probe(args.output)
            status = (f"{status}; surfel coverage probe " f"front={probe[LIT_FRONT][0]}/{probe[LIT_FRONT][1]}, " f"side={probe[LIT_SIDE][0]}/{probe[LIT_SIDE][1]}")
        write_status(status)
        return 0
    except SmokeSkip as exc:
        write_status(f"SKIP: {exc}")
        return SKIP_EXIT_CODE
    except SmokeFailure as exc:
        write_status(f"FAIL: {exc}")
        return 1
    finally:
        if backend:
            backend.close()


if __name__ == LIT_MAIN:
    sys.exit(main(sys.argv[1:]))
