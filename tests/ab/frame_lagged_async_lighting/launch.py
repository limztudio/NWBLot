#!/usr/bin/env python3
"""Build and run the frame-lagged async-lighting validation in one command.

From the repository root:

    python -m launcher frame-lagged-async-lighting

The command configures the required smoke target, builds its cooked runtime assets, and
then runs the lifecycle validator. Pass runner-specific options after ``--``.
"""

from __future__ import annotations

import argparse
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import List, Sequence


REPO = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO))

import launcher as ROOT_LAUNCHER  # noqa: E402

LIT_ON = "ON"
LIT_EXECUTABLE = "--executable"
LIT_RUNTIME_DIR = "--runtime-dir"
LIT_GPU_VALIDATION = "--gpu-validation"
LIT_NO_LOGSERVER = "--no-logserver"
LIT_LOGSERVER_EXECUTABLE = "--logserver-executable"
LIT_STORE_TRUE = "store_true"
LIT_GPU_VALIDATION_2 = "gpu_validation"
LIT_MAIN = "__main__"


RUNNER_SCRIPT = Path("tests") / "ab" / "frame_lagged_async_lighting" / "run.py"
SMOKE_TARGET = "nwb_frame_lagged_async_lighting_smoke"
RUNTIME_DIRECTORY = Path("Testing") / "smoke_runtime"
REQUIRED_DEFINES = {
    "NWB_BUILD_LOADER": LIT_ON,
    "NWB_BUILD_LOGSERVER": LIT_ON,
    "NWB_BUILD_PIPELINE": LIT_ON,
    "NWB_BUILD_TESTS": LIT_ON,
}


@dataclass(frozen=True)
class FrameLaggedPaths:
    executable: Path
    runtime_directory: Path


def resolve_paths(args: argparse.Namespace, settings) -> FrameLaggedPaths:
    executable = ROOT_LAUNCHER.resolve_executable_path(
        settings,
        SMOKE_TARGET,
        args.executable,
        None,
        args.dry_run,
    )
    runtime_directory = (
        ROOT_LAUNCHER.resolve_path(settings.root, args.runtime_dir)
        if args.runtime_dir is not None
        else settings.build_dir / RUNTIME_DIRECTORY / settings.config
    )
    return FrameLaggedPaths(executable, runtime_directory)


def runner_command(args: argparse.Namespace, paths: FrameLaggedPaths) -> List[object]:
    command: List[object] = [
        sys.executable,
        REPO / RUNNER_SCRIPT,
        LIT_EXECUTABLE,
        paths.executable,
        LIT_RUNTIME_DIR,
        paths.runtime_directory,
    ]
    if args.gpu_validation:
        command.append(LIT_GPU_VALIDATION)
    if args.no_logserver:
        command.append(LIT_NO_LOGSERVER)
    elif args.logserver_executable is not None:
        command += [LIT_LOGSERVER_EXECUTABLE, ROOT_LAUNCHER.resolve_path(REPO, args.logserver_executable)]
    command += list(args.runner_args)
    return command


def run_runner(args: argparse.Namespace, paths: FrameLaggedPaths) -> int:
    command = runner_command(args, paths)
    print("+ " + ROOT_LAUNCHER.format_command(command), flush=True)
    if args.dry_run:
        return 0
    return subprocess.run([str(part) for part in command], cwd=REPO).returncode


def make_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    ROOT_LAUNCHER.add_build_options(parser)
    parser.add_argument(LIT_EXECUTABLE, type=Path, help="Override the frame-lagged smoke executable.")
    parser.add_argument(LIT_RUNTIME_DIR, type=Path, help="Override the cooked smoke runtime directory.")
    parser.add_argument(LIT_LOGSERVER_EXECUTABLE, type=Path, help="Override the logserver executable.")
    parser.add_argument(LIT_NO_LOGSERVER, action=LIT_STORE_TRUE, help="Use standalone loader logs instead of logserver.")
    validation_group = parser.add_mutually_exclusive_group()
    validation_group.add_argument(
        LIT_GPU_VALIDATION,
        dest=LIT_GPU_VALIDATION_2,
        action=LIT_STORE_TRUE,
        help="Enable GPU validation for the selected backend (the default).",
    )
    validation_group.add_argument(
        "--no-gpu-validation",
        dest=LIT_GPU_VALIDATION_2,
        action="store_false",
        help="Do not pass --gpudbg to the smoke process.",
    )
    parser.set_defaults(gpu_validation=True)
    return parser


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    launcher_args, runner_args = ROOT_LAUNCHER.split_application_args(argv)
    args = make_parser().parse_args(launcher_args)
    args.runner_args = runner_args
    return args


def run(args: argparse.Namespace) -> int:
    environment = ROOT_LAUNCHER.build_environment(args)
    settings = ROOT_LAUNCHER.resolve_launch_settings(args, ROOT_LAUNCHER.DEFAULT_DOMAIN)
    ROOT_LAUNCHER.maybe_configure(args, settings, REQUIRED_DEFINES, environment)
    settings = ROOT_LAUNCHER.refresh_launch_settings(settings, args.domain)
    ROOT_LAUNCHER.build_target(args, settings, SMOKE_TARGET, environment)
    return run_runner(args, resolve_paths(args, settings))


def main(argv: Sequence[str]) -> int:
    args = parse_args(argv)
    return run(args)


if __name__ == LIT_MAIN:
    raise SystemExit(main(sys.argv[1:]))
