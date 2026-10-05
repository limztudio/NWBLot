#!/usr/bin/env python3
"""Build and run the command-IR overhead profile.

The profile measures the stable native recording path alongside command-IR copy-buffer
capture, validation-reader decode, preflight, Core::CommandList replay, and experimental
direct-Vulkan replay in one process. It writes a
timestamped evidence bundle and is intentionally a CPU-overhead probe rather than a
GPU-performance benchmark.
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
LIT_RECORDS = "--records"
LIT_WARMUP = "--warmup"
LIT_SAMPLES = "--samples"
LIT_GPU_VALIDATION = "--gpu-validation"
LIT_NO_GPU_VALIDATION = "--no-gpu-validation"
LIT_GPU_VALIDATION_2 = "gpu_validation"
LIT_STORE_TRUE = "store_true"
LIT_MAIN = "__main__"
LIT_ON = "ON"


RUNNER_SCRIPT = Path("tests") / "ab" / "command_ir" / "run.py"
PROFILE_TARGET = "nwb_command_ir_profile"
MAX_RECORDS = 65536
MAX_SAMPLES = 64
REQUIRED_DEFINES = {
    "NWB_BUILD_TESTS": LIT_ON,
}


@dataclass(frozen=True)
class ProfilePaths:
    executable: Path
    output_directory: Path


def default_output_directory(root: Path) -> Path:
    stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    return root / ".cozter" / "out" / "ab-results" / "command-ir" / stamp


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
        LIT_RECORDS,
        args.records,
        LIT_WARMUP,
        args.warmup,
        LIT_SAMPLES,
        args.samples,
        LIT_GPU_VALIDATION if args.gpu_validation else LIT_NO_GPU_VALIDATION,
    ]
    command += list(args.runner_args)
    return command


def make_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    ROOT_LAUNCHER.add_build_options(parser)
    parser.add_argument(LIT_EXECUTABLE, type=Path, help="Override the command-IR profile executable.")
    parser.add_argument(LIT_OUTPUT_DIR, type=Path, help="Directory for logs and the profile report.")
    parser.add_argument(
        LIT_ADAPTER_INDEX,
        type=int,
        default=0,
        help="Pinned Vulkan adapter enumeration index (default: 0).",
    )
    parser.add_argument(LIT_RECORDS, type=int, default=4096, help="Copy-buffer records per measured sample (default: 4096).")
    parser.add_argument(LIT_WARMUP, type=int, default=3, help="Unmeasured priming samples (default: 3).")
    parser.add_argument(LIT_SAMPLES, type=int, default=11, help="Measured samples per stage (default: 11).")
    validation_group = parser.add_mutually_exclusive_group()
    validation_group.add_argument(
        LIT_GPU_VALIDATION,
        dest=LIT_GPU_VALIDATION_2,
        action=LIT_STORE_TRUE,
        help="Enable Vulkan validation in the profile process (the default).",
    )
    validation_group.add_argument(
        LIT_NO_GPU_VALIDATION,
        dest=LIT_GPU_VALIDATION_2,
        action="store_false",
        help="Run without Vulkan validation when the target cannot expose the validation layer.",
    )
    parser.set_defaults(gpu_validation=True)
    return parser


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    launcher_args, runner_args = ROOT_LAUNCHER.split_application_args(argv)
    args = make_parser().parse_args(launcher_args)
    if args.adapter_index < 0:
        raise SystemExit("--adapter-index must be a non-negative Vulkan enumeration index")
    if args.records <= 0 or args.records > MAX_RECORDS:
        raise SystemExit(f"--records must be between 1 and {MAX_RECORDS}")
    if args.warmup <= 0:
        raise SystemExit("--warmup must be positive")
    if args.samples <= 0 or args.samples > MAX_SAMPLES:
        raise SystemExit(f"--samples must be between 1 and {MAX_SAMPLES}")
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
    print(f"Command-IR profiling artifacts: {paths.output_directory}", flush=True)
    print("+ " + ROOT_LAUNCHER.format_command(command), flush=True)
    if args.dry_run:
        return 0
    return subprocess.run([str(part) for part in command], cwd=REPO).returncode


def main(argv: Sequence[str]) -> int:
    args = parse_args(argv)
    return run(args)


if __name__ == LIT_MAIN:
    raise SystemExit(main(sys.argv[1:]))
