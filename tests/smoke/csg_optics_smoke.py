#!/usr/bin/env python3
"""Compare retained CSG optical solids against matched ordinary geometry."""

import argparse
import json
from pathlib import Path
import subprocess
import sys

import reflection_optical_smoke as reflection
import refraction_gallery_smoke as refraction
from window_capture_smoke import SKIP_EXIT_CODE, SmokeFailure


REFLECTION_CAPTURES = tuple(reflection.OpticalCapture(name, name) for name in
    ("optical_csg_reference", "optical_csg_cap", "optical_csg_cavity", "optical_sliver", "optical_csg_sliver",
        "optical_sub_ulp", "optical_csg_sub_ulp", "optical_group_gap", "optical_csg_group_gap",
        "optical_group_gap_sub_ulp", "optical_csg_group_gap_sub_ulp", "optical_group_entry", "optical_csg_group_entry"))
REFRACTION_CAPTURES = (("csg_reference", "automatic"), ("csg_reference", "screen"),
    ("csg_cap", "automatic"), ("csg_cap", "screen"), ("csg_middle", "automatic"),
    ("csg_middle", "screen"), ("csg_uncut", "automatic"))
# This rectangle stays inside the retained slab silhouette for every fixture. Rim rasterization and the original receiver silhouette cannot establish optical equivalence.
REFRACTION_RECTANGLE = (325, 235, 635, 485)
MAXIMUM_MEAN_ERROR = 0.4
MAXIMUM_CHANGED_FRACTION = 0.01
MINIMUM_DISTINCT_PIXELS = 64


def refraction_difference(reference, candidate):
    if reference[:2] != candidate[:2] or reference[:2] != (960, 720):
        raise SmokeFailure("CSG refraction requires matched 960x720 framebuffers")
    left, top, right, bottom = REFRACTION_RECTANGLE
    changed = total = maximum = 0
    for y in range(top, bottom):
        for x in range(left, right):
            deltas = tuple(abs(a - b) for a, b in zip(reference[2][y][x], candidate[2][y][x]))
            largest = max(deltas)
            changed += largest > 8
            total += sum(deltas)
            maximum = max(maximum, largest)
    count = (right - left) * (bottom - top)
    return {"rectangle": list(REFRACTION_RECTANGLE), "pixels": count,
        "pixels_over_eight": changed, "changed_fraction": changed / count,
        "mean_absolute_rgb_error": total / (count * 3), "maximum_channel_error": maximum}


def require_refraction_match(reference, candidate):
    metrics = refraction_difference(reference, candidate)
    if metrics["mean_absolute_rgb_error"] > MAXIMUM_MEAN_ERROR or metrics["changed_fraction"] > MAXIMUM_CHANGED_FRACTION:
        raise SmokeFailure("refraction control images disagree in the retained volume comparison region: " + json.dumps(metrics))
    return metrics


def require_refraction_distinct(reference, candidate, label):
    metrics = refraction_difference(reference, candidate)
    if metrics["pixels_over_eight"] < MINIMUM_DISTINCT_PIXELS:
        raise SmokeFailure(label + " failed to distinguish the optical path: " + json.dumps(metrics))
    return metrics


def run_reflection(args):
    completed, evidence = [], {}
    for spec in REFLECTION_CAPTURES:
        result = reflection.capture(args, spec)
        if result is None:
            return SKIP_EXIT_CODE
        completed.append(spec)
        evidence[spec.name] = result
    reflection.write_report(args, completed, evidence)
    metrics = reflection.analyze_suite(args.output_directory, REFLECTION_CAPTURES)
    reflection.write_report(args, completed, evidence, metrics)
    (args.output_directory / "csg_reflection_comparisons.json").write_text(json.dumps(metrics, indent=2) + "\n", encoding="utf-8")
    print("PASS: CSG reflection wall and removed-origin containment match the physical retained slab", flush=True)
    return 0


def run_refraction(args):
    frames = {}
    for case, route in REFRACTION_CAPTURES:
        frame = refraction.capture(args, case, route)
        if frame is None:
            return SKIP_EXIT_CODE
        frames[(case, route)] = frame
        (args.output_directory / f"{case}_{route}.png").write_bytes(refraction.png_rgb_bytes(frame))
    reference = frames[("csg_reference", "automatic")]
    carved = frames[("csg_cap", "automatic")]
    metrics = {
        "retained_slab_matches": require_refraction_match(reference, carved),
        "cap_only_slab_matches": require_refraction_match(reference, frames[("csg_middle", "automatic")]),
        "screen_entry_matches": require_refraction_match(frames[("csg_reference", "screen")], frames[("csg_cap", "screen")]),
        "screen_cap_only_matches": require_refraction_match(frames[("csg_reference", "screen")], frames[("csg_middle", "screen")]),
        "original_thickness_differs": require_refraction_distinct(reference, frames[("csg_uncut", "automatic")], "uncut thickness control"),
        "screen_approximation_differs": require_refraction_distinct(carved, frames[("csg_cap", "screen")], "screen fallback control"),
        "limitations": "Automatic captures require hardware dispatch. Central retained-solid pixels compare physical entry/exit and attenuation; the screen capture is a separate approximation control.",
    }
    (args.output_directory / "csg_refraction_comparisons.json").write_text(json.dumps(metrics, indent=2) + "\n", encoding="utf-8")
    print("PASS: generated CSG glass entry/exit match retained geometry in hardware and screen routes, with distinct thickness and approximation controls", flush=True)
    return 0


def parse_args(argv):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--effect", choices=("reflection", "refraction"), required=True)
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--working-directory", type=Path, required=True)
    parser.add_argument("--output-directory", type=Path, required=True)
    parser.add_argument("--logserver-executable", type=Path)
    parser.add_argument("--frames", type=int, default=16)
    parser.add_argument("--timeout", type=float, default=90)
    parser.add_argument("--application-arg", action="append", default=[])
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
        return run_reflection(args) if args.effect == "reflection" else run_refraction(args)
    except (OSError, SmokeFailure, subprocess.TimeoutExpired) as exc:
        print("FAIL: " + str(exc), file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
