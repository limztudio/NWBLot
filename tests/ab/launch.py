#!/usr/bin/env python3
"""Dispatch an A/B test launcher to one of its runnable validation workflows."""

from __future__ import annotations

import argparse
import subprocess
import sys
from datetime import datetime
from pathlib import Path
from typing import Callable, List, Optional, Sequence


REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO))

import launcher as ROOT_LAUNCHER  # noqa: E402


TESTS_DIR_NAME = "tests"
AB_SUBDIR_NAME = "ab"
MAIN_ENTRY = "__main__"

LIT_ON = "ON"
LIT_COZTER = ".cozter"
LIT_OUT = "out"
LIT_AB_RESULTS = "ab-results"
LIT_RUNTIME_DIR = "--runtime-dir"
LIT_OUTPUT_DIR = "--output-dir"
LIT_GPU_VALIDATION = "--gpu-validation"
LIT_NO_GPU_VALIDATION = "--no-gpu-validation"
LIT_NO_LOGSERVER = "--no-logserver"
LIT_LOGSERVER_EXECUTABLE = "--logserver-executable"
LIT_STORE_TRUE = "store_true"
LIT_STORE_FALSE = "store_false"
LIT_GPU_VALIDATION_DEST = "gpu_validation"

REQUIRED_AB_DEFINES = {
    "NWB_BUILD_LOADER": LIT_ON,
    "NWB_BUILD_LOGSERVER": LIT_ON,
    "NWB_BUILD_PIPELINE": LIT_ON,
    "NWB_BUILD_TESTS": LIT_ON,
}


def default_ab_output_directory(root: Path, benchmark_name: str) -> Path:
    stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    return root / LIT_COZTER / LIT_OUT / LIT_AB_RESULTS / benchmark_name / stamp


def append_ab_runner_args(command: List[object], args: argparse.Namespace, repo: Path = REPO) -> List[object]:
    if args.gpu_validation:
        command.append(LIT_GPU_VALIDATION)
    if args.no_logserver:
        command.append(LIT_NO_LOGSERVER)
    elif args.logserver_executable is not None:
        command += [LIT_LOGSERVER_EXECUTABLE, ROOT_LAUNCHER.resolve_path(repo, args.logserver_executable)]
    command += list(args.runner_args)
    return command


def run_ab_runner(
    command: Sequence[object],
    dry_run: bool,
    repo: Path = REPO,
    artifact_message: Optional[str] = None,
) -> int:
    if artifact_message is not None:
        print(artifact_message, flush=True)
    print("+ " + ROOT_LAUNCHER.format_command(command), flush=True)
    if dry_run:
        return 0
    return subprocess.run([str(part) for part in command], cwd=repo).returncode


def add_ab_runner_options(
    parser: argparse.ArgumentParser,
    *,
    default_gpu_validation: bool,
    gpu_validation_help: str,
    no_gpu_validation_help: str,
) -> None:
    parser.add_argument(LIT_LOGSERVER_EXECUTABLE, type=Path, help="Override the logserver executable.")
    parser.add_argument(LIT_NO_LOGSERVER, action=LIT_STORE_TRUE, help="Use standalone loader logs instead of logserver.")
    validation_group = parser.add_mutually_exclusive_group()
    validation_group.add_argument(
        LIT_GPU_VALIDATION,
        dest=LIT_GPU_VALIDATION_DEST,
        action=LIT_STORE_TRUE,
        help=gpu_validation_help,
    )
    validation_group.add_argument(
        LIT_NO_GPU_VALIDATION,
        dest=LIT_GPU_VALIDATION_DEST,
        action=LIT_STORE_FALSE,
        help=no_gpu_validation_help,
    )
    parser.set_defaults(gpu_validation=default_gpu_validation)


def parse_ab_launcher_args(
    make_parser_fn: Callable[[], argparse.ArgumentParser],
    argv: Sequence[str],
) -> argparse.Namespace:
    launcher_args, runner_args = ROOT_LAUNCHER.split_application_args(argv)
    args = make_parser_fn().parse_args(launcher_args)
    args.runner_args = runner_args
    return args


def main(argv: Sequence[str]) -> int:
    return ROOT_LAUNCHER.run_directory_launcher(Path(TESTS_DIR_NAME) / AB_SUBDIR_NAME, argv)


if __name__ == MAIN_ENTRY:
    raise SystemExit(main(sys.argv[1:]))
