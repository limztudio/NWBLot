#!/usr/bin/env python3
import argparse
import ctypes
import math
from dataclasses import dataclass
import os
from pathlib import Path
from types import SimpleNamespace
import platform
import subprocess
import sys
import tempfile
import threading
import time
from window_capture_outcome import SmokeSkip, WindowsDesktopUnavailable, SmokeFailure, write_status  # noqa: E402
from window_capture_image import TransparentCsgAnalysis, TextureSmokeAnalysis, write_bmp_24, capture_result_from_rgb_rows, write_capture_rows, read_bmp_24, read_bmp_24_rows, validate_surfel_coverage_probe, analyze_rgb_rows, transparent_csg_regions, analyze_transparent_csg_rows, analyze_texture_smoke_rows, estimate_background_rgb, count_foreground_pixels_in_region, ximage_rgb_rows, bgra_rgb_rows  # noqa: E402
from window_capture_x11 import LinuxXImageFuncs, LinuxXImage, LinuxXEvent, LinuxX11Capture  # noqa: E402
from window_capture_win32 import bind_windows_user32, create_windows_user32, WinRect, WinPoint, WindowsUserObjectFlags, require_windows_interactive_desktop, WindowsCapture  # noqa: E402
from window_capture_logs import choose_free_port, wait_for_tcp_port, snapshot_log_files, collect_log_delta, standalone_log_pattern, wait_for_log_message, wait_for_log_drain  # noqa: E402
from window_capture_environment import executable_sibling, make_runtime_launch_args, build_launch_environment, validate_resize_environment, application_capture_partial_path, prepare_application_capture_environment  # noqa: E402
from window_capture_validation import validate_capture_result, validate_transparent_csg_result, validate_texture_smoke_result, validate_expected_log_text, validate_expected_log_messages, validate_application_capture_log_text, validate_capture_for_args  # noqa: E402

LIT_MINIMUM_CAPTURE_SETTLE_SECONDS = 1.0

LIT_CLOSE_POLL_SECONDS = 0.2
LIT_SHORT_POLL_SECONDS = 0.05
LIT_CLOSE_HELPER_TIMEOUT_SECONDS = 6.0
LIT_GRACEFUL_EXIT_TIMEOUT_SECONDS = 10.0
LIT_FORCED_EXIT_TIMEOUT_SECONDS = 5.0
LIT_LOGSERVER_START_TIMEOUT_SECONDS = 10.0

LIT_GPUDBG = "--gpudbg"
LIT_FRONT = "front"
LIT_SIDE = "side"
LIT_RESIZE_SETTLE_SECONDS = "--resize-settle-seconds"

LIT_RB = "rb"
LIT_UTF_8 = "utf-8"
LIT_REPLACE = "replace"
LIT_WINDOWS = "Windows"
LIT_LINUX = "Linux"
LIT_NWB_OUTPUT_CAPTURE = "_nwb_output_capture"
LIT_OFF = "off"
LIT_ON = "on"
LIT_RESIZE_CLIENT = "resize_client"
LIT_WIDTH = "width"
LIT_HEIGHT = "height"
LIT_DEPTH = "depth"
LIT_LOGSERVER = "logserver"
LIT_P = "-p"
LIT_A = "-a"
LIT_TESTBED = "testbed"
LIT_BEFORE_LOG_COLLECTION = "before log collection"
LIT_TIMEOUT = "--timeout"
LIT_SETTLE_SECONDS = "--settle-seconds"
LIT_RENDER_READY_TIMEOUT = "--render-ready-timeout"
LIT_STORE_TRUE = "store_true"
LIT_APPLICATION_CAPTURE_FRAME_COUNT = "--application-capture-frame-count"
LIT_APPEND = "append"
LIT_MAIN = "__main__"


