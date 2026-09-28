#!/usr/bin/env python3
"""Run and report the command-IR CPU-overhead probe.

One native process records the same built-in copy-buffer command set through the stable direct
path and through command-IR capture, then measures reader decode, replay preflight,
Core::CommandList replay, and experimental direct-Vulkan CopyBuffer replay. The runner
treats the stream/replay invariants and allocation-free timed capture as correctness gates;
the timing values are CPU-only
per-command overhead samples, not GPU-performance results.
"""

from __future__ import annotations

import argparse
import json
import math
import re
import shlex
import statistics
import subprocess
import sys
import time
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from types import SimpleNamespace
from typing import Any, Dict, List, Optional, Sequence


sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from profile_probe import ProfileFailure, capture_vulkan_summary, parse_result  # noqa: E402

# Shared literals (no inline hardcodes below this block).
LIT_NATIVE_RECORD = "native_record"
LIT_CAPTURE_RECORD = "capture_record"
LIT_READER_DECODE = "reader_decode"
LIT_PREFLIGHT = "preflight"
LIT_REPLAY = "replay"
LIT_DIRECT_VULKAN_REPLAY = "direct_vulkan_replay"
LIT_RECORDS = "--records"
LIT_WARMUP = "--warmup"
LIT_SAMPLES = "--samples"
LIT_ADAPTER_INDEX = "--adapter-index"
LIT_GPU_VALIDATION = "--gpu-validation"
LIT_NO_GPU_VALIDATION = "--no-gpu-validation"
LIT_COMMAND_IR_PROFILE_LOG = "command_ir_profile.log"
LIT_UTF_8 = "utf-8"
LIT_N = "\n"
LIT_SAMPLES_NS_PER_COMMAND = "samples_ns_per_command"
LIT_MIN_NS_PER_COMMAND = "min_ns_per_command"
LIT_MEDIAN_NS_PER_COMMAND = "median_ns_per_command"
LIT_MAX_NS_PER_COMMAND = "max_ns_per_command"
LIT_STATUS = "status"
LIT_OK = "ok"
LIT_WORKLOAD = "workload"
LIT_COPY_BUFFER = "copy_buffer"
LIT_REQUESTED_ADAPTER_INDEX = "requested_adapter_index"
LIT_RECORDS_2 = "records"
LIT_WARMUP_2 = "warmup"
LIT_SAMPLES_2 = "samples"
LIT_DECODED_RECORDS = "decoded_records"
LIT_PREFLIGHT_RECORDS = "preflight_records"
LIT_REPLAYED_RECORDS = "replayed_records"
LIT_DIRECT_VULKAN_REPLAYED_RECORDS = "direct_vulkan_replayed_records"
LIT_LOGGER_ERRORS = "logger_errors"
LIT_CAPTURE_ALLOCATION_DELTA = "capture_allocation_delta"
LIT_CAPTURE_REALLOCATION_DELTA = "capture_reallocation_delta"
LIT_SELECTED_ADAPTER_VENDOR_ID = "selected_adapter_vendor_id"
LIT_SELECTED_ADAPTER_DEVICE_ID = "selected_adapter_device_id"
LIT_STREAM_BYTES = "stream_bytes"
LIT_PAYLOAD_BYTES = "payload_bytes"
LIT_SELECTED_ADAPTER_UUID = "selected_adapter_uuid"
LIT_STREAM_VALID = "stream_valid"
LIT_CHECKSUM_VERIFIED = "checksum_verified"
LIT_DIRECT_VULKAN_CHECKSUM_VERIFIED = "direct_vulkan_checksum_verified"
LIT_SKIP_REASON = "skip_reason"
LIT_ERROR = "error"
LIT_PROCESS = "process"
LIT_PROFILE = "profile"
LIT_PASSED = "passed"
LIT_METRICS = "metrics"
LIT_CAPTURE_ENCODE_INCREMENT_PERCENT = "capture_encode_increment_percent"
LIT_DIRECT_VULKAN_REPLAY_DELTA_PERCENT = "direct_vulkan_replay_delta_percent"
LIT_VULKANINFO_HOST_INVENTORY = "vulkaninfo_host_inventory"
LIT_SELF_TEST = "--self-test"
LIT_STORE_TRUE = "store_true"
LIT_GPU_VALIDATION_2 = "gpu_validation"
LIT_FAILED = "failed"
LIT_RETURN_CODE = "return_code"
LIT_ELAPSED_SECONDS = "elapsed_seconds"
LIT_LOG = "log"
LIT_SKIPPED = "skipped"
LIT_MAIN = "__main__"


