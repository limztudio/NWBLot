#!/usr/bin/env python3
"""Build and run the Phase 10 Transfer upload profiling harness.

The workflow compares an explicit Graphics setup-upload baseline with the automatic
Transfer-preferred route.  It is target-hardware-only: hosts without a distinct
Transfer family return 77 after preserving a topology/report artifact.
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


RUNNER_SCRIPT = Path("tests") / "ab" / "transfer_queue" / "run.py"
PROFILE_TARGET = "nwb_transfer_upload_profile"
REQUIRED_DEFINES = {
    "NWB_BUILD_TESTS": "ON",
}


@dataclass(frozen=True)
class ProfilePaths:
    executable: Path
    output_directory: Path


def default_output_directory(root: Path) -> Path:
    stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    return root / ".cozter" / "out" / "ab-results" / "transfer-queue" / stamp


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
        "--executable",
        paths.executable,
        "--output-dir",
        paths.output_directory,
        "--adapter-index",
        args.adapter_index,
        "--in-flight",
        args.in_flight,
    ]
    if args.gpu_validation:
        command.append("--gpu-validation")
    if args.external_profiler_report is not None:
        command += ["--external-profiler-report", ROOT_LAUNCHER.resolve_path(REPO, args.external_profiler_report)]
    command += list(args.runner_args)
    return command


def make_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    ROOT_LAUNCHER.add_build_options(parser)
    parser.add_argument("--executable", type=Path, help="Override the transfer-upload profile executable.")
    parser.add_argument("--output-dir", type=Path, help="Directory for logs and the profiling report.")
    parser.add_argument(
        "--adapter-index",
        type=int,
        default=0,
        help="Pinned Vulkan adapter enumeration index for both A/B processes (default: 0).",
    )
    parser.add_argument("--in-flight", type=int, default=2, help="Maximum concurrently retained upload windows per arm.")
    parser.add_argument(
        "--external-profiler-report",
        type=Path,
        help="Optional external GPU-profiler report/trace to copy into the artifact bundle.",
    )
    validation_group = parser.add_mutually_exclusive_group()
    validation_group.add_argument(
        "--gpu-validation",
        dest="gpu_validation",
        action="store_true",
        help="Enable Vulkan validation in both A/B arms (the default).",
    )
    validation_group.add_argument(
        "--no-gpu-validation",
        dest="gpu_validation",
        action="store_false",
        help="Run without Vulkan validation when a target cannot expose the validation layer.",
    )
    parser.set_defaults(gpu_validation=True)
    parser.add_argument("--self-test", action="store_true", help="Validate launcher command composition without Vulkan.")
    return parser


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    launcher_args, runner_args = ROOT_LAUNCHER.split_application_args(argv)
    args = make_parser().parse_args(launcher_args)
    if args.adapter_index < 0:
        raise SystemExit("--adapter-index must be a non-negative Vulkan enumeration index for paired A/B evidence")
    if args.in_flight <= 0:
        raise SystemExit("--in-flight must be positive")
    args.runner_args = runner_args
    return args


def run_self_test() -> int:
    assert parse_args(["--self-test"]).adapter_index == 0
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
        output_dir=None,
        dry_run=True,
        gpu_validation=True,
        adapter_index=1,
        in_flight=3,
        external_profiler_report=Path("capture.rgp"),
        runner_args=["--upload-mib", "32"],
    )
    paths = resolve_paths(args, settings)
    command = [str(item) for item in runner_command(args, paths)]
    assert command[command.index("--executable") + 1].endswith("transfer_upload_profile.exe")
    assert "/.cozter/out/ab-results/transfer-queue/" in paths.output_directory.as_posix()
    assert "--gpu-validation" in command
    assert command[command.index("--adapter-index") + 1] == "1"
    assert command[command.index("--in-flight") + 1] == "3"
    assert command[command.index("--external-profiler-report") + 1].endswith("capture.rgp")
    assert command[-2:] == ["--upload-mib", "32"]
    print("transfer-queue launcher self-test passed")
    return 0


def run(args: argparse.Namespace) -> int:
    environment = ROOT_LAUNCHER.build_environment(args)
    settings = ROOT_LAUNCHER.resolve_launch_settings(args, ROOT_LAUNCHER.DEFAULT_DOMAIN)
    ROOT_LAUNCHER.maybe_configure(args, settings, REQUIRED_DEFINES, environment)
    settings = ROOT_LAUNCHER.refresh_launch_settings(settings, args.domain)
    ROOT_LAUNCHER.build_target(args, settings, PROFILE_TARGET, environment)

    paths = resolve_paths(args, settings)
    command = runner_command(args, paths)
    print(f"Transfer-queue profiling artifacts: {paths.output_directory}", flush=True)
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
