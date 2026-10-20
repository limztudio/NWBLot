"""Shared UI capture argument admission and runtime path resolution."""
import argparse
import math
from pathlib import Path

LIT_DEFAULT = "default"

DEFAULT_TIMEOUT_SECONDS = 90.0


def parse_ui_smoke_args(argv, *, description, include_skin=False):
    parser = argparse.ArgumentParser(description=description)
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--working-directory", type=Path, required=True)
    parser.add_argument("--output-directory", type=Path, required=True)
    parser.add_argument("--logserver-executable", type=Path)
    if include_skin:
        parser.add_argument("--skin", choices=(LIT_DEFAULT, "alternate"), default=LIT_DEFAULT)
    parser.add_argument("--timeout", type=float, default=DEFAULT_TIMEOUT_SECONDS)
    parser.add_argument("--application-arg", action="append", default=[])
    parser.add_argument("--gpu-validation", action=argparse.BooleanOptionalAction, default=True, help="Request graphics validation; use --no-gpu-validation for a fin application.")
    args = parser.parse_args(argv)
    if not math.isfinite(args.timeout) or args.timeout <= 0.0:
        parser.error("--timeout must be finite and positive")
    args.executable = args.executable.resolve()
    args.working_directory = args.working_directory.resolve()
    args.output_directory = args.output_directory.resolve()
    args.window_title = "NWB UI Layer Smoke"
    args.no_logserver = False
    args.log_port = 0
    args.software_vulkan = "off"
    if args.gpu_validation:
        args.application_arg = ["--gpudbg", *args.application_arg]
    return args
