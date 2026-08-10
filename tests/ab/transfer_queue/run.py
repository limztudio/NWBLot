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
from types import SimpleNamespace
from typing import Any, Dict, List, Optional, Sequence, Tuple


SKIP_EXIT_CODE = 77
INCOMPLETE_EXIT_CODE = 2
RESULT_PREFIX = "NWB_TRANSFER_UPLOAD_PROFILE_RESULT "
GRAPHICS_AND_TRANSFER_SHARING_MASK = (1 << 0) | (1 << 2)


class ProfileFailure(RuntimeError):
    pass


@dataclass(frozen=True)
class ArmResult:
    route: str
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


def run_arm(args: argparse.Namespace, route: str, output_dir: Path) -> ArmResult:
    command: List[str] = [
        str(args.executable),
        "--route",
        route,
        "--resource",
        args.resource,
        "--adapter-index",
        str(args.adapter_index),
        "--upload-mib",
        str(args.upload_mib),
        "--iterations",
        str(args.iterations),
        "--in-flight",
        str(args.in_flight),
        "--contention-mib",
        str(args.contention_mib),
        "--contention-copies",
        str(args.contention_copies),
    ]
    command.append("--gpu-validation" if args.gpu_validation else "--no-gpu-validation")

    started = time.perf_counter()
    completed = subprocess.run(command, cwd=args.executable.parent, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    elapsed_seconds = time.perf_counter() - started
    log_path = output_dir / f"{route}.log"
    log_path.write_text(completed.stdout, encoding="utf-8")
    payload = parse_result(completed.stdout)
    return ArmResult(route, completed.returncode, elapsed_seconds, payload, log_path)


def capture_vulkan_summary(output_dir: Path) -> Optional[Path]:
    vulkaninfo = shutil.which("vulkaninfo")
    if vulkaninfo is None:
        return None
    completed = subprocess.run([vulkaninfo, "--summary"], text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    output_path = output_dir / "vulkaninfo-summary.txt"
    output_path.write_text(completed.stdout, encoding="utf-8")
    return output_path


def require_ok(arm: ArmResult) -> Dict[str, Any]:
    if arm.return_code != 0:
        raise ProfileFailure(f"{arm.route} arm failed with exit code {arm.return_code}; see {arm.log_path}")
    if arm.payload is None:
        raise ProfileFailure(f"{arm.route} arm did not emit a profile result; see {arm.log_path}")
    if arm.payload.get("status") != "ok":
        raise ProfileFailure(f"{arm.route} arm reported status {arm.payload.get('status')!r}; see {arm.log_path}")
    if not arm.payload.get("checksum_verified"):
        raise ProfileFailure(f"{arm.route} arm did not verify its upload readback; see {arm.log_path}")
    if not arm.payload.get("graphics_readiness_verified"):
        raise ProfileFailure(f"{arm.route} arm did not verify immediate Graphics readiness; see {arm.log_path}")
    if arm.payload.get("logger_errors") != 0:
        raise ProfileFailure(f"{arm.route} arm reported logger errors; see {arm.log_path}")
    return arm.payload


def validate_arms(args: argparse.Namespace, graphics: ArmResult, automatic: ArmResult) -> Dict[str, Any]:
    graphics_payload = require_ok(graphics)
    automatic_payload = require_ok(automatic)

    expected_upload_bytes = args.upload_mib * 1024 * 1024
    expected_contention_bytes = args.contention_mib * 1024 * 1024
    expected_in_flight_window = min(args.in_flight, args.iterations)
    expected_total_upload_bytes = expected_upload_bytes * args.iterations

    for route, payload in (("graphics", graphics_payload), ("automatic", automatic_payload)):
        expected_fields = {
            "requested_route": route,
            "resource": args.resource,
            "requested_adapter_index": args.adapter_index,
            "logical_upload_bytes": expected_upload_bytes,
            "iterations": args.iterations,
            "in_flight_window": expected_in_flight_window,
            "total_logical_upload_bytes": expected_total_upload_bytes,
            "modeled_upload_read_write_bytes": expected_total_upload_bytes * 2,
            "async_ingress_copy_bytes": 0,
            "graphics_readiness_copy_bytes": expected_upload_bytes,
            "graphics_readiness_copies": args.iterations,
            "modeled_graphics_readiness_read_write_bytes": expected_total_upload_bytes * 2,
            "graphics_contention_copy_bytes": expected_contention_bytes,
            "graphics_contention_copies": args.contention_copies,
            "modeled_graphics_contention_read_write_bytes": expected_contention_bytes
            * args.contention_copies
            * args.iterations
            * 2,
            "expected_exclusive_ownership_transfers": 0,
        }
        for field, expected in expected_fields.items():
            if payload.get(field) != expected:
                raise ProfileFailure(
                    f"{route} arm mismatch for {field}: expected {expected!r}, got {payload.get(field)!r}"
                )
        adapter_uuid = payload.get("selected_adapter_uuid")
        if not isinstance(adapter_uuid, str) or len(adapter_uuid) != 32:
            raise ProfileFailure(f"{route} arm did not emit a selected adapter UUID")

    if graphics_payload.get("producer_queue") != "graphics":
        raise ProfileFailure("Graphics baseline did not use the Graphics producer queue")
    if graphics_payload.get("producer_family") != graphics_payload.get("graphics_family"):
        raise ProfileFailure("Graphics baseline producer family does not match the Graphics family")
    if graphics_payload.get("transfer_queue_enabled") is not True:
        raise ProfileFailure("Graphics baseline did not preserve the dedicated Transfer queue topology")
    if automatic_payload.get("producer_queue") != "transfer":
        raise ProfileFailure("automatic arm did not use the dedicated Transfer producer queue")
    if automatic_payload.get("transfer_queue_enabled") is not True:
        raise ProfileFailure("automatic arm did not enable the Transfer queue")
    if automatic_payload.get("graphics_family") == automatic_payload.get("transfer_family"):
        raise ProfileFailure("automatic arm reported a non-dedicated Transfer family")
    if automatic_payload.get("transfer_family") == (1 << 32) - 1:
        raise ProfileFailure("automatic arm reported an invalid Transfer queue family")
    if automatic_payload.get("producer_family") != automatic_payload.get("transfer_family"):
        raise ProfileFailure("automatic arm producer family does not match the selected Transfer family")
    if graphics_payload.get("queue_sharing_mask") != 0:
        raise ProfileFailure("explicit Graphics baseline unexpectedly changed the requested exclusive-sharing contract")
    if automatic_payload.get("queue_sharing_mask") != GRAPHICS_AND_TRANSFER_SHARING_MASK:
        raise ProfileFailure("automatic arm did not publish concurrent Graphics/Transfer resource sharing")
    if graphics_payload.get("observed_graphics_readiness_bridge_submissions") != 0:
        raise ProfileFailure("Graphics baseline unexpectedly inserted readiness bridges")
    if automatic_payload.get("observed_graphics_readiness_bridge_submissions") != automatic_payload.get("iterations"):
        raise ProfileFailure("automatic arm did not observe exactly one Graphics readiness bridge per upload")
    compared_fields = (
        "resource",
        "requested_adapter_index",
        "selected_adapter_vendor_id",
        "selected_adapter_device_id",
        "selected_adapter_uuid",
        "graphics_family",
        "transfer_family",
        "logical_upload_bytes",
        "iterations",
        "in_flight_window",
        "expected_hash",
        "observed_hash",
    )
    for field in compared_fields:
        if graphics_payload.get(field) != automatic_payload.get(field):
            raise ProfileFailure(f"A/B mismatch for {field}: {graphics_payload.get(field)!r} != {automatic_payload.get(field)!r}")
    return {"graphics": graphics_payload, "automatic": automatic_payload}


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
            "incomplete_external_capture_required",
            1 if require_external_profiler else INCOMPLETE_EXIT_CODE,
        )
    return "capture_attached_pending_review", INCOMPLETE_EXIT_CODE


def markdown_report(report: Dict[str, Any]) -> str:
    status = report["status"]
    lines = [
        "# Transfer queue upload profile",
        "",
        f"Status: **{status}**",
        "",
        "This compares explicit Graphics setup uploads with the automatic Transfer-preferred route. "
        "Both arms keep the same logical-device queue topology. The reported throughput is a host completion envelope; "
        "memory-bandwidth contention requires an external GPU trace and review.",
        "",
    ]
    if report.get("skip_reason"):
        lines += [f"Skip reason: `{report['skip_reason']}`", ""]
    arms = report.get("arms")
    if arms:
        lines += [
            "| Arm | Producer | Logical upload | Iterations | In-flight | Host completion | Envelope MiB/s | Observed readiness bridges | Expected ownership transfers |",
            "| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |",
        ]
        for name in ("graphics", "automatic"):
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
    external = report.get("external_profiler_report")
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
    return "\n".join(lines)


def write_capture_commands(args: argparse.Namespace, output_dir: Path) -> None:
    commands = []
    for route in ("graphics", "automatic"):
        command = [
            str(args.executable),
            "--route",
            route,
            "--resource",
            args.resource,
            "--adapter-index",
            str(args.adapter_index),
            "--upload-mib",
            str(args.upload_mib),
            "--iterations",
            str(args.iterations),
            "--in-flight",
            str(args.in_flight),
            "--contention-mib",
            str(args.contention_mib),
            "--contention-copies",
            str(args.contention_copies),
            "--gpu-validation" if args.gpu_validation else "--no-gpu-validation",
        ]
        commands.append(subprocess.list2cmdline(command) if sys.platform == "win32" else shlex.join(command))
    (output_dir / "capture-commands.txt").write_text("\n".join(commands) + "\n", encoding="utf-8")


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--self-test", action="store_true", help="Run parser/verdict checks without Vulkan.")
    parser.add_argument("--executable", type=Path, help="Path to nwb_transfer_upload_profile.")
    parser.add_argument("--output-dir", type=Path, help="Artifact directory.")
    parser.add_argument("--resource", choices=("buffer", "texture"), default="buffer", help="Setup-upload resource kind.")
    parser.add_argument("--adapter-index", type=int, default=0, help="Pinned Vulkan adapter enumeration index (default: 0).")
    parser.add_argument("--upload-mib", type=int, default=16, help="Logical payload per upload, in MiB (default: 16).")
    parser.add_argument("--iterations", type=int, default=12, help="Uploads per arm (default: 12).")
    parser.add_argument("--in-flight", type=int, default=2, help="Maximum concurrently retained upload windows (default: 2).")
    parser.add_argument("--contention-mib", type=int, default=32, help="Independent Graphics copy size per iteration, in MiB (0 disables it).")
    parser.add_argument("--contention-copies", type=int, default=16, help="Graphics copies issued per iteration (default: 16).")
    parser.add_argument("--gpu-validation", action="store_true", help="Enable Vulkan validation in both arms.")
    parser.add_argument("--external-profiler-report", type=Path, help="Existing external profiler trace/report to copy into this result bundle.")
    parser.add_argument(
        "--require-external-profiler",
        action="store_true",
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
        "status": "failed",
        "generated_utc": datetime.now(timezone.utc).isoformat(),
        "parameters": {
            "resource": args.resource,
            "adapter_index": args.adapter_index,
            "upload_mib": args.upload_mib,
            "iterations": args.iterations,
            "in_flight": args.in_flight,
            "contention_mib": args.contention_mib,
            "contention_copies": args.contention_copies,
            "gpu_validation": args.gpu_validation,
        },
        # This is host inventory only. The paired native payloads pin and report the requested adapter index.
        "vulkaninfo_host_inventory": vulkan_summary.name if vulkan_summary else None,
    }
    try:
        graphics = run_arm(args, "graphics", args.output_dir)
        automatic = run_arm(args, "automatic", args.output_dir)
        report["arm_processes"] = {
            "graphics": {"return_code": graphics.return_code, "elapsed_seconds": graphics.elapsed_seconds, "log": graphics.log_path.name},
            "automatic": {"return_code": automatic.return_code, "elapsed_seconds": automatic.elapsed_seconds, "log": automatic.log_path.name},
        }
        report["arm_payloads"] = {
            "graphics": graphics.payload,
            "automatic": automatic.payload,
        }
        if graphics.return_code == SKIP_EXIT_CODE:
            if graphics.payload is None or graphics.payload.get("status") != "skipped":
                raise ProfileFailure("Graphics arm returned the skip code without a skipped profile result")
            if automatic.return_code not in (0, SKIP_EXIT_CODE):
                raise ProfileFailure(f"automatic arm failed with exit code {automatic.return_code}; see {automatic.log_path}")
            report["status"] = "skipped"
            report["skip_reason"] = graphics.payload.get("reason", "headless_vulkan_or_descriptor_buffer_unavailable")
            return_code = SKIP_EXIT_CODE
        elif automatic.return_code == SKIP_EXIT_CODE:
            require_ok(graphics)
            if automatic.payload is None or automatic.payload.get("status") != "skipped":
                raise ProfileFailure("automatic arm returned the skip code without a skipped profile result")
            report["status"] = "skipped"
            report["skip_reason"] = (automatic.payload or {}).get("reason", "dedicated_transfer_family_unavailable")
            return_code = SKIP_EXIT_CODE
        else:
            report["arms"] = validate_arms(args, graphics, automatic)
            external = copy_external_report(args.external_profiler_report, args.output_dir)
            report["external_profiler_report"] = external.name if external else None
            report["status"], return_code = completion_status(external, args.require_external_profiler)
    except ProfileFailure as error:
        report["status"] = "failed"
        report["error"] = str(error)
        return_code = 1
    finally:
        (args.output_dir / "transfer_queue_report.json").write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        (args.output_dir / "transfer_queue_report.md").write_text(markdown_report(report), encoding="utf-8")

    print(f"Transfer queue profile report: {args.output_dir / 'transfer_queue_report.md'}")
    return return_code


def run_self_test() -> int:
    assert parse_args(["--self-test"]).adapter_index == 0
    payload = {
        "status": "ok",
        "requested_route": "automatic",
        "transfer_queue_enabled": True,
        "requested_adapter_index": 0,
        "selected_adapter_vendor_id": 4098,
        "selected_adapter_device_id": 1234,
        "selected_adapter_uuid": "0123456789abcdef0123456789abcdef",
        "producer_queue": "transfer",
        "graphics_family": 0,
        "transfer_family": 1,
        "producer_family": 1,
        "observed_graphics_readiness_bridge_submissions": 2,
        "graphics_readiness_copies": 2,
        "iterations": 2,
        "in_flight_window": 2,
        "total_logical_upload_bytes": 2097152,
        "modeled_upload_read_write_bytes": 4194304,
        "async_ingress_copy_bytes": 0,
        "graphics_readiness_copy_bytes": 1048576,
        "modeled_graphics_readiness_read_write_bytes": 4194304,
        "graphics_contention_copy_bytes": 0,
        "graphics_contention_copies": 0,
        "modeled_graphics_contention_read_write_bytes": 0,
        "expected_exclusive_ownership_transfers": 0,
        "queue_sharing_mask": GRAPHICS_AND_TRANSFER_SHARING_MASK,
        "resource": "buffer",
        "logical_upload_bytes": 1048576,
        "expected_hash": 123,
        "observed_hash": 123,
        "checksum_verified": True,
        "graphics_readiness_verified": True,
        "logger_errors": 0,
    }
    text = f"noise\n{RESULT_PREFIX}{json.dumps(payload)}\n"
    assert parse_result(text) == payload
    assert parse_result("no result") is None
    graphics_payload = dict(
        payload,
        requested_route="graphics",
        producer_queue="graphics",
        producer_family=0,
        observed_graphics_readiness_bridge_submissions=0,
        queue_sharing_mask=0,
    )
    automatic_arm = ArmResult("automatic", 0, 0.1, payload, Path("automatic.log"))
    graphics_arm = ArmResult("graphics", 0, 0.1, graphics_payload, Path("graphics.log"))
    expected_args = SimpleNamespace(
        resource="buffer",
        adapter_index=0,
        upload_mib=1,
        iterations=2,
        in_flight=2,
        contention_mib=0,
        contention_copies=0,
    )
    validated = validate_arms(expected_args, graphics_arm, automatic_arm)
    assert validated["automatic"]["producer_queue"] == "transfer"
    assert validated["graphics"]["producer_queue"] == "graphics"
    assert completion_status(None, False) == ("incomplete_external_capture_required", INCOMPLETE_EXIT_CODE)
    assert completion_status(None, True) == ("incomplete_external_capture_required", 1)
    assert completion_status(Path("capture.rgp"), False) == ("capture_attached_pending_review", INCOMPLETE_EXIT_CODE)
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


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