SKIP_EXIT_CODE = 77
STRICT_LOG_FAILURE_MESSAGES = ("[WARNING]", "[CRITICAL WARNING]", "[ASSERT]", "[ERROR]", "[FATAL]", "VUID-", "Validation Error", "failed to resolve shader",)
FRAMEBUFFER_CAPTURE_PATH_ENV = "NWB_SMOKE_FRAMEBUFFER_CAPTURE_PATH"
FRAMEBUFFER_CAPTURE_READY_MESSAGE = "FramebufferCapture: capture ready"
FRAMEBUFFER_CAPTURE_SKIP_MESSAGE = "FramebufferCapture: skipped because swap-chain transfer-source usage is unavailable"
RENDER_READY_POLL_SECONDS = 0.1
RENDER_READY_TIMEOUT_SECONDS = 10.0
WINDOWS_ENUM_CALLBACK = getattr(ctypes, "WINFUNCTYPE", ctypes.CFUNCTYPE)(ctypes.c_int, ctypes.c_void_p, ctypes.c_void_p,)


# CSG pose names match the fixture's frozen yaw values 0, 1, and 2 radians.
# Coordinates are offsets from image center, measured in image heights so the vertical-FOV projection remains valid at different aspect ratios.
# These inset rectangles come from the projected radius-.468 octahedron at camera distance2.2/FOV60 and its retained local-y<=0 pyramid, after Ry(yaw)*Ry(yaw)*Rx(.32*yaw)*Rz(.16*yaw).
# Each cut rectangle lies inside the uncut silhouette and outside the retained pyramid; each retained rectangle lies inside that pyramid.
# Geometry-based synthetic tests protect those margins.
# The late retained wedge is above/right of center because the cutter rotates with the receiver.
TRANSPARENT_CSG_REGIONS = { "early": ((-.030, -.125, .030, -.065), (-.030, .065, .030, .125)), "mid": ((.025, -.125, .065, -.080), (-.065, .110, -.025, .140)), "late": ((-.055, -.128, -.025, -.104), (.045, -.075, .080, -.050)), }


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


@dataclass
class ProcessOutputCapture:
    path: Path
    stream: object


def parse_int(value):
    try:
        return int(value, 0)
    except ValueError as exc:
        raise argparse.ArgumentTypeError(f"invalid integer value '{value}'") from exc


def request_windows_graceful_exit(pid):
    # WM_CLOSE runs normal shutdown and writes name-symbol sidecars; forced termination skips it.
    user32 = create_windows_user32()
    wm_close = 0x0010
    targets = []

    @WINDOWS_ENUM_CALLBACK
    def _enum(hwnd, _lparam):
        owner_pid = ctypes.c_uint32(0)
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(owner_pid))
        if owner_pid.value == pid:
            targets.append(hwnd)
        return True

    user32.EnumWindows(_enum, 0)
    for hwnd in targets:
        user32.PostMessageW(hwnd, wm_close, 0, 0)
    return bool(targets)


def request_linux_graceful_exit(window_handle):
    # Run the helper separately so missing X11 libraries cannot prevent fallback termination.
    if window_handle is None:
        return False

    window_id = f"0x{int(window_handle):x}"
    close_helper = Path(__file__).with_name("x11_graceful_close.py")
    try:
        result = subprocess.run([sys.executable, str(close_helper), window_id], check=False, capture_output=True, text=True, timeout=LIT_CLOSE_HELPER_TIMEOUT_SECONDS,)
    except (OSError, subprocess.TimeoutExpired) as error:
        write_status(f"Linux graceful X11 close failed for {window_id} ({error}); terminating")
        return False

    if result.returncode == 0:
        return True

    detail = (result.stderr or result.stdout).strip()
    suffix = f": {detail}" if detail else ""
    write_status(f"Linux graceful X11 close failed for {window_id} (exit {result.returncode}){suffix}; terminating")
    return False


def create_process_output_capture(working_directory, name):
    try:
        stream = tempfile.NamedTemporaryFile(mode="w+b", prefix=f".nwb_window_capture_{name}_", suffix=".log", dir=working_directory, delete=False,)
    except OSError as error:
        raise SmokeFailure(f"failed to create {name} output capture in '{working_directory}': {error}") from error

    return ProcessOutputCapture(Path(stream.name), stream)


