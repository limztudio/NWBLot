#!/usr/bin/env python3
"""Exercise real logger HTTP acceptance and recoverable storage failures."""

import argparse
import http.client
import json
import os
from pathlib import Path
import platform
import shutil
import struct
import sys
import time
from types import SimpleNamespace

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "smoke"))
from window_capture_logs import collect_log_delta
from window_capture_smoke import SKIP_EXIT_CODE, SmokeFailure, SmokeSkip, create_capture_backend, launch_logserver, require_normal_process_exit, terminate_process


def post(port, path, body):
    connection = http.client.HTTPConnection("127.0.0.1", port, timeout=10)
    try:
        connection.request("POST", path, body=body, headers={"Content-Type": "application/octet-stream"})
        response = connection.getresponse()
        response.read()
        return response.status
    finally:
        connection.close()


def run_case(executable, root, block_storage):
    runtime = root / ("blocked" if block_storage else "stored")
    runtime.mkdir()
    staged = runtime / executable.name
    shutil.copy2(executable, staged)
    if block_storage:
        (runtime / "telemetry").write_bytes(b"not a directory")
        (runtime / "crashes").write_bytes(b"not a directory")
    environment = os.environ.copy()
    for variable in ("PATH", "LD_LIBRARY_PATH"):
        environment[variable] = str(executable.parent) + os.pathsep + environment.get(variable, "")
    options = SimpleNamespace(no_logserver=False, logserver_executable=str(staged), log_port=0, working_directory=runtime, timeout=10)
    process = None
    directory = baseline = pattern = None
    capture = None
    results = {}
    try:
        process, port, directory, baseline, pattern = launch_logserver(options, staged, environment)
        capture = getattr(process, "_nwb_output_capture", None)
        results["malformed_message"] = post(port, "/", b"x")
        if results["malformed_message"] != 400:
            raise SmokeFailure(f"malformed message returned {results['malformed_message']}, expected 400")
        encoding = "utf-16-le" if platform.system() == "Windows" else "utf-8"
        payload = struct.pack("<qB", 0, 1) + ("valid message" + "\0").encode(encoding)
        results["oversized_message"] = post(port, "/", b"x" * (1024 * 1024 + 1))
        if results["oversized_message"] != 413:
            raise SmokeFailure(f"oversized message returned {results['oversized_message']}, expected 413")
        results["valid_message"] = post(port, "/", payload)
        if results["valid_message"] != 200:
            raise SmokeFailure(f"valid message returned {results['valid_message']}, expected 200")
        results["empty_telemetry"] = post(port, "/telemetry", b"")
        if results["empty_telemetry"] != 400:
            raise SmokeFailure(f"empty telemetry returned {results['empty_telemetry']}, expected 400")
        results["invalid_telemetry"] = post(port, "/telemetry", b"invalid telemetry")
        expected_telemetry = 503 if block_storage else 200
        if results["invalid_telemetry"] != expected_telemetry:
            raise SmokeFailure(f"telemetry returned {results['invalid_telemetry']}, expected {expected_telemetry}")
        if block_storage:
            results["unwritable_crash"] = post(port, "/crash", b"crash")
            if results["unwritable_crash"] != 503:
                raise SmokeFailure(f"unwritable crash returned {results['unwritable_crash']}, expected 503")
        elif not list((runtime / "telemetry" / "raw").glob("*.nwbs")):
            raise SmokeFailure("accepted telemetry did not retain its raw stream")
    finally:
        if process is not None:
            exit_code, tail = terminate_process(process, "logger HTTP test", preserve_capture=True)
            if capture is not None and capture.path.exists():
                shutil.copy2(capture.path, runtime / "process_output.log")
            require_normal_process_exit(exit_code, tail, "logger HTTP test")
        if directory is not None:
            (runtime / "logger.log").write_text(collect_log_delta(directory, baseline, pattern), encoding="utf-8")
    return results


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", required=True, type=Path)
    parser.add_argument("--output-directory", required=True, type=Path)
    args = parser.parse_args()
    try:
        backend = create_capture_backend()
        backend.close()
    except SmokeSkip as error:
        print(f"SKIP: {error}")
        return SKIP_EXIT_CODE
    executable = args.executable.resolve(strict=True)
    args.output_directory.mkdir(parents=True, exist_ok=True)
    root = args.output_directory / f"run_{time.time_ns()}_{os.getpid()}"
    root.mkdir()
    results = {"stored": run_case(executable, root, False), "blocked": run_case(executable, root, True)}
    (root / "results.json").write_text(json.dumps(results, indent=2) + "\n", encoding="utf-8")
    print(f"PASS: logger HTTP rejection and storage status verified; evidence: {root}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
