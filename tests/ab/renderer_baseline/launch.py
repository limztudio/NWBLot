#!/usr/bin/env python3
"""Build and run one immutable current-renderer baseline capture.

Examples:

    python -m launcher renderer-baseline transparent-avboit
    python -m launcher renderer-baseline stress -- --reference-dir .cozter/out/ab-results/renderer-baseline/stress/<stamp>
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
from launcher.build import BuildController  # noqa: E402
from launcher.cli import LauncherCli  # noqa: E402
from launcher.cmake_settings import CmakeSettings  # noqa: E402
from launcher.constants import DEFAULT_DOMAIN  # noqa: E402
from profiles import get_profile, profile_names  # noqa: E402

LIT_RENDERER_BASELINE = "renderer-baseline"
LIT_PROFILE = "--profile"
LIT_EXECUTABLE = "--executable"
LIT_RUNTIME_DIR = AB_LAUNCH.LIT_RUNTIME_DIR
LIT_OUTPUT_DIR = AB_LAUNCH.LIT_OUTPUT_DIR
LIT_MAIN = "__main__"


RUNNER_SCRIPT = Path("tests") / "ab" / "renderer_baseline" / "run.py"
REQUIRED_DEFINES = AB_LAUNCH.REQUIRED_AB_DEFINES


@dataclass(frozen=True)
class BaselinePaths:
    executable: Path
    runtime_directory: Path
    output_directory: Path


def default_output_directory(root: Path, profile_name: str) -> Path:
    return AB_LAUNCH.default_ab_output_directory(root, f"{LIT_RENDERER_BASELINE}/{profile_name}")


def resolve_paths(args: argparse.Namespace, settings) -> BaselinePaths:
    profile = get_profile(args.profile)
    executable = BuildController.resolve_executable_path(settings, profile.target, args.executable, None, args.dry_run)
    runtime_directory = (CmakeSettings.resolve_path(settings.root, args.runtime_dir) if args.runtime_dir is not None else settings.build_dir / profile.runtime_directory / settings.config)
    output_directory = (CmakeSettings.resolve_path(settings.root, args.output_dir) if args.output_dir is not None else default_output_directory(settings.root, args.profile))
    return BaselinePaths(executable, runtime_directory, output_directory)


def build_target(args: argparse.Namespace, settings, environment) -> None:
    profile = get_profile(args.profile)
    BuildController.build_targets(args, settings, (profile.target,), environment)


def runner_command(args: argparse.Namespace, paths: BaselinePaths) -> List[object]:
    command: List[object] = [sys.executable, REPO / RUNNER_SCRIPT, LIT_PROFILE, args.profile, LIT_EXECUTABLE, paths.executable, LIT_RUNTIME_DIR, paths.runtime_directory, LIT_OUTPUT_DIR, paths.output_directory,]
    return AB_LAUNCH.append_ab_runner_args(command, args, REPO)


def make_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    LauncherCli.add_build_options(parser)
    parser.add_argument("profile", nargs="?", choices=profile_names(), help="Renderer scene to capture.")
    parser.add_argument(LIT_EXECUTABLE, type=Path, help="Override the selected smoke executable.")
    parser.add_argument(LIT_RUNTIME_DIR, type=Path, help="Override the selected cooked smoke runtime directory.")
    parser.add_argument(LIT_OUTPUT_DIR, type=Path, help="Directory for the immutable capture and manifest.")
    AB_LAUNCH.add_ab_runner_options(
        parser,
        default_gpu_validation=True,
        gpu_validation_help="Enable GPU validation for this capture.",
        no_gpu_validation_help="Do not pass --gpudbg.",
    )
    return parser


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    args = AB_LAUNCH.parse_ab_launcher_args(make_parser, argv)
    if args.profile is None:
        raise SystemExit("renderer-baseline requires a profile; choose one of: " + ", ".join(profile_names()))
    return args


def run(args: argparse.Namespace) -> int:
    environment = BuildController.build_environment(args)
    settings = CmakeSettings.resolve_launch_settings(args, DEFAULT_DOMAIN)
    CmakeSettings.maybe_configure(args, settings, REQUIRED_DEFINES, environment)
    settings = CmakeSettings.refresh_launch_settings(settings, args.domain)
    build_target(args, settings, environment)
    paths = resolve_paths(args, settings)
    return AB_LAUNCH.run_ab_runner(
        runner_command(args, paths),
        args.dry_run,
        REPO,
        f"Renderer baseline artifacts: {paths.output_directory}",
    )


def main(argv: Sequence[str]) -> int:
    args = parse_args(argv)
    return run(args)


if __name__ == LIT_MAIN:
    raise SystemExit(main(sys.argv[1:]))
