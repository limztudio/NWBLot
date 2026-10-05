#!/usr/bin/env python3
"""Compare exact duplicate optical volumes with matched single-volume framebuffer renders."""

import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import subprocess
import sys

from refraction_gallery_smoke import CASES, COMMON_LIMIT, VARIANT_LABELS, capture, parse_selection, write_gallery
from window_capture_smoke import SKIP_EXIT_CODE, SmokeFailure, read_bmp_24_rows

LIT_DUPLICATE_SINGLE_COOL = "duplicate_single_cool"
LIT_DUPLICATE_SINGLE_WARM = "duplicate_single_warm"
LIT_DUPLICATE_SINGLE_COOL_TINTED = "duplicate_single_cool_tinted"
LIT_DUPLICATE_SINGLE_WARM_TINTED = "duplicate_single_warm_tinted"
LIT_COINCIDENT_TINTED = "coincident_tinted"
LIT_COINCIDENT_TINTED_IDENTICAL = "coincident_tinted_identical"
LIT_COINCIDENT_PRESERVED = "coincident_preserved"
LIT_PIXELS_OVER_CHANNEL_TOLERANCE = "pixels_over_channel_tolerance"
LIT_MAXIMUM_CHANNEL_DIFFERENCE = "maximum_channel_difference"
LIT_MEAN_ABSOLUTE_RGB_DIFFERENCE = "mean_absolute_rgb_difference"
LIT_DISABLED = "disabled"
LIT_AUTOMATIC = "automatic"
LIT_SCREEN = "screen"
LIT_VARIANTS = "variants"
LIT_ID = "id"
LIT_CAPTURES = "captures"
LIT_MAIN = "__main__"
LIT_UTF_8 = "utf-8"
LIT_STORE_TRUE = "store_true"
LIT_APPEND = "append"


REFERENCES = (
    LIT_DUPLICATE_SINGLE_COOL, LIT_DUPLICATE_SINGLE_WARM,
    LIT_DUPLICATE_SINGLE_COOL_TINTED, LIT_DUPLICATE_SINGLE_WARM_TINTED,
)
MATCHES = {
    "coincident": LIT_DUPLICATE_SINGLE_COOL,
    "coincident_reversed": LIT_DUPLICATE_SINGLE_COOL,
    "coincident_priority_swap": LIT_DUPLICATE_SINGLE_WARM,
    "coincident_identical": LIT_DUPLICATE_SINGLE_COOL,
    LIT_COINCIDENT_TINTED: LIT_DUPLICATE_SINGLE_COOL_TINTED,
    "coincident_tinted_reversed": LIT_DUPLICATE_SINGLE_COOL_TINTED,
    "coincident_tinted_priority_swap": LIT_DUPLICATE_SINGLE_WARM_TINTED,
    LIT_COINCIDENT_TINTED_IDENTICAL: LIT_DUPLICATE_SINGLE_COOL_TINTED,
}
RETAINED_CASES = ("near_coincident", LIT_COINCIDENT_PRESERVED)
OPTICAL_CASES = REFERENCES + tuple(MATCHES) + RETAINED_CASES
DISABLED_CASES = (
    LIT_DUPLICATE_SINGLE_COOL, LIT_DUPLICATE_SINGLE_COOL_TINTED, LIT_COINCIDENT_TINTED_IDENTICAL,
    LIT_COINCIDENT_TINTED, LIT_COINCIDENT_PRESERVED,
)
MAXIMUM_CHANNEL_ERROR = 2
MAXIMUM_MEAN_ERROR = 0.02
MINIMUM_DISTINCT_PIXELS = 64


def pixel_difference(reference, output):
    if reference[:2] != output[:2]:
        raise SmokeFailure("duplicate comparison framebuffer dimensions differ")
    width, height, before_rows = reference
    after_rows = output[2]
    if width <= 0 or height <= 0 or len(before_rows) != height or len(after_rows) != height:
        raise SmokeFailure("duplicate comparison framebuffer is malformed")
    changed = total = maximum = 0
    for before_row, after_row in zip(before_rows, after_rows):
        if len(before_row) != width or len(after_row) != width:
            raise SmokeFailure("duplicate comparison framebuffer row is malformed")
        for before, after in zip(before_row, after_row):
            deltas = tuple(abs(a - b) for a, b in zip(before, after))
            largest = max(deltas)
            maximum = max(maximum, largest)
            changed += largest > MAXIMUM_CHANNEL_ERROR
            total += sum(deltas)
    return {
        "pixels": width * height,
        LIT_PIXELS_OVER_CHANNEL_TOLERANCE: changed,
        LIT_MAXIMUM_CHANNEL_DIFFERENCE: maximum,
        LIT_MEAN_ABSOLUTE_RGB_DIFFERENCE: total / (width * height * 3),
    }


def require_match(reference, output, label):
    metrics = pixel_difference(reference, output)
    if metrics[LIT_MAXIMUM_CHANNEL_DIFFERENCE] > MAXIMUM_CHANNEL_ERROR or metrics[LIT_MEAN_ABSOLUTE_RGB_DIFFERENCE] > MAXIMUM_MEAN_ERROR:
        raise SmokeFailure(f"{label}: duplicate does not match its selected single volume: {json.dumps(metrics)}")
    return metrics


