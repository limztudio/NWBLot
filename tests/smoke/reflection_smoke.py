#!/usr/bin/env python3
"""Validate screen-space and hardware reflection from actual application framebuffer readbacks."""

import argparse
import base64
import html
import json
import math
import os
from pathlib import Path
import re
import subprocess
import sys

from refraction_gallery_smoke import png_rgb_bytes
from window_capture_smoke import SKIP_EXIT_CODE, SmokeFailure, read_bmp_24_rows


CASES = ("offscreen", "moved", "opaque_glass", "onscreen", "onscreen_moved", "boundary", "floor")
MODES = ("disabled", "screen", "hardware", "hybrid")
BASELINE_CAPTURES = (("offscreen", "disabled"), ("offscreen", "screen"), ("offscreen", "hardware"),
    ("moved", "disabled"), ("moved", "hardware"), ("opaque_glass", "disabled"), ("opaque_glass", "hardware"),
    ("offscreen", "hybrid"))
SCREEN_CAPTURES = (("onscreen", "disabled"), ("onscreen", "screen"), ("onscreen", "hardware"),
    ("onscreen", "hybrid"), ("onscreen_moved", "screen"), ("boundary", "disabled"),
    ("boundary", "screen"), ("boundary", "hybrid"), ("floor", "disabled"), ("floor", "screen"),
    ("floor", "hardware"), ("floor", "hybrid"))
CAPTURES = BASELINE_CAPTURES + SCREEN_CAPTURES
DEFAULT_RAY_BUDGET = 2 * 960 * 720
BUDGET_CAPTURES = (("floor", "hybrid", 0), ("floor", "hybrid", 64))
STATISTICS_FIELDS = ("sequence", "generation", "frame", "mode", "width", "height", "requested_budget",
    "effective_budget", "queue_capacity", "hardware_requested", "hardware_available", "hardware_ready",
    "token_queue", "token_value", "physical_queue", "device_generation", "candidates", "hardware_rays",
    "hardware_hits", "opaque_pixels", "glass_pixels", "fallback_pixels", "screen_attempts", "screen_hits")
COUNTER_FIELDS = STATISTICS_FIELDS[-8:]
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


def panel_projection(width, height, color, case, reflected):
    focal_length = height / (2.0 * math.tan(math.pi / 6.0))
    source_x, source_y, half_height = (-1.7, 1.0, 0.25) if color == "red" else (1.7, 2.0, 0.35)
    if case == "onscreen_moved":
        source_x += 0.25 if color == "red" else -0.25
    elif case == "boundary" and color == "green":
        source_x = 3.0
    # Source z=-3 is three units from the camera. Its virtual image at z=+3 is nine units away.
    distance = 9.0 if reflected else 3.0
    if case == "floor":
        source_y = -2.0 if reflected else 2.0
        distance = 9.0
    return (width * 0.5 + focal_length * source_x / distance,
        height * 0.5 - focal_length * (source_y - 1.4) / distance,
        focal_length * 0.3 / distance, focal_length * half_height / distance)


def in_panel_rectangle(point, projection, margin):
    x, y = point
    cx, cy, half_width, half_height = projection
    return abs(x - cx) <= half_width + margin and abs(y - cy) <= half_height + margin


