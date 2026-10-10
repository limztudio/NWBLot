#!/usr/bin/env python3
"""Compare CSG GI against ordinary carved geometry, with an uncut false-positive control."""

import argparse
import json
import math
import os
import re
from pathlib import Path
import statistics
import subprocess
import sys

from window_capture_smoke import SKIP_EXIT_CODE, SmokeFailure, read_bmp_24_rows


ARMS = ("reference", "cut", "uncut")
EXPECTED_EXTENT = (1280, 900)
MIN_WARMUP_PRESENTATIONS = 360
MIN_LIVE_WARMUP_SECONDS = 30.0
NO_DEDICATED_ASYNC_COMPUTE = (
    "RendererSystem: frame-lagged async lighting Graphics queue route accepted (no dedicated Compute queue"
)


def build_environment(arm, lagged_lighting):
    environment = os.environ.copy()
    for name in tuple(environment):
        if name.startswith("NWB_GI_SMOKE_") or name.startswith("NWB_RENDERER_BASELINE_"):
            environment.pop(name)
    environment.pop("NWB_GPU_TIMING_FILE", None)
    environment["NWB_GI_CSG_ARM"] = arm
    environment["NWB_GI_CSG_LAGGED_LIGHTING"] = "1" if lagged_lighting else "0"
    return environment


def validate_capture_evidence(log, required_presentations):
    if required_presentations < MIN_WARMUP_PRESENTATIONS:
        raise SmokeFailure("CSG GI requires at least 360 successful warmup presentations")
    patterns = {
        "start": r"GiTestSmokeProject: CSG GI live warmup start source_frame=(\d+) presentations=(\d+) required_presentations=(\d+) required_seconds=(\S+)",
        "end": r"GiTestSmokeProject: CSG GI live warmup end source_frame=(\d+) presentations=(\d+) elapsed_seconds=(\S+)",
        "capture": r"GiTestSmokeProject: CSG GI capture source_frame=(\d+)",
        "readback": r"FramebufferCapture: graphics source frame (\d+)",
    }
    matches = {}
    for name, pattern in patterns.items():
        records = list(re.finditer(pattern, log))
        if len(records) != 1:
            raise SmokeFailure(f"CSG GI needs exactly one {name} evidence record, found {len(records)}")
        matches[name] = records[0]
    start_frame, start_presentations, declared_presentations = map(int, matches["start"].groups()[:3])
    end_frame, end_presentations = map(int, matches["end"].groups()[:2])
    capture_frame = int(matches["capture"].group(1))
    readback_frame = int(matches["readback"].group(1))
    numeric_pattern = r"[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?|[+-]?(?:inf|nan)"
    for value in (matches["start"].group(4), matches["end"].group(3)):
        if not re.fullmatch(numeric_pattern, value, flags=re.IGNORECASE):
            raise SmokeFailure("CSG GI live warmup has malformed numeric evidence")
    required_seconds = float(matches["start"].group(4))
    elapsed_seconds = float(matches["end"].group(3))
    if declared_presentations != required_presentations or start_presentations < required_presentations:
        raise SmokeFailure("CSG GI live warmup began before its required successful presentations")
    if not math.isfinite(required_seconds) or required_seconds != MIN_LIVE_WARMUP_SECONDS:
        raise SmokeFailure("CSG GI live warmup must require 30 steady-clock seconds")
    if not math.isfinite(elapsed_seconds) or elapsed_seconds < MIN_LIVE_WARMUP_SECONDS:
        raise SmokeFailure("CSG GI has no completed 30-second live warmup after its frame warmup")
    if end_frame <= start_frame or end_presentations <= start_presentations:
        raise SmokeFailure("CSG GI live warmup has no later rendered presentation progress")
    if capture_frame != readback_frame or capture_frame < end_frame or capture_frame >= 2**64 - 1:
        raise SmokeFailure("CSG GI capture source does not match its completed readback and live warmup")
    if not (matches["start"].start() < matches["end"].start() < matches["readback"].start() < matches["capture"].start()):
        raise SmokeFailure("CSG GI live warmup and capture evidence is out of order")
    return {
        "start_source_frame": start_frame,
        "start_successful_presentations": start_presentations,
        "required_presentations": required_presentations,
        "required_live_seconds": required_seconds,
        "end_source_frame": end_frame,
        "end_successful_presentations": end_presentations,
        "elapsed_live_seconds": elapsed_seconds,
        "capture_source_frame": capture_frame,
    }


