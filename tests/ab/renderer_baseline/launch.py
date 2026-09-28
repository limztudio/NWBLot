#!/usr/bin/env python3
"""Build and run one immutable current-renderer baseline capture.

Examples:

    python -m launcher renderer-baseline transparent-avboit
    python -m launcher renderer-baseline stress -- --reference-dir .cozter/out/ab-results/renderer-baseline/stress/<stamp>
"""

from __future__ import annotations

import argparse
import subprocess
import sys
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path
from types import SimpleNamespace
from typing import List, Sequence


REPO = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO))

import launcher as ROOT_LAUNCHER  # noqa: E402
from profiles import get_profile, profile_names  # noqa: E402

# Shared literals (no inline hardcodes below this block).
LIT_ON = "ON"
LIT_COZTER = ".cozter"
LIT_OUT = "out"
LIT_AB_RESULTS = "ab-results"
LIT_RENDERER_BASELINE = "renderer-baseline"
LIT_PROFILE = "--profile"
LIT_EXECUTABLE = "--executable"
LIT_RUNTIME_DIR = "--runtime-dir"
LIT_OUTPUT_DIR = "--output-dir"
LIT_GPU_VALIDATION = "--gpu-validation"
LIT_NO_LOGSERVER = "--no-logserver"
LIT_LOGSERVER_EXECUTABLE = "--logserver-executable"
LIT_STORE_TRUE = "store_true"
LIT_GPU_VALIDATION_2 = "gpu_validation"
LIT_SELF_TEST = "--self-test"
LIT_WINDOWS_CLANG_X64 = "windows-clang-x64"
LIT_TRANSPARENT_AVBOIT = "transparent-avboit"
LIT_REFERENCE_DIR = "--reference-dir"
LIT_REFERENCE = "reference"
LIT_MAIN = "__main__"


RUNNER_SCRIPT = Path("tests") / "ab" / "renderer_baseline" / "run.py"
REQUIRED_DEFINES = {
    "NWB_BUILD_LOADER": LIT_ON,
    "NWB_BUILD_LOGSERVER": LIT_ON,
    "NWB_BUILD_PIPELINE": LIT_ON,
    "NWB_BUILD_TESTS": LIT_ON,
}


@dataclass(frozen=True)
class BaselinePaths:
    executable: Path
    runtime_directory: Path
    output_directory: Path


def default_output_directory(root: Path, profile_name: str) -> Path:
    stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    return root / LIT_COZTER / LIT_OUT / LIT_AB_RESULTS / LIT_RENDERER_BASELINE / profile_name / stamp


def resolve_paths(args: argparse.Namespace, settings) -> BaselinePaths:
    profile = get_profile(args.profile)
    executable = ROOT_LAUNCHER.resolve_executable_path(
        settings, profile.target, args.executable, None, args.dry_run
    )
    runtime_directory = (
        ROOT_LAUNCHER.resolve_path(settings.root, args.runtime_dir)
        if args.runtime_dir is not None
        else settings.build_dir / profile.runtime_directory / settings.config
    )
    output_directory = (
        ROOT_LAUNCHER.resolve_path(settings.root, args.output_dir)
        if args.output_dir is not None
        else default_output_directory(settings.root, args.profile)
    )
    return BaselinePaths(executable, runtime_directory, output_directory)


def build_target(args: argparse.Namespace, settings, environment) -> None:
    profile = get_profile(args.profile)
    ROOT_LAUNCHER.build_targets(args, settings, (profile.target,), environment)


