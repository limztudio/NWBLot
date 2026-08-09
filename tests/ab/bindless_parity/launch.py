#!/usr/bin/env python3
"""Build and compare hardware and software bindless ray-path smoke captures.

The three cases exercise the production descriptor-buffer bindless paths without tying
the workflow to a particular operating system or an old migration phase:

    python launcher.py bindless-parity soft-shadows
    python launcher.py bindless-parity caustics
    python launcher.py bindless-parity surfel-gi

Each run builds the paired smoke executables, captures their native windows through the
shared cross-platform smoke capture backend, writes a BMP difference image and JSON report
under ``.cozter/out/ab-results/``, and optionally enforces caller-supplied difference limits.
"""

from __future__ import annotations

import argparse
import json
import os
import struct
import sys
import tempfile
import time
from dataclasses import asdict, dataclass
from datetime import datetime
from pathlib import Path
from types import SimpleNamespace
from typing import Dict, List, Mapping, Optional, Sequence, Tuple


REPO = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO))
sys.path.insert(0, str(REPO / "tests" / "smoke"))

import launcher as ROOT_LAUNCHER  # noqa: E402
from window_capture_smoke import (  # noqa: E402
    SKIP_EXIT_CODE,
    SmokeFailure,
    SmokeSkip,
    build_launch_environment,
    collect_log_delta,
    create_capture_backend,
    ensure_process_running,
    launch_logserver,
    launch_testbed,
    require_normal_testbed_exit,
    terminate_process,
    validate_capture_result,
)


REQUIRED_DEFINES = {
    "NWB_BUILD_LOADER": "ON",
    "NWB_BUILD_LOGSERVER": "ON",
    "NWB_BUILD_RESOURCE_COOKER": "ON",
    "NWB_BUILD_TESTS": "ON",
}
FORBIDDEN_LOG_MESSAGES = ("[ERROR]", "VUID-", "Validation Error")


@dataclass(frozen=True)
class ParityCase:
    hardware_target: str
    software_target: str
    runtime_directory: Path
    window_title: str
    default_settle_seconds: float
    frozen_environment: Mapping[str, str]
    description: str


PARITY_CASES: Mapping[str, ParityCase] = {
    "soft-shadows": ParityCase(
        hardware_target="nwb_soft_shadow_test_smoke",
        software_target="nwb_soft_shadow_test_sw_smoke",
        runtime_directory=Path("Testing") / "skinning_culling_benchmark_runtime",
        window_title="NWB Soft Shadow Test",
        default_settle_seconds=6.0,
        frozen_environment={"NWB_SOFT_SHADOW_TEST_SPIN_ANGLE": "0.6"},
        description="Pinned soft-shadow parity between the hardware-hybrid and forced-software paths.",
    ),
    "caustics": ParityCase(
        hardware_target="nwb_caustic_sphere_smoke",
        software_target="nwb_caustic_sphere_sw_smoke",
        runtime_directory=Path("Testing") / "smoke_runtime",
        window_title="NWB Caustic Sphere Smoke",
        default_settle_seconds=6.0,
        frozen_environment={"NWB_TRANSPARENT_MULTI_SPIN_ANGLE": "0.6"},
        description="Pinned caustic-sphere comparison after temporal accumulation settles.",
    ),
    "surfel-gi": ParityCase(
        hardware_target="nwb_gi_test_smoke",
        software_target="nwb_gi_test_sw_smoke",
        runtime_directory=Path("Testing") / "skinning_culling_benchmark_runtime",
        window_title="NWB GI Test",
        default_settle_seconds=6.0,
        frozen_environment={},
        description="Surfel-GI comparison between the hardware and forced-software trace paths.",
    ),
}


@dataclass(frozen=True)
class ParityPaths:
    hardware_executable: Path
    software_executable: Path
    runtime_directory: Path
    output_directory: Path


@dataclass(frozen=True)
class PixelDifference:
    width: int
    height: int
    max_abs: int
    mean_abs: float
    changed_pixels: int
    total_pixels: int

    @property
    def changed_fraction(self) -> float:
        return self.changed_pixels / self.total_pixels if self.total_pixels else 0.0


def default_output_directory(root: Path, case_name: str) -> Path:
    stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    return root / ".cozter" / "out" / "ab-results" / "bindless-parity" / case_name / stamp