def dispose_process_output_capture(capture):
    if capture is None:
        return

    try:
        capture.stream.close()
    except OSError as error:
        write_status(f"failed to close process output capture '{capture.path}': {error}")

    try:
        capture.path.unlink()
    except FileNotFoundError:
        pass
    except OSError as error:
        write_status(f"failed to remove process output capture '{capture.path}': {error}")


def launch_captured_process(command, working_directory, env, name):
    capture = create_process_output_capture(working_directory, name)
    try:
        process = subprocess.Popen(command, cwd=working_directory, env=env, stdout=capture.stream, stderr=subprocess.STDOUT,)
    except OSError:
        dispose_process_output_capture(capture)
        raise

    process._nwb_output_capture = capture
    return process


def terminate_process(process, name, window_handle=None):
    if process is None:
        return None, ""

    exit_code = None
    tail = ""
    try:
        if process.poll() is None:
            # Graceful exit writes Name-symbol sidecars; forced termination can skip the normal exit path.
            host_platform = platform.system()
            if host_platform == LIT_WINDOWS:
                try:
                    if request_windows_graceful_exit(process.pid):
                        try:
                            process.wait(timeout=LIT_GRACEFUL_EXIT_TIMEOUT_SECONDS)
                        except subprocess.TimeoutExpired:
                            write_status(f"{name}: did not exit after WM_CLOSE; terminating")
                except OSError as error:
                    write_status(f"{name}: graceful WM_CLOSE failed ({error}); terminating")

            elif host_platform == LIT_LINUX and window_handle is not None:
                if request_linux_graceful_exit(window_handle):
                    try:
                        process.wait(timeout=LIT_GRACEFUL_EXIT_TIMEOUT_SECONDS)
                    except subprocess.TimeoutExpired:
                        write_status(f"{name}: did not exit after X11 WM_DELETE_WINDOW; terminating")

            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=LIT_FORCED_EXIT_TIMEOUT_SECONDS)
                except subprocess.TimeoutExpired:
                    write_status(f"{name}: did not exit after terminate; killing")
                    process.kill()
                    process.wait(timeout=LIT_FORCED_EXIT_TIMEOUT_SECONDS)
    finally:
        exit_code = process.poll()
        if exit_code is not None:
            tail = read_process_tail(process)
            capture = getattr(process, LIT_NWB_OUTPUT_CAPTURE, None)
            process._nwb_output_capture = None
            dispose_process_output_capture(capture)

    return exit_code, tail


def finish_logserver_cleanup(logserver, output, errors, primary_failure, terminate, write_report):
    try:
        terminate(logserver, LIT_LOGSERVER)
    except Exception as error:
        errors.append(f"logger cleanup: {error}")
    if errors:
        write_report(output / "cleanup_error.json", {"errors": errors})
        if not primary_failure:
            raise SmokeFailure("; ".join(errors))


def require_positive_arg(parser, name, value):
    if value <= 0.0:
        parser.error(f"{name} must be positive")


def require_non_negative_arg(parser, name, value):
    if value < 0.0:
        parser.error(f"{name} must not be negative")


def read_file_tail(path, max_bytes=65536):
    try:
        with path.open(LIT_RB) as log_file:
            log_file.seek(0, os.SEEK_END)
            start = max(log_file.tell() - max_bytes, 0)
            log_file.seek(start)
            return log_file.read().decode(LIT_UTF_8, LIT_REPLACE)
    except OSError:
        return ""


def read_process_tail(process):
    if process is None:
        return ""

    capture = getattr(process, LIT_NWB_OUTPUT_CAPTURE, None)
    if capture is None:
        return ""

    output = read_file_tail(capture.path)
    if not output:
        return ""

    lines = output.splitlines()
    return "\n".join(lines[-40:])