def require_distinct(reference, output, label):
    metrics = pixel_difference(reference, output)
    if metrics[LIT_PIXELS_OVER_CHANNEL_TOLERANCE] < MINIMUM_DISTINCT_PIXELS:
        raise SmokeFailure(f"{label}: control is not distinguishable in at least {MINIMUM_DISTINCT_PIXELS} pixels: {json.dumps(metrics)}")
    return metrics


def validate_captures(directory, variants):
    results = []

    def compare(first_case, first_variant, second_case, second_variant, equal):
        first = read_bmp_24_rows(directory / f"{first_case}_{first_variant}.bmp")
        second = read_bmp_24_rows(directory / f"{second_case}_{second_variant}.bmp")
        label = f"{second_case}/{second_variant} vs {first_case}/{first_variant}"
        metrics = (require_match if equal else require_distinct)(first, second, label)
        results.append({"reference": f"{first_case}_{first_variant}", "candidate": f"{second_case}_{second_variant}",
            "expectation": "match" if equal else "distinct", **metrics})

    for variant in variants:
        compare(LIT_DUPLICATE_SINGLE_COOL, LIT_DISABLED, LIT_DUPLICATE_SINGLE_COOL, variant, False)
        for suffix in ("", "_tinted"):
            compare(LIT_DUPLICATE_SINGLE_COOL + suffix, variant, LIT_DUPLICATE_SINGLE_WARM + suffix, variant, False)
        for candidate, reference in MATCHES.items():
            compare(reference, variant, candidate, variant, True)
        for candidate in RETAINED_CASES:
            compare(LIT_DUPLICATE_SINGLE_COOL_TINTED, variant, candidate, variant, False)
    for candidate in (LIT_COINCIDENT_TINTED_IDENTICAL, LIT_COINCIDENT_TINTED):
        compare(LIT_DUPLICATE_SINGLE_COOL_TINTED, LIT_DISABLED, candidate, LIT_DISABLED, True)
    compare(LIT_DUPLICATE_SINGLE_COOL_TINTED, LIT_DISABLED, LIT_COINCIDENT_PRESERVED, LIT_DISABLED, False)
    return results


def parse_args(argv):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--working-directory", type=Path, required=True)
    parser.add_argument("--output-directory", type=Path, required=True)
    parser.add_argument("--logserver-executable", type=Path)
    parser.add_argument("--variants", type=lambda value: parse_selection(value, (LIT_AUTOMATIC, LIT_SCREEN), LIT_VARIANTS),
        default=(LIT_AUTOMATIC, LIT_SCREEN), help="Optical routes to verify; defaults to automatic,screen.")
    parser.add_argument("--require-hardware", action=LIT_STORE_TRUE)
    parser.add_argument("--frames", type=int, default=16)
    parser.add_argument("--timeout", type=float, default=60.0)
    parser.add_argument("--application-arg", action=LIT_APPEND, default=[])
    args = parser.parse_args(argv)
    if args.frames <= 0 or args.timeout <= 0:
        parser.error("frames and timeout must be positive")
    args.output_directory = args.output_directory.resolve()
    return args


def main(argv):
    args = parse_args(argv)
    args.output_directory.mkdir(parents=True, exist_ok=True)
    entries = {}
    try:
        for variant, cases in [(route, OPTICAL_CASES) for route in args.variants] + [(LIT_DISABLED, DISABLED_CASES)]:
            for case_id in cases:
                frame = capture(args, case_id, variant)
                if frame is None:
                    print(f"SKIP: framebuffer capture unavailable for {case_id}/{variant}", file=sys.stderr)
                    return SKIP_EXIT_CODE
                del frame
                if case_id not in entries:
                    title, description, note = CASES[case_id]
                    entries[case_id] = {LIT_ID: case_id, "title": title, "description": description, "note": note, LIT_CAPTURES: []}
                entries[case_id][LIT_CAPTURES].append({LIT_ID: variant, "file": f"{case_id}_{variant}.bmp"})
        labels = dict(VARIANT_LABELS)
        if args.require_hardware:
            labels[LIT_AUTOMATIC] = "Hardware RT (per-pixel fallback allowed)"
        manifest = {"captured_at": datetime.now(timezone.utc).strftime("%Y-%m-%d %H:%M UTC"),
            "frames": args.frames, "hardware_required": args.require_hardware, "application_args": args.application_arg,
            LIT_VARIANTS: list(args.variants) + [LIT_DISABLED], "variant_labels": labels, "limits": COMMON_LIMIT,
            "image_count": sum(len(entry[LIT_CAPTURES]) for entry in entries.values()), "cases": list(entries.values()),
            "validation": "Matched single-volume comparisons, insertion-order and priority checks, authored coincidence modes, and independent-layer controls."}
        write_gallery(args.output_directory, manifest)
        results = validate_captures(args.output_directory, args.variants)
        report = {"maximum_channel_error": MAXIMUM_CHANNEL_ERROR, "maximum_mean_error": MAXIMUM_MEAN_ERROR,
            "minimum_distinct_pixels": MINIMUM_DISTINCT_PIXELS, "comparisons": results}
        (args.output_directory / "duplicate_comparisons.json").write_text(json.dumps(report, indent=2) + "\n", encoding=LIT_UTF_8)
        print(f"PASS: {len(results)} duplicate/control comparisons; gallery: {args.output_directory / 'gallery.html'}", flush=True)
        return 0
    except (OSError, SmokeFailure, subprocess.TimeoutExpired) as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        return 1


if __name__ == LIT_MAIN:
    raise SystemExit(main(sys.argv[1:]))
