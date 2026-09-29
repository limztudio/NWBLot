#!/usr/bin/env python3
"""Capture a blank startup frame or a resized UI framebuffer without desktop composition."""
from pathlib import Path
import sys
import time


sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
import window_capture_smoke  # noqa: E402


def _resize_request(argv):
    if "--resize-before-capture" not in argv:
        return None, argv
    index = argv.index("--resize-before-capture")
    if index + 2 >= len(argv):
        raise window_capture_smoke.SmokeFailure("--resize-before-capture requires WIDTH HEIGHT")
    try:
        target = int(argv[index + 1]), int(argv[index + 2])
    except ValueError as error:
        raise window_capture_smoke.SmokeFailure("resize dimensions must be decimal integers") from error
    if any(value < 1 or value > 16384 for value in target):
        raise window_capture_smoke.SmokeFailure("resize dimensions must be between 1 and 16384")
    return target, argv[:index] + argv[index + 3:]


def _wait_for_resize(backend, handle, process, log_directory, baseline, pattern, target, timeout):
    marker = f"GraphicsRuntime: Back buffer resized to {target[0]}x{target[1]}"
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        window_capture_smoke.ensure_process_running(process, "while waiting for the framebuffer resize")
        size_matches = backend.client_size(handle) == target
        log_text = window_capture_smoke.collect_log_delta(log_directory, baseline, pattern)
        if size_matches and marker in log_text:
            return
        time.sleep(0.05)
    raise window_capture_smoke.SmokeFailure(f"resize did not produce client extent {target} and marker '{marker}'")


def _capture_after_resize(args, target):
    smoke = window_capture_smoke
    backend = logserver = application = None
    handle = None
    try:
        executable = Path(args.executable).resolve()
        if not executable.is_file():
            raise smoke.SmokeFailure(f"executable does not exist: {executable}")
        environment = smoke.prepare_application_capture_environment(args, smoke.build_launch_environment(args))
        backend = smoke.create_capture_backend()
        logserver, port, log_directory, baseline, pattern = smoke.launch_logserver(
            args, executable, environment)
        application = smoke.launch_testbed(args, executable, environment, port)
        handle = backend.wait_for_window(application.pid, args.timeout, args.window_title)
        if not handle:
            smoke.ensure_process_running(application, "before the resize window appeared")
            raise smoke.SmokeFailure("custom UI resize window did not appear")
        backend.prepare_window(handle)
        if backend.client_size(handle) == target:
            raise smoke.SmokeFailure(f"window already has target client extent {target}")
        backend.resize_client(handle, *target)
        _wait_for_resize(backend, handle, application, log_directory, baseline, pattern,
            target, min(args.timeout, 30.0))
        smoke.wait_for_application_capture_exit(application, args.output, args.timeout)
        exit_code, tail = smoke.terminate_process(application, "UI resize fixture", handle)
        application = None
        smoke.require_normal_process_exit(exit_code, tail, "UI resize fixture")
        log_text = smoke.shutdown_logserver_and_collect(logserver, log_directory, baseline, pattern)
        logserver = None
        if args.log_output:
            args.log_output.parent.mkdir(parents=True, exist_ok=True)
            args.log_output.write_text(log_text, encoding="utf-8")
        skip_reason = smoke.validate_application_capture_log_text(log_text, args)
        if skip_reason:
            raise smoke.SmokeSkip(skip_reason)
        if not args.output.is_file():
            raise smoke.SmokeFailure(f"resized application framebuffer capture is absent: {args.output}")
        if smoke.application_capture_partial_path(args.output).exists():
            raise smoke.SmokeFailure("resized application framebuffer capture left an incomplete artifact")
        result = smoke.read_bmp_24(args.output)
        smoke.validate_capture_for_args(args, result)
        if (result.width, result.height) != target:
            raise smoke.SmokeFailure(f"resized application framebuffer is {result.width}x{result.height}, expected {target}")
        smoke.write_status(f"captured resized application framebuffer {target[0]}x{target[1]} -> {args.output}")
        return 0
    except smoke.SmokeSkip as error:
        smoke.write_status(f"SKIP: {error}")
        return smoke.SKIP_EXIT_CODE
    except (smoke.SmokeFailure, OSError) as error:
        smoke.write_status(f"FAIL: {error}")
        return 1
    finally:
        if application is not None:
            smoke.terminate_process(application, "UI resize fixture", handle)
        if logserver is not None:
            smoke.terminate_process(logserver, "UI resize logserver")
        if backend is not None:
            backend.close()


def main(argv):
    try:
        resize, argv = _resize_request(argv)
    except window_capture_smoke.SmokeFailure as error:
        window_capture_smoke.write_status(f"FAIL: {error}")
        return 1
    if resize is not None:
        args = window_capture_smoke.parse_args(argv)
        if not args.application_capture:
            window_capture_smoke.write_status("FAIL: --resize-before-capture requires --application-capture")
            return 1
        return _capture_after_resize(args, resize)
    # The shared capture checks readiness and logs, but its generic visual gate rejects flat images.
    # This fixture independently requires every startup pixel to be black after the readback.
    window_capture_smoke.validate_capture_result = lambda result: None
    return window_capture_smoke.main(argv)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
