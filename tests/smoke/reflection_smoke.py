#!/usr/bin/env python3
"""Validate offscreen ray-traced reflection from actual application framebuffer readbacks."""

import argparse
import base64
import html
import json
import math
import os
from pathlib import Path
import subprocess
import sys

from refraction_gallery_smoke import png_rgb_bytes
from window_capture_smoke import SKIP_EXIT_CODE, SmokeFailure, read_bmp_24_rows


CASES = ("offscreen", "moved", "opaque_glass")
MODES = ("disabled", "screen", "hardware", "hybrid")
CAPTURES = (("offscreen", "disabled"), ("offscreen", "screen"), ("offscreen", "hardware"),
    ("moved", "disabled"), ("moved", "hardware"), ("opaque_glass", "disabled"), ("opaque_glass", "hardware"))
OPAQUE_REGION = (0.28, 0.36, 0.43, 0.57)
GLASS_REGION = (0.57, 0.36, 0.72, 0.57)
FOREGROUND_REGION = (0.21, 0.59, 0.79, 0.65)
EXTERIOR_REGIONS = ((0.02, 0.05, 0.20, 0.95), (0.80, 0.05, 0.98, 0.95), (0.2, 0.05, 0.8, 0.24))


def validate_frame(frame):
    width, height, rows = frame
    if width < 320 or height < 240 or len(rows) != height or any(len(row) != width for row in rows):
        raise SmokeFailure("reflection framebuffer is malformed or too small")
    return width, height, rows


def region_pixels(width, height, region):
    left, top, right, bottom = region
    for y in range(int(top * height), int(bottom * height)):
        for x in range(int(left * width), int(right * width)):
            yield x, y


def marker_color(pixel, color):
    channel = 0 if color == "red" else 1
    return pixel[channel] >= 40 and all(pixel[channel] >= pixel[other] + 25 for other in range(3) if other != channel)


def blue_foreground(pixel):
    return pixel[2] >= 110 and pixel[2] >= pixel[0] + 45 and pixel[2] >= pixel[1] + 45


def marker_projection(width, height, color, moved=False):
    # Reflect z=-8 through the z=0 plane. The virtual source is 14 units from the camera at z=-6.
    focal_length = height / (2.0 * math.tan(math.pi / 6.0))
    x, y = (-1.6, 1.0) if color == "red" else (1.6, 2.0)
    x += 0.9 if moved else 0.0
    return width * 0.5 + focal_length * x / 14.0, height * 0.5 - focal_length * (y - 1.4) / 14.0


def analyze_markers(frame, moved=False, required=True):
    width, height, rows = validate_frame(frame)
    results = {}
    for color in ("red", "green"):
        points = [(x, y) for y, row in enumerate(rows) for x, pixel in enumerate(row) if marker_color(pixel, color)]
        if not required:
            if len(points) > 16:
                raise SmokeFailure(f"offscreen {color} marker appeared without a hardware reflection hit ({len(points)} pixels)")
            results[color] = {"pixels": len(points)}
            continue
        projected_radius = height / (2.0 * math.tan(math.pi / 6.0)) * 0.65 / 14.0
        minimum_pixels = max(80, math.pi * projected_radius ** 2 * 0.25)
        if len(points) < minimum_pixels:
            raise SmokeFailure(f"hardware reflection is missing the offscreen {color} marker "
                f"({len(points)} pixels, at least {math.ceil(minimum_pixels)} required by its projected area)")
        expected_x, expected_y = marker_projection(width, height, color, moved)
        radius = projected_radius + height * 0.015
        outside = sum((x - expected_x) ** 2 + (y - expected_y) ** 2 > radius ** 2 for x, y in points)
        centroid_x = sum(x for x, _ in points) / len(points)
        centroid_y = sum(y for _, y in points) / len(points)
        if outside > max(16, len(points) * 0.05):
            raise SmokeFailure(f"{color} reflection is outside its geometrically predicted marker region ({outside}/{len(points)} pixels)")
        if math.hypot(centroid_x - expected_x, centroid_y - expected_y) > height * 0.025:
            raise SmokeFailure(f"{color} reflection centroid does not match the virtual marker position")
        results[color] = {"pixels": len(points), "minimum_projected_area_pixels": math.ceil(minimum_pixels),
            "centroid": [centroid_x, centroid_y], "expected_centroid": [expected_x, expected_y],
            "outside_predicted_region": outside}
    return results


def compare_marker_motion(original, moved):
    if original[:2] != moved[:2]:
        raise SmokeFailure("marker motion captures have different framebuffer dimensions")
    before = analyze_markers(original)
    after = analyze_markers(moved, moved=True)
    expected_shift = original[1] / (2.0 * math.tan(math.pi / 6.0)) * 0.9 / 14.0
    for color in ("red", "green"):
        dx = after[color]["centroid"][0] - before[color]["centroid"][0]
        dy = after[color]["centroid"][1] - before[color]["centroid"][1]
        if abs(dx - expected_shift) > original[1] * 0.014 or abs(dy) > original[1] * 0.014:
            raise SmokeFailure(f"{color} reflection did not follow the authored marker movement")
        after[color]["measured_motion"] = [dx, dy]
        after[color]["expected_motion_x"] = expected_shift
    return {"original": before, "moved": after}


