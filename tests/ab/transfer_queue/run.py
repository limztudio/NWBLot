#!/usr/bin/env python3
"""Run and report the dedicated-Transfer setup-upload A/B probe.

The probe compares an explicit Graphics producer with the public automatic route for
the same >= 1 MiB buffer or texture uploads.  Every iteration optionally first places
a large independent Graphics copy on the Graphics queue.  On a real dedicated Transfer
family this gives an external profiler a reproducible opportunity to observe copy-engine
overlap and shared-memory bandwidth pressure; CPU elapsed time is reported only as a
host-side envelope, never as a GPU-bandwidth claim.
"""

from __future__ import annotations


import argparse
import json
import shlex
import shutil
import subprocess
import sys
import time
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Dict, List, Optional, Sequence, Tuple


sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from profile_probe import ProfileFailure, capture_vulkan_summary, parse_result  # noqa: E402

LIT_ROUTE = "--route"
LIT_RESOURCE = "--resource"
LIT_ADAPTER_INDEX = "--adapter-index"
LIT_UPLOAD_MIB = "--upload-mib"
LIT_ITERATIONS = "--iterations"
LIT_IN_FLIGHT = "--in-flight"
LIT_CONTENTION_MIB = "--contention-mib"
LIT_CONTENTION_COPIES = "--contention-copies"
LIT_GPU_VALIDATION = "--gpu-validation"
LIT_NO_GPU_VALIDATION = "--no-gpu-validation"
LIT_UTF_8 = "utf-8"
LIT_STATUS = "status"
LIT_OK = "ok"
LIT_CHECKSUM_VERIFIED = "checksum_verified"
LIT_GRAPHICS_READINESS_VERIFIED = "graphics_readiness_verified"
LIT_LOGGER_ERRORS = "logger_errors"
LIT_GRAPHICS = "graphics"
LIT_AUTOMATIC = "automatic"
LIT_REQUESTED_ROUTE = "requested_route"
LIT_RESOURCE_2 = "resource"
LIT_REQUESTED_ADAPTER_INDEX = "requested_adapter_index"
LIT_LOGICAL_UPLOAD_BYTES = "logical_upload_bytes"
LIT_ITERATIONS_2 = "iterations"
LIT_IN_FLIGHT_WINDOW = "in_flight_window"
LIT_TOTAL_LOGICAL_UPLOAD_BYTES = "total_logical_upload_bytes"
LIT_MODELED_UPLOAD_READ_WRITE_BYTES = "modeled_upload_read_write_bytes"
LIT_ASYNC_INGRESS_COPY_BYTES = "async_ingress_copy_bytes"
LIT_GRAPHICS_READINESS_COPY_BYTES = "graphics_readiness_copy_bytes"
LIT_GRAPHICS_READINESS_COPIES = "graphics_readiness_copies"
LIT_MODELED_GRAPHICS_READINESS_READ_WRITE_ = "modeled_graphics_readiness_read_write_bytes"
LIT_GRAPHICS_CONTENTION_COPY_BYTES = "graphics_contention_copy_bytes"
LIT_GRAPHICS_CONTENTION_COPIES = "graphics_contention_copies"
LIT_MODELED_GRAPHICS_CONTENTION_READ_WRITE = "modeled_graphics_contention_read_write_bytes"
LIT_EXPECTED_EXCLUSIVE_OWNERSHIP_TRANSFERS = "expected_exclusive_ownership_transfers"
LIT_SELECTED_ADAPTER_UUID = "selected_adapter_uuid"
LIT_PRODUCER_QUEUE = "producer_queue"
LIT_PRODUCER_FAMILY = "producer_family"
LIT_GRAPHICS_FAMILY = "graphics_family"
LIT_TRANSFER_QUEUE_ENABLED = "transfer_queue_enabled"
LIT_TRANSFER = "transfer"
LIT_TRANSFER_FAMILY = "transfer_family"
LIT_QUEUE_SHARING_MASK = "queue_sharing_mask"
LIT_OBSERVED_GRAPHICS_READINESS_BRIDGE_SUB = "observed_graphics_readiness_bridge_submissions"
LIT_SELECTED_ADAPTER_VENDOR_ID = "selected_adapter_vendor_id"
LIT_SELECTED_ADAPTER_DEVICE_ID = "selected_adapter_device_id"
LIT_EXPECTED_HASH = "expected_hash"
LIT_OBSERVED_HASH = "observed_hash"
LIT_INCOMPLETE_EXTERNAL_CAPTURE_REQUIRED = "incomplete_external_capture_required"
LIT_CAPTURE_ATTACHED_PENDING_REVIEW = "capture_attached_pending_review"
LIT_SKIP_REASON = "skip_reason"
LIT_ARMS = "arms"
LIT_EXTERNAL_PROFILER_REPORT = "external_profiler_report"
LIT_N = "\n"
LIT_SELF_TEST = "--self-test"
LIT_STORE_TRUE = "store_true"
LIT_BUFFER = "buffer"
LIT_FAILED = "failed"
LIT_RETURN_CODE = "return_code"
LIT_ELAPSED_SECONDS = "elapsed_seconds"
LIT_LOG = "log"
LIT_SKIPPED = "skipped"
LIT_REASON = "reason"
LIT_MAIN = "__main__"


