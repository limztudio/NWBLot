#!/usr/bin/env python3
"""Build and run the Phase 11 command-IR overhead profile.

The profile measures the stable native recording path alongside command-IR copy-buffer
capture, validation-reader decode, preflight, Core::CommandList replay, and experimental
direct-Vulkan replay in one process. It writes a
timestamped evidence bundle and is intentionally a CPU-overhead probe rather than a
GPU-performance benchmark.
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


RUNNER_SCRIPT = Path("tests") / "ab" / "command_ir" / "run.py"
PROFILE_TARGET = "nwb_command_ir_profile"
MAX_RECORDS = 65536
MAX_SAMPLES = 64
REQUIRED_DEFINES = {
    "NWB_BUILD_TESTS": "ON",
}


@dataclass(frozen=True)
class ProfilePaths:
    executable: Path
    output_directory: Path


def resolve_path(root: Path, path: Path) -> Path:
    return path if path.is_absolute() else root / path


def default_output_directory(root: Path) -> Path:
    stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    return root / ".cozter" / "out" / "ab-results" / "command-ir" / stamp


def resolve_paths(args: argparse.Namespace, settings) -> ProfilePaths:
    executable = ROOT_LAUNCHER.resolve_executable_path(
        settings,
        PROFILE_TARGET,
        args.executable,
        None,
        args.dry_run,
    )
    output_directory = (
        resolve_path(settings.root, args.output_dir)
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
        "--records",
        args.records,
        "--warmup",
        args.warmup,
        "--samples",
        args.samples,
        "--gpu-validation" if args.gpu_validation else "--no-gpu-validation",
    ]
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
    parser.add_argument("--executable", type=Path, help="Override the command-IR profile executable.")
    parser.add_argument("--output-dir", type=Path, help="Directory for logs and the profile report.")
    parser.add_argument(
        "--adapter-index",
        type=int,
        default=0,
        help="Pinned Vulkan adapter enumeration index (default: 0).",
    )
    parser.add_argument("--records", type=int, default=4096, help="Copy-buffer records per measured sample (default: 4096).")
    parser.add_argument("--warmup", type=int, default=3, help="Unmeasured priming samples (default: 3).")
    parser.add_argument("--samples", type=int, default=11, help="Measured samples per stage (default: 11).")
    validation_group = parser.add_mutually_exclusive_group()
    validation_group.add_argument(
        "--gpu-validation",
        dest="gpu_validation",
        action="store_true",
        help="Enable Vulkan validation in the profile process (the default).",
    )
    validation_group.add_argument(
        "--no-gpu-validation",
        dest="gpu_validation",
        action="store_false",
        help="Run without Vulkan validation when the target cannot expose the validation layer.",
    )
    parser.set_defaults(gpu_validation=True)
    parser.add_argument("--self-test", action="store_true", help="Validate launcher command composition without Vulkan.")
    return parser


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    launcher_args, runner_args = split_runner_args(argv)
    args = make_parser().parse_args(launcher_args)
    if args.adapter_index < 0:
        raise SystemExit("--adapter-index must be a non-negative Vulkan enumeration index")
    if args.records <= 0 or args.records > MAX_RECORDS:
        raise SystemExit(f"--records must be between 1 and {MAX_RECORDS}")
    if args.warmup <= 0:
        raise SystemExit("--warmup must be positive")
    if args.samples <= 0 or args.samples > MAX_SAMPLES:
        raise SystemExit(f"--samples must be between 1 and {MAX_SAMPLES}")
    args.runner_args = runner_args
    return args


def run_self_test() -> int:
    assert parse_args(["--self-test"]).records == 4096
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
        records=8192,
        warmup=2,
        samples=7,
        runner_args=["--samples", "9"],
    )
    paths = resolve_paths(args, settings)
    command = [str(item) for item in runner_command(args, paths)]
    assert command[command.index("--executable") + 1].endswith("command_ir_profile.exe")
    assert "/.cozter/out/ab-results/command-ir/" in str(paths.output_directory)
    assert command[command.index("--adapter-index") + 1] == "1"
    assert command[command.index("--records") + 1] == "8192"
    assert command[command.index("--warmup") + 1] == "2"
    assert command[command.index("--samples") + 1] == "7"
    assert "--gpu-validation" in command
    assert command[-2:] == ["--samples", "9"]
    print("command-IR launcher self-test passed")
    return 0


def run(args: argparse.Namespace) -> int:
    environment = ROOT_LAUNCHER.build_environment(args)
    settings = ROOT_LAUNCHER.resolve_launch_settings(args, ROOT_LAUNCHER.DEFAULT_DOMAIN)
    ROOT_LAUNCHER.maybe_configure(args, settings, REQUIRED_DEFINES, environment)
    settings = ROOT_LAUNCHER.refresh_launch_settings(settings, args.domain)
    ROOT_LAUNCHER.build_target(args, settings, PROFILE_TARGET, environment)

    paths = resolve_paths(args, settings)
    command = runner_command(args, paths)
    print(f"Command-IR profiling artifacts: {paths.output_directory}", flush=True)
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
