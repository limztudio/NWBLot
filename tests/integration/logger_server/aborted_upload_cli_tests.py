"""Require incomplete HTTP crash uploads to retire on disconnect and daemon shutdown."""

import argparse
import http.client
import json
import math
import os
from pathlib import Path
import shutil
import socket
import sys
import tempfile
import time
from types import SimpleNamespace

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "smoke"))
from window_capture_smoke import (STRICT_LOG_FAILURE_MESSAGES, SmokeFailure, collect_log_delta, ensure_process_running, launch_logserver, require_normal_process_exit, terminate_process, validate_expected_log_text)


def wait_for_condition(process, condition, timeout, description):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        ensure_process_running(process, description, "logserver")
        value = condition()
        if value:
            return value
        time.sleep(min(0.02, max(0.0, deadline - time.monotonic())))
    raise SmokeFailure(f"timed out {description}")


def start_partial_upload(port, process, inbox, timeout, marker):
    connection = socket.create_connection(("127.0.0.1", port), timeout=timeout)
    try:
        headers = (f"POST /crash HTTP/1.1\r\nHost: 127.0.0.1:{port}\r\n"
                   "Content-Type: application/octet-stream\r\nContent-Length: 1048576\r\n\r\n")
        connection.sendall(headers.encode("ascii") + marker * 1024)
        path = wait_for_condition(process, lambda: next((path for path in inbox.glob("*.nwbcrashpkg") if path.stat().st_size > 0 and path.read_bytes().startswith(marker)), None), timeout, "waiting for an incomplete crash upload file")
        return connection, path
    except BaseException:
        connection.close()
        raise


def run(executable, output, timeout):
    output.mkdir(parents=True, exist_ok=True)
    evidence = {"disconnect_retired": False, "shutdown_retired": False, "response_recovered": False, "concurrent_uploads_independent": False}
    with tempfile.TemporaryDirectory(prefix="aborted_upload_", dir=output) as temporary:
        runtime = Path(temporary)
        staged = runtime / executable.name
        shutil.copy2(executable, staged)
        environment = os.environ.copy()
        environment["LD_LIBRARY_PATH"] = str(executable.parent) + os.pathsep + environment.get("LD_LIBRARY_PATH", "")
        options = SimpleNamespace(no_logserver=False, logserver_executable=str(staged), log_port=None, working_directory=runtime, timeout=timeout)
        process = second_process = None
        upload = open_upload = None
        directory = baseline = pattern = None
        try:
            process, port, directory, baseline, pattern = launch_logserver(options, staged, environment)
            inbox = runtime / "crashes" / "inbox"
            second_process, second_port, _, _, _ = launch_logserver(options, staged, environment)
            first_marker = b"first independent crash upload\n"
            second_marker = b"second independent crash upload\n"
            upload, first_path = start_partial_upload(port, process, inbox, timeout, first_marker)
            open_upload, second_path = start_partial_upload(second_port, second_process, inbox, timeout, second_marker)
            if first_path == second_path or not first_path.read_bytes().startswith(first_marker) or not second_path.read_bytes().startswith(second_marker):
                raise SmokeFailure("concurrent logger processes shared or overwrote an incomplete upload")
            upload.close()
            upload = None
            wait_for_condition(process, lambda: not first_path.exists(), timeout, "waiting for disconnected upload retirement")
            evidence["disconnect_retired"] = True
            if not second_path.is_file() or not second_path.read_bytes().startswith(second_marker):
                raise SmokeFailure("disconnecting one logger removed or altered the other logger's upload")
            evidence["concurrent_uploads_independent"] = True
            health = http.client.HTTPConnection("127.0.0.1", port, timeout=timeout)
            try:
                health.request("POST", "/namesym", body=b"")
                response = health.getresponse()
                body = response.read()
                if response.status != 404 or body:
                    raise SmokeFailure(f"logger failed to recover after disconnect: {response.status}, {body!r}")
                evidence["response_recovered"] = True
            finally:
                health.close()
            exit_code, tail = terminate_process(process, "logserver after disconnected upload")
            process = None
            require_normal_process_exit(exit_code, tail, "logserver")
            validate_expected_log_text(tail, (), STRICT_LOG_FAILURE_MESSAGES)
            exit_code, tail = terminate_process(second_process, "logserver with incomplete upload")
            second_process = None
            require_normal_process_exit(exit_code, tail, "second logserver")
            if second_path.exists() or list(inbox.iterdir()):
                raise SmokeFailure("daemon shutdown retained an incomplete crash upload")
            evidence["shutdown_retired"] = True
            validate_expected_log_text(tail, (), STRICT_LOG_FAILURE_MESSAGES)
        finally:
            if upload is not None:
                upload.close()
            if open_upload is not None:
                open_upload.close()
            if process is not None:
                exit_code, tail = terminate_process(process, "logserver upload cleanup")
                require_normal_process_exit(exit_code, tail, "logserver")
            if second_process is not None:
                exit_code, tail = terminate_process(second_process, "second logserver upload cleanup")
                require_normal_process_exit(exit_code, tail, "second logserver")
            if directory is not None:
                logs = collect_log_delta(directory, baseline, pattern)
                (output / "logger.log").write_text(logs, encoding="utf-8")
                validate_expected_log_text(logs, (), STRICT_LOG_FAILURE_MESSAGES)
    (output / "evidence.json").write_text(json.dumps(evidence, indent=2) + "\n", encoding="utf-8")
    print("PASS: concurrent crash uploads remained independent and retired on disconnect and daemon shutdown")


def main():
    parser = argparse.ArgumentParser(description=__doc__, allow_abbrev=False)
    parser.add_argument("--executable", required=True, type=Path)
    parser.add_argument("--output-directory", required=True, type=Path)
    parser.add_argument("--timeout", type=float, default=10.0)
    args = parser.parse_args()
    if not math.isfinite(args.timeout) or args.timeout <= 0:
        parser.error("--timeout must be positive and finite")
    run(args.executable.resolve(strict=True), args.output_directory.resolve(), args.timeout)


if __name__ == "__main__":
    main()