def analyze_panels(frame, case, mode):
    width, height, rows = validate_frame(frame)
    results = {}
    for color in ("red", "green"):
        points = [(x, y) for y, row in enumerate(rows) for x, pixel in enumerate(row) if marker_color(pixel, color)]
        allowed_regions = []
        color_results = {}
        for label, reflected in (("direct", False), ("reflection", True)):
            expected = not (case == "boundary" and color == "green") if not reflected else (
                mode != "disabled" and not (case == "boundary" and color == "green" and mode == "screen"))
            projection = panel_projection(width, height, color, case, reflected)
            region_points = [point for point in points if in_panel_rectangle(point, projection, height * 0.008)]
            if not expected:
                if len(region_points) > 16:
                    raise SmokeFailure(f"{case}/{mode}: unexpected {color} {label} panel ({len(region_points)} pixels)")
                color_results[label] = {"pixels": len(region_points), "expected": False}
                continue
            minimum = max(32, 4.0 * projection[2] * projection[3] * (0.25 if reflected else 0.5))
            if len(region_points) < minimum:
                raise SmokeFailure(f"{case}/{mode}: missing {color} {label} panel "
                    f"({len(region_points)} pixels; projected area requires {math.ceil(minimum)})")
            cx = sum(x for x, _ in region_points) / len(region_points)
            cy = sum(y for _, y in region_points) / len(region_points)
            if math.hypot(cx - projection[0], cy - projection[1]) > max(2.0, height * 0.012):
                raise SmokeFailure(f"{case}/{mode}: {color} {label} centroid does not match its geometric projection")
            allowed_regions.append(projection)
            color_results[label] = {"pixels": len(region_points), "expected": True,
                "centroid": [cx, cy], "expected_centroid": list(projection[:2]),
                "minimum_projected_area_pixels": math.ceil(minimum)}
        outside = sum(not any(in_panel_rectangle(point, region, height * 0.008) for region in allowed_regions)
            for point in points)
        if outside > max(16, len(points) * 0.03):
            raise SmokeFailure(f"{case}/{mode}: {color} pixels appeared outside direct/reflected panel regions ({outside})")
        color_results["outside_predicted_regions"] = outside
        results[color] = color_results
    return results


def compare_panel_motion(original, moved):
    if original[:2] != moved[:2]:
        raise SmokeFailure("panel motion captures have different framebuffer dimensions")
    before = analyze_panels(original, "onscreen", "screen")
    after = analyze_panels(moved, "onscreen_moved", "screen")
    focal_length = original[1] / (2.0 * math.tan(math.pi / 6.0))
    for color in ("red", "green"):
        direction = 1.0 if color == "red" else -1.0
        for label, distance in (("direct", 3.0), ("reflection", 9.0)):
            expected_dx = focal_length * direction * 0.25 / distance
            dx = after[color][label]["centroid"][0] - before[color][label]["centroid"][0]
            dy = after[color][label]["centroid"][1] - before[color][label]["centroid"][1]
            if abs(dx - expected_dx) > max(1.5, original[1] * 0.006) or abs(dy) > max(1.5, original[1] * 0.006):
                raise SmokeFailure(f"{color} {label} panel did not follow the authored inward movement")
            after[color][label]["measured_motion"] = [dx, dy]
            after[color][label]["expected_motion_x"] = expected_dx
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


def parse_statistics(log_text):
    samples = []
    prefix = "ReflectionSmokeStatistics:"
    for line in log_text.splitlines():
        if prefix not in line:
            continue
        fields = line.split(prefix, 1)[1].split()
        if len(fields) != len(STATISTICS_FIELDS):
            raise SmokeFailure("completed reflection statistics have missing or duplicate fields")
        sample = {}
        for field in fields:
            match = re.fullmatch(r"([a-z_]+)=([0-9]+)", field)
            if not match or match[1] not in STATISTICS_FIELDS or match[1] in sample:
                raise SmokeFailure("completed reflection statistics contain a malformed field")
            sample[match[1]] = int(match[2])
        samples.append(sample)
    if not samples:
        raise SmokeFailure("no completed reflection statistics were published")
    return samples