def capture_arm(args, arm):
    output = args.output_directory / f"{arm}.bmp"
    command = [sys.executable, str(Path(__file__).with_name("window_capture_smoke.py")),
        "--executable", str(args.executable), "--working-directory", str(args.working_directory),
        "--output", str(output), "--application-capture", "--application-capture-frame-count", str(args.frames),
        "--timeout", str(args.timeout), "--log-output", str(output.with_suffix(".log")),
        "--expect-log-message", f"GiTestSmokeProject: CSG GI arm={arm} overlapping_box_cutters=2 accepted_presentations={args.frames}",
        "--expect-log-message", "RendererSystem: dispatched surfel GI resolve",
        "--expect-log-message", "GiTestSmokeProject: mixed CSG receiver has 12 closed and 2 open triangles",
        "--expect-log-message", "GiTestSmokeProject: shutdown",
        "--gpu-validation" if args.gpu_validation else "--no-gpu-validation"]
    if args.route == "software":
        command += ["--application-arg=--disable-hardware-ray-tracing",
            "--expect-log-message", "Loader: hardware ray tracing disabled before device creation",
            "--expect-log-message", "RendererSystem: created surfel trace compute pipeline",
            "--reject-log-message", "RendererSystem: created surfel HW trace compute pipeline"]
    if args.lagged_lighting:
        command += ["--expect-log-message", "RendererSystem: frame-lagged async lighting bootstrap accepted",
            "--expect-log-message", "RendererSystem: frame-lagged async lighting active history accepted",
            "--skip-log-message", NO_DEDICATED_ASYNC_COMPUTE]
    if args.logserver_executable:
        command += ["--logserver-executable", str(args.logserver_executable)]
    else:
        command.append("--no-logserver")
    result = subprocess.run(command, env=build_environment(arm, args.lagged_lighting), check=False,
        timeout=args.timeout + 45.0)
    if result.returncode == SKIP_EXIT_CODE:
        return None
    if result.returncode:
        raise SmokeFailure(f"CSG GI {arm} capture failed: {result.returncode}")
    evidence = validate_capture_evidence(output.with_suffix(".log").read_text(encoding="utf-8"), args.frames)
    frame = read_bmp_24_rows(output)
    if frame[:2] != EXPECTED_EXTENT:
        raise SmokeFailure("CSG GI requires the actual 1280x900 framebuffer")
    return frame, evidence


def project_point(x, y, z):
    # Static camera at (0, 1.3, -5.5), no rotation, 60-degree vertical field of view.
    scale = EXPECTED_EXTENT[1] / (2.0 * math.tan(math.pi / 6.0) * (z + 5.5))
    return round(EXPECTED_EXTENT[0] * 0.5 + x * scale - 0.5), round(EXPECTED_EXTENT[1] * 0.5 - (y - 1.3) * scale - 0.5)


