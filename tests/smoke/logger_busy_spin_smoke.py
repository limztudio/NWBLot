#!/usr/bin/env python3
"""Regress an active logger window busy-spinning while it has no new messages."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import ctypes
import http.client
import json
import os
from pathlib import Path
import struct
import sys
import time
from types import SimpleNamespace

from window_capture_smoke import (SKIP_EXIT_CODE, STRICT_LOG_FAILURE_MESSAGES, SmokeFailure, SmokeSkip, WindowsCapture, WinRect, collect_log_delta, ensure_process_running, launch_logserver, read_bmp_24_rows, request_windows_graceful_exit, require_normal_process_exit,
    snapshot_log_files, terminate_process, validate_expected_log_text, wait_for_log_message, write_status,)

LIT_IDLE_SAMPLE_POLL_SECONDS = 0.1
LIT_MINIMUM_WAIT_SECONDS = 0.001

LIT_WINDOW_MESSAGE_TIMEOUT_MS = 1000
LIT_LOGGER_READY_TIMEOUT_SECONDS = 10.0
LIT_GRACEFUL_EXIT_TIMEOUT_SECONDS = 10.0
LIT_LOG_ROW_POLL_SECONDS = 0.05
LIT_HTTP_TIMEOUT_SECONDS = 5.0
LIT_MESSAGE_PUBLICATION_INTERVAL_SECONDS = 0.01
LIT_RESIZE_POLL_SECONDS = 0.005

LIT_LOGSERVER = "logserver"
LIT_SINGLE_CORE_CPU_FRACTION = "single_core_cpu_fraction"
LIT_MESSAGES = "messages"
LIT_EXPECTED_ROWS = "expected_rows"
LIT_STARTUP_RECEIVED = "startup_received"
LIT_CLEAN_WM_CLOSE_EXIT = "clean_wm_close_exit"
LIT_LOGGER_BMP = "logger.bmp"
LIT_CPU = "cpu"
LIT_SCREENSHOT = "screenshot"
LIT_RESIZE = "resize"
LIT_UTF_8 = "utf-8"

SETTLE_SECONDS = 1.0
SAMPLE_SECONDS = 3.0
MAX_CPU_FRACTION = 0.15
FILETIME_SECONDS = 1e-7
PROCESS_QUERY_LIMITED_INFORMATION = 0x1000
LB_GETCOUNT = 0x018B
LB_GETITEMRECT = 0x0198
SMTO_ABORTIFHUNG = 0x0002
WM_EXITSIZEMOVE = 0x0232
RESIZE_MESSAGE_COUNT = 6
RESIZE_SECONDS = 8.0
LOG_FAILURE_MESSAGES = (*STRICT_LOG_FAILURE_MESSAGES, "Log server: failed", "Log client: failed", "Exception:")


class FileTime(ctypes.Structure):
    _fields_ = [("low", ctypes.c_uint32), ("high", ctypes.c_uint32)]


class GuiThreadInfo(ctypes.Structure):
    _fields_ = [("size", ctypes.c_uint32), ("flags", ctypes.c_uint32), ("active", ctypes.c_void_p), ("focus", ctypes.c_void_p), ("capture", ctypes.c_void_p), ("menu_owner", ctypes.c_void_p), ("move_size", ctypes.c_void_p), ("caret", ctypes.c_void_p),
        ("caret_rect", WinRect),]


def bind_window_queries(user32):
    user32.GetForegroundWindow.argtypes = []
    user32.GetForegroundWindow.restype = ctypes.c_void_p
    user32.GetGUIThreadInfo.argtypes = [ctypes.c_uint32, ctypes.POINTER(GuiThreadInfo)]
    user32.GetGUIThreadInfo.restype = ctypes.c_int
    user32.GetDlgItem.argtypes = [ctypes.c_void_p, ctypes.c_int]
    user32.GetDlgItem.restype = ctypes.c_void_p
    user32.SendMessageTimeoutW.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_size_t, ctypes.c_ssize_t, ctypes.c_uint, ctypes.c_uint, ctypes.POINTER(ctypes.c_size_t),]
    user32.SendMessageTimeoutW.restype = ctypes.c_ssize_t


def require_active_window(user32, handle):
    info = GuiThreadInfo()
    info.size = ctypes.sizeof(info)
    if not user32.GetGUIThreadInfo(0, ctypes.byref(info)):
        raise SmokeFailure(f"GetGUIThreadInfo failed: {ctypes.get_last_error()}")
    if user32.GetForegroundWindow() != handle or info.active != handle:
        raise SmokeFailure("logger window lost foreground or active status during the idle sample")


def sample_idle_cpu(process, backend, handle):
    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel32.OpenProcess.argtypes = [ctypes.c_uint32, ctypes.c_int, ctypes.c_uint32]
    kernel32.OpenProcess.restype = ctypes.c_void_p
    kernel32.CloseHandle.argtypes = [ctypes.c_void_p]
    kernel32.CloseHandle.restype = ctypes.c_int
    kernel32.GetProcessTimes.argtypes = [ctypes.c_void_p] + [ctypes.POINTER(FileTime)] * 4
    kernel32.GetProcessTimes.restype = ctypes.c_int
    process_handle = kernel32.OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, False, process.pid)
    if not process_handle:
        raise SmokeFailure(f"OpenProcess failed: {ctypes.get_last_error()}")

    def read_times():
        creation, exit_time, kernel, user = (FileTime() for _ in range(4))
        if not kernel32.GetProcessTimes(process_handle, ctypes.byref(creation), ctypes.byref(exit_time), ctypes.byref(kernel), ctypes.byref(user),):
            raise SmokeFailure(f"GetProcessTimes failed: {ctypes.get_last_error()}")
        return ((kernel.high << 32) | kernel.low), ((user.high << 32) | user.low)

    try:
        require_active_window(backend.user32, handle)
        kernel_start, user_start = read_times()
        wall_start = time.perf_counter()
        deadline = time.monotonic() + SAMPLE_SECONDS
        foreground_checks = 0
        while time.monotonic() < deadline:
            time.sleep(min(LIT_IDLE_SAMPLE_POLL_SECONDS, max(0.0, deadline - time.monotonic())))
            ensure_process_running(process, "during idle CPU sample", LIT_LOGSERVER)
            require_active_window(backend.user32, handle)
            foreground_checks += 1
        kernel_end, user_end = read_times()
        wall_seconds = time.perf_counter() - wall_start
        kernel_seconds = (kernel_end - kernel_start) * FILETIME_SECONDS
        user_seconds = (user_end - user_start) * FILETIME_SECONDS
        return { "wall_seconds": wall_seconds, "kernel_cpu_seconds": kernel_seconds, "user_cpu_seconds": user_seconds, LIT_SINGLE_CORE_CPU_FRACTION: (kernel_seconds + user_seconds) / wall_seconds, "foreground_active_checks": foreground_checks, }
    finally:
        if not kernel32.CloseHandle(process_handle):
            raise SmokeFailure(f"CloseHandle failed: {ctypes.get_last_error()}")


def query_log_list(backend, handle, message, index=0, address=0):
    list_handle = backend.user32.GetDlgItem(handle, 1)
    if not list_handle or not backend.user32.IsWindowVisible(list_handle):
        raise SmokeFailure("logger message list is not visible")
    result = ctypes.c_size_t()
    if not backend.user32.SendMessageTimeoutW(list_handle, message, index, address, SMTO_ABORTIFHUNG, LIT_WINDOW_MESSAGE_TIMEOUT_MS, ctypes.byref(result),):
        raise SmokeFailure(f"logger message list did not answer query 0x{message:x}")
    value = ctypes.c_ssize_t(result.value).value
    if value < 0:
        raise SmokeFailure(f"logger message list query 0x{message:x} failed")
    return value


def wait_for_visible_log_rows(process, backend, handle, minimum_rows, timeout_seconds=LIT_LOGGER_READY_TIMEOUT_SECONDS):
    deadline = time.monotonic() + timeout_seconds
    while time.monotonic() < deadline:
        ensure_process_running(process, "while waiting for its HTTP log row", LIT_LOGSERVER)
        if query_log_list(backend, handle, LB_GETCOUNT) >= minimum_rows:
            return
        time.sleep(LIT_LOG_ROW_POLL_SECONDS)
    raise SmokeFailure(f"HTTP log reached the file but fewer than {minimum_rows} rows are displayed")


def post_log_message(port, message):
    # Current Windows log transport: packed Timer{}, u8 EssentialInfo, and null-terminated UTF-16 text.
    payload = struct.pack("<qB", 0, 1) + (message + "\0").encode("utf-16-le")
    connection = http.client.HTTPConnection("127.0.0.1", port, timeout=LIT_HTTP_TIMEOUT_SECONDS)
    try:
        connection.request("POST", "/", body=payload, headers={"Content-Type": "application/octet-stream"})
        response = connection.getresponse()
        body = response.read()
        if response.status != 200 or body:
            raise SmokeFailure(f"HTTP log endpoint returned status {response.status}, body {body!r}")
        return {"message": message, "status": response.status, "payload_bytes": len(payload)}
    finally:
        connection.close()


def exercise_resize_message_publication(process, backend, handle, port, directory, pattern):
    messages = [f"LoggerResizeUnique: pid={process.pid} id={index}" for index in range(RESIZE_MESSAGE_COUNT)]
    baseline = snapshot_log_files(directory, pattern)
    before_rows = query_log_list(backend, handle, LB_GETCOUNT)
    deadline = time.monotonic() + RESIZE_SECONDS

    def publish_messages():
        responses = []
        for message in messages:
            if time.monotonic() >= deadline:
                raise SmokeFailure("HTTP resize-message publication exceeded its deadline")
            responses.append(post_log_message(port, message))
            time.sleep(LIT_MESSAGE_PUBLICATION_INTERVAL_SECONDS)
        return responses

    completions = 0
    with ThreadPoolExecutor(max_workers=1) as executor:
        future = executor.submit(publish_messages)
        for index in range(32):
            if time.monotonic() >= deadline:
                raise SmokeFailure("logger resize completion exceeded its deadline")
            ensure_process_running(process, "during resize/message publication", LIT_LOGSERVER)
            backend.resize_client(handle, 920 + (index & 1) * 40, 600)
            result = ctypes.c_size_t()
            if not backend.user32.SendMessageTimeoutW(handle, WM_EXITSIZEMOVE, 0, 0, SMTO_ABORTIFHUNG, LIT_WINDOW_MESSAGE_TIMEOUT_MS, ctypes.byref(result)):
                raise SmokeFailure("logger did not process resize completion during HTTP publication")
            completions += 1
            time.sleep(LIT_RESIZE_POLL_SECONDS)
        responses = future.result(timeout=max(LIT_MINIMUM_WAIT_SECONDS, deadline - time.monotonic()))

    wait_for_log_message(directory, baseline, pattern, messages[-1], LIT_LOGGER_READY_TIMEOUT_SECONDS)
    expected_rows = before_rows + len(messages)
    wait_for_visible_log_rows(process, backend, handle, expected_rows)
    actual_rows = query_log_list(backend, handle, LB_GETCOUNT)
    if actual_rows != expected_rows:
        raise SmokeFailure(f"resize/message publication displays {actual_rows} rows; expected exactly {expected_rows}")
    text = collect_log_delta(directory, baseline, pattern)
    for message in messages:
        if text.count(message) != 1:
            raise SmokeFailure(f"resize HTTP message does not occur exactly once in the log: {message}")
    return {LIT_MESSAGES: messages, "responses": responses, "resize_completions": completions, "rows_before": before_rows, LIT_EXPECTED_ROWS: expected_rows, "actual_rows": actual_rows}


def capture_visible_messages(backend, handle, output, minimum_rows=1):
    capture = backend.capture_client_window(handle, output)
    width, height, rows = read_bmp_24_rows(output)
    if (width, height) != (capture.width, capture.height):
        raise SmokeFailure("saved logger capture dimensions changed")
    count = query_log_list(backend, handle, LB_GETCOUNT)
    if count < minimum_rows:
        raise SmokeFailure(f"logger displays {count} rows; expected at least {minimum_rows}")
    visible_rows = []
    for index in range(count):
        rect = WinRect()
        query_log_list(backend, handle, LB_GETITEMRECT, index, ctypes.addressof(rect))
        if rect.left < 0 or rect.top < 0 or rect.right > capture.width or rect.bottom > capture.height:
            raise SmokeFailure("log rows are outside the captured client area")
        # Owner-drawn rows have no stored text for LB_GETTEXT. Require painted glyphs in every visible row.
        dark_pixels = sum(max(pixel) < 180 for row in rows[rect.top:rect.bottom] for pixel in row[max(1, rect.left):max(1, rect.right - 1)])
        if dark_pixels < 20:
            raise SmokeFailure(f"logger row {index} has no visible log text ({dark_pixels} dark pixels)")
        visible_rows.append({"index": index, "text_pixels": dark_pixels, "rect": [rect.left, rect.top, rect.right, rect.bottom]})
    return {"path": str(output), "width": capture.width, "height": capture.height, "visible_log_rows": visible_rows}


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", required=True, type=Path)
    parser.add_argument("--working-directory", required=True, type=Path)
    parser.add_argument("--output-directory", required=True, type=Path)
    parser.add_argument("--max-cpu-fraction", type=float, default=MAX_CPU_FRACTION)
    args = parser.parse_args(argv)
    if args.max_cpu_fraction <= 0 or not args.max_cpu_fraction < float("inf"):
        parser.error("--max-cpu-fraction must be positive and finite")
    if os.name != "nt":
        write_status("SKIP: logger idle CPU/window-close regression requires Windows")
        return SKIP_EXIT_CODE

    args.executable = args.executable.resolve()
    args.working_directory = args.working_directory.resolve()
    args.output_directory = args.output_directory.resolve()
    args.output_directory.mkdir(parents=True, exist_ok=True)
    evidence = {"scenario": "active_logger_busy_spin", "executable": str(args.executable), "settle_seconds": SETTLE_SECONDS, "sample_seconds": SAMPLE_SECONDS, "max_cpu_fraction": args.max_cpu_fraction, LIT_STARTUP_RECEIVED: False,
                LIT_CLEAN_WM_CLOSE_EXIT: False, "screenshot_path": str(args.output_directory / LIT_LOGGER_BMP)}
    failures = []
    skip_reason = None
    required_messages = []
    process = backend = handle = None
    directory = baseline = pattern = None
    screenshot = args.output_directory / LIT_LOGGER_BMP
    wake_screenshot = args.output_directory / "logger_after_idle.bmp"
    try:
        backend = WindowsCapture()
        bind_window_queries(backend.user32)
        launch_args = SimpleNamespace(no_logserver=False, logserver_executable=str(args.executable), log_port=None, working_directory=args.working_directory, timeout=LIT_LOGGER_READY_TIMEOUT_SECONDS)
        process, port, directory, baseline, pattern = launch_logserver(launch_args, args.executable, os.environ.copy())
        evidence.update({"pid": process.pid, "port": port})
        handle = backend.wait_for_window(process.pid, LIT_LOGGER_READY_TIMEOUT_SECONDS)
        if not handle:
            raise SmokeFailure("logger did not create a visible window")
        evidence["window_handle"] = handle
        startup = f"Log server: listening on port {port}"
        required_messages.append(startup)
        wait_for_log_message(directory, baseline, pattern, startup, LIT_LOGGER_READY_TIMEOUT_SECONDS)
        evidence.update({LIT_STARTUP_RECEIVED: True, "startup_message": startup})
        backend.prepare_window(handle)
        require_active_window(backend.user32, handle)
        initial_rows = query_log_list(backend, handle, LB_GETCOUNT)
        message = f"LoggerBusySpin: HTTP message before idle sample (pid={process.pid})"
        required_messages.append(message)
        message_baseline = snapshot_log_files(directory, pattern)
        evidence["http_before_idle"] = post_log_message(port, message)
        wait_for_log_message(directory, message_baseline, pattern, message, LIT_LOGGER_READY_TIMEOUT_SECONDS)
        wait_for_visible_log_rows(process, backend, handle, initial_rows + 1)
        time.sleep(SETTLE_SECONDS)
        evidence[LIT_CPU] = sample_idle_cpu(process, backend, handle)
        evidence[LIT_SCREENSHOT] = capture_visible_messages(backend, handle, screenshot, initial_rows + 1)

        rows_before_wakeup = query_log_list(backend, handle, LB_GETCOUNT)
        wake_message = f"LoggerBusySpin: fresh HTTP message after idle sample (pid={process.pid})"
        required_messages.append(wake_message)
        wake_baseline = snapshot_log_files(directory, pattern)
        wake_started = time.monotonic()
        evidence["http_after_idle"] = post_log_message(port, wake_message)
        wait_for_log_message(directory, wake_baseline, pattern, wake_message, LIT_LOGGER_READY_TIMEOUT_SECONDS)
        wait_for_visible_log_rows(process, backend, handle, rows_before_wakeup + 1)
        evidence["post_idle_wakeup_seconds"] = time.monotonic() - wake_started
        evidence["post_idle_wakeup_received"] = True
        evidence["screenshot_after_idle"] = capture_visible_messages(backend, handle, wake_screenshot, rows_before_wakeup + 1,)
        if evidence[LIT_CPU][LIT_SINGLE_CORE_CPU_FRACTION] >= args.max_cpu_fraction:
            raise SmokeFailure(f"active idle logger consumed {evidence[LIT_CPU][LIT_SINGLE_CORE_CPU_FRACTION]:.3f} of one CPU core; " f"required < {args.max_cpu_fraction:.3f}")
        evidence[LIT_RESIZE] = exercise_resize_message_publication(process, backend, handle, port, directory, pattern)
        required_messages.extend(evidence[LIT_RESIZE][LIT_MESSAGES])
        evidence["screenshot_after_resize"] = capture_visible_messages(backend, handle, args.output_directory / "logger_after_resize.bmp", evidence[LIT_RESIZE][LIT_EXPECTED_ROWS])
    except SmokeSkip as error:
        skip_reason = str(error)
    except Exception as error:
        failures.append(str(error))
    finally:
        if process is not None:
            if handle and LIT_SCREENSHOT not in evidence and process.poll() is None:
                try:
                    evidence[LIT_SCREENSHOT] = capture_visible_messages(backend, handle, screenshot)
                except Exception as error:
                    failures.append(f"capture: {error}")
            try:
                ensure_process_running(process, "before WM_CLOSE", LIT_LOGSERVER)
                if not request_windows_graceful_exit(process.pid):
                    raise SmokeFailure("no logger window accepted WM_CLOSE")
                exit_code = process.wait(timeout=LIT_GRACEFUL_EXIT_TIMEOUT_SECONDS)
                require_normal_process_exit(exit_code, "", LIT_LOGSERVER)
                evidence[LIT_CLEAN_WM_CLOSE_EXIT] = True
            except Exception as error:
                failures.append(f"WM_CLOSE: {error}")
            finally:
                try:
                    exit_code, tail = terminate_process(process, LIT_LOGSERVER)
                    evidence.update({"exit_code": exit_code, "process_output_tail": tail})
                    if tail:
                        validate_expected_log_text(tail, (), LOG_FAILURE_MESSAGES)
                    evidence["process_output_checked"] = True
                except Exception as error:
                    failures.append(f"shutdown: {error}")
        if directory:
            try:
                log_text = collect_log_delta(directory, baseline, pattern)
                (args.output_directory / "logger.log").write_text(log_text, encoding=LIT_UTF_8)
                validate_expected_log_text(log_text, required_messages, LOG_FAILURE_MESSAGES)
                evidence["log_diagnostics_checked"] = True
            except Exception as error:
                failures.append(f"log evidence: {error}")
        if backend:
            backend.close()
        evidence.update({"passed": not failures and skip_reason is None, "skipped": skip_reason is not None, "skip_reason": skip_reason, "failures": failures})
        (args.output_directory / "evidence.json").write_text(json.dumps(evidence, indent=2) + "\n", encoding=LIT_UTF_8)

    for failure in failures:
        write_status(f"FAIL: {failure}")
    if failures:
        return 1
    if skip_reason is not None:
        write_status(f"SKIP: {skip_reason}")
        return SKIP_EXIT_CODE
    write_status(f"PASS: active logger idle CPU fraction {evidence[LIT_CPU][LIT_SINGLE_CORE_CPU_FRACTION]:.3f}; clean WM_CLOSE exit")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
