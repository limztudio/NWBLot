#!/usr/bin/env python3
"""Regress an active logger window busy-spinning while it has no new messages."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import ctypes
import http.client
import json
import os
import shutil
from pathlib import Path
import struct
import sys
import time
from types import SimpleNamespace

from window_capture_logs import wait_for_log_message
from window_capture_process_closer import WindowProcessCloser
from window_capture_termination import CaptureProcessTermination
from window_capture_smoke import (SKIP_EXIT_CODE, STRICT_LOG_FAILURE_MESSAGES, SmokeFailure, SmokeSkip, WindowsCapture, collect_log_delta, ensure_process_running, launch_logserver, require_normal_process_exit,
    snapshot_log_files, write_status,)
from window_capture_validation import validate_expected_log_text
from logger_window_evidence import (LoggerWindowEvidence, LB_GETCOUNT, LIT_LOGGER_READY_TIMEOUT_SECONDS, LIT_LOGSERVER, LIT_SINGLE_CORE_CPU_FRACTION,
    LIT_WINDOW_MESSAGE_TIMEOUT_MS, SAMPLE_SECONDS, SMTO_ABORTIFHUNG,)

LIT_MINIMUM_WAIT_SECONDS = 0.001

LIT_GRACEFUL_EXIT_TIMEOUT_SECONDS = 10.0
LIT_HTTP_TIMEOUT_SECONDS = 5.0
LIT_MESSAGE_PUBLICATION_INTERVAL_SECONDS = 0.01
LIT_RESIZE_POLL_SECONDS = 0.005

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
MAX_CPU_FRACTION = 0.15
WM_EXITSIZEMOVE = 0x0232
RESIZE_MESSAGE_COUNT = 6
RESIZE_SECONDS = 8.0
LOG_FAILURE_MESSAGES = (*STRICT_LOG_FAILURE_MESSAGES, "Log server: failed", "Log client: failed", "Exception:")


def retain_and_validate_process_output(capture, output_directory):
    output_path = output_directory / f"process_output_{capture.path.name}"
    shutil.copyfile(capture.path, output_path)
    output_text = output_path.read_text(encoding=LIT_UTF_8, errors="replace")
    if output_text:
        validate_expected_log_text(output_text, (), LOG_FAILURE_MESSAGES)
    return output_path


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
    before_rows = LoggerWindowEvidence.query_log_list(backend, handle, LB_GETCOUNT)
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
    LoggerWindowEvidence.wait_for_visible_log_rows(process, backend, handle, expected_rows)
    actual_rows = LoggerWindowEvidence.query_log_list(backend, handle, LB_GETCOUNT)
    if actual_rows != expected_rows:
        raise SmokeFailure(f"resize/message publication displays {actual_rows} rows; expected exactly {expected_rows}")
    text = collect_log_delta(directory, baseline, pattern)
    for message in messages:
        if text.count(message) != 1:
            raise SmokeFailure(f"resize HTTP message does not occur exactly once in the log: {message}")
    return {LIT_MESSAGES: messages, "responses": responses, "resize_completions": completions, "rows_before": before_rows, LIT_EXPECTED_ROWS: expected_rows, "actual_rows": actual_rows}


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
        LoggerWindowEvidence.bind_window_queries(backend.user32)
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
        LoggerWindowEvidence.require_active_window(backend.user32, handle)
        initial_rows = LoggerWindowEvidence.query_log_list(backend, handle, LB_GETCOUNT)
        message = f"LoggerBusySpin: HTTP message before idle sample (pid={process.pid})"
        required_messages.append(message)
        message_baseline = snapshot_log_files(directory, pattern)
        evidence["http_before_idle"] = post_log_message(port, message)
        wait_for_log_message(directory, message_baseline, pattern, message, LIT_LOGGER_READY_TIMEOUT_SECONDS)
        LoggerWindowEvidence.wait_for_visible_log_rows(process, backend, handle, initial_rows + 1)
        time.sleep(SETTLE_SECONDS)
        evidence[LIT_CPU] = LoggerWindowEvidence.sample_idle_cpu(process, backend, handle)
        evidence[LIT_SCREENSHOT] = LoggerWindowEvidence.capture_visible_messages(backend, handle, screenshot, initial_rows + 1)

        rows_before_wakeup = LoggerWindowEvidence.query_log_list(backend, handle, LB_GETCOUNT)
        wake_message = f"LoggerBusySpin: fresh HTTP message after idle sample (pid={process.pid})"
        required_messages.append(wake_message)
        wake_baseline = snapshot_log_files(directory, pattern)
        wake_started = time.monotonic()
        evidence["http_after_idle"] = post_log_message(port, wake_message)
        wait_for_log_message(directory, wake_baseline, pattern, wake_message, LIT_LOGGER_READY_TIMEOUT_SECONDS)
        LoggerWindowEvidence.wait_for_visible_log_rows(process, backend, handle, rows_before_wakeup + 1)
        evidence["post_idle_wakeup_seconds"] = time.monotonic() - wake_started
        evidence["post_idle_wakeup_received"] = True
        evidence["screenshot_after_idle"] = LoggerWindowEvidence.capture_visible_messages(backend, handle, wake_screenshot, rows_before_wakeup + 1,)
        if evidence[LIT_CPU][LIT_SINGLE_CORE_CPU_FRACTION] >= args.max_cpu_fraction:
            raise SmokeFailure(f"active idle logger consumed {evidence[LIT_CPU][LIT_SINGLE_CORE_CPU_FRACTION]:.3f} of one CPU core; " f"required < {args.max_cpu_fraction:.3f}")
        evidence[LIT_RESIZE] = exercise_resize_message_publication(process, backend, handle, port, directory, pattern)
        required_messages.extend(evidence[LIT_RESIZE][LIT_MESSAGES])
        evidence["screenshot_after_resize"] = LoggerWindowEvidence.capture_visible_messages(backend, handle, args.output_directory / "logger_after_resize.bmp", evidence[LIT_RESIZE][LIT_EXPECTED_ROWS])
    except SmokeSkip as error:
        skip_reason = str(error)
    except Exception as error:
        failures.append(str(error))
    finally:
        if process is not None:
            if handle and LIT_SCREENSHOT not in evidence and process.poll() is None:
                try:
                    evidence[LIT_SCREENSHOT] = LoggerWindowEvidence.capture_visible_messages(backend, handle, screenshot)
                except Exception as error:
                    failures.append(f"capture: {error}")
            try:
                ensure_process_running(process, "before WM_CLOSE", LIT_LOGSERVER)
                if not WindowProcessCloser.request_windows_graceful_exit(process.pid):
                    raise SmokeFailure("no logger window accepted WM_CLOSE")
                exit_code = process.wait(timeout=LIT_GRACEFUL_EXIT_TIMEOUT_SECONDS)
                require_normal_process_exit(exit_code, "", LIT_LOGSERVER)
                evidence[LIT_CLEAN_WM_CLOSE_EXIT] = True
            except Exception as error:
                failures.append(f"WM_CLOSE: {error}")
            finally:
                try:
                    capture = getattr(process, "_nwb_output_capture", None)
                    exit_code, tail = CaptureProcessTermination.terminate_process(process, LIT_LOGSERVER, preserve_capture=True)
                    if capture is None:
                        raise SmokeFailure("logger process output capture is missing")
                    output_path = retain_and_validate_process_output(capture, args.output_directory)
                    evidence.update({"exit_code": exit_code, "process_output_tail": tail, "process_output_path": str(output_path)})
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
            try:
                backend.close()
            except Exception as error:
                failures.append(f"window backend cleanup: {error}")
        evidence.update({"passed": not failures and skip_reason is None, "skipped": skip_reason is not None, "skip_reason": skip_reason, "failures": failures})
        try:
            (args.output_directory / "evidence.json").write_text(json.dumps(evidence, indent=2) + "\n", encoding=LIT_UTF_8)
        except OSError as error:
            failures.append(f"evidence write: {error}")

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