def validate_statistics(samples, case, mode, budget=DEFAULT_RAY_BUDGET, allow_zero_samples=False):
    stable = []
    previous = None
    for sample in samples:
        if set(sample) != set(STATISTICS_FIELDS) or any(type(value) is not int or value < 0 for value in sample.values()):
            raise SmokeFailure("completed reflection statistics are malformed")
        wide_fields = ("sequence", "generation", "token_value")
        if any(value > (0xffffffffffffffff if name in wide_fields else 0xffffffff) for name, value in sample.items()):
            raise SmokeFailure("completed reflection statistics exceed their integer field widths")
        if sample["mode"] != MODES.index(mode) or (sample["width"], sample["height"]) != (960, 720):
            raise SmokeFailure("completed reflection statistics do not match the requested mode and dimensions")
        capacity = sample["queue_capacity"]
        if capacity <= 0 or capacity > 2 * 960 * 720 or sample["requested_budget"] != budget \
            or sample["effective_budget"] != min(budget, capacity):
            raise SmokeFailure("completed reflection statistics do not match the requested budget and capacity")
        if not sample["sequence"] or not sample["generation"] or not sample["token_value"] \
            or not 0 < sample["device_generation"] <= 0xffff or not 0 <= sample["physical_queue"] < 0xffff \
            or sample["token_queue"] > 2:
            raise SmokeFailure("reflection statistics lack an accepted token and device identity")
        if any(sample[name] not in (0, 1) for name in ("hardware_requested", "hardware_available", "hardware_ready")) \
            or sample["hardware_requested"] != int(mode in ("hardware", "hybrid")):
            raise SmokeFailure("reflection statistics have inconsistent hardware route metadata")
        if previous:
            identity = ("generation", "device_generation", "physical_queue", "token_queue")
            if any(sample[name] != previous[name] for name in identity):
                raise SmokeFailure("static reflection capture mixed statistics generations or physical queues")
            if sample["sequence"] <= previous["sequence"] or sample["frame"] <= previous["frame"] \
                or sample["token_value"] <= previous["token_value"]:
                raise SmokeFailure("completed reflection statistics are stale or out of order")
        previous = sample
        eligible = sample["opaque_pixels"] + sample["glass_pixels"]
        if sample["opaque_pixels"] > 960 * 720 or sample["glass_pixels"] > 960 * 720 \
            or sample["candidates"] > eligible or sample["screen_attempts"] > eligible \
            or sample["screen_hits"] > sample["screen_attempts"]:
            raise SmokeFailure("reflection counters exceed their eligible pixel population")
        if sample["hardware_rays"] > min(sample["effective_budget"], sample["candidates"]) \
            or sample["hardware_hits"] > sample["hardware_rays"]:
            raise SmokeFailure("actual hardware rays or hits exceeded the completed frame budget")
        zero_samples = eligible - sample["screen_hits"] - sample["hardware_hits"] - sample["fallback_pixels"]
        # GGX samples below the geometric horizon are valid zero radiance, with no trace or fallback.
        if not (0 <= zero_samples <= sample["opaque_pixels"] if allow_zero_samples else zero_samples == 0):
            raise SmokeFailure("reflection outcome counters do not partition eligible pixels")
        if mode in ("disabled", "screen") and (sample["hardware_rays"] or sample["hardware_hits"] or sample["candidates"]):
            raise SmokeFailure("a non-hardware reflection route issued hardware work")
        if mode == "disabled" and any(sample[name] for name in COUNTER_FIELDS):
            raise SmokeFailure("disabled reflection produced nonzero counters")
        if mode == "hardware" and (sample["screen_attempts"] or sample["screen_hits"]):
            raise SmokeFailure("hardware-only reflection attempted screen tracing")
        if not sample["hardware_ready"] and (sample["hardware_rays"] or sample["hardware_hits"]):
            raise SmokeFailure("reflection issued hardware rays without a ready hardware route")
        if sample["frame"] < 3:
            continue
        if mode != "disabled" and not sample["opaque_pixels"]:
            raise SmokeFailure("stable reflection statistics are missing the opaque fixture")
        if mode in ("screen", "hybrid") and not sample["screen_attempts"]:
            raise SmokeFailure("stable screen route did not attempt screen tracing")
        if mode in ("hardware", "hybrid"):
            if not sample["hardware_available"] or not sample["hardware_ready"]:
                raise SmokeFailure("stable hardware capture has no ready hardware route")
            if sample["hardware_rays"] != min(sample["candidates"], sample["effective_budget"]):
                raise SmokeFailure("ready hardware route did not execute its bounded candidate queue")
        if case == "opaque_glass" and mode != "disabled" and not sample["glass_pixels"]:
            raise SmokeFailure("stable reflection statistics are missing primary glass")
        if case == "offscreen" and mode in ("hardware", "hybrid") and not sample["hardware_hits"]:
            raise SmokeFailure("offscreen reflection has no completed hardware hits")
        if case == "floor" and mode in ("screen", "hybrid") and not sample["screen_hits"]:
            raise SmokeFailure("floor reflection has no accepted screen hits")
        if budget < DEFAULT_RAY_BUDGET and (sample["candidates"] <= sample["effective_budget"] or not sample["fallback_pixels"]):
            raise SmokeFailure("limited-budget reflection did not exercise excess candidates and fallback")
        stable.append(sample)
    if not stable:
        raise SmokeFailure("no completed reflection statistics reached stable frame 3")
    return stable