def compare_opaque_glass(reference, reflected):
    width, height, before = validate_frame(reference)
    validate_frame(reflected)
    if reference[:2] != reflected[:2]:
        raise SmokeFailure("opaque/glass captures have different framebuffer dimensions")
    after = reflected[2]
    metrics = {}
    for name, region in (("opaque", OPAQUE_REGION), ("glass", GLASS_REGION)):
        added = 0
        reference_colored = 0
        region_count = 0
        for x, y in region_pixels(width, height, region):
            region_count += 1
            old_color = marker_color(before[y][x], "red") or marker_color(before[y][x], "green")
            new_color = marker_color(after[y][x], "red") or marker_color(after[y][x], "green")
            reference_colored += old_color
            added += new_color and not old_color
        if reference_colored > 16 or added < max(48, region_count * 0.02):
            raise SmokeFailure(f"{name} sphere did not acquire localized offscreen reflection colors ({added} added pixels)")
        metrics[name + "_new_reflected_pixels"] = added
    foreground = [(x, y) for x, y in region_pixels(width, height, FOREGROUND_REGION) if blue_foreground(before[y][x])]
    retained = sum(blue_foreground(after[y][x]) for x, y in foreground)
    added_foreground = sum(blue_foreground(after[y][x]) and not blue_foreground(before[y][x])
        for x, y in region_pixels(width, height, FOREGROUND_REGION))
    if len(foreground) < width * height * 0.002 or retained < len(foreground) * 0.97 or added_foreground > len(foreground) * 0.03:
        raise SmokeFailure("reflection displaced, expanded, or removed the foreground AVBOIT strip")
    exterior_count = 0
    exterior_changed = 0
    for region in EXTERIOR_REGIONS:
        for x, y in region_pixels(width, height, region):
            exterior_count += 1
            exterior_changed += max(abs(a - b) for a, b in zip(before[y][x], after[y][x])) > 8
    if exterior_changed > max(16, exterior_count * 0.005):
        raise SmokeFailure("reflection changed pixels outside the opaque/glass silhouettes")
    metrics.update(foreground_pixels=len(foreground), foreground_retained=retained,
        foreground_added=added_foreground, exterior_changed=exterior_changed)
    return metrics


def capture_environment(case, mode):
    env = os.environ.copy()
    for name in tuple(env):
        if name.startswith("NWB_REFLECTION_SMOKE_") or name.startswith("NWB_REFRACTION_SMOKE_"):
            env.pop(name)
    env.pop("NWB_RENDERER_BASELINE_CAPTURE_FREEZE_FRAME", None)
    env.pop("NWB_GPU_TIMING_FILE", None)
    env["NWB_REFLECTION_SMOKE_CASE"] = case
    env["NWB_REFLECTION_SMOKE_MODE"] = mode
    env["NWB_RENDERER_BASELINE_FIXED_DELTA_SECONDS"] = "0.016666667"
    return env


def capture(args, case, mode):
    output = args.output_directory / f"{case}_{mode}.bmp"
    command = [sys.executable, str(Path(__file__).with_name("window_capture_smoke.py")),
        "--executable", str(args.executable), "--working-directory", str(args.working_directory),
        "--output", str(output), "--application-capture", "--application-capture-frame-count", str(args.frames),
        "--timeout", str(args.timeout), "--expect-log-message", f"ReflectionSmokeProject: case {case} created",
        "--expect-log-message", f"ReflectionSmokeProject: reflection mode {mode}",
        "--expect-log-message", "ReflectionSmokeProject: shutdown"]
    if args.logserver_executable:
        command += ["--logserver-executable", str(args.logserver_executable)]
    else:
        command.append("--no-logserver")
    if mode in ("hardware", "hybrid"):
        command += ["--expect-log-message", "Reflection resolve: hardware"]
        command += ["--expect-log-message" if args.require_hardware else "--skip-log-message",
            "ReflectionSmokeProject: hardware available" if args.require_hardware else "ReflectionSmokeProject: hardware unavailable"]
    elif mode == "disabled":
        command += ["--expect-log-message", "Reflection resolve: disabled",
            "--reject-log-message", "Reflection resolve: hardware"]
    elif mode == "screen":
        command += ["--expect-log-message", "Reflection resolve: environment",
            "--reject-log-message", "Reflection resolve: hardware"]
    if case == "opaque_glass":
        command += ["--expect-log-message", "AVBOIT refraction resolve:"]
    command.extend("--application-arg=" + argument for argument in args.application_arg)
    print(f"Capturing {case}/{mode} ({args.frames} presentation frames)...", flush=True)
    # The child owns app/logserver cleanup: leave time for startup, graceful exit, kill fallback, and log drain.
    result = subprocess.run(command, env=capture_environment(case, mode), check=False, timeout=args.timeout + 90)
    if result.returncode == SKIP_EXIT_CODE:
        return None
    if result.returncode:
        raise SmokeFailure(f"reflection {case}/{mode} capture failed with exit {result.returncode}")
    frame = read_bmp_24_rows(output)
    validate_frame(frame)
    if frame[:2] != (960, 720):
        raise SmokeFailure(f"expected actual 960x720 framebuffer, got {frame[0]}x{frame[1]}")
    return frame


