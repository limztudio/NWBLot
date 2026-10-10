#!/usr/bin/env python3
"""Qualify retained/clipped CSG open shadows and the explicit 64/65 event boundary."""

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import re
import sys

from csg_shadow_reference import BYTE_TOLERANCE, TRANSMISSION, region_pixels
from csg_shadow_smoke import capture_environment
import window_capture_smoke as capture
from window_capture_smoke import SKIP_EXIT_CODE, SmokeFailure, SmokeSkip, read_bmp_24_rows


CASES = ("reference", "retained", "clipped_reference", "clipped", "boundary64", "terminal65")
ORDINARY_CASES = ("reference", "clipped_reference")
OPEN_CAPACITY_MESSAGE = "RendererSystem: CSG direct-shadow traversal exceeded open-surface capacity (flags=16); conservative occlusion retained"
COUNTS = {"reference": (5, 6), "retained": (5, 6), "clipped_reference": (4, 5), "clipped": (4, 5),
    "boundary64": (64, 6), "terminal65": (65, 6)}
LENGTHS = {"reference": (2., 1.2), "retained": (2., 1.2), "clipped_reference": (1., 1.6), "clipped": (1., 1.6),
    "boundary64": (64. / 63., 1.2), "terminal65": (None, 1.2)}
IOR = 1.5
REGIONS = (("first_receiver", -1.2, 0.), ("six_sheet_control", 1.2, 0.), ("clear_receiver", 0., 1.2))


def expected_open_rgb(length, events):
    if length is None:
        return (0., 0., 0.)
    interface_transmission = (1. - ((IOR - 1.) / (IOR + 1.)) ** 2) ** events
    linear = [channel ** length * interface_transmission for channel in TRANSMISSION]
    return tuple(255. * (12.92 * value if value <= 0.0031308 else 1.055 * value ** (1. / 2.4) - 0.055)
        for value in linear)


def validate_runtime(log_text, case, route, frames):
    if case not in CASES or route not in ("hardware", "software"):
        raise SmokeFailure("unknown open-shadow case or route")
    lines = log_text.splitlines()
    warning_indices = [index for index, line in enumerate(lines) if "[WARNING]" in line]
    remaining = log_text
    if case == "terminal65":
        if len(warning_indices) != 1:
            raise SmokeFailure("terminal65 requires only the exact open-capacity flags=16 diagnostic")
        start = warning_indices[0]
        if not re.fullmatch(r"\d{2}:\d{2}:\d{2}\.\d{3} \[WARNING\]:", lines[start]):
            raise SmokeFailure("terminal65 warning has an invalid severity-record header")
        end = next((index for index in range(start + 1, len(lines))
            if re.fullmatch(r"\d{2}:\d{2}:\d{2}\.\d{3} \[[A-Z ]+\]:", lines[index])), len(lines))
        message = "\n".join(lines[start + 1:end]).strip("\n")
        if message != OPEN_CAPACITY_MESSAGE:
            raise SmokeFailure("terminal65 requires the complete exact open-capacity flags=16 warning message")
        # Remove only this admitted severity/message record; detached text never waives another diagnostic.
        remaining = "\n".join(lines[:start] + lines[end:])
    elif warning_indices:
        raise SmokeFailure(f"{case}: valid open shadow emitted a warning")
    for marker in capture.STRICT_LOG_FAILURE_MESSAGES:
        if marker in remaining:
            raise SmokeFailure(f"{case}: rejected runtime diagnostic {marker}")
    first, second = COUNTS[case]
    required = ["FramebufferCapture: capture ready", "CsgShadowSmokeProject: shutdown",
        f"CsgShadowSmokeProject: open_case={case} retained_events={first},{second} receiver_z=0 sheets_z_min=-7 sheets_z_max=-5 ior=1.5 unit_transmission=0.25,0.5,0.75 coverage=1",
        f"CsgShadowSmokeProject: atlas arm=1 light=directional hardware={int(route == 'hardware')}",
        "SoftwareShadowSmoke: requested backend=0 directional_resolution=512 point_resolution=256 budget_bytes=268435456 coverage=0 blocker_search=0 capture_cadence=0"]
    if route == "software":
        required.append("Loader: hardware ray tracing disabled before device creation")
        if "RendererSystem: dispatched hardware transparent shadow traversal" in log_text:
            raise SmokeFailure("software arm used hardware transparent shadows")
    if case in ORDINARY_CASES:
        required.append("RendererSystem: dispatched hardware transparent shadow traversal" if route == "hardware"
            else "RendererSystem: dispatched light-space shadow maps")
        if "RendererSystem: dispatched CSG light-space shadows" in log_text:
            raise SmokeFailure("ordinary open reference admitted CSG shadow work")
    else:
        required.append(f"RendererSystem: dispatched CSG light-space shadows (hardware_compose={int(route == 'hardware')}")
    for message in required:
        if message not in log_text:
            raise SmokeFailure(f"{case}: missing exact runtime evidence: {message}")
    sources = [int(value) for value in re.findall(r"FramebufferCapture: graphics source frame (\d+)", log_text)]
    if len(sources) != 1 or sources[0] < frames - 1:
        raise SmokeFailure(f"{case}: capture lacks one completed source after the required frame warmup")
    return {"graphics_source_frame": sources[0], "route": route, "retained_events": [first, second],
        "diagnostic_flags": 16 if case == "terminal65" else 0, "expected_capacity_terminal": case == "terminal65"}


