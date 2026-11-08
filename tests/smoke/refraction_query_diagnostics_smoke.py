#!/usr/bin/env python3
"""Validate completed hardware refraction work and diagnostics optical parity."""

import argparse
import json
import math
import os
from pathlib import Path
import re
import subprocess
import sys

from refraction_density_benchmark import Cell, DEFAULT_EXTENTS, EXTENTS
from refraction_density_evidence import RefractionDensityEvidence
from refraction_density_capture_smoke import SANITIZED_ENVIRONMENT, launch_density_capture, selection
from refraction_density_image_evidence import RefractionDensityImageEvidence
from refraction_query_evidence import RefractionQueryEvidence
from window_capture_image import read_bmp_24_rows
from window_capture_smoke import SKIP_EXIT_CODE, SmokeFailure, SmokeSkip


PROFILE_DENSITIES = (0, 50, 100)


def capture_diagnostics(args, cell):
    directory = args.output_directory / cell.extent / "hardware"
    directory.mkdir(parents=True, exist_ok=True)
    image, log = directory / f"{cell.density:03d}.bmp", directory / f"{cell.density:03d}.log"
    environment = os.environ.copy()
    for name in SANITIZED_ENVIRONMENT:
        environment.pop(name, None)
    environment.update(NWB_REFRACTION_SMOKE_DENSITY=str(cell.density), NWB_REFRACTION_SMOKE_EXTENT=cell.extent,
        NWB_REFRACTION_SMOKE_ENABLED="1", NWB_REFRACTION_SMOKE_HARDWARE="1", NWB_REFRACTION_SMOKE_DIAGNOSTICS="1",
        NWB_RENDERER_BASELINE_FIXED_DELTA_SECONDS="0.016666667")
    text, extent = launch_density_capture(args, cell, image, log, environment)
    admission = RefractionDensityEvidence.validate_fixture(text, cell)
    if re.findall(r"^AVBOIT refraction resolve: (hardware|screen-space)$", text.replace("\r\n", "\n"), re.MULTILINE) != ["hardware"]:
        raise SmokeFailure("query profiling requires exactly one accepted hardware route")
    statistics = RefractionQueryEvidence.parse_completed_statistics(text)
    checks = RefractionQueryEvidence.validate_statistics(statistics, cell)
    frame = read_bmp_24_rows(image)
    if frame[:2] != (cell.width, cell.height):
        raise SmokeFailure("query diagnostic framebuffer extent differs from the selected target")
    parity = None
    if args.reference_directory:
        reference_path = args.reference_directory / cell.extent / "hardware" / image.name
        reference = read_bmp_24_rows(reference_path)
        if frame != reference:
            raise SmokeFailure(f"query instrumentation changed exact RGB output: {cell.key}")
        parity = {"reference": str(reference_path), "exact_rgb_equal": True, "compared_pixels": cell.width * cell.height}
    return frame, {"statistics": statistics, "checks": checks, "admission": admission, "extent_evidence": extent,
        "image": str(image), "runtime_log": str(log), "optical_parity": parity}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", required=True, type=Path)
    parser.add_argument("--working-directory", required=True, type=Path)
    parser.add_argument("--output-directory", required=True, type=Path)
    parser.add_argument("--logserver-executable", type=Path)
    parser.add_argument("--reference-directory", type=Path, help="Uninstrumented density captures, arranged as extent/hardware/NNN.bmp")
    parser.add_argument("--extents", type=lambda text: selection(text, EXTENTS, "extents"), default=DEFAULT_EXTENTS)
    parser.add_argument("--densities", type=lambda text: tuple(map(int, selection(text, tuple(map(str, PROFILE_DENSITIES)), "densities"))), default=PROFILE_DENSITIES)
    parser.add_argument("--frames", type=int, default=16)
    parser.add_argument("--timeout", type=float, default=120)
    parser.add_argument("--application-arg", action="append", default=[])
    args = parser.parse_args(argv)
    if args.frames < 16 or not math.isfinite(args.timeout) or args.timeout <= 0:
        parser.error("query diagnostics require at least sixteen capture frames and a finite positive timeout")
    for name in ("executable", "working_directory", "output_directory", "logserver_executable", "reference_directory"):
        if getattr(args, name):
            setattr(args, name, getattr(args, name).resolve())
    args.output_directory.mkdir(parents=True, exist_ok=True)
    metrics = {"passed": False, "instrumented": True, "timing_claim": False, "cells": {}}
    output = args.output_directory / "refraction_query_diagnostics.json"
    try:
        for extent in args.extents:
            reference, info = capture_diagnostics(args, Cell(extent, 0, "hardware"))
            info["image_checks"] = RefractionDensityImageEvidence.validate_backdrop(reference)
            metrics["cells"][Cell(extent, 0, "hardware").key] = info
            for percent in args.densities:
                if not percent:
                    continue
                cell = Cell(extent, percent, "hardware")
                frame, info = capture_diagnostics(args, cell)
                info["image_checks"] = RefractionDensityImageEvidence.analyze_density_frame(reference, frame, percent, "hardware")
                metrics["cells"][cell.key] = info
        metrics["passed"] = True
        output.write_text(json.dumps(metrics, indent=2) + "\n", encoding="utf-8")
        print(f"PASS: completed query work and optical output validated in {len(metrics['cells'])} cells")
        return 0
    except SmokeSkip as error:
        metrics["error"] = str(error)
        output.write_text(json.dumps(metrics, indent=2) + "\n", encoding="utf-8")
        print(f"SKIP: {error}", file=sys.stderr)
        return SKIP_EXIT_CODE
    except (SmokeFailure, OSError, subprocess.TimeoutExpired) as error:
        metrics["error"] = str(error)
        output.write_text(json.dumps(metrics, indent=2) + "\n", encoding="utf-8")
        print(f"FAIL: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
