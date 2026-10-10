#!/usr/bin/env python3
"""Qualify exact refractor identity, diagnosed CSG failure, and near-air volume thickness."""

import argparse
import json
from pathlib import Path
import subprocess
import sys

from csg_optics_smoke import require_refraction_match, require_refraction_distinct
from refraction_gallery_smoke import capture, png_rgb_bytes
from window_capture_smoke import SKIP_EXIT_CODE, SmokeFailure


IDENTITY_CAPTURES = tuple((case, route) for case in
    ("identity_reference", "identity_same_ior", "identity_different_ior") for route in ("automatic", "screen"))
FAILURE_CAPTURES = (("crossing_overflow", "automatic"), ("crossing_overflow", "screen"))
NEAR_AIR_CAPTURES = tuple((case, route) for case in ("near_air_ordinary", "near_air_csg") for route in ("automatic", "screen"))
# This bright stripe interior avoids the foreground/behind panels, horizontal markers and stripe boundaries.
NEAR_AIR_RECTANGLE = (462, 345, 468, 350)
MINIMUM_NEAR_AIR_TRANSMITTED_RED = 2
MINIMUM_NEAR_AIR_SCREEN_RED = 12
MINIMUM_NEAR_AIR_RED_ATTENUATION = 8
# These central rays cross all 33 closed components, including the farthest box at z=32.5.
FAILURE_RECTANGLE = (475, 354, 485, 366)
MAXIMUM_BLACK_CHANNEL = 2
MINIMUM_SCREEN_MEAN_CHANNEL = 12.0


def require_capacity_rejection(hardware, screen):
    if hardware[:2] != (960, 720) or screen[:2] != hardware[:2]:
        raise SmokeFailure("capacity rejection requires matched 960x720 framebuffers")
    left, top, right, bottom = FAILURE_RECTANGLE
    maximum = transmitted = 0
    for y in range(top, bottom):
        for x in range(left, right):
            maximum = max(maximum, *hardware[2][y][x])
            transmitted += sum(screen[2][y][x])
    pixels = (right - left) * (bottom - top)
    mean = transmitted / (pixels * 3)
    if maximum > MAXIMUM_BLACK_CHANNEL:
        raise SmokeFailure("diagnosed CSG capacity failure leaked transmission through screen fallback")
    if mean < MINIMUM_SCREEN_MEAN_CHANNEL:
        raise SmokeFailure("screen capacity control lacks visible transmitted energy")
    return {"rectangle": list(FAILURE_RECTANGLE), "pixels": pixels,
        "maximum_hardware_channel": maximum, "mean_screen_channel": mean}


def require_near_air_thickness(hardware, screen):
    distinct = require_refraction_distinct(screen, hardware, "near-air measured thickness control")
    left, top, right, bottom = NEAR_AIR_RECTANGLE
    minimum_hardware = minimum_screen = minimum_attenuation = 255
    for y in range(top, bottom):
        for x in range(left, right):
            hardware_red, screen_red = hardware[2][y][x][0], screen[2][y][x][0]
            minimum_hardware = min(minimum_hardware, hardware_red)
            minimum_screen = min(minimum_screen, screen_red)
            minimum_attenuation = min(minimum_attenuation, screen_red - hardware_red)
    if minimum_hardware < MINIMUM_NEAR_AIR_TRANSMITTED_RED or minimum_screen < MINIMUM_NEAR_AIR_SCREEN_RED:
        raise SmokeFailure("near-air thickness controls lack positive transmitted energy on the bright stripe interior")
    if minimum_attenuation < MINIMUM_NEAR_AIR_RED_ATTENUATION:
        raise SmokeFailure("near-air hardware did not retain the longer tinted volume path")
    return {"difference": distinct, "bright_rectangle": list(NEAR_AIR_RECTANGLE),
        "minimum_hardware_red": minimum_hardware, "minimum_screen_red": minimum_screen,
        "minimum_red_attenuation": minimum_attenuation}


