#!/usr/bin/env python3
"""Validate clear-glass distortion and AVBOIT foreground preservation using real frames."""

import argparse
import json
import os
from pathlib import Path
import subprocess
import sys

from window_capture_smoke import SKIP_EXIT_CODE, SmokeFailure, read_bmp_24_rows, write_bmp_24


GLASS_REGION = (0.39, 0.38, 0.61, 0.58)
EXTERIOR_REGIONS = ((0.29, 0.35, 0.35, 0.68), (0.65, 0.35, 0.71, 0.68))
FOREGROUND_REGION = (0.30, 0.58, 0.70, 0.67)


def region_pixels(width, height, region):
    left, top, right, bottom = region
    for y in range(int(height * top), int(height * bottom)):
        for x in range(int(width * left), int(width * right)):
            yield x, y


def red_foreground(pixel):
    red, green, blue = pixel
    return red >= 110 and red >= green + 45 and red >= blue + 45


def compare_refraction_frames(reference, refracted):
    width, height, baseline = reference
    if (width, height) != refracted[:2]:
        raise SmokeFailure("refraction captures have different dimensions")
    output = refracted[2]
    if width < 320 or height < 240:
        raise SmokeFailure("refraction capture is too small to resolve stripe displacement")

    glass_count = 0
    dark_to_light = 0
    light_to_dark = 0
    for x, y in region_pixels(width, height, GLASS_REGION):
        glass_count += 1
        before, after = baseline[y][x], output[y][x]
        # Both directions are required: merely attenuating, tinting, or replacing
        # clear glass with black cannot pass as geometric stripe displacement.
        if max(before) - min(before) < 25 and max(after) - min(after) < 25:
            before_luma, after_luma = sum(before) / 3.0, sum(after) / 3.0
            dark_to_light += before_luma < 85 and after_luma > 155
            light_to_dark += before_luma > 170 and after_luma < 100

    exterior_count = 0
    exterior_changed = 0
    for region in EXTERIOR_REGIONS:
        for x, y in region_pixels(width, height, region):
            exterior_count += 1
            exterior_changed += max(abs(a - b) for a, b in zip(baseline[y][x], output[y][x])) > 8

    foreground_before = 0
    foreground_retained = 0
    foreground_added = 0
    for x, y in region_pixels(width, height, FOREGROUND_REGION):
        before, after = red_foreground(baseline[y][x]), red_foreground(output[y][x])
        foreground_before += before
        foreground_retained += before and after
        foreground_added += after and not before

    metrics = {
        "glass_pixels": glass_count,
        "dark_to_light_pixels": dark_to_light,
        "light_to_dark_pixels": light_to_dark,
        "exterior_pixels": exterior_count,
        "exterior_changed_pixels": exterior_changed,
        "foreground_reference_pixels": foreground_before,
        "foreground_retained_pixels": foreground_retained,
        "foreground_added_pixels": foreground_added,
    }
    failures = []
    if min(dark_to_light, light_to_dark) < max(30, glass_count * 0.005):
        failures.append("clear glass did not displace both dark and light stripe edges")
    if exterior_changed > max(16, exterior_count * 0.01):
        failures.append("refraction changed pixels outside the glass silhouette")
    if foreground_before < width * height * 0.002:
        failures.append("reference frame is missing the foreground AVBOIT red panel")
    elif foreground_retained < foreground_before * 0.97 or foreground_added > foreground_before * 0.03:
        failures.append("foreground AVBOIT panel moved, disappeared, or bent through the glass")
    if failures:
        raise SmokeFailure("; ".join(failures) + "\n" + json.dumps(metrics, indent=2))
    return metrics