SKIP_EXIT_CODE = 77
INCOMPLETE_EXIT_CODE = 2
RESULT_PREFIX = "NWB_TRANSFER_UPLOAD_PROFILE_RESULT "
GRAPHICS_AND_TRANSFER_SHARING_MASK = (1 << 0) | (1 << 2)


@dataclass(frozen=True)
class ArmResult:
    route: str
    return_code: int
    elapsed_seconds: float
    payload: Optional[Dict[str, Any]]
    log_path: Path


def run_arm(args: argparse.Namespace, route: str, output_dir: Path) -> ArmResult:
    command: List[str] = [
        str(args.executable),
        LIT_ROUTE,
        route,
        LIT_RESOURCE,
        args.resource,
        LIT_ADAPTER_INDEX,
        str(args.adapter_index),
        LIT_UPLOAD_MIB,
        str(args.upload_mib),
        LIT_ITERATIONS,
        str(args.iterations),
        LIT_IN_FLIGHT,
        str(args.in_flight),
        LIT_CONTENTION_MIB,
        str(args.contention_mib),
        LIT_CONTENTION_COPIES,
        str(args.contention_copies),
    ]
    command.append(LIT_GPU_VALIDATION if args.gpu_validation else LIT_NO_GPU_VALIDATION)

    started = time.perf_counter()
    completed = subprocess.run(command, cwd=args.executable.parent, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    elapsed_seconds = time.perf_counter() - started
    log_path = output_dir / f"{route}.log"
    log_path.write_text(completed.stdout, encoding=LIT_UTF_8)
    payload = parse_result(completed.stdout, RESULT_PREFIX)
    return ArmResult(route, completed.returncode, elapsed_seconds, payload, log_path)


def require_ok(arm: ArmResult) -> Dict[str, Any]:
    if arm.return_code != 0:
        raise ProfileFailure(f"{arm.route} arm failed with exit code {arm.return_code}; see {arm.log_path}")
    if arm.payload is None:
        raise ProfileFailure(f"{arm.route} arm did not emit a profile result; see {arm.log_path}")
    if arm.payload.get(LIT_STATUS) != LIT_OK:
        raise ProfileFailure(f"{arm.route} arm reported status {arm.payload.get('status')!r}; see {arm.log_path}")
    if not arm.payload.get(LIT_CHECKSUM_VERIFIED):
        raise ProfileFailure(f"{arm.route} arm did not verify its upload readback; see {arm.log_path}")
    if not arm.payload.get(LIT_GRAPHICS_READINESS_VERIFIED):
        raise ProfileFailure(f"{arm.route} arm did not verify immediate Graphics readiness; see {arm.log_path}")
    if arm.payload.get(LIT_LOGGER_ERRORS) != 0:
        raise ProfileFailure(f"{arm.route} arm reported logger errors; see {arm.log_path}")
    return arm.payload


def validate_arms(args: argparse.Namespace, graphics: ArmResult, automatic: ArmResult) -> Dict[str, Any]:
    graphics_payload = require_ok(graphics)
    automatic_payload = require_ok(automatic)

    expected_upload_bytes = args.upload_mib * 1024 * 1024
    expected_contention_bytes = args.contention_mib * 1024 * 1024
    expected_in_flight_window = min(args.in_flight, args.iterations)
    expected_total_upload_bytes = expected_upload_bytes * args.iterations

    for route, payload in ((LIT_GRAPHICS, graphics_payload), (LIT_AUTOMATIC, automatic_payload)):
        expected_fields = {
            LIT_REQUESTED_ROUTE: route,
            LIT_RESOURCE_2: args.resource,
            LIT_REQUESTED_ADAPTER_INDEX: args.adapter_index,
            LIT_LOGICAL_UPLOAD_BYTES: expected_upload_bytes,
            LIT_ITERATIONS_2: args.iterations,
            LIT_IN_FLIGHT_WINDOW: expected_in_flight_window,
            LIT_TOTAL_LOGICAL_UPLOAD_BYTES: expected_total_upload_bytes,
            LIT_MODELED_UPLOAD_READ_WRITE_BYTES: expected_total_upload_bytes * 2,
            LIT_ASYNC_INGRESS_COPY_BYTES: 0,
            LIT_GRAPHICS_READINESS_COPY_BYTES: expected_upload_bytes,
            LIT_GRAPHICS_READINESS_COPIES: args.iterations,
            LIT_MODELED_GRAPHICS_READINESS_READ_WRITE_: expected_total_upload_bytes * 2,
            LIT_GRAPHICS_CONTENTION_COPY_BYTES: expected_contention_bytes,
            LIT_GRAPHICS_CONTENTION_COPIES: args.contention_copies,
            LIT_MODELED_GRAPHICS_CONTENTION_READ_WRITE: expected_contention_bytes
            * args.contention_copies
            * args.iterations
            * 2,
            LIT_EXPECTED_EXCLUSIVE_OWNERSHIP_TRANSFERS: 0,
        }
        for field, expected in expected_fields.items():
            if payload.get(field) != expected:
                raise ProfileFailure(
                    f"{route} arm mismatch for {field}: expected {expected!r}, got {payload.get(field)!r}"
                )
        adapter_uuid = payload.get(LIT_SELECTED_ADAPTER_UUID)
        if not isinstance(adapter_uuid, str) or len(adapter_uuid) != 32:
            raise ProfileFailure(f"{route} arm did not emit a selected adapter UUID")

    if graphics_payload.get(LIT_PRODUCER_QUEUE) != LIT_GRAPHICS:
        raise ProfileFailure("Graphics baseline did not use the Graphics producer queue")
    if graphics_payload.get(LIT_PRODUCER_FAMILY) != graphics_payload.get(LIT_GRAPHICS_FAMILY):
        raise ProfileFailure("Graphics baseline producer family does not match the Graphics family")
    if graphics_payload.get(LIT_TRANSFER_QUEUE_ENABLED) is not True:
        raise ProfileFailure("Graphics baseline did not preserve the dedicated Transfer queue topology")
    if automatic_payload.get(LIT_PRODUCER_QUEUE) != LIT_TRANSFER:
        raise ProfileFailure("automatic arm did not use the dedicated Transfer producer queue")
    if automatic_payload.get(LIT_TRANSFER_QUEUE_ENABLED) is not True:
        raise ProfileFailure("automatic arm did not enable the Transfer queue")
    if automatic_payload.get(LIT_GRAPHICS_FAMILY) == automatic_payload.get(LIT_TRANSFER_FAMILY):
        raise ProfileFailure("automatic arm reported a non-dedicated Transfer family")
    if automatic_payload.get(LIT_TRANSFER_FAMILY) == (1 << 32) - 1:
        raise ProfileFailure("automatic arm reported an invalid Transfer queue family")
    if automatic_payload.get(LIT_PRODUCER_FAMILY) != automatic_payload.get(LIT_TRANSFER_FAMILY):
        raise ProfileFailure("automatic arm producer family does not match the selected Transfer family")
    if graphics_payload.get(LIT_QUEUE_SHARING_MASK) != 0:
        raise ProfileFailure("explicit Graphics baseline unexpectedly changed the requested exclusive-sharing contract")
    if automatic_payload.get(LIT_QUEUE_SHARING_MASK) != GRAPHICS_AND_TRANSFER_SHARING_MASK:
        raise ProfileFailure("automatic arm did not publish concurrent Graphics/Transfer resource sharing")
    if graphics_payload.get(LIT_OBSERVED_GRAPHICS_READINESS_BRIDGE_SUB) != 0:
        raise ProfileFailure("Graphics baseline unexpectedly inserted readiness bridges")
    if automatic_payload.get(LIT_OBSERVED_GRAPHICS_READINESS_BRIDGE_SUB) != automatic_payload.get(LIT_ITERATIONS_2):
        raise ProfileFailure("automatic arm did not observe exactly one Graphics readiness bridge per upload")
    compared_fields = (
        LIT_RESOURCE_2,
        LIT_REQUESTED_ADAPTER_INDEX,
        LIT_SELECTED_ADAPTER_VENDOR_ID,
        LIT_SELECTED_ADAPTER_DEVICE_ID,
        LIT_SELECTED_ADAPTER_UUID,
        LIT_GRAPHICS_FAMILY,
        LIT_TRANSFER_FAMILY,
        LIT_LOGICAL_UPLOAD_BYTES,
        LIT_ITERATIONS_2,
        LIT_IN_FLIGHT_WINDOW,
        LIT_EXPECTED_HASH,
        LIT_OBSERVED_HASH,
    )
    for field in compared_fields:
        if graphics_payload.get(field) != automatic_payload.get(field):
            raise ProfileFailure(f"A/B mismatch for {field}: {graphics_payload.get(field)!r} != {automatic_payload.get(field)!r}")
    return {LIT_GRAPHICS: graphics_payload, LIT_AUTOMATIC: automatic_payload}


def copy_external_report(source: Optional[Path], output_dir: Path) -> Optional[Path]:
    if source is None:
        return None
    source = source.resolve()
    if not source.is_file():
        raise ProfileFailure(f"external profiler report does not exist: {source}")
    destination = output_dir / f"external-profiler{source.suffix}"
    if source != destination:
        shutil.copy2(source, destination)
    return destination


def completion_status(external_report: Optional[Path], require_external_profiler: bool) -> Tuple[str, int]:
    if external_report is None:
        return (
            LIT_INCOMPLETE_EXTERNAL_CAPTURE_REQUIRED,
            1 if require_external_profiler else INCOMPLETE_EXIT_CODE,
        )
    return LIT_CAPTURE_ATTACHED_PENDING_REVIEW, INCOMPLETE_EXIT_CODE


def markdown_report(report: Dict[str, Any]) -> str:
    status = report[LIT_STATUS]
    lines = [
        "# Transfer queue upload profile",
        "",
        f"Status: **{status}**",
        "",
        "This compares explicit Graphics setup uploads with the automatic Transfer route. "
        "Both arms keep the same logical-device queue topology. The reported throughput is a host completion envelope; "
        "memory-bandwidth contention requires an external GPU trace and review.",
        "",
    ]
    if report.get(LIT_SKIP_REASON):
        lines += [f"Skip reason: `{report['skip_reason']}`", ""]
    arms = report.get(LIT_ARMS)
    if arms:
        lines += [
            "| Arm | Producer | Logical upload | Iterations | In-flight | Host completion | Envelope MiB/s | Observed readiness bridges | Expected ownership transfers |",
            "| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |",
        ]
        for name in (LIT_GRAPHICS, LIT_AUTOMATIC):
            payload = arms[name]
            lines.append(
                f"| {name} | {payload['producer_queue']} | {payload['logical_upload_bytes']} B | "
                f"{payload['iterations']} | {payload['in_flight_window']} | {payload['completion_seconds']:.6f} s | "
                f"{payload['completion_mib_per_second']:.2f} | "
                f"{payload['observed_graphics_readiness_bridge_submissions']} | "
                f"{payload['expected_exclusive_ownership_transfers']} |"
            )
        lines += [
            "",
            "Each arm immediately submits a Graphics consumer copy without a CPU wait. The observed Graphics timeline "
            "count proves one readiness bridge per automatic upload. The zero ownership value is the setup API's declared "
            "concurrent-sharing contract, not a backend-wide barrier counter.",
            "",
        ]
    external = report.get(LIT_EXTERNAL_PROFILER_REPORT)
    if external:
        lines += [
            f"External profiler artifact: `{external}`",
            "",
            "The trace is attached for review; this microprobe alone does not accept the Phase 10 graph-wide profiling gate.",
            "",
        ]
    else:
        lines += [
            "External profiler artifact: **not supplied**. Capture the two commands in `capture-commands.txt` with the target GPU profiler and attach its bandwidth/copy-engine trace before using this as Phase 10 evidence.",
            "",
        ]
    return LIT_N.join(lines)


def write_capture_commands(args: argparse.Namespace, output_dir: Path) -> None:
    commands = []
    for route in (LIT_GRAPHICS, LIT_AUTOMATIC):
        command = [
            str(args.executable),
            LIT_ROUTE,
            route,
            LIT_RESOURCE,
            args.resource,
            LIT_ADAPTER_INDEX,
            str(args.adapter_index),
            LIT_UPLOAD_MIB,
            str(args.upload_mib),
            LIT_ITERATIONS,
            str(args.iterations),
            LIT_IN_FLIGHT,
            str(args.in_flight),
            LIT_CONTENTION_MIB,
            str(args.contention_mib),
            LIT_CONTENTION_COPIES,
            str(args.contention_copies),
            LIT_GPU_VALIDATION if args.gpu_validation else LIT_NO_GPU_VALIDATION,
        ]
        commands.append(subprocess.list2cmdline(command) if sys.platform == "win32" else shlex.join(command))
    (output_dir / "capture-commands.txt").write_text(LIT_N.join(commands) + LIT_N, encoding=LIT_UTF_8)


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(LIT_SELF_TEST, action=LIT_STORE_TRUE, help="Run parser/verdict checks without Vulkan.")
    parser.add_argument("--executable", type=Path, help="Path to nwb_transfer_upload_profile.")
    parser.add_argument("--output-dir", type=Path, help="Artifact directory.")
    parser.add_argument(LIT_RESOURCE, choices=(LIT_BUFFER, "texture"), default=LIT_BUFFER, help="Setup-upload resource kind.")
    parser.add_argument(LIT_ADAPTER_INDEX, type=int, default=0, help="Pinned Vulkan adapter enumeration index (default: 0).")
    parser.add_argument(LIT_UPLOAD_MIB, type=int, default=16, help="Logical payload per upload, in MiB (default: 16).")
    parser.add_argument(LIT_ITERATIONS, type=int, default=12, help="Uploads per arm (default: 12).")
    parser.add_argument(LIT_IN_FLIGHT, type=int, default=2, help="Maximum concurrently retained upload windows (default: 2).")
    parser.add_argument(LIT_CONTENTION_MIB, type=int, default=32, help="Independent Graphics copy size per iteration, in MiB (0 disables it).")
    parser.add_argument(LIT_CONTENTION_COPIES, type=int, default=16, help="Graphics copies issued per iteration (default: 16).")
    parser.add_argument(LIT_GPU_VALIDATION, action=LIT_STORE_TRUE, help="Enable Vulkan validation in both arms.")
    parser.add_argument("--external-profiler-report", type=Path, help="Existing external profiler trace/report to copy into this result bundle.")
    parser.add_argument(
        "--require-external-profiler",
        action=LIT_STORE_TRUE,
        help="Return failure when no external profiler artifact was supplied.",
    )
    args = parser.parse_args(argv)
    if args.self_test:
        return args
    if args.executable is None or args.output_dir is None:
        parser.error("--executable and --output-dir are required")
    if args.adapter_index < 0:
        parser.error("--adapter-index must be a non-negative Vulkan enumeration index for paired A/B evidence")
    if args.upload_mib <= 0 or args.iterations <= 0 or args.in_flight <= 0 or args.contention_mib < 0 or args.contention_copies < 0:
        parser.error("upload MiB, iterations, and in-flight values must be positive; contention values cannot be negative")
    if args.contention_mib > 0 and args.contention_copies == 0:
        parser.error("--contention-copies must be positive when --contention-mib is nonzero")
    args.executable = args.executable.resolve()
    args.output_dir = args.output_dir.resolve()
    return args


def run(args: argparse.Namespace) -> int:
    if not args.executable.is_file():
        raise ProfileFailure(f"profile executable does not exist: {args.executable}")
    args.output_dir.mkdir(parents=True, exist_ok=False)
    write_capture_commands(args, args.output_dir)
    vulkan_summary = capture_vulkan_summary(args.output_dir)
    report: Dict[str, Any] = {
        LIT_STATUS: LIT_FAILED,
        "generated_utc": datetime.now(timezone.utc).isoformat(),
        "parameters": {
            LIT_RESOURCE_2: args.resource,
            "adapter_index": args.adapter_index,
            "upload_mib": args.upload_mib,
            LIT_ITERATIONS_2: args.iterations,
            "in_flight": args.in_flight,
            "contention_mib": args.contention_mib,
            "contention_copies": args.contention_copies,
            "gpu_validation": args.gpu_validation,
        },
        # This is host inventory only. The paired native payloads pin and report the requested adapter index.
        "vulkaninfo_host_inventory": vulkan_summary.name if vulkan_summary else None,
    }
    try:
        graphics = run_arm(args, LIT_GRAPHICS, args.output_dir)
        automatic = run_arm(args, LIT_AUTOMATIC, args.output_dir)
        report["arm_processes"] = {
            LIT_GRAPHICS: {LIT_RETURN_CODE: graphics.return_code, LIT_ELAPSED_SECONDS: graphics.elapsed_seconds, LIT_LOG: graphics.log_path.name},
            LIT_AUTOMATIC: {LIT_RETURN_CODE: automatic.return_code, LIT_ELAPSED_SECONDS: automatic.elapsed_seconds, LIT_LOG: automatic.log_path.name},
        }
        report["arm_payloads"] = {
            LIT_GRAPHICS: graphics.payload,
            LIT_AUTOMATIC: automatic.payload,
        }
        if graphics.return_code == SKIP_EXIT_CODE:
            if graphics.payload is None or graphics.payload.get(LIT_STATUS) != LIT_SKIPPED:
                raise ProfileFailure("Graphics arm returned the skip code without a skipped profile result")
            if automatic.return_code not in (0, SKIP_EXIT_CODE):
                raise ProfileFailure(f"automatic arm failed with exit code {automatic.return_code}; see {automatic.log_path}")
            report[LIT_STATUS] = LIT_SKIPPED
            report[LIT_SKIP_REASON] = graphics.payload.get(LIT_REASON, "headless_vulkan_or_descriptor_buffer_unavailable")
            return_code = SKIP_EXIT_CODE
        elif automatic.return_code == SKIP_EXIT_CODE:
            require_ok(graphics)
            if automatic.payload is None or automatic.payload.get(LIT_STATUS) != LIT_SKIPPED:
                raise ProfileFailure("automatic arm returned the skip code without a skipped profile result")
            report[LIT_STATUS] = LIT_SKIPPED
            report[LIT_SKIP_REASON] = (automatic.payload or {}).get(LIT_REASON, "dedicated_transfer_family_unavailable")
            return_code = SKIP_EXIT_CODE
        else:
            report[LIT_ARMS] = validate_arms(args, graphics, automatic)
            external = copy_external_report(args.external_profiler_report, args.output_dir)
            report[LIT_EXTERNAL_PROFILER_REPORT] = external.name if external else None
            report[LIT_STATUS], return_code = completion_status(external, args.require_external_profiler)
    except ProfileFailure as error:
        report[LIT_STATUS] = LIT_FAILED
        report["error"] = str(error)
        return_code = 1
    finally:
        (args.output_dir / "transfer_queue_report.json").write_text(json.dumps(report, indent=2, sort_keys=True) + LIT_N, encoding=LIT_UTF_8)
        (args.output_dir / "transfer_queue_report.md").write_text(markdown_report(report), encoding=LIT_UTF_8)

    print(f"Transfer queue profile report: {args.output_dir / 'transfer_queue_report.md'}")
    return return_code


def run_self_test() -> int:
    assert parse_result("no result", RESULT_PREFIX) is None
    print("transfer-queue harness self-test passed")
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