def measure_frame(frame):
    _, _, rows = frame
    regions = {
        "passage": {project_point(-1.3 + ix * 0.05, 1.3 + iy * 0.05, 1.0)
            for iy in range(-4, 5) for ix in range(-4, 5)},
        "cap_bounce": {project_point(1.3 + ix * 0.05, 0.61, -0.32 + iz * 0.035)
            for iz in range(-4, 5) for ix in range(-4, 5)},
        "cap_surface": {project_point(1.3 + ix * 0.05, 1.3 + iy * 0.05, 0.4)
            for iy in range(-4, 5) for ix in range(-4, 5)},
        "control": {project_point(ix * 0.035, 2.92 + iy * 0.025, 0.0)
            for iy in range(-4, 5) for ix in range(-4, 5)},
        "mixed_passage": {project_point(ix * 0.03, -0.55 + iy * 0.025, 1.0)
            for iy in range(-4, 5) for ix in range(-4, 5)},
        "mixed_floor": {project_point(-1.025 + ix * 0.03125, -0.77, -1.475 + iz * 0.04375)
            for iz in range(-4, 5) for ix in range(-4, 5)},
        "mixed_open_surface": {project_point(-0.95 + ix * 0.025, -0.3 + iy * 0.025, -1.0)
            for iy in range(-4, 5) for ix in range(-4, 5)},
        "mixed_open_surface_right": {project_point(0.95 + ix * 0.025, -0.3 + iy * 0.025, -1.0)
            for iy in range(-4, 5) for ix in range(-4, 5)},
    }
    metrics = {}
    for name, pixels in regions.items():
        rgb = [tuple(rows[y][x][channel] for x, y in sorted(pixels)) for channel in range(3)]
        means = [statistics.fmean(channel) for channel in rgb]
        metrics[name] = {
            "samples": len(pixels),
            "mean_rgb": means,
            "median_rgb": [statistics.median(channel) for channel in rgb],
            "blue_excess": means[2] - means[0],
            "green_excess": means[1] - means[0],
        }
    # Interior grids catch missing passage/cap columns that a central mean can hide behind bright surviving pixels.
    for name, z, first_x, channel, minimum in (
        ("visible_passage", 1.0, -1.8, 2, 15.0), ("visible_cap", 0.4, 1.05, 1, 25.0)
    ):
        pixels = {project_point(first_x + ix * 0.1, 0.85 + iy * 0.1125, z)
            for iy in range(9) for ix in range(9)}
        signals = [statistics.fmean(rows[y + dy][x + dx][channel] - rows[y + dy][x + dx][0]
            for dy in range(-1, 2) for dx in range(-1, 2)) for x, y in sorted(pixels)]
        covered = sum(signal >= minimum for signal in signals)
        metrics[name] = {
            "samples": len(pixels),
            "covered_samples": covered,
            "coverage": covered / len(pixels),
            "signal_threshold": minimum,
            "minimum_signal": min(signals),
            "mean_signal": statistics.fmean(signals),
        }
    pixels = regions["mixed_passage"]
    signals = [statistics.fmean(max(rows[y + dy][x + dx][0], rows[y + dy][x + dx][2])
        - rows[y + dy][x + dx][1] for dy in range(-1, 2) for dx in range(-1, 2)) for x, y in sorted(pixels)]
    covered = sum(signal >= 15.0 for signal in signals)
    metrics["visible_mixed_passage"] = {
        "samples": len(pixels),
        "covered_samples": covered,
        "coverage": covered / len(pixels),
        "signal_threshold": 15.0,
        "minimum_signal": min(signals),
        "mean_signal": statistics.fmean(signals),
    }
    return metrics


def validate_metrics(metrics):
    reference, cut, uncut = (metrics[arm] for arm in ARMS)
    for arm in ARMS:
        if metrics[arm]["control"]["blue_excess"] < 15.0:
            raise SmokeFailure(f"{arm} has no independently visible blue GI control")
    for region, signal, minimum in (("passage", "blue_excess", 15.0), ("cap_bounce", "green_excess", 8.0)):
        expected = reference[region][signal]
        if expected < minimum:
            raise SmokeFailure(f"ordinary reference has insufficient {region} GI signal: {expected:.3f}")
        if expected - uncut[region][signal] < minimum:
            raise SmokeFailure(f"uncut control does not distinguish {region} from the ordinary reference")
        # The same visible G-buffer surfaces seed different scene-instance orders. Compare regional color energy
        # after convergence, rather than requiring identical stochastic ray sequences or edge pixels.
        tolerance = max(10.0, expected * 0.30)
        if abs(cut[region][signal] - expected) > tolerance:
            raise SmokeFailure(f"CSG {region} differs from ordinary carved geometry: "
                f"reference={expected:.3f}, cut={cut[region][signal]:.3f}, tolerance={tolerance:.3f}")
    if reference["cap_surface"]["green_excess"] < 25.0 or cut["cap_surface"]["green_excess"] < 25.0:
        raise SmokeFailure("the generated cap and ordinary reference cap are not both visibly green")
    for region in ("visible_passage", "visible_cap"):
        for arm in ("reference", "cut"):
            coverage = metrics[arm][region]["coverage"]
            if coverage < 0.95:
                raise SmokeFailure(f"{arm} has missing {region} geometry: coverage={coverage:.3%}, required=95%")
        coverage = uncut[region]["coverage"]
        if coverage > 0.05:
            raise SmokeFailure(f"uncut control unexpectedly exposes {region}: coverage={coverage:.3%}, allowed=5%")
    for region in ("mixed_open_surface", "mixed_open_surface_right"):
        for arm in ARMS:
            surface = metrics[arm][region]["mean_rgb"]
            if surface[0] - surface[1] < 25.0:
                raise SmokeFailure(f"{arm} has no visible retained red {region} control")
        for channel, expected in enumerate(reference[region]["mean_rgb"]):
            actual = cut[region]["mean_rgb"][channel]
            tolerance = max(10.0, expected * 0.30)
            if abs(actual - expected) > tolerance:
                raise SmokeFailure(f"CSG {region} brightness channel {channel} differs from ordinary geometry: "
                    f"reference={expected:.3f}, cut={actual:.3f}, tolerance={tolerance:.3f}")
    for arm in ARMS:
        floor = metrics[arm]["mixed_floor"]["mean_rgb"]
        if floor[0] - floor[1] < 8.0:
            raise SmokeFailure(f"{arm} has insufficient GI bounce from retained open triangles")
    if uncut["mixed_passage"]["green_excess"] < 25.0:
        raise SmokeFailure("uncut mixed-mesh control does not expose the removed green stripe")
    for arm in ("reference", "cut"):
        rgb = metrics[arm]["mixed_passage"]["mean_rgb"]
        if max(rgb[0], rgb[2]) - rgb[1] < 15.0:
            raise SmokeFailure(f"{arm} has insufficient GI through the clipped mixed-mesh passage")
        coverage = metrics[arm]["visible_mixed_passage"]["coverage"]
        if coverage < 0.95:
            raise SmokeFailure(f"{arm} has missing mixed-mesh passage geometry: coverage={coverage:.3%}")
    if uncut["visible_mixed_passage"]["coverage"] > 0.05:
        raise SmokeFailure("uncut mixed-mesh control falsely exposes the GI passage")
    for region in ("mixed_passage", "mixed_floor"):
        for channel, expected in enumerate(reference[region]["mean_rgb"]):
            actual = cut[region]["mean_rgb"][channel]
            tolerance = max(10.0, expected * 0.30)
            if abs(actual - expected) > tolerance:
                raise SmokeFailure(f"CSG {region} GI channel {channel} differs from ordinary geometry: "
                    f"reference={expected:.3f}, cut={actual:.3f}, tolerance={tolerance:.3f}")
    expected_green = reference["mixed_passage"]["mean_rgb"][1]
    if cut["mixed_passage"]["mean_rgb"][1] - expected_green > max(8.0, expected_green * 0.30):
        raise SmokeFailure("the removed green open surface contributes false GI in the mixed-mesh passage")


