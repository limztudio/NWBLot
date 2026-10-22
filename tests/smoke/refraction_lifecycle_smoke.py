#!/usr/bin/env python3
"""Validate refraction tile retirement, movement, edge coverage, and reactivation on real frames."""

import argparse
import json
import math
import os
from pathlib import Path
import re
import subprocess
import sys

from refraction_capture_smoke import compare_refraction_frames, red_foreground, region_pixels, FOREGROUND_REGION
from window_capture_smoke import SKIP_EXIT_CODE, SmokeFailure, read_bmp_24_rows


PHASES = ("baseline", "active", "moved", "edge", "empty", "disabled", "reenabled")
EXTENT = (967, 727)
# Camera distance 6, vertical FOV 60 degrees, aspect 967/727, sphere center X1.9
# and radius 1.15 project to approximately X[.58,.85], Y[.33,.67].
MOVED_REGION = (.57, .31, .86, .68)
# This part of the moved silhouette lies beyond the original sphere's X~.63 rim.
MOVED_NEW_REGION = (.77, .38, .86, .58)
RETIRED_REGION = (.42, .38, .55, .58)
# The same projection for the edge sphere at X4.2 starts near X.82.
EDGE_REGION = (.81, .31, 1.0, .68)


def require_unchanged(reference, candidate, region, label):
    width, height, rows = reference
    count = changed = 0
    for x, y in region_pixels(width, height, region):
        count += 1
        changed += max(abs(a - b) for a, b in zip(rows[y][x], candidate[2][y][x])) > 8
    if changed > max(16, count * .01):
        raise SmokeFailure(f"{label} retained stale or changed output: {changed}/{count} pixels")
    return {"pixels": count, "changed_pixels": changed}


def require_displacement(reference, candidate, region, label):
    width, height, rows = reference
    count = dark_to_light = light_to_dark = 0
    for x, y in region_pixels(width, height, region):
        count += 1
        before, after = rows[y][x], candidate[2][y][x]
        if max(before) - min(before) < 25 and max(after) - min(after) < 25:
            before_luma, after_luma = sum(before) / 3, sum(after) / 3
            dark_to_light += before_luma < 85 and after_luma > 155
            light_to_dark += before_luma > 170 and after_luma < 100
    if min(dark_to_light, light_to_dark) < max(12, count * .005):
        raise SmokeFailure(f"{label} did not displace both stripe directions: {dark_to_light}, {light_to_dark}")
    return {"pixels": count, "dark_to_light_pixels": dark_to_light, "light_to_dark_pixels": light_to_dark}


def expected_sheet_fallback_gray(before, x, y, width, height):
    # Fixed fixture: eye(0,1.4,-6), sphere(4.2,1.4,0), radius1.15, IOR1.5.
    # Match the two Schlick factors in nwbRefractionStraightFallback. Presentation
    # applies the default Reinhard shoulder then sRGB; exposure cancels when the
    # disabled reference is inverted. A 1.5-code tolerance below covers reference
    # and result quantization plus the faceted sphere's interpolated normals.
    tangent = math.tan(math.pi / 6)
    ray_x = (2 * (x + .5) / width - 1) * tangent * width / height
    ray_y = (1 - 2 * (y + .5) / height) * tangent
    projection = (4.2 * ray_x + 6) / math.sqrt(ray_x * ray_x + ray_y * ray_y + 1)
    discriminant = projection * projection - (4.2 * 4.2 + 36 - 1.15 * 1.15)
    if discriminant <= 0:
        return None
    cosine = min(1, math.sqrt(discriminant) / 1.15)
    transmission = (1 - (.04 + .96 * (1 - cosine) ** 5)) ** 2
    encoded = before / 255
    linear = encoded / 12.92 if encoded <= .04045 else ((encoded + .055) / 1.055) ** 2.4
    attenuated = transmission * linear / (1 - linear + transmission * linear)
    return 255 * (12.92 * attenuated if attenuated <= .0031308 else 1.055 * attenuated ** (1 / 2.4) - .055)


