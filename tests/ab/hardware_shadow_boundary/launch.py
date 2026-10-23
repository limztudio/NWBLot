#!/usr/bin/env python3
"""Build and run the hardware transparent-versus-opaque shadow-boundary A/B benchmark.

From the repository root:

    python -m launcher hardware-shadow-boundary

The launcher builds one fixed-yaw stress-scene executable and runs it twice: the normal hardware transparent-shadow arm and
a test-owned opaque-only scene baseline that naturally uses hardware shadows without a transparent shadow tail.
It writes timestamped artifacts beneath ``.cozter/out/ab-results/hardware-shadow-boundary``.
Pass options for ``run.py`` after ``--``, for example:

    python -m launcher hardware-shadow-boundary -- --measure-seconds 30
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

LIT_DBG = "dbg"
LIT_OPT = "opt"
LIT_HARDWARE_SHADOW_BOUNDARY = "hardware-shadow-boundary"
LIT_HEALTHY_EXECUTABLE = "--healthy-executable"
LIT_BASELINE_EXECUTABLE = "--baseline-executable"
LIT_RUNTIME_DIR = AB_LAUNCH.LIT_RUNTIME_DIR
LIT_OUTPUT_DIR = AB_LAUNCH.LIT_OUTPUT_DIR
LIT_STORE_TRUE = AB_LAUNCH.LIT_STORE_TRUE
LIT_SELF_TEST = "--self-test"
LIT_MAIN = "__main__"


RUNNER_SCRIPT = Path("tests") / "ab" / "hardware_shadow_boundary" / "run.py"
HEALTHY_TARGET = "nwb_hardware_shadow_boundary_healthy_benchmark"
RUNTIME_DIRECTORY = Path("Testing") / "skinning_culling_benchmark_runtime"
REQUIRED_DEFINES = AB_LAUNCH.REQUIRED_AB_DEFINES
DIAGNOSTIC_CONFIGURATIONS = (LIT_DBG, LIT_OPT)


@dataclass(frozen=True)
class BoundaryPaths:
    healthy_executable: Path
    baseline_executable: Path
    runtime_directory: Path
    output_directory: Path


def default_output_directory(root: Path) -> Path:
    return AB_LAUNCH.default_ab_output_directory(root, LIT_HARDWARE_SHADOW_BOUNDARY)


def require_diagnostic_configuration(config: str) -> None:
    if config not in DIAGNOSTIC_CONFIGURATIONS:
        raise SystemExit("hardware-shadow-boundary requires --config dbg or --config opt because fin omits the warning " "diagnostics used to reject transient hardware-shadow route degradation")


def resolve_paths(args: argparse.Namespace, settings) -> BoundaryPaths:
    healthy_executable = ROOT_LAUNCHER.resolve_executable_path(settings, HEALTHY_TARGET, args.healthy_executable, None, args.dry_run,)
    baseline_executable = args.baseline_executable
    if baseline_executable is None:
        baseline_executable = healthy_executable
    else:
        baseline_executable = ROOT_LAUNCHER.resolve_executable_path(settings, HEALTHY_TARGET, baseline_executable, None, args.dry_run,)
    runtime_directory = (ROOT_LAUNCHER.resolve_path(settings.root, args.runtime_dir) if args.runtime_dir is not None else settings.build_dir / RUNTIME_DIRECTORY / settings.config)
    output_directory = (ROOT_LAUNCHER.resolve_path(settings.root, args.output_dir) if args.output_dir is not None else default_output_directory(settings.root))
    return BoundaryPaths(healthy_executable, baseline_executable, runtime_directory, output_directory)


def build_benchmark_targets(args: argparse.Namespace, settings, environment) -> None:
    ROOT_LAUNCHER.build_target(args, settings, HEALTHY_TARGET, environment)


def runner_command(args: argparse.Namespace, paths: BoundaryPaths) -> List[object]:
    command: List[object] = [sys.executable, REPO / RUNNER_SCRIPT, LIT_HEALTHY_EXECUTABLE, paths.healthy_executable, LIT_BASELINE_EXECUTABLE, paths.baseline_executable, LIT_RUNTIME_DIR, paths.runtime_directory, LIT_OUTPUT_DIR, paths.output_directory,]
    return AB_LAUNCH.append_ab_runner_args(command, args, REPO)


def run_runner(args: argparse.Namespace, paths: BoundaryPaths) -> int:
    return AB_LAUNCH.run_ab_runner(
        runner_command(args, paths),
        args.dry_run,
        REPO,
        f"Hardware-shadow boundary artifacts: {paths.output_directory}",
    )


def make_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    ROOT_LAUNCHER.add_build_options(parser)
    parser.add_argument(LIT_HEALTHY_EXECUTABLE, type=Path, help="Override the healthy hardware transparent-shadow executable.")
    parser.add_argument(LIT_BASELINE_EXECUTABLE, dest="baseline_executable", type=Path, help="Override the executable reused for the opaque baseline arm.",)
    parser.add_argument(LIT_RUNTIME_DIR, type=Path, help="Override the cooked stress-scene runtime directory.")
    parser.add_argument(LIT_OUTPUT_DIR, type=Path, help="Directory for boundary timing, logs, captures, and reports.")
    AB_LAUNCH.add_ab_runner_options(
        parser,
        default_gpu_validation=False,
        gpu_validation_help="Enable GPU validation for a correctness-oriented run.",
        no_gpu_validation_help="Measure without --gpudbg layer overhead (the default).",
    )
    parser.add_argument(LIT_SELF_TEST, action=LIT_STORE_TRUE, help="Check diagnostic-configuration rejection without starting a graphics application.")
    return parser


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    return AB_LAUNCH.parse_ab_launcher_args(make_parser, argv)


def run_self_test() -> int:
    try:
        require_diagnostic_configuration("fin")
    except SystemExit as error:
        assert "requires --config dbg or --config opt" in str(error)
    else:
        raise AssertionError("fin must not claim the warning-based semantic verdict")
    require_diagnostic_configuration(LIT_DBG)
    require_diagnostic_configuration(LIT_OPT)
    return 0


def run(args: argparse.Namespace) -> int:
    if args.self_test:
        return run_self_test()
    require_diagnostic_configuration(args.config)
    environment = ROOT_LAUNCHER.build_environment(args)
    settings = ROOT_LAUNCHER.resolve_launch_settings(args, ROOT_LAUNCHER.DEFAULT_DOMAIN)
    ROOT_LAUNCHER.maybe_configure(args, settings, REQUIRED_DEFINES, environment)
    settings = ROOT_LAUNCHER.refresh_launch_settings(settings, args.domain)
    build_benchmark_targets(args, settings, environment)
    return run_runner(args, resolve_paths(args, settings))


def main(argv: Sequence[str]) -> int:
    args = parse_args(argv)
    return run(args)


if __name__ == LIT_MAIN:
    raise SystemExit(main(sys.argv[1:]))
