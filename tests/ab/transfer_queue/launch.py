#!/usr/bin/env python3
"""Build and run the transfer-upload profiling harness.

The workflow compares an explicit Graphics setup-upload baseline with the automatic
Transfer route.  It is target-hardware-only: hosts without a distinct
Transfer family return 77 after preserving a topology/report artifact.
"""

from __future__ import annotations

import argparse
import subprocess
import sys
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path
from typing import List, Sequence


REPO = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO))

import launcher as ROOT_LAUNCHER  # noqa: E402

LIT_EXECUTABLE = "--executable"
LIT_OUTPUT_DIR = "--output-dir"
LIT_ADAPTER_INDEX = "--adapter-index"
LIT_IN_FLIGHT = "--in-flight"
LIT_GPU_VALIDATION = "--gpu-validation"
LIT_EXTERNAL_PROFILER_REPORT = "--external-profiler-report"
LIT_GPU_VALIDATION_2 = "gpu_validation"
LIT_STORE_TRUE = "store_true"
LIT_MAIN = "__main__"
LIT_ON = "ON"


RUNNER_SCRIPT = Path("tests") / "ab" / "transfer_queue" / "run.py"
PROFILE_TARGET = "nwb_transfer_upload_profile"
REQUIRED_DEFINES = {
    "NWB_BUILD_TESTS": LIT_ON,
}


@dataclass(frozen=True)
class ProfilePaths:
    executable: Path
    output_directory: Path


def default_output_directory(root: Path) -> Path:
    stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    return root / ".cozter" / "out" / "ab-results" / "transfer-queue" / stamp


def resolve_paths(args: argparse.Namespace, settings) -> ProfilePaths:
    executable = ROOT_LAUNCHER.resolve_executable_path(
        settings,
        PROFILE_TARGET,
        args.executable,
        None,
        args.dry_run,
    )
    output_directory = (
        ROOT_LAUNCHER.resolve_path(settings.root, args.output_dir)
        if args.output_dir is not None
        else default_output_directory(settings.root)
    )
    return ProfilePaths(executable, output_directory)


def runner_command(args: argparse.Namespace, paths: ProfilePaths) -> List[object]:
    command: List[object] = [
        sys.executable,
        REPO / RUNNER_SCRIPT,
        LIT_EXECUTABLE,
        paths.executable,
        LIT_OUTPUT_DIR,
        paths.output_directory,
        LIT_ADAPTER_INDEX,
        args.adapter_index,
        LIT_IN_FLIGHT,
        args.in_flight,
    ]
    if args.gpu_validation:
        command.append(LIT_GPU_VALIDATION)
    if args.external_profiler_report is not None:
        command += [LIT_EXTERNAL_PROFILER_REPORT, ROOT_LAUNCHER.resolve_path(REPO, args.external_profiler_report)]
    command += list(args.runner_args)
    return command


def make_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    ROOT_LAUNCHER.add_build_options(parser)
    parser.add_argument(LIT_EXECUTABLE, type=Path, help="Override the transfer-upload profile executable.")
    parser.add_argument(LIT_OUTPUT_DIR, type=Path, help="Directory for logs and the profiling report.")
    parser.add_argument(
        LIT_ADAPTER_INDEX,
        type=int,
        default=0,
        help="Pinned Vulkan adapter enumeration index for both A/B processes (default: 0).",
    )
    parser.add_argument(LIT_IN_FLIGHT, type=int, default=2, help="Maximum concurrently retained upload windows per arm.")
    parser.add_argument(
        LIT_EXTERNAL_PROFILER_REPORT,
        type=Path,
        help="Optional external GPU-profiler report/trace to copy into the artifact bundle.",
    )
    validation_group = parser.add_mutually_exclusive_group()
    validation_group.add_argument(
        LIT_GPU_VALIDATION,
        dest=LIT_GPU_VALIDATION_2,
        action=LIT_STORE_TRUE,
        help="Enable Vulkan validation in both A/B arms (the default).",
    )
    validation_group.add_argument(
        "--no-gpu-validation",
        dest=LIT_GPU_VALIDATION_2,
        action="store_false",
        help="Run without Vulkan validation when a target cannot expose the validation layer.",
    )
    parser.set_defaults(gpu_validation=True)
    return parser


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    launcher_args, runner_args = ROOT_LAUNCHER.split_application_args(argv)
    args = make_parser().parse_args(launcher_args)
    if args.adapter_index < 0:
        raise SystemExit("--adapter-index must be a non-negative Vulkan enumeration index for paired A/B evidence")
    if args.in_flight <= 0:
        raise SystemExit("--in-flight must be positive")
    args.runner_args = runner_args
    return args


def run(args: argparse.Namespace) -> int:
    environment = ROOT_LAUNCHER.build_environment(args)
    settings = ROOT_LAUNCHER.resolve_launch_settings(args, ROOT_LAUNCHER.DEFAULT_DOMAIN)
    ROOT_LAUNCHER.maybe_configure(args, settings, REQUIRED_DEFINES, environment)
    settings = ROOT_LAUNCHER.refresh_launch_settings(settings, args.domain)
    ROOT_LAUNCHER.build_target(args, settings, PROFILE_TARGET, environment)

    paths = resolve_paths(args, settings)
    command = runner_command(args, paths)
    print(f"Transfer-queue profiling artifacts: {paths.output_directory}", flush=True)
    print("+ " + ROOT_LAUNCHER.format_command(command), flush=True)
    if args.dry_run:
        return 0
    return subprocess.run([str(part) for part in command], cwd=REPO).returncode


def main(argv: Sequence[str]) -> int:
    args = parse_args(argv)
    return run(args)


if __name__ == LIT_MAIN:
    raise SystemExit(main(sys.argv[1:]))
