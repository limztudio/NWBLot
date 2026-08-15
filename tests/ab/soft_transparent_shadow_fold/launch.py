#!/usr/bin/env python3
"""Build and run the target-scene soft-transparent shadow-fold A/B workflow."""

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


RUNNER_SCRIPT = Path("tests") / "ab" / "soft_transparent_shadow_fold" / "run.py"
GRAPH_TARGET = "nwb_soft_transparent_shadow_fold_graph_benchmark"
MONOLITHIC_TARGET = "nwb_soft_transparent_shadow_fold_monolithic_benchmark"
RUNTIME_DIRECTORY = Path("Testing") / "skinning_culling_benchmark_runtime"
REQUIRED_DEFINES = {
    "NWB_BUILD_LOADER": "ON",
    "NWB_BUILD_LOGSERVER": "ON",
    "NWB_BUILD_RESOURCE_COOKER": "ON",
    "NWB_BUILD_TESTS": "ON",
}


@dataclass(frozen=True)
class BenchmarkPaths:
    graph_executable: Path
    monolithic_executable: Path
    runtime_directory: Path
    output_directory: Path


def resolve_path(root: Path, path: Path) -> Path:
    return path if path.is_absolute() else root / path


def default_output_directory(root: Path) -> Path:
    stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    return root / ".cozter" / "out" / "ab-results" / "soft-transparent-shadow-fold" / stamp


def resolve_paths(args: argparse.Namespace, settings) -> BenchmarkPaths:
    graph_executable = ROOT_LAUNCHER.resolve_executable_path(
        settings, GRAPH_TARGET, args.graph_executable, None, args.dry_run
    )
    monolithic_executable = ROOT_LAUNCHER.resolve_executable_path(
        settings, MONOLITHIC_TARGET, args.monolithic_executable, None, args.dry_run
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
    return BenchmarkPaths(graph_executable, monolithic_executable, runtime_directory, output_directory)


def build_benchmark_targets(args: argparse.Namespace, settings, environment) -> None:
    if args.skip_build:
        return
    command: List[object] = list(settings.cmake) + [
        "--build", str(settings.build_dir), "--target", GRAPH_TARGET, MONOLITHIC_TARGET, "--config", settings.config,
    ]
    if args.jobs:
        command += ["--parallel", str(args.jobs)]
    ROOT_LAUNCHER.run_checked(command, settings.root, environment, args.dry_run)


def runner_command(args: argparse.Namespace, paths: BenchmarkPaths) -> List[object]:
    command: List[object] = [
        sys.executable,
        REPO / RUNNER_SCRIPT,
        "--graph-executable", paths.graph_executable,
        "--monolithic-executable", paths.monolithic_executable,
        "--runtime-dir", paths.runtime_directory,
        "--output-dir", paths.output_directory,
    ]
    if args.gpu_validation:
        command.append("--gpu-validation")
    if args.no_logserver:
        command.append("--no-logserver")
    elif args.logserver_executable is not None:
        command += ["--logserver-executable", resolve_path(REPO, args.logserver_executable)]
    command += list(args.runner_args)
    return command


def split_runner_args(argv: Sequence[str]) -> Tuple[List[str], List[str]]:
    values = list(argv)
    if "--" not in values:
        return values, []
    separator = values.index("--")
    return values[:separator], values[separator + 1 :]


def make_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    ROOT_LAUNCHER.add_build_options(parser)
    parser.add_argument("--graph-executable", type=Path, help="Override the graph-fold benchmark executable.")
    parser.add_argument("--monolithic-executable", type=Path, help="Override the monolithic benchmark executable.")
    parser.add_argument("--runtime-dir", type=Path, help="Override the cooked stress-scene runtime directory.")
    parser.add_argument("--output-dir", type=Path, help="Directory for timing files, logs, captures, and reports.")
    parser.add_argument("--logserver-executable", type=Path, help="Override the logserver executable.")
    parser.add_argument("--no-logserver", action="store_true", help="Use standalone loader logs instead of logserver.")
    validation_group = parser.add_mutually_exclusive_group()
    validation_group.add_argument("--gpu-validation", dest="gpu_validation", action="store_true")
    validation_group.add_argument("--no-gpu-validation", dest="gpu_validation", action="store_false")
    parser.set_defaults(gpu_validation=False)
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
        root=root, platform_name="windows", arch="x64", domain="full", config="dbg",
        configure_preset="windows-clang-x64", build_dir=root / "__cmake" / "build" / "windows-clang-x64", cmake=("cmake",),
    )
    args = SimpleNamespace(
        graph_executable=None, monolithic_executable=None, runtime_dir=None,
        output_dir=root / ".cozter" / "out" / "ab-results" / "soft-transparent-shadow-fold" / "self-test",
        dry_run=True, gpu_validation=True, no_logserver=False, logserver_executable=None,
        runner_args=["--measure-seconds", "30"],
    )
    paths = resolve_paths(args, settings)
    command = [str(value) for value in runner_command(args, paths)]
    assert command[command.index("--graph-executable") + 1].endswith("soft_transparent_shadow_fold_graph_benchmark.exe")
    assert command[command.index("--monolithic-executable") + 1].endswith("soft_transparent_shadow_fold_monolithic_benchmark.exe")
    assert "--gpu-validation" in command
    assert command[-2:] == ["--measure-seconds", "30"]
    assert parse_args(["--self-test"]).gpu_validation is False
    print("soft-transparent shadow-fold launcher self-test passed")
    return 0


def run(args: argparse.Namespace) -> int:
    environment = ROOT_LAUNCHER.build_environment(args)
    settings = ROOT_LAUNCHER.resolve_launch_settings(args, ROOT_LAUNCHER.DEFAULT_DOMAIN)
    ROOT_LAUNCHER.maybe_configure(args, settings, REQUIRED_DEFINES, environment)
    settings = ROOT_LAUNCHER.refresh_launch_settings(settings, args.domain)
    build_benchmark_targets(args, settings, environment)
    paths = resolve_paths(args, settings)
    command = runner_command(args, paths)
    print(f"Soft-transparent shadow-fold artifacts: {paths.output_directory}", flush=True)
    print("+ " + ROOT_LAUNCHER.format_command(command), flush=True)
    if args.dry_run:
        return 0
    return subprocess.run([str(part) for part in command], cwd=REPO).returncode


def main(argv: Sequence[str]) -> int:
    args = parse_args(argv)
    if args.self_test:
        return run_self_test()
    return run(args)


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