def require_partial_tile_displacement(reference, candidate, allow_screen_fallback=False):
    width, height, rows = reference
    region = (960 / width, .38, 1, .58)
    available_dark = available_light = dark_to_light = light_to_dark = 0
    for x, y in region_pixels(width, height, region):
        before, after = rows[y][x], candidate[2][y][x]
        if max(before) - min(before) >= 25:
            continue
        before_luma, after_luma = sum(before) / 3, sum(after) / 3
        available_dark += before_luma < 85
        available_light += before_luma > 170
        if max(after) - min(after) < 25:
            dark_to_light += before_luma < 85 and after_luma > 155
            light_to_dark += before_luma > 170 and after_luma < 100
    # A seven-column tail can lie wholly inside one background stripe. The wider
    # edge region proves both polarities; the tail must displace every polarity
    # that has enough reference pixels to test, rather than invent missing ones.
    if max(available_dark, available_light) < 12:
        raise SmokeFailure("partial right tile lacks measurable baseline stripe pixels")
    metrics = {"available_dark_pixels": available_dark, "available_light_pixels": available_light, "dark_to_light_pixels": dark_to_light, "light_to_dark_pixels": light_to_dark}
    displaced = all(available < 12 or changed >= max(12, available * .005) for available, changed in ((available_dark, dark_to_light), (available_light, light_to_dark)))
    if displaced:
        return {**metrics, "mode": "displaced_stripes"}
    if allow_screen_fallback:
        tested = accepted = 0
        for x, y in region_pixels(width, height, region):
            before, after = rows[y][x], candidate[2][y][x]
            if max(before) - min(before) > 1 or min(before) < 20:
                continue
            expected = expected_sheet_fallback_gray(sum(before) / 3, x, y, width, height)
            if expected is None:
                continue
            tested += 1
            # A missing tile restores the unchanged reference; a black or colored
            # tail cannot imitate the clear sheet's small neutral Fresnel loss.
            accepted += max(after) - min(after) <= 1 and min(after) > 0 and sum(before) / 3 - sum(after) / 3 >= 1 and abs(sum(after) / 3 - expected) <= 1.5
        if tested >= 12 and accepted >= tested * .95:
            return {**metrics, "mode": "bounded_sheet_fallback", "fallback_reference_pixels": tested, "fallback_accepted_pixels": accepted, "encoded_tolerance": 1.5}
    raise SmokeFailure(f"partial right tile lacked displaced stripes or qualified screen fallback: {dark_to_light}/{available_dark} dark, {light_to_dark}/{available_light} light")


def require_foreground(reference, candidate, label):
    width, height, rows = reference
    before_count = retained = added = 0
    for x, y in region_pixels(width, height, FOREGROUND_REGION):
        before, after = red_foreground(rows[y][x]), red_foreground(candidate[2][y][x])
        before_count += before
        retained += before and after
        added += after and not before
    if before_count < width * height * .002 or retained < before_count * .97 or added > before_count * .03:
        raise SmokeFailure(f"{label} moved or lost the foreground AVBOIT panel")
    return {"reference_pixels": before_count, "retained_pixels": retained, "added_pixels": added}


def validate_lifecycle_log(text):
    captures = re.findall(r"^RefractionLifecycle: captured (\w+) graphics frame (\d+)$", text.replace("\r\n", "\n"), re.MULTILINE)
    if tuple(name for name, _ in captures) != PHASES:
        raise SmokeFailure("lifecycle capture phases are missing, repeated, or reordered")
    frames = [int(frame) for _, frame in captures]
    if any(after <= before for before, after in zip(frames, frames[1:])):
        raise SmokeFailure("lifecycle captures did not advance completed graphics frames")
    return dict(zip(PHASES, frames))


def analyze_lifecycle(frames, trace_route="hardware"):
    if set(frames) != set(PHASES) or any(frame[:2] != EXTENT for frame in frames.values()):
        raise SmokeFailure("lifecycle requires every capture at the odd 967x727 framebuffer extent")
    baseline = frames["baseline"]
    metrics = {
        "active": compare_refraction_frames(baseline, frames["active"]),
        "reenabled": compare_refraction_frames(baseline, frames["reenabled"]),
        "moved": require_displacement(baseline, frames["moved"], MOVED_REGION, "moved glass"),
        "newly_active_tiles": require_displacement(baseline, frames["moved"], MOVED_NEW_REGION, "newly active moved tiles"),
        "retired_tiles": require_unchanged(baseline, frames["moved"], RETIRED_REGION, "moved glass original tiles"),
        "edge": require_displacement(baseline, frames["edge"], EDGE_REGION, "edge glass"),
        "empty": require_unchanged(baseline, frames["empty"], (0, 0, 1, 1), "empty frame"),
        "disabled": require_unchanged(baseline, frames["disabled"], (0, 0, 1, 1), "disabled frame"),
        "reactivation": require_unchanged(frames["active"], frames["reenabled"], (0, 0, 1, 1), "reenabled frame"),
    }
    # The seven trailing columns are the partial 8x8 tile, rather than merely an odd image size.
    tail = require_partial_tile_displacement(baseline, frames["edge"], allow_screen_fallback=trace_route == "screen-space")
    metrics["partial_tile"] = tail
    metrics["foreground"] = {name: require_foreground(baseline, frames[name], name) for name in PHASES[1:]}
    return metrics