def resolve_paths(args: argparse.Namespace, settings, case_name: str, case: ParityCase) -> ParityPaths:
    hardware_executable = ROOT_LAUNCHER.resolve_executable_path(
        settings,
        case.hardware_target,
        args.hardware_executable,
        None,
        args.dry_run,
    )
    software_executable = ROOT_LAUNCHER.resolve_executable_path(
        settings,
        case.software_target,
        args.software_executable,
        None,
        args.dry_run,
    )
    runtime_directory = (
        ROOT_LAUNCHER.resolve_path(settings.root, args.runtime_dir)
        if args.runtime_dir is not None
        else settings.build_dir / case.runtime_directory / settings.config
    )
    output_directory = (
        ROOT_LAUNCHER.resolve_path(settings.root, args.output_dir)
        if args.output_dir is not None
        else default_output_directory(settings.root, case_name)
    )
    return ParityPaths(hardware_executable, software_executable, runtime_directory, output_directory)


def build_case_targets(args: argparse.Namespace, settings, case: ParityCase, environment: Dict[str, str]) -> None:
    ROOT_LAUNCHER.build_targets(args, settings, (case.hardware_target, case.software_target), environment)


def capture_launch_args(args: argparse.Namespace, runtime_directory: Path) -> SimpleNamespace:
    return SimpleNamespace(
        no_logserver=args.no_logserver,
        logserver_executable=args.logserver_executable,
        log_port=0,
        working_directory=runtime_directory,
        timeout=args.startup_timeout,
        application_arg=["--gpudbg"] if args.gpu_validation else [],
        software_vulkan=args.software_vulkan,
    )


def capture_variant(
    args: argparse.Namespace,
    case: ParityCase,
    variant_name: str,
    executable: Path,
    output_path: Path,
    settle_seconds: float,
) -> None:
    if not executable.is_file():
        raise SmokeFailure(f"{variant_name} executable does not exist: {executable}")
    if not args.runtime_directory.is_dir():
        raise SmokeFailure(f"runtime directory does not exist: {args.runtime_directory}")

    launch_args = capture_launch_args(args, args.runtime_directory)
    environment = build_launch_environment(launch_args)
    environment["NWB_RENDER_UNFOCUSED"] = "1"
    for name, default_value in case.frozen_environment.items():
        environment[name] = os.environ.get(name, default_value)

    backend = None
    logserver_process = None
    app_process = None
    window_handle = None
    app_exit_code = None
    app_exit_tail = ""
    log_directory = None
    log_baseline = {}
    log_pattern = ""
    log_text = ""
    try:
        backend = create_capture_backend()
        logserver_process, log_port, log_directory, log_baseline, log_pattern = launch_logserver(
            launch_args, executable, environment
        )
        app_process = launch_testbed(launch_args, executable, environment, log_port)
        window_handle = backend.wait_for_window(app_process.pid, args.startup_timeout, case.window_title)
        if not window_handle:
            ensure_process_running(app_process, f"while waiting for the {variant_name} window")
            raise SmokeFailure(f"{variant_name} smoke did not expose '{case.window_title}'")

        time.sleep(settle_seconds)
        ensure_process_running(app_process, f"before {variant_name} capture")
        result = backend.capture_window(window_handle, output_path)
        validate_capture_result(result)
    finally:
        if app_process is not None:
            app_exit_code, app_exit_tail = terminate_process(app_process, f"{variant_name} bindless parity", window_handle)
        time.sleep(0.25)
        if log_directory:
            log_text = collect_log_delta(log_directory, log_baseline, log_pattern)
        terminate_process(logserver_process, f"{variant_name} bindless parity logserver")
        if backend:
            backend.close()

    require_normal_testbed_exit(app_exit_code, app_exit_tail)
    rejected = [message for message in FORBIDDEN_LOG_MESSAGES if message in log_text]
    if rejected:
        raise SmokeFailure(f"{variant_name} bindless parity capture found forbidden log messages: {rejected}")