def measure_frame(frame, case):
    width, height, rows = frame
    if width < 320 or height < 240 or len(rows) != height or any(len(row) != width for row in rows):
        raise SmokeFailure("invalid open-shadow capture extent")
    measurements = {}
    for index, region in enumerate(REGIONS):
        pixels = region_pixels(region, width, height)
        rgb = tuple(sum(rows[y][x][channel] for x, y in pixels) / len(pixels) for channel in range(3))
        length = LENGTHS[case][index] if index < 2 else 0.
        events = COUNTS[case][index] if index < 2 else 0
        expected = expected_open_rgb(length, events)
        tolerance = 4. if length is None else BYTE_TOLERANCE
        error = max(abs(value - target) for value, target in zip(rgb, expected))
        if error > tolerance:
            raise SmokeFailure(f"{case}/{region[0]}: open shadow disagrees with independent pairing/Beer/Fresnel oracle; rgb={rgb}, expected={expected}")
        measurements[region[0]] = {"rgb": rgb, "expected_rgb": expected, "optical_length": length, "interface_count": events,
            "maximum_mean_byte_error": error, "samples": len(pixels)}
    return measurements


def compare_frames(frames):
    if set(frames) != set(CASES) or len({frame[:2] for frame in frames.values()}) != 1:
        raise SmokeFailure("open-shadow qualification requires all six matched capture cases")
    measured = {case: measure_frame(frame, case) for case, frame in frames.items()}
    for reference, csg in (("reference", "retained"), ("clipped_reference", "clipped")):
        for region in REGIONS:
            difference = max(abs(a - b) for a, b in zip(measured[reference][region[0]]["rgb"], measured[csg][region[0]]["rgb"]))
            if difference > BYTE_TOLERANCE:
                raise SmokeFailure(f"{csg}: retained open optics differ from independent ordinary reference")
    for name in ("first_receiver", "six_sheet_control"):
        change = max(abs(a - b) for a, b in zip(measured["retained"][name]["rgb"], measured["clipped"][name]["rgb"]))
        if change < 15.:
            raise SmokeFailure(f"{name}: clipping did not change the open pairing control")
    return {"oracle": "independent odd span/even pairs, clipped ordinary geometry, Beer absorption and IOR1.5 interface Fresnel through the 64/65 boundary",
        "ior": IOR, "unit_transmission": TRANSMISSION,
        "mean_rgb_tolerance_bytes": BYTE_TOLERANCE, "regions": measured}