def capture_variant(args, name, enabled, hardware, expected_dispatch):
    path = args.output_directory / f"refraction_{name}.bmp"
    command = [
        sys.executable, str(Path(__file__).with_name("window_capture_smoke.py")),
        "--executable", str(args.executable), "--working-directory", str(args.working_directory),
        "--output", str(path), "--application-capture",
        "--application-capture-frame-count", str(args.frames), "--timeout", str(args.timeout),
        "--expect-log-message", "RefractionSmokeProject: zero-coverage clear sphere + opaque stripes + foreground/background AVBOIT panels created",
        "--expect-log-message", "RefractionSmokeProject: refraction " + ("enabled" if enabled else "disabled"),
        "--expect-log-message", "RefractionSmokeProject: shutdown",
    ]
    if args.logserver_executable:
        command += ["--logserver-executable", str(args.logserver_executable)]
    else:
        command += ["--no-logserver"]
    if expected_dispatch:
        command += ["--expect-log-message", expected_dispatch]
    else:
        command += ["--reject-log-message", "AVBOIT refraction resolve:"]
    for argument in args.application_arg:
        command.append("--application-arg=" + argument)

    env = os.environ.copy()
    env["NWB_REFRACTION_SMOKE_ENABLED"] = "1" if enabled else "0"
    env["NWB_REFRACTION_SMOKE_HARDWARE"] = "1" if hardware else "0"
    env["NWB_RENDERER_BASELINE_FIXED_DELTA_SECONDS"] = "0.016666667"
    # A previously pinned interactive smoke frame must not suspend the readback.
    env.pop("NWB_RENDERER_BASELINE_CAPTURE_FREEZE_FRAME", None)
    result = subprocess.run(command, env=env, check=False, timeout=args.timeout + 30)
    if result.returncode == SKIP_EXIT_CODE:
        return None
    if result.returncode != 0:
        raise SmokeFailure(f"refraction {name} capture failed with exit {result.returncode}")
    return read_bmp_24_rows(path)


def parse_args(argv):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", required=True, type=Path)
    parser.add_argument("--working-directory", required=True, type=Path)
    parser.add_argument("--output-directory", required=True, type=Path)
    parser.add_argument("--logserver-executable", type=Path)
    parser.add_argument("--frames", type=int, default=16)
    parser.add_argument("--timeout", type=float, default=60.0)
    parser.add_argument("--require-hardware", action="store_true", help="Require actual hardware ray-query dispatch in the automatic variant.")
    parser.add_argument("--application-arg", action="append", default=[])
    args = parser.parse_args(argv)
    if args.frames <= 0 or args.timeout <= 0:
        parser.error("frames and timeout must be positive")
    args.output_directory = args.output_directory.resolve()
    return args


def main(argv):
    args = parse_args(argv)
    args.output_directory.mkdir(parents=True, exist_ok=True)
    try:
        baseline = capture_variant(args, "disabled", False, True, None)
        if baseline is None:
            return SKIP_EXIT_CODE
        metrics = {}
        for name, hardware, diagnostic in (
            ("automatic", True, "AVBOIT refraction resolve: hardware" if args.require_hardware else "AVBOIT refraction resolve:"),
            ("screen_space", False, "AVBOIT refraction resolve: screen-space"),
        ):
            result = capture_variant(args, name, True, hardware, diagnostic)
            if result is None:
                return SKIP_EXIT_CODE
            metrics[name] = compare_refraction_frames(baseline, result)
            width, height, rows = result
            difference = [[tuple(min(abs(a - b) * 3, 255) for a, b in zip(before, after))
                for before, after in zip(reference_row, output_row)]
                for reference_row, output_row in zip(baseline[2], rows)]
            write_bmp_24(args.output_directory / f"refraction_{name}_difference.bmp", width, height, difference)
        (args.output_directory / "refraction_metrics.json").write_text(json.dumps(metrics, indent=2) + "\n", encoding="utf-8")
        print("PASS: clear-glass displacement, foreground AVBOIT preservation, exterior stability, and both resolve routes\n" + json.dumps(metrics, indent=2))
        return 0
    except (SmokeFailure, subprocess.TimeoutExpired) as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