def parse_args(argv):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--working-directory", type=Path, required=True)
    parser.add_argument("--output-directory", type=Path, required=True)
    logging = parser.add_mutually_exclusive_group()
    logging.add_argument("--logserver-executable", type=Path)
    logging.add_argument("--no-logserver", action="store_true")
    parser.add_argument("--route", choices=("natural", "software"), default="software")
    parser.add_argument("--lagged-lighting", action=argparse.BooleanOptionalAction, default=False,
        help="Require accepted bootstrap and active frame-lagged lighting history for every arm.")
    parser.add_argument("--frames", type=int, default=360)
    parser.add_argument("--timeout", type=float, default=240.0)
    parser.add_argument("--gpu-validation", action=argparse.BooleanOptionalAction, default=True)
    args = parser.parse_args(argv)
    if args.frames < MIN_WARMUP_PRESENTATIONS or not math.isfinite(args.timeout) or args.timeout <= 0.0:
        parser.error("frames must be at least 360, and timeout must be finite and positive")
    args.executable = args.executable.resolve()
    args.working_directory = args.working_directory.resolve()
    args.output_directory = args.output_directory.resolve()
    return args


def main(argv):
    args = parse_args(argv)
    args.output_directory.mkdir(parents=True, exist_ok=True)
    metrics = {"route": args.route, "lagged_lighting": args.lagged_lighting,
        "accepted_presentations": args.frames, "live_warmup": {}, "regions": {}}
    try:
        for arm in ARMS:
            captured = capture_arm(args, arm)
            if captured is None:
                return SKIP_EXIT_CODE
            frame, evidence = captured
            metrics["live_warmup"][arm] = evidence
            metrics["regions"][arm] = measure_frame(frame)
        validate_metrics(metrics["regions"])
        metrics["passed"] = True
        print("PASS: CSG GI matches ordinary geometry through overlapping cuts and from a generated cap")
        return 0
    except (SmokeFailure, subprocess.TimeoutExpired) as error:
        metrics["passed"] = False
        metrics["error"] = str(error)
        print(str(error), file=sys.stderr)
        return 1
    finally:
        (args.output_directory / "result.json").write_text(json.dumps(metrics, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