LinuxXImage._fields_ = [(LIT_WIDTH, ctypes.c_int), (LIT_HEIGHT, ctypes.c_int), ("xoffset", ctypes.c_int), ("format", ctypes.c_int), ("data", ctypes.c_void_p), ("byte_order", ctypes.c_int), ("bitmap_unit", ctypes.c_int), ("bitmap_bit_order", ctypes.c_int),
    ("bitmap_pad", ctypes.c_int), (LIT_DEPTH, ctypes.c_int), ("bytes_per_line", ctypes.c_int), ("bits_per_pixel", ctypes.c_int), ("red_mask", ctypes.c_ulong), ("green_mask", ctypes.c_ulong), ("blue_mask", ctypes.c_ulong), ("obdata", ctypes.c_void_p),
    ("f", LinuxXImageFuncs),]


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
    process = launch_captured_process([str(logserver), LIT_P, str(port)], args.working_directory, env, LIT_LOGSERVER,)
    try:
        wait_for_tcp_port(port, min(args.timeout, LIT_LOGSERVER_START_TIMEOUT_SECONDS))
    except Exception as error:
        tail = read_process_tail(process)
        terminate_process(process, LIT_LOGSERVER)
        if tail:
            raise SmokeFailure(f"{error}\n{tail}") from error
        raise

    return process, port, log_directory, log_baseline, log_pattern


def launch_testbed(args, executable, env, log_port):
    command = [str(executable)]
    if log_port:
        command.extend([LIT_A, "http://localhost", LIT_P, str(log_port)])
    else:
        command.extend([LIT_A, "", LIT_P, "0"])
    command.extend(args.application_arg)

    return launch_captured_process(command, args.working_directory, env, LIT_TESTBED,)


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

    tail = read_process_tail(process)
    raise SmokeFailure(f"{process_name} exited {stage} (exit {process.returncode})\n{tail}")


def require_normal_process_exit(exit_code, tail, process_name):
    if exit_code == 0:
        return

    if exit_code is None:
        raise SmokeFailure(f"{process_name} did not exit during shutdown")

    detail = f"\n{tail}" if tail else ""
    raise SmokeFailure(f"{process_name} exited during graceful shutdown (exit {exit_code}){detail}")


def shutdown_logserver_and_collect(process, directory, baseline, pattern, shutdown_name=LIT_LOGSERVER):
    if process is not None:
        ensure_process_running(process, LIT_BEFORE_LOG_COLLECTION, LIT_LOGSERVER)
        exit_code, tail = terminate_process(process, shutdown_name)
        require_normal_process_exit(exit_code, tail, LIT_LOGSERVER)
    wait_for_log_drain(directory, baseline, pattern)
    return collect_log_delta(directory, baseline, pattern) if directory else ""


def capture_checked_window(args, backend, handle):
    result = backend.capture_window(handle, args.output)
    validate_capture_for_args(args, result)
    return result


def capture_existing_handle(args, backend):
    return capture_checked_window(args, backend, args.window_handle)


def capture_render_ready_window(args, backend, handle, process):
    deadline = time.monotonic() + args.render_ready_timeout
    while True:
        ensure_process_running(process, "while waiting for rendered window content")
        result = backend.capture_window(handle, args.output)
        if not result.appears_empty_or_white:
            validate_capture_for_args(args, result)
            return result

        ensure_process_running(process, "after a blank window capture")
        remaining_seconds = deadline - time.monotonic()
        if remaining_seconds <= 0.0:
            raise SmokeFailure(f"captured window 0x{result.handle:x}, but it remained blank or white for " f"{args.render_ready_timeout:.2f} seconds while waiting for rendered content")
        time.sleep(min(RENDER_READY_POLL_SECONDS, remaining_seconds))


