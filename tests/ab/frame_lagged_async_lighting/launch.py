#!/usr/bin/env python3
"""Build and run the frame-lagged async-lighting validation in one command.

From the repository root:

    python -m launcher frame-lagged-async-lighting

The command configures the required smoke target, builds its cooked runtime assets, and
then runs the lifecycle validator. Pass runner-specific options after ``--``.
"""

from __future__ import annotations

import argparse
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import List, Sequence


REPO = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO))
sys.path.insert(0, str(REPO / "tests" / "ab"))

import launch as AB_LAUNCH  # noqa: E402
import launcher as ROOT_LAUNCHER  # noqa: E402

LIT_EXECUTABLE = "--executable"
LIT_RUNTIME_DIR = AB_LAUNCH.LIT_RUNTIME_DIR
LIT_MAIN = "__main__"


RUNNER_SCRIPT = Path("tests") / "ab" / "frame_lagged_async_lighting" / "run.py"
SMOKE_TARGET = "nwb_frame_lagged_async_lighting_smoke"
RUNTIME_DIRECTORY = Path("Testing") / "smoke_runtime"
REQUIRED_DEFINES = AB_LAUNCH.REQUIRED_AB_DEFINES


@dataclass(frozen=True)
class FrameLaggedPaths:
    executable: Path
    runtime_directory: Path


def resolve_paths(args: argparse.Namespace, settings) -> FrameLaggedPaths:
    executable = ROOT_LAUNCHER.resolve_executable_path(settings, SMOKE_TARGET, args.executable, None, args.dry_run,)
    runtime_directory = (ROOT_LAUNCHER.resolve_path(settings.root, args.runtime_dir) if args.runtime_dir is not None else settings.build_dir / RUNTIME_DIRECTORY / settings.config)
    return FrameLaggedPaths(executable, runtime_directory)


def runner_command(args: argparse.Namespace, paths: FrameLaggedPaths) -> List[object]:
    command: List[object] = [sys.executable, REPO / RUNNER_SCRIPT, LIT_EXECUTABLE, paths.executable, LIT_RUNTIME_DIR, paths.runtime_directory,]
    return AB_LAUNCH.append_ab_runner_args(command, args, REPO)


def run_runner(args: argparse.Namespace, paths: FrameLaggedPaths) -> int:
    return AB_LAUNCH.run_ab_runner(runner_command(args, paths), args.dry_run, REPO)


def make_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    ROOT_LAUNCHER.add_build_options(parser)
    parser.add_argument(LIT_EXECUTABLE, type=Path, help="Override the frame-lagged smoke executable.")
    parser.add_argument(LIT_RUNTIME_DIR, type=Path, help="Override the cooked smoke runtime directory.")
    AB_LAUNCH.add_ab_runner_options(
        parser,
        default_gpu_validation=True,
        gpu_validation_help="Enable GPU validation for the selected backend (the default).",
        no_gpu_validation_help="Do not pass --gpudbg to the smoke process.",
    )
    return parser


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    return AB_LAUNCH.parse_ab_launcher_args(make_parser, argv)


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