def compare_hybrid_statistics(hardware, hybrid):
    populations = [sample["opaque_pixels"] + sample["glass_pixels"] for sample in hardware + hybrid]
    if not populations or min(populations) <= 0 or max(populations) - min(populations) > max(16, min(populations) * 0.01):
        raise SmokeFailure("floor hardware and hybrid statistics do not represent matched eligible populations")
    hardware_rays = [sample["hardware_rays"] for sample in hardware]
    hybrid_rays = [sample["hardware_rays"] for sample in hybrid]
    if not hardware_rays or not hybrid_rays or min(sample["screen_hits"] for sample in hybrid) <= 0 \
        or max(hybrid_rays) >= min(hardware_rays):
        raise SmokeFailure("floor hybrid did not reduce completed hardware rays while accepting screen hits")
    return {"hardware_ray_range": [min(hardware_rays), max(hardware_rays)],
        "hybrid_hardware_ray_range": [min(hybrid_rays), max(hybrid_rays)],
        "eligible_pixel_range": [min(populations), max(populations)],
        "hybrid_accepted_screen_hit_range": [min(sample["screen_hits"] for sample in hybrid),
            max(sample["screen_hits"] for sample in hybrid)]}


def capture_stem(case, mode, budget=DEFAULT_RAY_BUDGET):
    return f"{case}_{mode}" + (f"_budget_{budget}" if budget != DEFAULT_RAY_BUDGET else "")


def capture_environment(case, mode, budget=DEFAULT_RAY_BUDGET):
    env = os.environ.copy()
    for name in tuple(env):
        if name.startswith("NWB_REFLECTION_SMOKE_") or name.startswith("NWB_REFRACTION_SMOKE_"):
            env.pop(name)
    env.pop("NWB_RENDERER_BASELINE_CAPTURE_FREEZE_FRAME", None)
    env.pop("NWB_GPU_TIMING_FILE", None)
    env["NWB_REFLECTION_SMOKE_CASE"] = case
    env["NWB_REFLECTION_SMOKE_MODE"] = mode
    env["NWB_REFLECTION_SMOKE_RAY_BUDGET"] = str(budget)
    env["NWB_RENDERER_BASELINE_FIXED_DELTA_SECONDS"] = "0.016666667"
    return env