def write_report(args, captures, metrics=None):
    metadata = {"frames": args.frames, "frame_source": "actual application framebuffer readback", "size": [960, 720],
        "hardware_required": args.require_hardware, "application_args": args.application_arg,
        "settings": {"environment_top": [0, 0, 0], "environment_bottom": [0, 0, 0],
            "max_hardware_rays_per_frame": 1382400, "roughness": 0, "glass_ior": 3.8},
        "captures": [{"case": case, "mode": mode, "file": f"{case}_{mode}.bmp"} for case, mode in captures],
        "metrics": metrics, "limitations": "Single-bounce smooth reflection baseline. Hardware hit shading is approximate; no SSR hit, roughness, history, or performance claim is established here."}
    (args.output_directory / "reflection_manifest.json").write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")
    cards = []
    for item in metadata["captures"]:
        bmp = args.output_directory / item["file"]
        png = png_rgb_bytes(read_bmp_24_rows(bmp))
        bmp.with_suffix(".png").write_bytes(png)
        label = html.escape(item["case"] + " / " + item["mode"])
        cards.append(f'<article><h2>{label}</h2><a href="{html.escape(item["file"])}">Raw BMP</a>'
            f'<img alt="Actual {label} framebuffer" src="data:image/png;base64,{base64.b64encode(png).decode("ascii")}"></article>')
    document = '<!doctype html><html lang="en"><meta charset="utf-8"><title>Reflection smoke captures</title>'
    document += '<style>body{background:#141922;color:#e7edf5;font:16px system-ui;margin:28px}main{display:grid;grid-template-columns:repeat(auto-fit,minmax(440px,1fr));gap:20px}article{background:#202937;padding:16px}img{display:block;width:100%;margin-top:12px}a{color:#88c9ff}pre{white-space:pre-wrap}h2{font-size:19px}</style>'
    document += '<h1>Reflection smoke - actual renderer captures</h1><p>' + html.escape(metadata["limitations"]) + '</p>'
    document += '<p>Images contain unchanged framebuffer RGB pixels, converted losslessly to PNG. Raw BMPs remain beside this report. Glass uses IOR 3.8 and its matching dielectric Fresnel F0.</p><main>'
    document += ''.join(cards) + '</main><pre>' + html.escape(json.dumps(metrics, indent=2) if metrics else 'Capture evidence; visual assertions have not passed yet.') + '</pre></html>'
    (args.output_directory / "reflection.html").write_text(document, encoding="utf-8")


def parse_args(argv):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", required=True, type=Path)
    parser.add_argument("--working-directory", required=True, type=Path)
    parser.add_argument("--output-directory", required=True, type=Path)
    parser.add_argument("--logserver-executable", type=Path)
    parser.add_argument("--frames", type=int, default=16)
    parser.add_argument("--timeout", type=float, default=60.0)
    parser.add_argument("--require-hardware", action="store_true")
    parser.add_argument("--application-arg", action="append", default=[])
    args = parser.parse_args(argv)
    if args.frames <= 0 or args.timeout <= 0:
        parser.error("frames and timeout must be positive")
    args.output_directory = args.output_directory.resolve()
    return args


def main(argv):
    args = parse_args(argv)
    args.output_directory.mkdir(parents=True, exist_ok=True)
    completed = []
    try:
        for case, mode in CAPTURES:
            frame = capture(args, case, mode)
            if frame is None:
                print("SKIP: required reflection hardware or framebuffer readback is unavailable", file=sys.stderr)
                return SKIP_EXIT_CODE
            completed.append((case, mode))
            del frame
        write_report(args, completed)
        metrics = {}
        for case, mode in (("offscreen", "disabled"), ("offscreen", "screen"), ("moved", "disabled")):
            metrics[f"{case}_{mode}"] = analyze_markers(read_bmp_24_rows(args.output_directory / f"{case}_{mode}.bmp"), required=False)
        metrics["marker_motion"] = compare_marker_motion(
            read_bmp_24_rows(args.output_directory / "offscreen_hardware.bmp"),
            read_bmp_24_rows(args.output_directory / "moved_hardware.bmp"))
        metrics["opaque_glass"] = compare_opaque_glass(
            read_bmp_24_rows(args.output_directory / "opaque_glass_disabled.bmp"),
            read_bmp_24_rows(args.output_directory / "opaque_glass_hardware.bmp"))
        write_report(args, completed, metrics)
        print("PASS: offscreen marker geometry and movement, opaque/glass reflection, foreground AVBOIT and exterior stability\n"
            + json.dumps(metrics, indent=2), flush=True)
        return 0
    except (SmokeFailure, OSError, subprocess.TimeoutExpired) as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