def runner_command(args: argparse.Namespace, paths: BaselinePaths) -> List[object]:
    command: List[object] = [
        sys.executable,
        REPO / RUNNER_SCRIPT,
        LIT_PROFILE,
        args.profile,
        LIT_EXECUTABLE,
        paths.executable,
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


def make_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    ROOT_LAUNCHER.add_build_options(parser)
    parser.add_argument("profile", nargs="?", choices=profile_names(), help="Pinned renderer scene to capture.")
    parser.add_argument(LIT_EXECUTABLE, type=Path, help="Override the selected smoke executable.")
    parser.add_argument(LIT_RUNTIME_DIR, type=Path, help="Override the selected cooked smoke runtime directory.")
    parser.add_argument(LIT_OUTPUT_DIR, type=Path, help="Directory for the immutable capture and manifest.")
    parser.add_argument(LIT_LOGSERVER_EXECUTABLE, type=Path, help="Override the logserver executable.")
    parser.add_argument(LIT_NO_LOGSERVER, action=LIT_STORE_TRUE, help="Use standalone loader logs instead of logserver.")
    validation_group = parser.add_mutually_exclusive_group()
    validation_group.add_argument(
        LIT_GPU_VALIDATION, dest=LIT_GPU_VALIDATION_2, action=LIT_STORE_TRUE, help="Enable Vulkan validation for this capture."
    )
    validation_group.add_argument(
        "--no-gpu-validation", dest=LIT_GPU_VALIDATION_2, action="store_false", help="Do not pass --gpudbg."
    )
    parser.set_defaults(gpu_validation=True)
    parser.add_argument(LIT_SELF_TEST, action=LIT_STORE_TRUE, help="Validate launcher command composition without building Vulkan targets.")
    return parser


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    launcher_args, runner_args = ROOT_LAUNCHER.split_application_args(argv)
    args = make_parser().parse_args(launcher_args)
    args.runner_args = runner_args
    if not args.self_test and args.profile is None:
        raise SystemExit("renderer-baseline requires a profile; choose one of: " + ", ".join(profile_names()))
    return args


def run_self_test() -> int:
    root = Path("/nwb")
    settings = ROOT_LAUNCHER.LaunchSettings(
        root=root,
        platform_name="windows",
        arch="x64",
        domain="full",
        config="dbg",
        configure_preset=LIT_WINDOWS_CLANG_X64,
        build_dir=root / "__cmake" / "build" / LIT_WINDOWS_CLANG_X64,
        cmake=("cmake",),
    )
    args = SimpleNamespace(
        profile=LIT_TRANSPARENT_AVBOIT,
        executable=None,
        runtime_dir=None,
        output_dir=root / LIT_COZTER / LIT_OUT / LIT_AB_RESULTS / LIT_RENDERER_BASELINE / LIT_TRANSPARENT_AVBOIT / "self-test",
        dry_run=True,
        gpu_validation=True,
        no_logserver=False,
        logserver_executable=None,
        runner_args=[LIT_REFERENCE_DIR, LIT_REFERENCE],
    )
    paths = resolve_paths(args, settings)
    command = [str(item) for item in runner_command(args, paths)]
    assert paths.runtime_directory.as_posix().endswith("Testing/smoke_runtime/dbg")
    assert command[command.index(LIT_EXECUTABLE) + 1].endswith("transparent_multi_smoke.exe")
    assert command[command.index(LIT_PROFILE) + 1] == LIT_TRANSPARENT_AVBOIT
    assert LIT_GPU_VALIDATION in command
    assert command[-2:] == [LIT_REFERENCE_DIR, LIT_REFERENCE]
    assert parse_args([LIT_SELF_TEST]).gpu_validation is True
    print("renderer-baseline launcher self-test passed")
    return 0


def run(args: argparse.Namespace) -> int:
    environment = ROOT_LAUNCHER.build_environment(args)
    settings = ROOT_LAUNCHER.resolve_launch_settings(args, ROOT_LAUNCHER.DEFAULT_DOMAIN)
    ROOT_LAUNCHER.maybe_configure(args, settings, REQUIRED_DEFINES, environment)
    settings = ROOT_LAUNCHER.refresh_launch_settings(settings, args.domain)
    build_target(args, settings, environment)
    paths = resolve_paths(args, settings)
    command = runner_command(args, paths)
    print(f"Renderer baseline artifacts: {paths.output_directory}", flush=True)
    print("+ " + ROOT_LAUNCHER.format_command(command), flush=True)
    if args.dry_run:
        return 0
    return subprocess.run([str(part) for part in command], cwd=REPO).returncode


def main(argv: Sequence[str]) -> int:
    args = parse_args(argv)
    if args.self_test:
        return run_self_test()
    return run(args)


if __name__ == LIT_MAIN:
    raise SystemExit(main(sys.argv[1:]))