def capture_resized_window(args, backend, handle, process, log_directory, log_baseline, log_pattern):
    backend.prepare_window(handle)
    before_args = SimpleNamespace(**vars(args))
    before_args.output = args.output.with_name(args.output.stem + ".before-resize" + args.output.suffix)
    before = capture_render_ready_window(before_args, backend, handle, process)
    original_size = (before.width, before.height)
    target_size = tuple(args.resize_client)
    if original_size == target_size:
        raise SmokeFailure("--resize-client must change the original client extent")
    if backend.client_size(handle) != original_size:
        raise SmokeFailure("original client and capture extents disagree before resize")
    write_status(f"rendered original client {before.width}x{before.height}; settling {args.settle_seconds:g}s before resize")
    time.sleep(args.settle_seconds)
    ensure_process_running(process, "before client resize")
    if backend.client_size(handle) != original_size:
        raise SmokeFailure("client extent changed during the original-size settle interval")
    settled_before = capture_render_ready_window(before_args, backend, handle, process)
    if (settled_before.width, settled_before.height) != original_size or backend.client_size(handle) != original_size:
        raise SmokeFailure("settled original client/capture extent does not match the original extent")

    backend.resize_client(handle, *target_size)
    marker = f"GraphicsRuntime: Back buffer resized to {target_size[0]}x{target_size[1]}"
    deadline = time.monotonic() + args.render_ready_timeout
    while True:
        ensure_process_running(process, "while waiting for client and framebuffer resize")
        client_matches = backend.client_size(handle) == target_size
        log_text = collect_log_delta(log_directory, log_baseline, log_pattern)
        if client_matches and marker in log_text:
            break
        remaining_seconds = deadline - time.monotonic()
        if remaining_seconds <= 0.0:
            raise SmokeFailure(f"resize did not produce client extent {target_size} and runtime marker '{marker}'")
        time.sleep(min(RENDER_READY_POLL_SECONDS, remaining_seconds))

    write_status(f"confirmed resized client and framebuffer {target_size[0]}x{target_size[1]}; settling {args.resize_settle_seconds:g}s")
    time.sleep(args.resize_settle_seconds)
    ensure_process_running(process, "after resized-frame settle interval")
    result = capture_render_ready_window(args, backend, handle, process)
    if (result.width, result.height) != target_size or backend.client_size(handle) != target_size:
        raise SmokeFailure(f"final client/capture extent does not match requested resize {target_size}")
    return result


def launch_and_capture(args, backend):
    executable = Path(args.executable).resolve()
    if not executable.exists():
        raise SmokeFailure(f"executable does not exist: {executable}")

    env = build_launch_environment(args)
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
                tail = read_process_tail(testbed_process)
                raise SmokeFailure(f"testbed exited before a window was visible (exit {exit_code})\n{tail}")
            raise SmokeFailure("timed out waiting for a visible testbed window")

        if getattr(args, LIT_RESIZE_CLIENT, None):
            result = capture_resized_window(args, backend, handle, testbed_process, log_directory, log_baseline, log_pattern,)
        else:
            time.sleep(args.settle_seconds)
            ensure_process_running(testbed_process, "before checked capture")
            result = capture_render_ready_window(args, backend, handle, testbed_process)
        testbed_exit_code, testbed_exit_tail = terminate_process(testbed_process, LIT_TESTBED, handle)
        testbed_process = None
        require_normal_process_exit(testbed_exit_code, testbed_exit_tail, LIT_TESTBED)
        if logserver_process is not None:
            ensure_process_running(logserver_process, LIT_BEFORE_LOG_COLLECTION, LIT_LOGSERVER)
            logserver_exit_code, logserver_exit_tail = terminate_process(logserver_process, LIT_LOGSERVER)
            logserver_process = None
            require_normal_process_exit(logserver_exit_code, logserver_exit_tail, LIT_LOGSERVER)
        wait_for_log_drain(log_directory, log_baseline, log_pattern)
        skip_reason = validate_expected_log_messages(log_directory, log_baseline, log_pattern, args.expect_log_message, args.reject_log_message, args.skip_log_message, args.skip_blocking_log_message,)
    finally:
        try:
            if testbed_process is not None:
                terminate_process(testbed_process, LIT_TESTBED, handle)
        finally:
            if logserver_process is not None:
                terminate_process(logserver_process, LIT_LOGSERVER)

    if skip_reason:
        raise SmokeSkip(skip_reason)
    return result