def capture(args, case, mode, budget=DEFAULT_RAY_BUDGET):
    output = args.output_directory / (capture_stem(case, mode, budget) + ".bmp")
    log_output = output.with_suffix(".log")
    command = [sys.executable, str(Path(__file__).with_name("window_capture_smoke.py")),
        "--executable", str(args.executable), "--working-directory", str(args.working_directory),
        "--output", str(output), "--application-capture", "--application-capture-frame-count", str(args.frames),
        "--timeout", str(args.timeout), "--expect-log-message", f"ReflectionSmokeProject: case {case} created",
        "--expect-log-message", f"ReflectionSmokeProject: reflection mode {mode}",
        "--expect-log-message", "ReflectionSmokeProject: shutdown",
        "--expect-log-message", f"ReflectionSmokeProject: hardware ray budget {budget}",
        "--expect-log-message", "ReflectionSmokeStatistics:", "--log-output", str(log_output)]
    if args.logserver_executable:
        command += ["--logserver-executable", str(args.logserver_executable)]
    else:
        command.append("--no-logserver")
    if mode in ("hardware", "hybrid"):
        if budget == 0:
            route = "screen-space" if mode == "hybrid" else "environment"
            command += ["--expect-log-message", "Reflection resolve: " + route,
                "--reject-log-message", "Reflection resolve: hardware"]
        else:
            command += ["--expect-log-message", "Reflection resolve: hardware"]
        command += ["--expect-log-message" if args.require_hardware else "--skip-log-message",
            "ReflectionSmokeProject: hardware available" if args.require_hardware else "ReflectionSmokeProject: hardware unavailable"]
    elif mode == "disabled":
        command += ["--expect-log-message", "Reflection resolve: disabled",
            "--reject-log-message", "Reflection resolve: hardware"]
    elif mode == "screen":
        command += ["--expect-log-message", "Reflection resolve: screen-space",
            "--reject-log-message", "Reflection resolve: hardware"]
    if case == "opaque_glass":
        command += ["--expect-log-message", "AVBOIT refraction resolve:"]
    command.extend("--application-arg=" + argument for argument in args.application_arg)
    print(f"Capturing {case}/{mode} budget={budget} ({args.frames} presentation frames)...", flush=True)
    # The child owns app/logserver cleanup: leave time for startup, graceful exit, kill fallback, and log drain.
    result = subprocess.run(command, env=capture_environment(case, mode, budget), check=False, timeout=args.timeout + 90)
    if result.returncode == SKIP_EXIT_CODE:
        return None
    if result.returncode:
        raise SmokeFailure(f"reflection {case}/{mode} capture failed with exit {result.returncode}")
    frame = read_bmp_24_rows(output)
    validate_frame(frame)
    if frame[:2] != (960, 720):
        raise SmokeFailure(f"expected actual 960x720 framebuffer, got {frame[0]}x{frame[1]}")
    samples = parse_statistics(log_output.read_text(encoding="utf-8"))
    validate_statistics(samples, case, mode, budget)
    return frame, samples