def read_bmp_rgb(path: Path) -> Tuple[int, int, bytes]:
    data = path.read_bytes()
    if len(data) < 54 or data[:2] != b"BM":
        raise SmokeFailure(f"capture is not a BMP: {path}")

    pixel_offset = struct.unpack_from("<I", data, 10)[0]
    dib_size = struct.unpack_from("<I", data, 14)[0]
    if dib_size < 40 or len(data) < 14 + dib_size:
        raise SmokeFailure(f"capture has an unsupported DIB header: {path}")

    width, signed_height, planes, bits_per_pixel, compression = struct.unpack_from("<iiHHI", data, 18)
    if width <= 0 or signed_height == 0 or planes != 1 or bits_per_pixel != 24 or compression != 0:
        raise SmokeFailure(f"capture must be an uncompressed 24-bit BMP: {path}")

    height = abs(signed_height)
    source_stride = ((width * 3 + 3) // 4) * 4
    required_bytes = pixel_offset + source_stride * height
    if required_bytes > len(data):
        raise SmokeFailure(f"capture pixel data is truncated: {path}")

    rows: List[bytes] = []
    bottom_up = signed_height > 0
    for logical_row in range(height):
        source_row = height - 1 - logical_row if bottom_up else logical_row
        source_offset = pixel_offset + source_row * source_stride
        bgr = data[source_offset:source_offset + width * 3]
        rgb = bytearray(width * 3)
        for pixel_offset_in_row in range(0, len(bgr), 3):
            rgb[pixel_offset_in_row] = bgr[pixel_offset_in_row + 2]
            rgb[pixel_offset_in_row + 1] = bgr[pixel_offset_in_row + 1]
            rgb[pixel_offset_in_row + 2] = bgr[pixel_offset_in_row]
        rows.append(bytes(rgb))
    return width, height, b"".join(rows)


def write_bmp_rgb(path: Path, width: int, height: int, rgb: bytes) -> None:
    if len(rgb) != width * height * 3:
        raise ValueError("RGB payload size does not match BMP dimensions")

    path.parent.mkdir(parents=True, exist_ok=True)
    row_stride = ((width * 3 + 3) // 4) * 4
    image_size = row_stride * height
    padding = b"\0" * (row_stride - width * 3)
    with path.open("wb") as output:
        output.write(struct.pack("<2sIHHI", b"BM", 14 + 40 + image_size, 0, 0, 54))
        output.write(struct.pack("<IIIHHIIIIII", 40, width, height, 1, 24, 0, image_size, 0, 0, 0, 0))
        for row_index in range(height - 1, -1, -1):
            row = rgb[row_index * width * 3:(row_index + 1) * width * 3]
            bgr = bytearray(width * 3)
            for pixel_offset in range(0, len(row), 3):
                bgr[pixel_offset] = row[pixel_offset + 2]
                bgr[pixel_offset + 1] = row[pixel_offset + 1]
                bgr[pixel_offset + 2] = row[pixel_offset]
            output.write(bgr)
            output.write(padding)


def compare_bmp_rgb(hardware_path: Path, software_path: Path, difference_path: Path) -> PixelDifference:
    hardware_width, hardware_height, hardware_rgb = read_bmp_rgb(hardware_path)
    software_width, software_height, software_rgb = read_bmp_rgb(software_path)
    if (hardware_width, hardware_height) != (software_width, software_height):
        raise SmokeFailure(
            "hardware and software captures have different dimensions: "
            f"{hardware_width}x{hardware_height} vs {software_width}x{software_height}"
        )

    total_abs = 0
    max_abs = 0
    changed_pixels = 0
    difference_rgb = bytearray(len(hardware_rgb))
    for pixel_offset in range(0, len(hardware_rgb), 3):
        pixel_changed = False
        for channel in range(3):
            delta = abs(hardware_rgb[pixel_offset + channel] - software_rgb[pixel_offset + channel])
            total_abs += delta
            max_abs = max(max_abs, delta)
            difference_rgb[pixel_offset + channel] = min(255, delta * 8)
            pixel_changed = pixel_changed or delta != 0
        if pixel_changed:
            changed_pixels += 1

    write_bmp_rgb(difference_path, hardware_width, hardware_height, bytes(difference_rgb))
    total_pixels = hardware_width * hardware_height
    return PixelDifference(
        width=hardware_width,
        height=hardware_height,
        max_abs=max_abs,
        mean_abs=total_abs / len(hardware_rgb),
        changed_pixels=changed_pixels,
        total_pixels=total_pixels,
    )


def difference_failures(args: argparse.Namespace, difference: PixelDifference) -> List[str]:
    failures: List[str] = []
    maximum_max_abs = 0 if args.require_exact else args.maximum_max_abs
    maximum_mean_abs = 0.0 if args.require_exact else args.maximum_mean_abs
    maximum_changed_fraction = 0.0 if args.require_exact else args.maximum_changed_fraction
    if maximum_max_abs is not None and difference.max_abs > maximum_max_abs:
        failures.append(f"max abs {difference.max_abs} exceeds {maximum_max_abs}")
    if maximum_mean_abs is not None and difference.mean_abs > maximum_mean_abs:
        failures.append(f"mean abs {difference.mean_abs:.6f} exceeds {maximum_mean_abs:.6f}")
    if maximum_changed_fraction is not None and difference.changed_fraction > maximum_changed_fraction:
        failures.append(
            f"changed fraction {difference.changed_fraction:.6f} exceeds {maximum_changed_fraction:.6f}"
        )
    return failures


def write_report(
    path: Path,
    case_name: str,
    case: ParityCase,
    paths: ParityPaths,
    difference: PixelDifference,
    failures: Sequence[str],
) -> None:
    payload = {
        "schema": "nwb.bindless-parity.v1",
        "case": case_name,
        "description": case.description,
        "verdict": "fail" if failures else "report-only",
        "failures": list(failures),
        "hardware_capture": str(paths.output_directory / "hardware.bmp"),
        "software_capture": str(paths.output_directory / "software.bmp"),
        "difference_capture": str(paths.output_directory / "difference.bmp"),
        "difference": asdict(difference) | {"changed_fraction": difference.changed_fraction},
    }
    path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def require_positive(parser: argparse.ArgumentParser, option: str, value: float) -> None:
    if value <= 0.0:
        parser.error(f"{option} must be positive")


def require_non_negative(parser: argparse.ArgumentParser, option: str, value: float) -> None:
    if value < 0.0:
        parser.error(f"{option} must not be negative")


def make_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    ROOT_LAUNCHER.add_build_options(parser)
    parser.add_argument("case", nargs="?", choices=sorted(PARITY_CASES), help="Parity profile to capture.")
    parser.add_argument("--hardware-executable", type=Path, help="Override the hardware smoke executable.")
    parser.add_argument("--software-executable", type=Path, help="Override the forced-software smoke executable.")
    parser.add_argument("--runtime-dir", type=Path, help="Override the cooked smoke runtime directory.")
    parser.add_argument("--output-dir", type=Path, help="Directory for captures, difference image, and report.")
    parser.add_argument("--logserver-executable", type=Path, help="Override the logserver executable.")
    parser.add_argument("--no-logserver", action="store_true", help="Use standalone loader logs instead of logserver.")
    parser.add_argument("--startup-timeout", type=float, default=45.0, help="Timeout for device creation and window visibility.")
    parser.add_argument("--settle-seconds", type=float, help="Override the case-specific capture settle duration.")
    parser.add_argument("--gpu-validation", action="store_true", help="Pass --gpudbg to both smoke processes.")
    parser.add_argument(
        "--software-vulkan",
        choices=("auto", "on", "off"),
        default="off",
        help="On Linux, optionally use Mesa lavapipe through the shared smoke capture backend.",
    )
    parser.add_argument("--maximum-max-abs", type=int, help="Fail when a channel difference exceeds this value (0-255).")
    parser.add_argument("--maximum-mean-abs", type=float, help="Fail when mean channel difference exceeds this value.")
    parser.add_argument(
        "--maximum-changed-fraction",
        type=float,
        help="Fail when the changed-pixel fraction exceeds this value (0-1).",
    )
    parser.add_argument("--require-exact", action="store_true", help="Require byte-identical captures.")
    parser.add_argument("--self-test", action="store_true", help="Run parser and BMP-difference checks without Vulkan.")
    parser.set_defaults(config="opt")
    return parser


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = make_parser()
    args = parser.parse_args(argv)
    if args.self_test:
        return args
    if args.case is None:
        parser.error("case is required unless --self-test is used")
    require_positive(parser, "--startup-timeout", args.startup_timeout)
    if args.settle_seconds is not None:
        require_non_negative(parser, "--settle-seconds", args.settle_seconds)
    if args.maximum_max_abs is not None and not 0 <= args.maximum_max_abs <= 255:
        parser.error("--maximum-max-abs must be between 0 and 255")
    if args.maximum_mean_abs is not None:
        require_non_negative(parser, "--maximum-mean-abs", args.maximum_mean_abs)
    if args.maximum_changed_fraction is not None and not 0.0 <= args.maximum_changed_fraction <= 1.0:
        parser.error("--maximum-changed-fraction must be between 0 and 1")
    return args


def run_self_test() -> int:
    parsed = parse_args(["soft-shadows", "--dry-run"])
    assert parsed.config == "opt"
    assert parsed.case == "soft-shadows"

    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        hardware = root / "hardware.bmp"
        software = root / "software.bmp"
        difference = root / "difference.bmp"
        write_bmp_rgb(hardware, 2, 1, bytes((0, 0, 0, 10, 20, 30)))
        write_bmp_rgb(software, 2, 1, bytes((0, 0, 0, 13, 18, 35)))
        metrics = compare_bmp_rgb(hardware, software, difference)
        assert metrics.width == 2 and metrics.height == 1
        assert metrics.max_abs == 5
        assert metrics.changed_pixels == 1
        assert abs(metrics.mean_abs - (10.0 / 6.0)) < 1.0e-9
        assert difference.is_file()

    strict_args = SimpleNamespace(
        require_exact=True,
        maximum_max_abs=None,
        maximum_mean_abs=None,
        maximum_changed_fraction=None,
    )
    assert difference_failures(strict_args, PixelDifference(1, 1, 0, 0.0, 0, 1)) == []
    assert difference_failures(strict_args, PixelDifference(1, 1, 1, 1.0 / 3.0, 1, 1))
    print("bindless parity launcher self-test passed")
    return 0


def run(args: argparse.Namespace) -> int:
    case_name = args.case
    assert case_name is not None
    case = PARITY_CASES[case_name]
    environment = ROOT_LAUNCHER.build_environment(args)
    settings = ROOT_LAUNCHER.resolve_launch_settings(args, ROOT_LAUNCHER.DEFAULT_DOMAIN)
    ROOT_LAUNCHER.maybe_configure(args, settings, REQUIRED_DEFINES, environment)
    settings = ROOT_LAUNCHER.refresh_launch_settings(settings, args.domain)
    build_case_targets(args, settings, case, environment)
    paths = resolve_paths(args, settings, case_name, case)
    if args.dry_run:
        print(f"bindless parity artifacts: {paths.output_directory}", flush=True)
        return 0

    if args.logserver_executable is not None:
        args.logserver_executable = ROOT_LAUNCHER.resolve_path(settings.root, args.logserver_executable)
    paths.output_directory.mkdir(parents=True, exist_ok=True)
    args.runtime_directory = paths.runtime_directory
    settle_seconds = case.default_settle_seconds if args.settle_seconds is None else args.settle_seconds
    hardware_capture = paths.output_directory / "hardware.bmp"
    software_capture = paths.output_directory / "software.bmp"
    capture_variant(args, case, "hardware", paths.hardware_executable, hardware_capture, settle_seconds)
    capture_variant(args, case, "software", paths.software_executable, software_capture, settle_seconds)
    difference = compare_bmp_rgb(hardware_capture, software_capture, paths.output_directory / "difference.bmp")
    failures = difference_failures(args, difference)
    write_report(paths.output_directory / "report.json", case_name, case, paths, difference, failures)
    print(
        f"bindless parity {case_name}: max={difference.max_abs}, mean={difference.mean_abs:.6f}, "
        f"changed={difference.changed_fraction:.4%}; artifacts: {paths.output_directory}",
        flush=True,
    )
    if failures:
        print("FAIL: " + "; ".join(failures), file=sys.stderr)
        return 1
    return 0


def main(argv: Sequence[str]) -> int:
    args = parse_args(argv)
    if args.self_test:
        return run_self_test()
    try:
        return run(args)
    except SmokeSkip as error:
        print(f"SKIP: {error}", file=sys.stderr)
        return SKIP_EXIT_CODE
    except SmokeFailure as error:
        print(f"FAIL: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