def analyze_near_air_frames(frames):
    metrics = {}
    for route in ("automatic", "screen"):
        metrics["near_air_" + route + "_retained_geometry"] = require_refraction_match(
            frames[("near_air_ordinary", route)], frames[("near_air_csg", route)])
    for case in ("near_air_ordinary", "near_air_csg"):
        metrics[case + "_measured_thickness"] = require_near_air_thickness(
            frames[(case, "automatic")], frames[(case, "screen")])
    return metrics


def analyze_frames(frames, identity_only=False):
    reference_screen = frames[("identity_reference", "screen")]
    metrics = {"valid_volume_uses_hardware": require_refraction_distinct(
        reference_screen, frames[("identity_reference", "automatic")], "valid closed-volume hardware control")}
    for case in ("identity_same_ior", "identity_different_ior"):
        metrics[case + "_capture_primary"] = require_refraction_match(reference_screen, frames[(case, "screen")])
        metrics[case + "_exact_association"] = require_refraction_match(frames[(case, "screen")], frames[(case, "automatic")])
    metrics["ior_does_not_select_entity"] = require_refraction_match(
        frames[("identity_different_ior", "automatic")], frames[("identity_same_ior", "automatic")])
    if not identity_only:
        metrics["explicit_capacity_failure_is_terminal"] = require_capacity_rejection(
            frames[("crossing_overflow", "automatic")], frames[("crossing_overflow", "screen")])
    return metrics


def parse_args(argv):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--working-directory", type=Path, required=True)
    parser.add_argument("--output-directory", type=Path, required=True)
    parser.add_argument("--logserver-executable", type=Path)
    parser.add_argument("--frames", type=int, default=16)
    parser.add_argument("--timeout", type=float, default=90.0)
    parser.add_argument("--application-arg", action="append", default=[])
    selection = parser.add_mutually_exclusive_group()
    selection.add_argument("--identity-only", action="store_true", help="Run only the six identity controls.")
    selection.add_argument("--near-air-only", action="store_true", help="Run only the four near-air measured-thickness controls.")
    args = parser.parse_args(argv)
    if args.frames <= 0 or args.timeout <= 0:
        parser.error("frames and timeout must be positive")
    args.require_hardware = True
    args.output_directory = args.output_directory.resolve()
    return args


def main(argv):
    args = parse_args(argv)
    args.output_directory.mkdir(parents=True, exist_ok=True)
    try:
        frames = {}
        captures = NEAR_AIR_CAPTURES if args.near_air_only else IDENTITY_CAPTURES + (() if args.identity_only else FAILURE_CAPTURES + NEAR_AIR_CAPTURES)
        for case, route in captures:
            frame = capture(args, case, route)
            if frame is None:
                return SKIP_EXIT_CODE
            frames[(case, route)] = frame
            (args.output_directory / f"{case}_{route}.png").write_bytes(png_rgb_bytes(frame))
        comparisons = {} if args.near_air_only else analyze_frames(frames, args.identity_only)
        if not args.identity_only:
            comparisons.update(analyze_near_air_frames(frames))
        report = {"captures": [list(spec) for spec in captures], "comparisons": comparisons,
            "contract": "The camera near plane clips the foreign entry from raster capture; the raw retained CSG slab front owns completed depth ahead of the foreign backface. The clipped foreign entry remains in the hardware association band and shares IOR. Full entity generations establish association; explicit collector failure terminates hardware transmission, and ordinary missing-volume cases retain the screen approximation. Every finite IOR above one remains a refractive volume: the first half value above air must retain three-unit Beer thickness over z=-0.25 through z=2.75, positive transmission and ordinary/CSG equivalence; its exit is separate from the blue backdrop panel at z=3."}
        (args.output_directory / "refraction_identity_comparisons.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        print("PASS: selected refraction controls qualify exact entity association, terminal CSG failure and valid near-air volume thickness", flush=True)
        return 0
    except (OSError, SmokeFailure, subprocess.TimeoutExpired) as exc:
        print("FAIL: " + str(exc), file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