def capture_case(args, case):
    output = args.output_directory / (case + ".bmp")
    runtime_log = output.with_suffix(".log")
    command = ["--executable", str(args.executable), "--working-directory", str(args.working_directory),
        "--output", str(output), "--application-capture", "--application-capture-frame-count", str(args.frames),
        "--timeout", str(args.timeout), "--log-output", str(runtime_log),
        "--gpu-validation" if args.gpu_validation else "--no-gpu-validation"]
    if args.logserver_executable:
        command.extend(("--logserver-executable", str(args.logserver_executable)))
    else:
        command.append("--no-logserver")
    if args.route == "software":
        command.append("--application-arg=--disable-hardware-ray-tracing")
    else:
        command.extend(("--skip-log-message", "atlas arm=1 light=directional hardware=0"))
    command.extend("--application-arg=" + argument for argument in args.application_arg)
    capture_args = capture.parse_args(command)
    if case == "terminal65":
        # Only this named terminal arm defers WARNING validation to the exact single flags=16 check below.
        capture_args.reject_log_message.remove("[WARNING]")
    environment = capture_environment("cut", "directional", "open")
    for key in tuple(environment):
        if key.startswith(("NWB_CAUSTIC_", "NWB_REFLECTION_", "NWB_REFRACTION_", "NWB_CSG_CAUSTIC_")):
            environment.pop(key)
    environment["NWB_CSG_SHADOW_OPEN_CASE"] = case
    previous_environment = dict(os.environ)
    print(f"Capturing CSG open shadows {args.route}/{case}...", flush=True)
    try:
        os.environ.clear()
        os.environ.update(environment)
        capture_result = capture.launch_and_capture_application(capture_args)
        if (capture_result.width, capture_result.height) != (960, 720):
            raise SmokeFailure("open-shadow native capture has the wrong framebuffer extent")
    finally:
        os.environ.clear()
        os.environ.update(previous_environment)
    receipt = validate_runtime(runtime_log.read_text(encoding="utf-8"), case, args.route, args.frames)
    frame = read_bmp_24_rows(output)
    if frame[:2] != (960, 720):
        raise SmokeFailure("open-shadow smoke requires the actual 960x720 framebuffer")
    receipt["bmp_sha256"] = hashlib.sha256(output.read_bytes()).hexdigest()
    receipt["log_sha256"] = hashlib.sha256(runtime_log.read_bytes()).hexdigest()
    return frame, receipt


def parse_args(argv):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--working-directory", type=Path, required=True)
    parser.add_argument("--output-directory", type=Path, required=True)
    parser.add_argument("--logserver-executable", type=Path)
    parser.add_argument("--route", choices=("hardware", "software"), required=True)
    parser.add_argument("--frames", type=int, default=120)
    parser.add_argument("--timeout", type=float, default=120.)
    parser.add_argument("--application-arg", action="append", default=[])
    parser.add_argument("--gpu-validation", action=argparse.BooleanOptionalAction, default=True)
    args = parser.parse_args(argv)
    if args.frames < 120 or not math.isfinite(args.timeout) or args.timeout <= 0.:
        parser.error("at least 120 capture frames and a finite positive timeout are required")
    args.output_directory = args.output_directory.resolve()
    return args


def main(argv):
    args = parse_args(argv)
    args.output_directory.mkdir(parents=True, exist_ok=True)
    if any(args.output_directory.iterdir()):
        print("open-shadow output directory must be fresh", file=sys.stderr)
        return 1
    try:
        frames, receipts = {}, {}
        for case in CASES:
            frames[case], receipts[case] = capture_case(args, case)
        result = compare_frames(frames)
        result.update({"status": "passed", "route": args.route, "capture_count": len(receipts), "captures": receipts,
            "limits": "source receipts prove completed static captures and selected route; no per-ray diagnostic attribution or performance claim"})
        (args.output_directory / "result.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
        print("PASS: retained/clipped five/six-sheet optics, exact 64 capacity, and conservative 65 terminal")
        return 0
    except SmokeSkip as error:
        (args.output_directory / "skip.json").write_text(json.dumps({"status": "skipped", "reason": str(error)}, indent=2) + "\n", encoding="utf-8")
        return SKIP_EXIT_CODE
    except (SmokeFailure, OSError) as error:
        (args.output_directory / "failure.json").write_text(json.dumps({"status": "failed", "error": str(error)}, indent=2) + "\n", encoding="utf-8")
        print(str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