SKIP_EXIT_CODE = 77
RESULT_PREFIX = "NWB_COMMAND_IR_PROFILE_RESULT "
TIMING_STAGES = (
    LIT_NATIVE_RECORD,
    LIT_CAPTURE_RECORD,
    LIT_READER_DECODE,
    LIT_PREFLIGHT,
    LIT_REPLAY,
    LIT_DIRECT_VULKAN_REPLAY,
)
MAX_RECORDS = 65536
MAX_SAMPLES = 64


@dataclass(frozen=True)
class ProfileResult:
    return_code: int
    elapsed_seconds: float
    payload: Optional[Dict[str, Any]]
    log_path: Path


def profile_command(args: argparse.Namespace) -> List[str]:
    return [
        str(args.executable),
        LIT_RECORDS,
        str(args.records),
        LIT_WARMUP,
        str(args.warmup),
        LIT_SAMPLES,
        str(args.samples),
        LIT_ADAPTER_INDEX,
        str(args.adapter_index),
        LIT_GPU_VALIDATION if args.gpu_validation else LIT_NO_GPU_VALIDATION,
    ]


def run_profile(args: argparse.Namespace, output_dir: Path) -> ProfileResult:
    command = profile_command(args)
    started = time.perf_counter()
    completed = subprocess.run(
        command,
        cwd=args.executable.parent,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    elapsed_seconds = time.perf_counter() - started
    log_path = output_dir / LIT_COMMAND_IR_PROFILE_LOG
    log_path.write_text(completed.stdout, encoding=LIT_UTF_8)
    return ProfileResult(completed.returncode, elapsed_seconds, parse_result(completed.stdout, RESULT_PREFIX), log_path)


def write_profile_command(args: argparse.Namespace, output_dir: Path) -> None:
    command = profile_command(args)
    rendered = subprocess.list2cmdline(command) if sys.platform == "win32" else shlex.join(command)
    (output_dir / "profile-command.txt").write_text(rendered + LIT_N, encoding=LIT_UTF_8)


def require_integer(payload: Dict[str, Any], field: str, *, minimum: int = 0) -> int:
    value = payload.get(field)
    if not isinstance(value, int) or isinstance(value, bool) or value < minimum:
        raise ProfileFailure(f"profile result field {field!r} must be an integer >= {minimum}, got {value!r}")
    return value


def require_finite_number(value: Any, description: str) -> float:
    if not isinstance(value, (int, float)) or isinstance(value, bool):
        raise ProfileFailure(f"{description} must be a finite number, got {value!r}")
    numeric = float(value)
    if not math.isfinite(numeric) or numeric < 0.0:
        raise ProfileFailure(f"{description} must be finite and non-negative, got {value!r}")
    return numeric


def require_timing(payload: Dict[str, Any], stage: str, samples: int) -> Dict[str, Any]:
    timing = payload.get(stage)
    if not isinstance(timing, dict):
        raise ProfileFailure(f"profile result is missing the {stage!r} timing object")

    raw_samples = timing.get(LIT_SAMPLES_NS_PER_COMMAND)
    if not isinstance(raw_samples, list) or len(raw_samples) != samples:
        actual = len(raw_samples) if isinstance(raw_samples, list) else type(raw_samples).__name__
        raise ProfileFailure(f"{stage} timing must contain exactly {samples} per-command samples, got {actual}")
    values = [require_finite_number(value, f"{stage} sample {index}") for index, value in enumerate(raw_samples)]

    aggregates = {
        LIT_MIN_NS_PER_COMMAND: min(values),
        LIT_MEDIAN_NS_PER_COMMAND: float(statistics.median(values)),
        LIT_MAX_NS_PER_COMMAND: max(values),
    }
    for field, expected in aggregates.items():
        actual = require_finite_number(timing.get(field), f"{stage} {field}")
        tolerance = max(1.0e-6, abs(expected) * 1.0e-9)
        if not math.isclose(actual, expected, rel_tol=0.0, abs_tol=tolerance):
            raise ProfileFailure(
                f"{stage} {field} does not match raw samples: expected {expected!r}, got {actual!r}"
            )
    return timing


def require_ok(args: argparse.Namespace, result: ProfileResult) -> Dict[str, Any]:
    if result.return_code != 0:
        raise ProfileFailure(f"profile process failed with exit code {result.return_code}; see {result.log_path}")
    if result.payload is None:
        raise ProfileFailure(f"profile process did not emit a result; see {result.log_path}")

    payload = result.payload
    if payload.get(LIT_STATUS) != LIT_OK:
        raise ProfileFailure(f"profile process reported status {payload.get('status')!r}; see {result.log_path}")

    if payload.get(LIT_WORKLOAD) != LIT_COPY_BUFFER:
        raise ProfileFailure(f"profile workload must be 'copy_buffer', got {payload.get('workload')!r}")

    expected_fields = {
        LIT_REQUESTED_ADAPTER_INDEX: args.adapter_index,
        LIT_RECORDS_2: args.records,
        LIT_WARMUP_2: args.warmup,
        LIT_SAMPLES_2: args.samples,
        LIT_DECODED_RECORDS: args.records,
        LIT_PREFLIGHT_RECORDS: args.records,
        LIT_REPLAYED_RECORDS: args.records,
        LIT_DIRECT_VULKAN_REPLAYED_RECORDS: args.records,
        LIT_LOGGER_ERRORS: 0,
        LIT_CAPTURE_ALLOCATION_DELTA: 0,
        LIT_CAPTURE_REALLOCATION_DELTA: 0,
    }
    for field, expected in expected_fields.items():
        actual = require_integer(payload, field)
        if actual != expected:
            raise ProfileFailure(f"profile mismatch for {field}: expected {expected!r}, got {actual!r}")

    for field in (LIT_SELECTED_ADAPTER_VENDOR_ID, LIT_SELECTED_ADAPTER_DEVICE_ID, LIT_STREAM_BYTES, LIT_PAYLOAD_BYTES):
        require_integer(payload, field)
    adapter_uuid = payload.get(LIT_SELECTED_ADAPTER_UUID)
    if not isinstance(adapter_uuid, str) or re.fullmatch(r"[0-9a-fA-F]{32}", adapter_uuid) is None:
        raise ProfileFailure("profile did not emit a 32-hex-character selected adapter UUID")
    if payload[LIT_STREAM_BYTES] <= 0:
        raise ProfileFailure("profile stream_bytes must be positive")
    if payload[LIT_PAYLOAD_BYTES] <= 0 or payload[LIT_PAYLOAD_BYTES] > payload[LIT_STREAM_BYTES]:
        raise ProfileFailure("profile payload_bytes must be positive and no larger than stream_bytes")
    if payload.get(LIT_STREAM_VALID) is not True:
        raise ProfileFailure("profile did not validate the command-IR stream")
    if payload.get(LIT_CHECKSUM_VERIFIED) is not True:
        raise ProfileFailure("profile did not verify replay output against the known source data")
    if payload.get(LIT_DIRECT_VULKAN_CHECKSUM_VERIFIED) is not True:
        raise ProfileFailure("profile did not verify direct-Vulkan replay output against the known source data")

    for stage in TIMING_STAGES:
        require_timing(payload, stage, args.samples)
    return payload


def format_timing(timing: Dict[str, Any]) -> str:
    return (
        f"{float(timing['median_ns_per_command']):.3f} ns "
        f"(min {float(timing['min_ns_per_command']):.3f}, max {float(timing['max_ns_per_command']):.3f})"
    )


def capture_increment_percent(payload: Dict[str, Any]) -> Optional[float]:
    native = float(payload[LIT_NATIVE_RECORD][LIT_MEDIAN_NS_PER_COMMAND])
    capture = float(payload[LIT_CAPTURE_RECORD][LIT_MEDIAN_NS_PER_COMMAND])
    if native <= 0.0:
        return None
    return (capture - native) * 100.0 / native


def direct_vulkan_replay_delta_percent(payload: Dict[str, Any]) -> Optional[float]:
    command_list = float(payload[LIT_REPLAY][LIT_MEDIAN_NS_PER_COMMAND])
    direct_vulkan = float(payload[LIT_DIRECT_VULKAN_REPLAY][LIT_MEDIAN_NS_PER_COMMAND])
    if command_list <= 0.0:
        return None
    return (direct_vulkan - command_list) * 100.0 / command_list


def markdown_report(report: Dict[str, Any]) -> str:
    status = report[LIT_STATUS]
    lines = [
        "# Command-IR copy-buffer overhead profile",
        "",
        f"Status: **{status}**",
        "",
        "This is a CPU-only overhead probe for the copy-buffer opcode shape. It compares direct native recording with command-IR capture, "
        "reader decode, replay preflight, Core::CommandList replay, and experimental direct-Vulkan replay in the same native process.",
        "",
    ]
    if report.get(LIT_SKIP_REASON):
        lines += [f"Skip reason: `{report['skip_reason']}`", ""]
    if report.get(LIT_ERROR):
        lines += [f"Error: `{report['error']}`", ""]

    process = report.get(LIT_PROCESS)
    if process:
        lines += [
            "## Process",
            "",
            f"- Return code: `{process['return_code']}`",
            f"- Host elapsed time: `{process['elapsed_seconds']:.6f} s`",
            f"- Log: `{process['log']}`",
            "",
        ]

    payload = report.get(LIT_PROFILE)
    if payload and report.get(LIT_STATUS) == LIT_PASSED:
        lines += [
            "## Integrity",
            "",
            f"- Records per sample: `{payload['records']}`",
            f"- Warm-up samples: `{payload['warmup']}`",
            f"- Measured samples: `{payload['samples']}`",
            f"- Stream bytes: `{payload['stream_bytes']}` (payload `{payload['payload_bytes']}`)",
            f"- Decoded / preflight / Core replayed / direct replayed records: `{payload['decoded_records']}` / "
            f"`{payload['preflight_records']}` / `{payload['replayed_records']}` / "
            f"`{payload['direct_vulkan_replayed_records']}`",
            f"- Timed capture allocation / reallocation deltas: `{payload['capture_allocation_delta']}` / "
            f"`{payload['capture_reallocation_delta']}`",
            "",
            "## CPU overhead per command",
            "",
            "| Stage | Median (min, max) |",
            "| --- | ---: |",
        ]
        labels = {
            LIT_NATIVE_RECORD: "Native record",
            LIT_CAPTURE_RECORD: "IR capture record",
            LIT_READER_DECODE: "Reader decode",
            LIT_PREFLIGHT: "Replay preflight",
            LIT_REPLAY: "Core::CommandList replay",
            LIT_DIRECT_VULKAN_REPLAY: "Direct Vulkan replay",
        }
        for stage in TIMING_STAGES:
            lines.append(f"| {labels[stage]} | {format_timing(payload[stage])} |")
        increment = report.get(LIT_METRICS, {}).get(LIT_CAPTURE_ENCODE_INCREMENT_PERCENT)
        if increment is not None:
            lines += ["", f"Median capture encode increment over native recording: `{float(increment):.2f}%`."]
        direct_delta = report.get(LIT_METRICS, {}).get(LIT_DIRECT_VULKAN_REPLAY_DELTA_PERCENT)
        if direct_delta is not None:
            lines += [
                f"Median direct-Vulkan replay delta relative to Core::CommandList replay: `{float(direct_delta):.2f}%`."
            ]
        lines += [
            "",
            "All timed capture samples must remain allocation-free after the capture buffer is primed. "
            "This allocation gate applies only to the dedicated capture arena. Native/capture recording include "
            "the recorder's command-list creation; both replay measurements exclude list create/open/close but include "
            "their internal preflight and lowering. Direct-Vulkan replay also excludes the caller's graph-owned state "
            "setup, which is verified separately. These values do not measure GPU execution time or establish a runtime "
            "adoption decision by themselves.",
            "",
        ]

    inventory = report.get(LIT_VULKANINFO_HOST_INVENTORY)
    if inventory:
        lines += [f"Host Vulkan inventory: `{inventory}`", ""]
    return LIT_N.join(lines)


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(LIT_SELF_TEST, action=LIT_STORE_TRUE, help="Run parser/result checks without Vulkan.")
    parser.add_argument("--executable", type=Path, help="Path to nwb_command_ir_profile.")
    parser.add_argument("--output-dir", type=Path, help="Artifact directory.")
    parser.add_argument(LIT_RECORDS, type=int, default=4096, help="Copy-buffer records per measured sample (default: 4096).")
    parser.add_argument(LIT_WARMUP, type=int, default=3, help="Unmeasured priming samples (default: 3).")
    parser.add_argument(LIT_SAMPLES, type=int, default=11, help="Measured samples per stage (default: 11).")
    parser.add_argument(LIT_ADAPTER_INDEX, type=int, default=0, help="Pinned Vulkan adapter enumeration index (default: 0).")
    validation_group = parser.add_mutually_exclusive_group()
    validation_group.add_argument(LIT_GPU_VALIDATION, dest=LIT_GPU_VALIDATION_2, action=LIT_STORE_TRUE, help="Enable Vulkan validation.")
    validation_group.add_argument(
        LIT_NO_GPU_VALIDATION,
        dest=LIT_GPU_VALIDATION_2,
        action="store_false",
        help="Run without Vulkan validation.",
    )
    parser.set_defaults(gpu_validation=True)
    args = parser.parse_args(argv)
    if args.self_test:
        return args
    if args.executable is None or args.output_dir is None:
        parser.error("--executable and --output-dir are required")
    if args.records <= 0 or args.records > MAX_RECORDS:
        parser.error(f"--records must be between 1 and {MAX_RECORDS}")
    if args.warmup <= 0:
        parser.error("--warmup must be positive")
    if args.samples <= 0 or args.samples > MAX_SAMPLES:
        parser.error(f"--samples must be between 1 and {MAX_SAMPLES}")
    if args.adapter_index < 0:
        parser.error("--adapter-index must be a non-negative Vulkan enumeration index")
    args.executable = args.executable.resolve()
    args.output_dir = args.output_dir.resolve()
    return args


def run(args: argparse.Namespace) -> int:
    if not args.executable.is_file():
        raise ProfileFailure(f"profile executable does not exist: {args.executable}")
    args.output_dir.mkdir(parents=True, exist_ok=False)
    write_profile_command(args, args.output_dir)
    vulkan_summary = capture_vulkan_summary(args.output_dir)
    report: Dict[str, Any] = {
        LIT_STATUS: LIT_FAILED,
        "generated_utc": datetime.now(timezone.utc).isoformat(),
        "parameters": {
            LIT_WORKLOAD: LIT_COPY_BUFFER,
            LIT_RECORDS_2: args.records,
            LIT_WARMUP_2: args.warmup,
            LIT_SAMPLES_2: args.samples,
            "adapter_index": args.adapter_index,
            LIT_GPU_VALIDATION_2: args.gpu_validation,
        },
        # This is host inventory only; the native payload identifies the pinned adapter.
        LIT_VULKANINFO_HOST_INVENTORY: vulkan_summary.name if vulkan_summary else None,
    }
    try:
        result = run_profile(args, args.output_dir)
        report[LIT_PROCESS] = {
            LIT_RETURN_CODE: result.return_code,
            LIT_ELAPSED_SECONDS: result.elapsed_seconds,
            LIT_LOG: result.log_path.name,
        }
        report[LIT_PROFILE] = result.payload
        if result.return_code == SKIP_EXIT_CODE:
            if result.payload is None or result.payload.get(LIT_STATUS) != LIT_SKIPPED:
                raise ProfileFailure("profile returned the skip code without a skipped result payload")
            report[LIT_STATUS] = LIT_SKIPPED
            report[LIT_SKIP_REASON] = result.payload.get("reason", "vulkan_or_profile_requirements_unavailable")
            return_code = SKIP_EXIT_CODE
        else:
            payload = require_ok(args, result)
            report[LIT_METRICS] = {
                LIT_CAPTURE_ENCODE_INCREMENT_PERCENT: capture_increment_percent(payload),
                LIT_DIRECT_VULKAN_REPLAY_DELTA_PERCENT: direct_vulkan_replay_delta_percent(payload),
            }
            report[LIT_STATUS] = LIT_PASSED
            return_code = 0
    except ProfileFailure as error:
        report[LIT_STATUS] = LIT_FAILED
        report[LIT_ERROR] = str(error)
        return_code = 1
    finally:
        (args.output_dir / "command_ir_profile_report.json").write_text(
            json.dumps(report, indent=2, sort_keys=True) + LIT_N,
            encoding=LIT_UTF_8,
        )
        (args.output_dir / "command_ir_profile_report.md").write_text(markdown_report(report), encoding=LIT_UTF_8)

    print(f"Command-IR profile report: {args.output_dir / 'command_ir_profile_report.md'}")
    return return_code


def run_self_test() -> int:
    assert parse_args([LIT_SELF_TEST]).records == 4096
    timing = {
        LIT_SAMPLES_NS_PER_COMMAND: [1.0, 2.0, 3.0],
        LIT_MIN_NS_PER_COMMAND: 1.0,
        LIT_MEDIAN_NS_PER_COMMAND: 2.0,
        LIT_MAX_NS_PER_COMMAND: 3.0,
    }
    payload: Dict[str, Any] = {
        LIT_STATUS: LIT_OK,
        LIT_WORKLOAD: LIT_COPY_BUFFER,
        LIT_REQUESTED_ADAPTER_INDEX: 0,
        LIT_SELECTED_ADAPTER_VENDOR_ID: 4098,
        LIT_SELECTED_ADAPTER_DEVICE_ID: 1234,
        LIT_SELECTED_ADAPTER_UUID: "0123456789abcdef0123456789abcdef",
        LIT_RECORDS_2: 4,
        LIT_WARMUP_2: 1,
        LIT_SAMPLES_2: 3,
        LIT_STREAM_BYTES: 512,
        LIT_PAYLOAD_BYTES: 480,
        LIT_DECODED_RECORDS: 4,
        LIT_PREFLIGHT_RECORDS: 4,
        LIT_REPLAYED_RECORDS: 4,
        LIT_DIRECT_VULKAN_REPLAYED_RECORDS: 4,
        LIT_STREAM_VALID: True,
        LIT_CHECKSUM_VERIFIED: True,
        "direct_vulkan_observed_hash": 1234,
        LIT_DIRECT_VULKAN_CHECKSUM_VERIFIED: True,
        LIT_LOGGER_ERRORS: 0,
        LIT_CAPTURE_ALLOCATION_DELTA: 0,
        LIT_CAPTURE_REALLOCATION_DELTA: 0,
    }
    payload.update({stage: dict(timing) for stage in TIMING_STAGES})
    text = f"noise\n{RESULT_PREFIX}{json.dumps(payload)}\n"
    assert parse_result(text, RESULT_PREFIX) == payload
    assert parse_result("no result", RESULT_PREFIX) is None
    args = SimpleNamespace(adapter_index=0, records=4, warmup=1, samples=3)
    result = ProfileResult(0, 0.1, payload, Path(LIT_COMMAND_IR_PROFILE_LOG))
    assert require_ok(args, result)[LIT_REPLAYED_RECORDS] == 4
    assert capture_increment_percent(payload) == 0.0
    assert direct_vulkan_replay_delta_percent(payload) == 0.0
    command_args = SimpleNamespace(
        executable=Path("/nwb/command_ir_profile"),
        records=4,
        warmup=1,
        samples=3,
        adapter_index=0,
        gpu_validation=False,
    )
    assert profile_command(command_args)[-1] == LIT_NO_GPU_VALIDATION
    report = {
        LIT_STATUS: LIT_PASSED,
        LIT_PROCESS: {LIT_RETURN_CODE: 0, LIT_ELAPSED_SECONDS: 0.1, LIT_LOG: LIT_COMMAND_IR_PROFILE_LOG},
        LIT_PROFILE: payload,
        LIT_METRICS: {
            LIT_CAPTURE_ENCODE_INCREMENT_PERCENT: 0.0,
            LIT_DIRECT_VULKAN_REPLAY_DELTA_PERCENT: 0.0,
        },
    }
    assert "CPU overhead per command" in markdown_report(report)
    print("command-IR harness self-test passed")
    return 0


def main(argv: Sequence[str]) -> int:
    args = parse_args(argv)
    if args.self_test:
        return run_self_test()
    try:
        return run(args)
    except ProfileFailure as error:
        print(f"FAIL: {error}", file=sys.stderr)
        return 1


if __name__ == LIT_MAIN:
    raise SystemExit(main(sys.argv[1:]))