def write_report(args, captures, metrics=None, statistics=None):
    metadata = {"frames": args.frames, "frame_source": "actual application framebuffer readback", "size": [960, 720],
        "hardware_required": args.require_hardware, "application_args": args.application_arg,
        "settings": {"environment_top": [0, 0, 0], "environment_bottom": [0, 0, 0],
            "max_hardware_rays_per_frame": 1382400, "roughness": 0, "glass_ior": 3.8},
        "suite": args.suite,
        "captures": [{"case": case, "mode": mode, "hardware_budget": budget,
            "file": capture_stem(case, mode, budget) + ".bmp", "log": capture_stem(case, mode, budget) + ".log"}
            for case, mode, budget in captures], "statistics": statistics,
        "metrics": metrics, "limitations": "Smooth single-bounce reflection with on-screen and offscreen geometric marker checks. Wall-mirror screen hits approach marker backs and have provisional confidence; the floor case approaches their fronts. Completed per-frame counters separately check floor hybrid ray reduction and bounded budgets; they do not establish GPU speed. Hardware secondary-hit lighting is approximate; roughness reconstruction and temporal history are not validated here."}
    (args.output_directory / "reflection_manifest.json").write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")
    cards = []
    for item in metadata["captures"]:
        bmp = args.output_directory / item["file"]
        png = png_rgb_bytes(read_bmp_24_rows(bmp))
        bmp.with_suffix(".png").write_bytes(png)
        label = html.escape(item["case"] + " / " + item["mode"] + " / budget " + str(item["hardware_budget"]))
        cards.append(f'<article><h2>{label}</h2><a href="{html.escape(item["file"])}">Raw BMP</a>'
            f' / <a href="{html.escape(item["log"])}">Completed-frame log</a>'
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
    parser.add_argument("--suite", choices=("all", "baseline", "screen", "budget", "rough", "temporal", "optical"), default="all",
        help="Capture the original 22 comparisons, a subset, or the separate roughness/history suites.")
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
    if args.suite in ("rough", "temporal"):
        from reflection_roughness_smoke import run_suite
        return run_suite(args)
    if args.suite == "optical":
        from reflection_optical_smoke import run_suite
        return run_suite(args)
    completed = []
    statistics = {}
    try:
        visual = BASELINE_CAPTURES if args.suite == "baseline" else SCREEN_CAPTURES if args.suite == "screen" else CAPTURES
        selected = [(case, mode, DEFAULT_RAY_BUDGET) for case, mode in visual] if args.suite != "budget" else []
        if args.suite in ("all", "budget"):
            selected.extend(BUDGET_CAPTURES)
        for case, mode, budget in selected:
            result = capture(args, case, mode, budget)
            if result is None:
                print("SKIP: required reflection hardware or framebuffer readback is unavailable", file=sys.stderr)
                return SKIP_EXIT_CODE
            frame, samples = result
            del result
            completed.append((case, mode, budget))
            statistics[capture_stem(case, mode, budget)] = samples
            del frame
        write_report(args, completed, statistics=statistics)
        metrics = {}
        if args.suite in ("all", "baseline"):
            for case, mode in (("offscreen", "disabled"), ("offscreen", "screen"), ("moved", "disabled")):
                metrics[f"{case}_{mode}"] = analyze_markers(read_bmp_24_rows(args.output_directory / f"{case}_{mode}.bmp"), required=False)
            metrics["marker_motion"] = compare_marker_motion(
                read_bmp_24_rows(args.output_directory / "offscreen_hardware.bmp"),
                read_bmp_24_rows(args.output_directory / "moved_hardware.bmp"))
            metrics["offscreen_hybrid"] = analyze_markers(read_bmp_24_rows(args.output_directory / "offscreen_hybrid.bmp"))
            metrics["opaque_glass"] = compare_opaque_glass(
                read_bmp_24_rows(args.output_directory / "opaque_glass_disabled.bmp"),
                read_bmp_24_rows(args.output_directory / "opaque_glass_hardware.bmp"))
        if args.suite in ("all", "screen"):
            for case, mode in SCREEN_CAPTURES:
                metrics[f"{case}_{mode}"] = analyze_panels(read_bmp_24_rows(args.output_directory / f"{case}_{mode}.bmp"), case, mode)
            metrics["panel_motion"] = compare_panel_motion(
                read_bmp_24_rows(args.output_directory / "onscreen_screen.bmp"),
                read_bmp_24_rows(args.output_directory / "onscreen_moved_screen.bmp"))
            metrics["floor_completed_ray_comparison"] = compare_hybrid_statistics(
                validate_statistics(statistics["floor_hardware"], "floor", "hardware"),
                validate_statistics(statistics["floor_hybrid"], "floor", "hybrid"))
        if args.suite in ("all", "budget"):
            for case, mode, budget in BUDGET_CAPTURES:
                stem = capture_stem(case, mode, budget)
                metrics[stem] = analyze_panels(read_bmp_24_rows(args.output_directory / (stem + ".bmp")), case, mode)
        write_report(args, completed, metrics, statistics)
        print(f"PASS: reflection {args.suite} geometric marker, route, and composition checks\n"
            + json.dumps(metrics, indent=2), flush=True)
        return 0
    except (SmokeFailure, OSError, subprocess.TimeoutExpired) as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