def launch_and_capture_application(args):
    executable = Path(args.executable).resolve()
    if not executable.exists():
        raise SmokeFailure(f"executable does not exist: {executable}")

    env = prepare_application_capture_environment(args, build_launch_environment(args))
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

        testbed_exit_code, testbed_exit_tail = terminate_process(testbed_process, LIT_TESTBED)
        testbed_process = None
        log_text = shutdown_logserver_and_collect(logserver_process, log_directory, log_baseline, log_pattern,)
        logserver_process = None
    finally:
        try:
            if testbed_process is not None:
                terminate_process(testbed_process, LIT_TESTBED)
        finally:
            if logserver_process is not None:
                terminate_process(logserver_process, LIT_LOGSERVER)

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


def parse_args(argv):
    parser = argparse.ArgumentParser(description="Launch an NWB executable and capture its rendered output.")
    parser.add_argument("--executable", help="Path to the executable. Optional when --window-handle is used.")
    parser.add_argument("--working-directory", type=Path, default=Path.cwd(), help="Working directory for launched processes.")
    parser.add_argument("--output", type=Path, required=True, help="Screenshot output path. The script writes a 24-bit BMP.")
    parser.add_argument("--window-handle", type=parse_int, help="Capture an existing native window handle instead of launching testbed.")
    parser.add_argument("--window-title", default="", help="Expected window title when matching a launched testbed window.")
    parser.add_argument(LIT_TIMEOUT, type=float, default=45.0, help="Seconds to wait for logserver and the testbed window.")
    parser.add_argument(LIT_SETTLE_SECONDS, type=float, default=2.0, help="Seconds to wait after the window becomes visible.")
    parser.add_argument("--resize-client", nargs=2, type=int, metavar=("WIDTH", "HEIGHT"), help="After rendering and settling at the original extent, resize the launched client once and verify its framebuffer extent.")
    parser.add_argument(LIT_RESIZE_SETTLE_SECONDS, type=float, help="Seconds to render after the requested client/framebuffer resize is confirmed; defaults to 2, minimum 1.")
    parser.add_argument(LIT_RENDER_READY_TIMEOUT, type=float, default=RENDER_READY_TIMEOUT_SECONDS, help="Seconds to wait for a blank visible window to publish its first rendered frame.",)
    parser.add_argument("--application-capture", action=LIT_STORE_TRUE, help="Use the application's framebuffer readback instead of desktop window capture.",)
    parser.add_argument(LIT_APPLICATION_CAPTURE_FRAME_COUNT, type=int, default=360, help="Positive rendered frame count at which application capture is requested. Defaults to 360.",)
    parser.add_argument("--logserver-executable", help="Path to nwb_logserver/logserver. Defaults to a sibling of --executable.")
    parser.add_argument("--no-logserver", action=LIT_STORE_TRUE, help="Do not start a logserver; launch with standalone log output.")
    parser.add_argument("--gpu-validation", action=argparse.BooleanOptionalAction, default=False, help="Request graphics validation from a launched dbg/opt application; fin applications reject the request.")
    parser.add_argument("--log-port", type=int, default=0, help="Logserver port. Defaults to an available localhost port.")
    parser.add_argument("--log-output", type=Path, help="For application capture, save the exact collected per-launch log text before validation.")
    parser.add_argument("--expect-log-message", action=LIT_APPEND, default=[], help="Required substring in the logserver output.")
    parser.add_argument("--reject-log-message", action=LIT_APPEND, default=list(STRICT_LOG_FAILURE_MESSAGES), help="Forbidden substring in the logserver output. Runtime warnings, assertions, errors, and validation failures are always rejected.",)
    parser.add_argument("--skip-log-message", action=LIT_APPEND, default=[], help="Log substring that classifies the launched smoke as unsupported instead of failed.",)
    parser.add_argument("--skip-blocking-log-message", action=LIT_APPEND, default=list(STRICT_LOG_FAILURE_MESSAGES),
        help="Log substring that remains a failure even when an explicit capability-skip marker is present. Runtime warnings, assertions, errors, and validation failures always block skips.",)
    parser.add_argument("--expect-transparent-multi", action=LIT_STORE_TRUE, help="Assert that the captured scene contains multiple distinct transparent mesh colors.",)
    parser.add_argument("--expect-transparent-csg", action=LIT_STORE_TRUE, help="Assert that the captured transparent center mesh contains a clipped CSG void.",)
    parser.add_argument("--transparent-csg-pose", choices=tuple(TRANSPARENT_CSG_REGIONS), help="Required with --expect-transparent-csg: early/mid/late selects the fixture's frozen yaw 0/1/2 interior regions.",)
    parser.add_argument("--expect-texture-smoke", action=LIT_STORE_TRUE, help="Assert that the captured scene visibly samples the authored red, green, and blue texture pattern.",)
    parser.add_argument("--expect-surfel-coverage-probe", action=LIT_STORE_TRUE, help="Assert that the closed GI scene's perpendicular same-cell probe faces receive surfel coverage.",)
    parser.add_argument("--software-vulkan", choices=("auto", LIT_ON, LIT_OFF), default=LIT_OFF, help="On Linux, optionally use Mesa lavapipe. Defaults to the system Vulkan ICD.",)
    parser.add_argument("--application-arg", action=LIT_APPEND, default=[], help="Extra argument to pass to the launched application. Repeat for multiple arguments.",)
    args = parser.parse_args(argv)

    if args.expect_transparent_csg and args.transparent_csg_pose is None:
        parser.error("--expect-transparent-csg requires --transparent-csg-pose")
    if args.transparent_csg_pose is not None and not args.expect_transparent_csg:
        parser.error("--transparent-csg-pose requires --expect-transparent-csg")
    if args.gpu_validation and args.window_handle is not None:
        parser.error("--gpu-validation requires launching an executable, not --window-handle")
    if args.application_capture and args.window_handle is not None:
        parser.error("--application-capture cannot be combined with --window-handle")
    if args.log_output and not args.application_capture:
        parser.error("--log-output requires --application-capture")
    if args.window_handle is None and not args.executable:
        parser.error("--executable is required unless --window-handle is provided")
    for name, value in ((LIT_TIMEOUT, args.timeout), (LIT_RENDER_READY_TIMEOUT, args.render_ready_timeout), (LIT_SETTLE_SECONDS, args.settle_seconds), (LIT_RESIZE_SETTLE_SECONDS, args.resize_settle_seconds)):
        if value is not None and not math.isfinite(value):
            parser.error(f"{name} must be finite")
    require_positive_arg(parser, LIT_TIMEOUT, args.timeout)
    require_positive_arg(parser, LIT_RENDER_READY_TIMEOUT, args.render_ready_timeout)
    require_positive_arg(parser, LIT_APPLICATION_CAPTURE_FRAME_COUNT, args.application_capture_frame_count,)
    require_non_negative_arg(parser, LIT_SETTLE_SECONDS, args.settle_seconds)

    if args.resize_client is not None:
        if args.application_capture or args.window_handle is not None:
            parser.error("--resize-client requires a launched desktop capture; application capture and existing handles are incompatible")
        if any(dimension < 1 or dimension > 16384 for dimension in args.resize_client):
            parser.error("--resize-client dimensions must be between 1 and 16384")
        if args.settle_seconds < LIT_MINIMUM_CAPTURE_SETTLE_SECONDS:
            parser.error("--resize-client requires --settle-seconds to be at least 1")
        if args.resize_settle_seconds is None:
            args.resize_settle_seconds = 2.0
        if args.resize_settle_seconds < LIT_MINIMUM_CAPTURE_SETTLE_SECONDS:
            parser.error("--resize-settle-seconds must be at least 1")
    elif args.resize_settle_seconds is not None:
        parser.error("--resize-settle-seconds requires --resize-client")

    args.working_directory = args.working_directory.resolve()
    args.output = args.output.resolve()
    if args.gpu_validation:
        args.application_arg = [LIT_GPUDBG, *args.application_arg]
    return args


def main(argv):
    args = parse_args(argv)
    backend = None
    try:
        if args.application_capture:
            result = launch_and_capture_application(args)
        else:
            backend = create_capture_backend()
            if args.window_handle is not None:
                result = capture_existing_handle(args, backend)
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
