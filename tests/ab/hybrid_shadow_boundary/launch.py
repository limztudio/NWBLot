#!/usr/bin/env python3
"""Build and run the hardware transparent-versus-opaque shadow-boundary A/B benchmark.

From the repository root:

    python -m launcher hybrid-shadow-boundary

The launcher builds one fixed-yaw stress-scene executable and runs it twice: the normal hardware transparent-shadow arm and
a test-owned opaque-only scene baseline that naturally uses hardware shadows without a transparent shadow tail.
The legacy hybrid target and launcher identifiers remain stable for existing automation.
It writes timestamped artifacts beneath ``.cozter/out/ab-results/hybrid-shadow-boundary``.
Pass options for ``run.py`` after ``--``, for example:

    python -m launcher hybrid-shadow-boundary -- --measure-seconds 30
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

# Shared literals (no inline hardcodes below this block).
LIT_ON = "ON"
LIT_DBG = "dbg"
LIT_OPT = "opt"
LIT_COZTER = ".cozter"
LIT_OUT = "out"
LIT_AB_RESULTS = "ab-results"
LIT_HYBRID_SHADOW_BOUNDARY = "hybrid-shadow-boundary"
LIT_HEALTHY_EXECUTABLE = "--healthy-executable"
LIT_BASELINE_EXECUTABLE = "--baseline-executable"
LIT_RUNTIME_DIR = "--runtime-dir"
LIT_OUTPUT_DIR = "--output-dir"
LIT_GPU_VALIDATION = "--gpu-validation"
LIT_NO_LOGSERVER = "--no-logserver"
LIT_LOGSERVER_EXECUTABLE = "--logserver-executable"
LIT_STORE_TRUE = "store_true"
LIT_GPU_VALIDATION_2 = "gpu_validation"
LIT_SELF_TEST = "--self-test"
LIT_MAIN = "__main__"


RUNNER_SCRIPT = Path("tests") / "ab" / "hybrid_shadow_boundary" / "run.py"
HEALTHY_TARGET = "nwb_hybrid_shadow_boundary_healthy_benchmark"
RUNTIME_DIRECTORY = Path("Testing") / "skinning_culling_benchmark_runtime"
REQUIRED_DEFINES = {
    "NWB_BUILD_LOADER": LIT_ON,
    "NWB_BUILD_LOGSERVER": LIT_ON,
    "NWB_BUILD_PIPELINE": LIT_ON,
    "NWB_BUILD_TESTS": LIT_ON,
}
DIAGNOSTIC_CONFIGURATIONS = (LIT_DBG, LIT_OPT)


@dataclass(frozen=True)
class BoundaryPaths:
    healthy_executable: Path
    baseline_executable: Path
    runtime_directory: Path
    output_directory: Path


def default_output_directory(root: Path) -> Path:
    stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    return root / LIT_COZTER / LIT_OUT / LIT_AB_RESULTS / LIT_HYBRID_SHADOW_BOUNDARY / stamp


def require_diagnostic_configuration(config: str) -> None:
    if config not in DIAGNOSTIC_CONFIGURATIONS:
        raise SystemExit(
            "hybrid-shadow-boundary requires --config dbg or --config opt because fin omits the warning "
            "diagnostics used to reject transient hardware-shadow route degradation"
        )


def resolve_paths(args: argparse.Namespace, settings) -> BoundaryPaths:
    healthy_executable = ROOT_LAUNCHER.resolve_executable_path(
        settings,
        HEALTHY_TARGET,
        args.healthy_executable,
        None,
        args.dry_run,
    )
    baseline_executable = args.baseline_executable
    if baseline_executable is None:
        baseline_executable = healthy_executable
    else:
        baseline_executable = ROOT_LAUNCHER.resolve_executable_path(
            settings,
            HEALTHY_TARGET,
            baseline_executable,
            None,
            args.dry_run,
        )
    runtime_directory = (
        ROOT_LAUNCHER.resolve_path(settings.root, args.runtime_dir)
        if args.runtime_dir is not None
        else settings.build_dir / RUNTIME_DIRECTORY / settings.config
    )
    output_directory = (
        ROOT_LAUNCHER.resolve_path(settings.root, args.output_dir)
        if args.output_dir is not None
        else default_output_directory(settings.root)
    )
    return BoundaryPaths(healthy_executable, baseline_executable, runtime_directory, output_directory)


def build_benchmark_targets(args: argparse.Namespace, settings, environment) -> None:
    if args.skip_build:
        return

    command: List[object] = list(settings.cmake) + [
        "--build",
        str(settings.build_dir),
        "--target",
        HEALTHY_TARGET,
        "--config",
        settings.config,
    ]
    if args.jobs:
        command += ["--parallel", str(args.jobs)]
    ROOT_LAUNCHER.run_checked(command, settings.root, environment, args.dry_run)


def runner_command(args: argparse.Namespace, paths: BoundaryPaths) -> List[object]:
    command: List[object] = [
        sys.executable,
        REPO / RUNNER_SCRIPT,
        LIT_HEALTHY_EXECUTABLE,
        paths.healthy_executable,
        LIT_BASELINE_EXECUTABLE,
        paths.baseline_executable,
        LIT_RUNTIME_DIR,
        paths.runtime_directory,
        LIT_OUTPUT_DIR,
        paths.output_directory,
    ]
    if args.gpu_validation:
        command.append(LIT_GPU_VALIDATION)
    if args.no_logserver:
        command.append(LIT_NO_LOGSERVER)
    elif args.logserver_executable is not None:
        command += [LIT_LOGSERVER_EXECUTABLE, ROOT_LAUNCHER.resolve_path(REPO, args.logserver_executable)]
    command += list(args.runner_args)
    return command


def run_runner(args: argparse.Namespace, paths: BoundaryPaths) -> int:
    command = runner_command(args, paths)
    print(f"Hybrid-shadow boundary artifacts: {paths.output_directory}", flush=True)
    print("+ " + ROOT_LAUNCHER.format_command(command), flush=True)
    if args.dry_run:
        return 0
    return subprocess.run([str(part) for part in command], cwd=REPO).returncode


def make_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    ROOT_LAUNCHER.add_build_options(parser)
    parser.add_argument(LIT_HEALTHY_EXECUTABLE, type=Path, help="Override the healthy hardware transparent-shadow executable.")
    parser.add_argument(
        LIT_BASELINE_EXECUTABLE,
        dest="baseline_executable",
        type=Path,
        help="Override the executable reused for the opaque baseline arm.",
    )
    parser.add_argument(LIT_RUNTIME_DIR, type=Path, help="Override the cooked stress-scene runtime directory.")
    parser.add_argument(LIT_OUTPUT_DIR, type=Path, help="Directory for boundary timing, logs, captures, and reports.")
    parser.add_argument(LIT_LOGSERVER_EXECUTABLE, type=Path, help="Override the logserver executable.")
    parser.add_argument(LIT_NO_LOGSERVER, action=LIT_STORE_TRUE, help="Use standalone loader logs instead of logserver.")
    validation_group = parser.add_mutually_exclusive_group()
    validation_group.add_argument(
        LIT_GPU_VALIDATION,
        dest=LIT_GPU_VALIDATION_2,
        action=LIT_STORE_TRUE,
        help="Enable Vulkan validation for a correctness-oriented run.",
    )
    validation_group.add_argument(
        "--no-gpu-validation",
        dest=LIT_GPU_VALIDATION_2,
        action="store_false",
        help="Measure without --gpudbg layer overhead (the default).",
    )
    parser.set_defaults(gpu_validation=False)
    parser.add_argument(LIT_SELF_TEST, action=LIT_STORE_TRUE, help="Check rejection of configurations without diagnostic evidence, without Vulkan.")
    return parser


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    launcher_args, runner_args = ROOT_LAUNCHER.split_application_args(argv)
    args = make_parser().parse_args(launcher_args)
    args.runner_args = runner_args
    return args


def run_self_test() -> int:
    try:
        require_diagnostic_configuration("fin")
    except SystemExit as error:
        assert "requires --config dbg or --config opt" in str(error)
    else:
        raise AssertionError("fin must not claim the warning-based semantic verdict")
    print("hybrid-shadow boundary launcher self-test passed")
    return 0


def run(args: argparse.Namespace) -> int:
    environment = ROOT_LAUNCHER.build_environment(args)
    settings = ROOT_LAUNCHER.resolve_launch_settings(args, ROOT_LAUNCHER.DEFAULT_DOMAIN)
    require_diagnostic_configuration(settings.config)
    ROOT_LAUNCHER.maybe_configure(args, settings, REQUIRED_DEFINES, environment)
    settings = ROOT_LAUNCHER.refresh_launch_settings(settings, args.domain)
    build_benchmark_targets(args, settings, environment)
    return run_runner(args, resolve_paths(args, settings))


def main(argv: Sequence[str]) -> int:
    args = parse_args(argv)
    if args.self_test:
        return run_self_test()
    return run(args)


if __name__ == LIT_MAIN:
    raise SystemExit(main(sys.argv[1:]))
