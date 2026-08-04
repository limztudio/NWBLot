#!/usr/bin/env python3
"""Build and run the frame-lagged async-lighting validation in one command.

From the repository root:

    python launcher.py frame-lagged-async-lighting

The command configures the required smoke target, builds its cooked runtime assets, and
then runs the lifecycle validator. Pass runner-specific options after ``--``.
"""

from __future__ import annotations

import argparse
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from types import SimpleNamespace
from typing import List, Sequence, Tuple


REPO = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO))

import launcher as ROOT_LAUNCHER  # noqa: E402


RUNNER_SCRIPT = Path("tests") / "ab" / "frame_lagged_async_lighting" / "run.py"
SMOKE_TARGET = "nwb_frame_lagged_async_lighting_smoke"
RUNTIME_DIRECTORY = Path("Testing") / "smoke_runtime"
REQUIRED_DEFINES = {
    "NWB_BUILD_LOADER": "ON",
    "NWB_BUILD_LOGSERVER": "ON",
    "NWB_BUILD_RESOURCE_COOKER": "ON",
    "NWB_BUILD_TESTS": "ON",
}


@dataclass(frozen=True)
class FrameLaggedPaths:
    executable: Path
    runtime_directory: Path


def resolve_path(root: Path, path: Path) -> Path:
    return path if path.is_absolute() else root / path


def resolve_paths(args: argparse.Namespace, settings) -> FrameLaggedPaths:
    executable = ROOT_LAUNCHER.resolve_executable_path(
        settings,
        SMOKE_TARGET,
        args.executable,
        None,
        args.dry_run,
    )
    runtime_directory = (
        resolve_path(settings.root, args.runtime_dir)
        if args.runtime_dir is not None
        else settings.build_dir / RUNTIME_DIRECTORY / settings.config
    )
    return FrameLaggedPaths(executable, runtime_directory)


def runner_command(args: argparse.Namespace, paths: FrameLaggedPaths) -> List[object]:
    command: List[object] = [
        sys.executable,
        REPO / RUNNER_SCRIPT,
        "--executable",
        paths.executable,
        "--runtime-dir",
        paths.runtime_directory,
    ]
    if args.gpu_validation:
        command.append("--gpu-validation")
    if args.no_logserver:
        command.append("--no-logserver")
    elif args.logserver_executable is not None:
        command += ["--logserver-executable", resolve_path(REPO, args.logserver_executable)]
    command += list(args.runner_args)
    return command


def run_runner(args: argparse.Namespace, paths: FrameLaggedPaths) -> int:
    command = runner_command(args, paths)
    print("+ " + ROOT_LAUNCHER.format_command(command), flush=True)
    if args.dry_run:
        return 0
    return subprocess.run([str(part) for part in command], cwd=REPO).returncode


def split_runner_args(argv: Sequence[str]) -> Tuple[List[str], List[str]]:
    values = list(argv)
    if "--" not in values:
        return values, []
    separator = values.index("--")
    return values[:separator], values[separator + 1 :]


def make_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    ROOT_LAUNCHER.add_build_options(parser)
    parser.add_argument("--executable", type=Path, help="Override the frame-lagged smoke executable.")
    parser.add_argument("--runtime-dir", type=Path, help="Override the cooked smoke runtime directory.")
    parser.add_argument("--logserver-executable", type=Path, help="Override the logserver executable.")
    parser.add_argument("--no-logserver", action="store_true", help="Use standalone loader logs instead of logserver.")
    validation_group = parser.add_mutually_exclusive_group()
    validation_group.add_argument(
        "--gpu-validation",
        dest="gpu_validation",
        action="store_true",
        help="Enable Vulkan GPU validation (the default).",
    )
    validation_group.add_argument(
        "--no-gpu-validation",
        dest="gpu_validation",
        action="store_false",
        help="Do not pass --gpudbg to the smoke process.",
    )
    parser.set_defaults(gpu_validation=True)
    parser.add_argument("--self-test", action="store_true", help="Validate launcher command composition without Vulkan.")
    return parser


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    launcher_args, runner_args = split_runner_args(argv)
    args = make_parser().parse_args(launcher_args)
    args.runner_args = runner_args
    return args


def run_self_test() -> int:
    root = Path("/nwb")
    settings = ROOT_LAUNCHER.LaunchSettings(
        root=root,
        platform_name="windows",
        arch="x64",
        domain="full",
        config="dbg",
        configure_preset="windows-clang-x64",
        build_dir=root / "__cmake" / "build" / "windows-clang-x64",
        cmake=("cmake",),
    )
    args = SimpleNamespace(
        executable=None,
        runtime_dir=None,
        dry_run=True,
        gpu_validation=True,
        no_logserver=False,
        logserver_executable=None,
        runner_args=["--transition-timeout", "25"],
    )
    paths = resolve_paths(args, settings)
    command = [str(item) for item in runner_command(args, paths)]
    assert str(paths.runtime_directory).endswith("Testing/smoke_runtime/dbg")
    assert command[command.index("--executable") + 1].endswith("frame_lagged_async_lighting_smoke.exe")
    assert "--gpu-validation" in command
    assert command[-2:] == ["--transition-timeout", "25"]
    print("frame-lagged async-lighting launcher self-test passed")
    return 0


def run(args: argparse.Namespace) -> int:
    environment = ROOT_LAUNCHER.build_environment(args)
    settings = ROOT_LAUNCHER.resolve_launch_settings(args, ROOT_LAUNCHER.DEFAULT_DOMAIN)
    ROOT_LAUNCHER.maybe_configure(args, settings, REQUIRED_DEFINES, environment)
    settings = ROOT_LAUNCHER.refresh_launch_settings(settings, args.domain)
    ROOT_LAUNCHER.build_target(args, settings, SMOKE_TARGET, environment)
    return run_runner(args, resolve_paths(args, settings))


def main(argv: Sequence[str]) -> int:
    args = parse_args(argv)
    if args.self_test:
        return run_self_test()
    return run(args)


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
