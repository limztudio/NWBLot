#!/usr/bin/env python3
"""Build and run the async-shadow M4 target-hardware validation in one command.

From the repository root, the usual invocation is simply:

    python launcher.py async-shadow-m4

The launcher configures the M4 dependencies, builds the paired synchronous and asynchronous
benchmarks (including their cooked runtime assets), enables Vulkan GPU validation, and writes a
timestamped report beneath ``.cozter/out/ab-results/async-shadow-m4``. Pass additional ``run.py`` options after ``--``; for
example, ``python launcher.py async-shadow-m4 -- --measure-seconds 30``.
"""

from __future__ import annotations

import argparse
import subprocess
import sys
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path
from types import SimpleNamespace
from typing import List, Sequence, Tuple


REPO = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO))

import launcher as ROOT_LAUNCHER  # noqa: E402


RUNNER_SCRIPT = Path("tests") / "ab" / "async_shadow_m4" / "run.py"
SYNC_TARGET = "nwb_async_shadow_m4_sync_benchmark"
ASYNC_TARGET = "nwb_async_shadow_m4_async_benchmark"
RUNTIME_DIRECTORY = Path("Testing") / "skinning_culling_benchmark_runtime"
REQUIRED_DEFINES = {
    "NWB_BUILD_LOADER": "ON",
    "NWB_BUILD_LOGSERVER": "ON",
    "NWB_BUILD_RESOURCE_COOKER": "ON",
    "NWB_BUILD_TESTS": "ON",
}


@dataclass(frozen=True)
class M4Paths:
    sync_executable: Path
    async_executable: Path
    runtime_directory: Path
    output_directory: Path


def resolve_path(root: Path, path: Path) -> Path:
    return path if path.is_absolute() else root / path


def default_output_directory(root: Path) -> Path:
    stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    return root / ".cozter" / "out" / "ab-results" / "async-shadow-m4" / stamp


def resolve_paths(args: argparse.Namespace, settings) -> M4Paths:
    sync_executable = ROOT_LAUNCHER.resolve_executable_path(
        settings,
        SYNC_TARGET,
        args.sync_executable,
        None,
        args.dry_run,
    )
    async_executable = ROOT_LAUNCHER.resolve_executable_path(
        settings,
        ASYNC_TARGET,
        args.async_executable,
        None,
        args.dry_run,
    )
    runtime_directory = (
        resolve_path(settings.root, args.runtime_dir)
        if args.runtime_dir is not None
        else settings.build_dir / RUNTIME_DIRECTORY / settings.config
    )
    output_directory = (
        resolve_path(settings.root, args.output_dir)
        if args.output_dir is not None
        else default_output_directory(settings.root)
    )
    return M4Paths(sync_executable, async_executable, runtime_directory, output_directory)


def build_benchmark_targets(args: argparse.Namespace, settings, environment) -> None:
    if args.skip_build:
        return

    command: List[object] = list(settings.cmake) + [
        "--build",
        str(settings.build_dir),
        "--target",
        SYNC_TARGET,
        ASYNC_TARGET,
        "--config",
        settings.config,
    ]
    if args.jobs:
        command += ["--parallel", str(args.jobs)]
    ROOT_LAUNCHER.run_checked(command, settings.root, environment, args.dry_run)


def runner_command(args: argparse.Namespace, paths: M4Paths) -> List[object]:
    command: List[object] = [
        sys.executable,
        REPO / RUNNER_SCRIPT,
        "--sync-executable",
        paths.sync_executable,
        "--async-executable",
        paths.async_executable,
        "--runtime-dir",
        paths.runtime_directory,
        "--output-dir",
        paths.output_directory,
    ]
    if args.gpu_validation:
        command.append("--gpu-validation")
    if args.no_logserver:
        command.append("--no-logserver")
    elif args.logserver_executable is not None:
        command += ["--logserver-executable", resolve_path(REPO, args.logserver_executable)]
    command += list(args.runner_args)
    return command


def run_runner(args: argparse.Namespace, paths: M4Paths) -> int:
    command = runner_command(args, paths)
    print(f"M4 validation artifacts: {paths.output_directory}", flush=True)
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
    parser.add_argument("--sync-executable", type=Path, help="Override the synchronous benchmark executable.")
    parser.add_argument("--async-executable", type=Path, help="Override the asynchronous benchmark executable.")
    parser.add_argument("--runtime-dir", type=Path, help="Override the cooked M4 runtime directory.")
    parser.add_argument("--output-dir", type=Path, help="Directory for M4 logs, captures, and reports.")
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
        help="Do not pass --gpudbg to the paired benchmark processes.",
    )
    parser.set_defaults(gpu_validation=True)
    parser.add_argument("--self-test", action="store_true", help="Validate launcher command composition without building or running Vulkan.")
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
        sync_executable=None,
        async_executable=None,
        runtime_dir=None,
        output_dir=root / ".cozter" / "out" / "ab-results" / "async-shadow-m4" / "self-test",
        dry_run=True,
        gpu_validation=True,
        no_logserver=False,
        logserver_executable=None,
        runner_args=["--measure-seconds", "30"],
    )
    paths = resolve_paths(args, settings)
    command = [str(item) for item in runner_command(args, paths)]
    assert str(paths.runtime_directory).endswith("Testing/skinning_culling_benchmark_runtime/dbg")
    assert command[command.index("--sync-executable") + 1].endswith("async_shadow_m4_sync_benchmark.exe")
    assert command[command.index("--async-executable") + 1].endswith("async_shadow_m4_async_benchmark.exe")
    assert "--gpu-validation" in command
    assert command[-2:] == ["--measure-seconds", "30"]
    print("async-shadow M4 launcher self-test passed")
    return 0


def run(args: argparse.Namespace) -> int:
    environment = ROOT_LAUNCHER.build_environment(args)
    settings = ROOT_LAUNCHER.resolve_launch_settings(args, ROOT_LAUNCHER.DEFAULT_DOMAIN)
    ROOT_LAUNCHER.maybe_configure(args, settings, REQUIRED_DEFINES, environment)
    settings = ROOT_LAUNCHER.refresh_launch_settings(settings, args.domain)
    build_benchmark_targets(args, settings, environment)
    return run_runner(args, resolve_paths(args, settings))


def main(argv: Sequence[str]) -> int:
    args = parse_args(argv)
    if args.self_test:
        return run_self_test()
    return run(args)


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
