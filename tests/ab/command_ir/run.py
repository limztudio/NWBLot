#!/usr/bin/env python3
"""Run and report the Phase 11 command-IR CPU-overhead probe.

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
import shutil
import statistics
import subprocess
import sys
import time
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from types import SimpleNamespace
from typing import Any, Dict, List, Optional, Sequence


SKIP_EXIT_CODE = 77
RESULT_PREFIX = "NWB_COMMAND_IR_PROFILE_RESULT "
TIMING_STAGES = (
    "native_record",
    "capture_record",
    "reader_decode",
    "preflight",
    "replay",
    "direct_vulkan_replay",
)
MAX_RECORDS = 65536
MAX_SAMPLES = 64


class ProfileFailure(RuntimeError):
    pass


@dataclass(frozen=True)
class ProfileResult:
    return_code: int
    elapsed_seconds: float
    payload: Optional[Dict[str, Any]]
    log_path: Path


def parse_result(text: str) -> Optional[Dict[str, Any]]:
    for line in reversed(text.splitlines()):
        if not line.startswith(RESULT_PREFIX):
            continue
        try:
            payload = json.loads(line[len(RESULT_PREFIX) :])
        except json.JSONDecodeError as error:
            raise ProfileFailure(f"invalid profile result JSON: {error}") from error
        if not isinstance(payload, dict):
            raise ProfileFailure("profile result must be a JSON object")
        return payload
    return None


def profile_command(args: argparse.Namespace) -> List[str]:
    return [
        str(args.executable),
        "--records",
        str(args.records),
        "--warmup",
        str(args.warmup),
        "--samples",
        str(args.samples),
        "--adapter-index",
        str(args.adapter_index),
        "--gpu-validation" if args.gpu_validation else "--no-gpu-validation",
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
    log_path = output_dir / "command_ir_profile.log"
    log_path.write_text(completed.stdout, encoding="utf-8")
    return ProfileResult(completed.returncode, elapsed_seconds, parse_result(completed.stdout), log_path)


def capture_vulkan_summary(output_dir: Path) -> Optional[Path]:
    vulkaninfo = shutil.which("vulkaninfo")
    if vulkaninfo is None:
        return None
    completed = subprocess.run([vulkaninfo, "--summary"], text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    output_path = output_dir / "vulkaninfo-summary.txt"
    output_path.write_text(completed.stdout, encoding="utf-8")
    return output_path


def write_profile_command(args: argparse.Namespace, output_dir: Path) -> None:
    command = profile_command(args)
    rendered = subprocess.list2cmdline(command) if sys.platform == "win32" else shlex.join(command)
    (output_dir / "profile-command.txt").write_text(rendered + "\n", encoding="utf-8")


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

    raw_samples = timing.get("samples_ns_per_command")
    if not isinstance(raw_samples, list) or len(raw_samples) != samples:
        actual = len(raw_samples) if isinstance(raw_samples, list) else type(raw_samples).__name__
        raise ProfileFailure(f"{stage} timing must contain exactly {samples} per-command samples, got {actual}")
    values = [require_finite_number(value, f"{stage} sample {index}") for index, value in enumerate(raw_samples)]

    aggregates = {
        "min_ns_per_command": min(values),
        "median_ns_per_command": float(statistics.median(values)),
        "max_ns_per_command": max(values),
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
    if payload.get("status") != "ok":
        raise ProfileFailure(f"profile process reported status {payload.get('status')!r}; see {result.log_path}")

    if payload.get("workload") != "copy_buffer":
        raise ProfileFailure(f"profile workload must be 'copy_buffer', got {payload.get('workload')!r}")

    expected_fields = {
        "requested_adapter_index": args.adapter_index,
        "records": args.records,
        "warmup": args.warmup,
        "samples": args.samples,
        "decoded_records": args.records,
        "preflight_records": args.records,
        "replayed_records": args.records,
        "direct_vulkan_replayed_records": args.records,
        "logger_errors": 0,
        "capture_allocation_delta": 0,
        "capture_reallocation_delta": 0,
    }
    for field, expected in expected_fields.items():
        actual = require_integer(payload, field)
        if actual != expected:
            raise ProfileFailure(f"profile mismatch for {field}: expected {expected!r}, got {actual!r}")

    for field in ("selected_adapter_vendor_id", "selected_adapter_device_id", "stream_bytes", "payload_bytes"):
        require_integer(payload, field)
    adapter_uuid = payload.get("selected_adapter_uuid")
    if not isinstance(adapter_uuid, str) or re.fullmatch(r"[0-9a-fA-F]{32}", adapter_uuid) is None:
        raise ProfileFailure("profile did not emit a 32-hex-character selected adapter UUID")
    if payload["stream_bytes"] <= 0:
        raise ProfileFailure("profile stream_bytes must be positive")
    if payload["payload_bytes"] <= 0 or payload["payload_bytes"] > payload["stream_bytes"]:
        raise ProfileFailure("profile payload_bytes must be positive and no larger than stream_bytes")
    if payload.get("stream_valid") is not True:
        raise ProfileFailure("profile did not validate the command-IR stream")
    if payload.get("checksum_verified") is not True:
        raise ProfileFailure("profile did not verify replay output against the known source data")
    if payload.get("direct_vulkan_checksum_verified") is not True:
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
    native = float(payload["native_record"]["median_ns_per_command"])
    capture = float(payload["capture_record"]["median_ns_per_command"])
    if native <= 0.0:
        return None
    return (capture - native) * 100.0 / native


def direct_vulkan_replay_delta_percent(payload: Dict[str, Any]) -> Optional[float]:
    command_list = float(payload["replay"]["median_ns_per_command"])
    direct_vulkan = float(payload["direct_vulkan_replay"]["median_ns_per_command"])
    if command_list <= 0.0:
        return None
    return (direct_vulkan - command_list) * 100.0 / command_list


def markdown_report(report: Dict[str, Any]) -> str:
    status = report["status"]
    lines = [
        "# Command-IR copy-buffer overhead profile",
        "",
        f"Status: **{status}**",
        "",
        "This is a CPU-only overhead probe for the copy-buffer opcode shape. It compares direct native recording with command-IR capture, "
        "reader decode, replay preflight, Core::CommandList replay, and experimental direct-Vulkan replay in the same native process.",
        "",
    ]
    if report.get("skip_reason"):
        lines += [f"Skip reason: `{report['skip_reason']}`", ""]
    if report.get("error"):
        lines += [f"Error: `{report['error']}`", ""]

    process = report.get("process")
    if process:
        lines += [
            "## Process",
            "",
            f"- Return code: `{process['return_code']}`",
            f"- Host elapsed time: `{process['elapsed_seconds']:.6f} s`",
            f"- Log: `{process['log']}`",
            "",
        ]

    payload = report.get("profile")
    if payload and report.get("status") == "passed":
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
            "native_record": "Native record",
            "capture_record": "IR capture record",
            "reader_decode": "Reader decode",
            "preflight": "Replay preflight",
            "replay": "Core::CommandList replay",
            "direct_vulkan_replay": "Direct Vulkan replay",
        }
        for stage in TIMING_STAGES:
            lines.append(f"| {labels[stage]} | {format_timing(payload[stage])} |")
        increment = report.get("metrics", {}).get("capture_encode_increment_percent")
        if increment is not None:
            lines += ["", f"Median capture encode increment over native recording: `{float(increment):.2f}%`."]
        direct_delta = report.get("metrics", {}).get("direct_vulkan_replay_delta_percent")
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

    inventory = report.get("vulkaninfo_host_inventory")
    if inventory:
        lines += [f"Host Vulkan inventory: `{inventory}`", ""]
    return "\n".join(lines)


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--self-test", action="store_true", help="Run parser/result checks without Vulkan.")
    parser.add_argument("--executable", type=Path, help="Path to nwb_command_ir_profile.")
    parser.add_argument("--output-dir", type=Path, help="Artifact directory.")
    parser.add_argument("--records", type=int, default=4096, help="Copy-buffer records per measured sample (default: 4096).")
    parser.add_argument("--warmup", type=int, default=3, help="Unmeasured priming samples (default: 3).")
    parser.add_argument("--samples", type=int, default=11, help="Measured samples per stage (default: 11).")
    parser.add_argument("--adapter-index", type=int, default=0, help="Pinned Vulkan adapter enumeration index (default: 0).")
    validation_group = parser.add_mutually_exclusive_group()
    validation_group.add_argument("--gpu-validation", dest="gpu_validation", action="store_true", help="Enable Vulkan validation.")
    validation_group.add_argument(
        "--no-gpu-validation",
        dest="gpu_validation",
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
        "status": "failed",
        "generated_utc": datetime.now(timezone.utc).isoformat(),
        "parameters": {
            "workload": "copy_buffer",
            "records": args.records,
            "warmup": args.warmup,
            "samples": args.samples,
            "adapter_index": args.adapter_index,
            "gpu_validation": args.gpu_validation,
        },
        # This is host inventory only; the native payload identifies the pinned adapter.
        "vulkaninfo_host_inventory": vulkan_summary.name if vulkan_summary else None,
    }
    try:
        result = run_profile(args, args.output_dir)
        report["process"] = {
            "return_code": result.return_code,
            "elapsed_seconds": result.elapsed_seconds,
            "log": result.log_path.name,
        }
        report["profile"] = result.payload
        if result.return_code == SKIP_EXIT_CODE:
            if result.payload is None or result.payload.get("status") != "skipped":
                raise ProfileFailure("profile returned the skip code without a skipped result payload")
            report["status"] = "skipped"
            report["skip_reason"] = result.payload.get("reason", "vulkan_or_profile_requirements_unavailable")
            return_code = SKIP_EXIT_CODE
        else:
            payload = require_ok(args, result)
            report["metrics"] = {
                "capture_encode_increment_percent": capture_increment_percent(payload),
                "direct_vulkan_replay_delta_percent": direct_vulkan_replay_delta_percent(payload),
            }
            report["status"] = "passed"
            return_code = 0
    except ProfileFailure as error:
        report["status"] = "failed"
        report["error"] = str(error)
        return_code = 1
    finally:
        (args.output_dir / "command_ir_profile_report.json").write_text(
            json.dumps(report, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
        (args.output_dir / "command_ir_profile_report.md").write_text(markdown_report(report), encoding="utf-8")

    print(f"Command-IR profile report: {args.output_dir / 'command_ir_profile_report.md'}")
    return return_code


def run_self_test() -> int:
    assert parse_args(["--self-test"]).records == 4096
    timing = {
        "samples_ns_per_command": [1.0, 2.0, 3.0],
        "min_ns_per_command": 1.0,
        "median_ns_per_command": 2.0,
        "max_ns_per_command": 3.0,
    }
    payload: Dict[str, Any] = {
        "status": "ok",
        "workload": "copy_buffer",
        "requested_adapter_index": 0,
        "selected_adapter_vendor_id": 4098,
        "selected_adapter_device_id": 1234,
        "selected_adapter_uuid": "0123456789abcdef0123456789abcdef",
        "records": 4,
        "warmup": 1,
        "samples": 3,
        "stream_bytes": 512,
        "payload_bytes": 480,
        "decoded_records": 4,
        "preflight_records": 4,
        "replayed_records": 4,
        "direct_vulkan_replayed_records": 4,
        "stream_valid": True,
        "checksum_verified": True,
        "direct_vulkan_observed_hash": 1234,
        "direct_vulkan_checksum_verified": True,
        "logger_errors": 0,
        "capture_allocation_delta": 0,
        "capture_reallocation_delta": 0,
    }
    payload.update({stage: dict(timing) for stage in TIMING_STAGES})
    text = f"noise\n{RESULT_PREFIX}{json.dumps(payload)}\n"
    assert parse_result(text) == payload
    assert parse_result("no result") is None
    args = SimpleNamespace(adapter_index=0, records=4, warmup=1, samples=3)
    result = ProfileResult(0, 0.1, payload, Path("command_ir_profile.log"))
    assert require_ok(args, result)["replayed_records"] == 4
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
    assert profile_command(command_args)[-1] == "--no-gpu-validation"
    report = {
        "status": "passed",
        "process": {"return_code": 0, "elapsed_seconds": 0.1, "log": "command_ir_profile.log"},
        "profile": payload,
        "metrics": {
            "capture_encode_increment_percent": 0.0,
            "direct_vulkan_replay_delta_percent": 0.0,
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


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