def capture_route(args, name, hardware):
    directory = args.output_directory / name
    directory.mkdir(parents=True, exist_ok=True)
    output = directory / "lifecycle.bmp"
    log_path = directory / "runtime.log"
    command = [sys.executable, str(Path(__file__).with_name("window_capture_smoke.py")), "--executable", str(args.executable), "--working-directory", str(args.working_directory),
        "--output", str(output), "--log-output", str(log_path), "--application-capture", "--application-capture-frame-count", str(args.frames), "--timeout", str(args.timeout),
        "--expect-log-message", "RefractionLifecycle: complete samples=7", "--expect-log-message", "RefractionSmokeProject: shutdown", "--expect-log-message",
        "AVBOIT refraction resolve: hardware" if args.require_hardware and hardware else "AVBOIT refraction resolve:" if hardware else "AVBOIT refraction resolve: screen-space"]
    command += ["--logserver-executable", str(args.logserver_executable)] if args.logserver_executable else ["--no-logserver"]
    for argument in args.application_arg:
        command.append("--application-arg=" + argument)
    env = os.environ.copy()
    for key in ("NWB_REFRACTION_SMOKE_CASE", "NWB_REFRACTION_SMOKE_GEOMETRY", "NWB_REFRACTION_SMOKE_TIMING", "NWB_RENDERER_BASELINE_CAPTURE_FREEZE_FRAME"):
        env.pop(key, None)
    env.update(NWB_REFRACTION_SMOKE_LIFECYCLE="1", NWB_REFRACTION_SMOKE_EXTENT="npot", NWB_REFRACTION_SMOKE_ENABLED="1", NWB_REFRACTION_SMOKE_HARDWARE="1" if hardware else "0",
        NWB_RENDERER_BASELINE_FIXED_DELTA_SECONDS="0.016666667")
    result = subprocess.run(command, env=env, check=False, timeout=args.timeout + 30)
    if result.returncode == SKIP_EXIT_CODE:
        return None
    if result.returncode != 0:
        raise SmokeFailure(f"{name} lifecycle capture failed with exit {result.returncode}")
    log = log_path.read_text(encoding="utf-8")
    frame_indices = validate_lifecycle_log(log)
    routes = set(re.findall(r"^AVBOIT refraction resolve: (hardware|screen-space)$", log.replace("\r\n", "\n"), re.MULTILINE))
    if len(routes) != 1:
        raise SmokeFailure("lifecycle requires one unambiguous runtime refraction route")
    trace_route = routes.pop()
    frames = {phase: read_bmp_24_rows(output if phase == PHASES[-1] else Path(str(output) + f".{phase}.bmp")) for phase in PHASES}
    return {"graphics_source_frames": frame_indices, "trace_route": trace_route, "images": analyze_lifecycle(frames, trace_route)}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", required=True, type=Path)
    parser.add_argument("--working-directory", required=True, type=Path)
    parser.add_argument("--output-directory", required=True, type=Path)
    parser.add_argument("--logserver-executable", type=Path)
    parser.add_argument("--frames", type=int, default=16)
    parser.add_argument("--timeout", type=float, default=150)
    parser.add_argument("--require-hardware", action="store_true")
    parser.add_argument("--application-arg", action="append", default=[])
    args = parser.parse_args(argv)
    if args.frames <= 0 or args.timeout <= 0:
        parser.error("frames and timeout must be positive")
    args.output_directory = args.output_directory.resolve()
    args.output_directory.mkdir(parents=True, exist_ok=True)
    try:
        metrics = {}
        for name, hardware in (("automatic", True), ("screen_space", False)):
            metrics[name] = capture_route(args, name, hardware)
            if metrics[name] is None:
                return SKIP_EXIT_CODE
        (args.output_directory / "lifecycle_metrics.json").write_text(json.dumps(metrics, indent=2) + "\n", encoding="utf-8")
        print("PASS: refraction movement, partial tiles, stale-output retirement, disabling, reactivation, and AVBOIT foreground preservation")
        return 0
    except (SmokeFailure, OSError, subprocess.TimeoutExpired) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
