#!/usr/bin/env python3
"""Measure a CSG frame sequence from one process with reproducible source-frame poses."""

import argparse
from array import array
import hashlib
import json
import math
import os
from pathlib import Path
import re
import statistics
import subprocess
import sys

from window_capture_smoke import (SKIP_EXIT_CODE, STRICT_LOG_FAILURE_MESSAGES, SmokeFailure,
    read_bmp_24_rows, validate_expected_log_text)


SAMPLE_COUNT = 32
MINIMUM_FRAME = 360
WARMUP_SECONDS = 30
FRAME_INTERVAL = 8
EXPECTED_EXTENT = (1280, 900)
REGIONS = {"plane": (454, 324, 32, 32), "box": (826, 324, 32, 32), "sphere": (454, 576, 32, 32),
    "capsule": (826, 576, 32, 32), "box_upper_band": (836, 290, 40, 8),
    "sphere_upper_band": (414, 552, 40, 16), "capsule_stripes": (805, 576, 32, 40),
    "background": (640, 450, 32, 32)}
STATIC_EXTERIOR_REGIONS = {"sphere_exterior": (502, 578, 6, 24),
    "capsule_exterior": (882, 576, 6, 24), "box_exterior": (896, 324, 3, 20)}
ANIMATED_INTERIOR_REGIONS = {"box_inner_wall": (832, 316, 24, 8)}
QUALITY_LIMITS = {"channel_jump": 20, "max_frame_jump_fraction": .10, "static_p95_temporal_range": 6,
    "colored_core_minimum": 32, "tint_margin": 4, "direct_core_minimum": 16,
    "direct_background_separation": 8, "background_tolerance": 2}
# Analytical ray/cutter intersections for the static camera (0,.75,-6.5), default near=.001/far=10000,
# 1280x900 extent, and shape yaw phases 0/.18/.36/.54. These distinguish physical caps from enclosing faces.
GEOMETRY_PROBES = {
    "plane": {"pixel": (430, 320), "normal": (0., 0., -1.), "position": (-1.747126065, 1.829965754, 0.)},
    "box": {"pixel": (835, 315), "normal": (-.179029573, 0., -.983843693), "position": (1.721953360, 1.934668680, .365114160)},
    "sphere": {"pixel": (415, 570), "normal": (.318619304, -.078551107, -.944622392), "position": (-1.881587477, -.259938935, .032526944)},
    "capsule": {"pixel": (805, 570), "normal": (-.177317767, 0., -.984153651), "position": (1.361729587, -.241470787, -.086929462)}}
GEOMETRY_LIMITS = {"normal_l2_error": .04, "position_component_max": .06, "position_arithmetic_allowance": .005}
CAP_STATE_REQUIRED_PROBES = {name: probe["pixel"] for name, probe in GEOMETRY_PROBES.items()}
# This interior rim block previously lost its receiver span when raster events arrived BACK first.
CAP_STATE_REQUIRED_PROBES.update({f"capsule_{x}_{y}": (x, y) for y in range(604, 608) for x in range(784, 787)})
CAP_STATE_PROBES = dict(CAP_STATE_REQUIRED_PROBES)
CAP_STATE_PROBES.update({f"capsule_{x}_{y}": (x, y) for y in range(604, 608) for x in (783, 787)})
STATE_CHANNELS = {"cap_state": ("raw_removed_count", "nearest_cap_visible", "owns_gbuffer_depth"),
    "interval_state": ("receiver_span_count", "peel_id_present", "peel_cap_coverage"),
    "event_state": ("raw_count", "front_count", "back_count"),
    "event_data": ("span_present", "matching_event_pair", "ordered_event_depths"),
    "event_order": ("span_present", "first_front", "second_front"),
    "span_state": ("span_count", "span_flags", "event_count")}
EVENT_DATA_FLAGS_MARKER = "CsgVisibleSmokeProject: event-data red stores missing-span flags divided by 33"
EVENT_ORDER_MARKER = "CsgVisibleSmokeProject: event-order RGB stores span/flags and validated raw FRONT=1 BACK=0.5"
SPAN_STATE_MARKER = "CsgVisibleSmokeProject: span-state RGB stores span count, flags, raw event count divided by 33"
OVERLAPPING_MARKER = "CsgVisibleSmokeProject: overlapping plane receiver created z_offset=0.125"
SPAN_EXPECTATIONS = {name: {"span_count": 2 if name == "plane" else 1, "span_flags": 0,
    "event_count": 4 if name == "plane" else 2} for name in GEOMETRY_PROBES}


def sample_path(directory, sample):
    return directory / ("series.bmp" if sample + 1 == SAMPLE_COUNT else f"series.bmp.{sample}.bmp")


def capture_series(args):
    args.output_directory.mkdir(parents=True, exist_ok=True)
    output = sample_path(args.output_directory, SAMPLE_COUNT - 1)
    if args.overwrite:
        for sample in range(SAMPLE_COUNT):
            path = sample_path(args.output_directory, sample)
            path.unlink(missing_ok=True)
            path.with_name(path.name + ".partial").unlink(missing_ok=True)
        for name in ("manifest.json", "runtime.log"):
            (args.output_directory / name).unlink(missing_ok=True)
    if any(sample_path(args.output_directory, index).exists() for index in range(SAMPLE_COUNT)):
        raise SmokeFailure("temporal acquisition requires an empty capture directory")
    environment = os.environ.copy()
    for name in tuple(environment):
        if name.startswith(("NWB_GI_SMOKE_", "NWB_RENDERER_BASELINE_", "NWB_CSG_GI_TEMPORAL")):
            environment.pop(name)
    environment.pop("NWB_GPU_TIMING_FILE", None)
    if args.gpu_timing_output:
        args.gpu_timing_output.parent.mkdir(parents=True, exist_ok=True)
        if args.overwrite:
            args.gpu_timing_output.unlink(missing_ok=True)
        elif args.gpu_timing_output.exists():
            raise SmokeFailure("temporal acquisition requires a fresh GPU timing file")
        environment["NWB_GPU_TIMING_FILE"] = str(args.gpu_timing_output.resolve())
    environment.update({"NWB_CSG_GI_TEMPORAL": "1", "NWB_CSG_GI_TEMPORAL_VIEW": args.view,
        "NWB_CSG_GI_TEMPORAL_ANIMATED": str(int(args.motion == "animated")),
        "NWB_CSG_GI_TEMPORAL_INTERVAL": str(args.frame_interval),
        "NWB_CSG_GI_TEMPORAL_LAGGED": str(int(args.lagged_lighting)),
        "NWB_CSG_GI_TEMPORAL_OVERLAPPING": str(int(args.overlapping))})
    command = [sys.executable, str(Path(__file__).with_name("window_capture_smoke.py")),
        "--executable", str(args.executable), "--working-directory", str(args.working_directory),
        "--output", str(output), "--application-capture", "--application-capture-frame-count", "1",
        "--timeout", str(args.timeout), "--log-output", str(args.output_directory / "runtime.log"),
        "--expect-log-message", f"CsgVisibleSmokeProject: temporal scene animated={int(args.motion == 'animated')} view={args.view} minimum_frame=360 warmup_seconds=30 interval={args.frame_interval} samples=32",
        "--expect-log-message", "CsgVisibleSmokeProject: temporal warmup started graphics frame",
        "--expect-log-message", "CsgVisibleSmokeProject: temporal warmup complete elapsed_seconds=",
        "--expect-log-message", "CsgVisibleSmokeProject: temporal series complete samples=32",
        "--expect-log-message", "CsgVisibleSmokeProject: shutdown",
        "--expect-log-message", "RendererSystem: dispatched surfel GI resolve",
        "--gpu-validation" if args.gpu_validation else "--no-gpu-validation"]
    if args.route == "software":
        command += ["--application-arg=--disable-hardware-ray-tracing",
            "--expect-log-message", "Loader: hardware ray tracing disabled before device creation",
            "--reject-log-message", "RendererSystem: created surfel HW trace compute pipeline"]
    if args.lagged_lighting:
        command += ["--expect-log-message", "RendererSystem: frame-lagged async lighting bootstrap accepted",
            "--expect-log-message", "RendererSystem: frame-lagged async lighting active history accepted",
            "--skip-log-message", "RendererSystem: frame-lagged async lighting Graphics queue route accepted (no dedicated Compute queue"]
    if args.view == "event_data":
        command += ["--expect-log-message", EVENT_DATA_FLAGS_MARKER]
    elif args.view == "event_order":
        command += ["--expect-log-message", EVENT_ORDER_MARKER]
    elif args.view == "span_state":
        command += ["--expect-log-message", SPAN_STATE_MARKER]
    if args.overlapping:
        command += ["--expect-log-message", OVERLAPPING_MARKER]
    else:
        command += ["--reject-log-message", OVERLAPPING_MARKER]
    if args.logserver_executable:
        command += ["--logserver-executable", str(args.logserver_executable)]
    else:
        command.append("--no-logserver")
    result = subprocess.run(command, env=environment, check=False, timeout=args.timeout + 45.0)
    if result.returncode and result.returncode != SKIP_EXIT_CODE:
        raise SmokeFailure(f"temporal capture failed with exit {result.returncode}")
    return result.returncode


def percentile(values, fraction):
    ordered = sorted(values)
    return ordered[round((len(ordered) - 1) * fraction)]


def measure_region_frame(rows, region):
    center_x, center_y, half_width, half_height = region
    total = [0, 0, 0]
    horizontal_jumps = vertical_jumps = 0
    for y in range(center_y - half_height, center_y + half_height):
        for x in range(center_x - half_width, center_x + half_width):
            for channel, value in enumerate(rows[y][x]):
                total[channel] += value
                if x > center_x - half_width:
                    horizontal_jumps += abs(value - rows[y][x - 1][channel]) > QUALITY_LIMITS["channel_jump"]
                if y > center_y - half_height:
                    vertical_jumps += abs(value - rows[y - 1][x][channel]) > QUALITY_LIMITS["channel_jump"]
    return {"mean_rgb": [value / (4 * half_width * half_height) for value in total],
        "horizontal_jump_fraction": horizontal_jumps / ((2 * half_width - 1) * 2 * half_height * 3),
        "vertical_jump_fraction": vertical_jumps / ((2 * half_height - 1) * 2 * half_width * 3)}


def decode_diagnostic(rgb, view):
    display_linear = [value / 255 / 12.92 if value / 255 <= .04045 else ((value / 255 + .055) / 1.055) ** 2.4
        for value in rgb]
    # The fixture preserves the renderer's exposure=1, Reinhard shoulder=0.65 presentation defaults.
    if any(value >= 1 for value in display_linear):
        raise SmokeFailure("diagnostic color cannot invert saturated Reinhard presentation")
    encoded = [.65 * value / (1 - value) for value in display_linear]
    if view in STATE_CHANNELS:
        return encoded
    return [value * scale - offset for value, scale, offset in zip(encoded,
        (2, 2, 2) if view == "normal" else (6, 6, 2), (1, 1, 1) if view == "normal" else (3, 3, 1))]


def position_quantization_tolerance(rgb, decoded):
    lower = decode_diagnostic([max(value - .5, 0) for value in rgb], "position")
    upper = decode_diagnostic([min(value + .5, 254.5) for value in rgb], "position")
    return [min(GEOMETRY_LIMITS["position_component_max"],
        max(value - low, high - value) + GEOMETRY_LIMITS["position_arithmetic_allowance"])
        for value, low, high in zip(decoded, lower, upper)]


def measure_series(directory, frame_interval=FRAME_INTERVAL):
    log = (directory / "runtime.log").read_text(encoding="utf-8")
    validate_expected_log_text(log, ("CsgVisibleSmokeProject: temporal series complete samples=32",
        "CsgVisibleSmokeProject: shutdown", "RendererSystem: dispatched surfel GI resolve"), STRICT_LOG_FAILURE_MESSAGES)
    setups = re.findall(r"CsgVisibleSmokeProject: temporal scene animated=([01]) view=(full|indirect|direct|normal|position|cap_state|interval_state|event_state|event_data|event_order|span_state) minimum_frame=(\d+) warmup_seconds=(\d+) interval=(\d+) samples=(\d+)", log)
    if len(setups) != 1:
        raise SmokeFailure("temporal series must have exactly one logged scene setup")
    animated, view, minimum_frame, warmup_seconds, logged_interval, sample_count = setups[0]
    if view == "event_data" and log.count(EVENT_DATA_FLAGS_MARKER) != 1:
        raise SmokeFailure("event-data series must identify its missing-span flag encoding")
    if view == "event_order" and log.count(EVENT_ORDER_MARKER) != 1:
        raise SmokeFailure("event-order series must identify its validated raw face encoding")
    if view == "span_state" and log.count(SPAN_STATE_MARKER) != 1:
        raise SmokeFailure("span-state series must identify its count and flag encoding")
    overlap_count = log.count(OVERLAPPING_MARKER)
    if overlap_count > 1 or (overlap_count and animated != "0"):
        raise SmokeFailure("overlapping receiver series requires exactly one static duplicate")
    if (int(minimum_frame), int(warmup_seconds), int(logged_interval), int(sample_count)) != (
        MINIMUM_FRAME, WARMUP_SECONDS, frame_interval, SAMPLE_COUNT):
        raise SmokeFailure("logged temporal setup differs from the requested sample schedule")
    warmup_starts = re.findall(r"CsgVisibleSmokeProject: temporal warmup started graphics frame (\d+)", log)
    warmup_completions = re.findall(
        r"CsgVisibleSmokeProject: temporal warmup complete elapsed_seconds=(\d+\.\d+) first_frame=(\d+)", log)
    if len(warmup_starts) != 1 or len(warmup_completions) != 1:
        raise SmokeFailure("temporal series must identify one live warmup start and completion")
    warmup_start_frame = int(warmup_starts[0])
    elapsed, first_frame = warmup_completions[0]
    warmup_elapsed_seconds = float(elapsed)
    first_frame = int(first_frame)
    if (warmup_start_frame < MINIMUM_FRAME or first_frame <= warmup_start_frame
        or not math.isfinite(warmup_elapsed_seconds) or warmup_elapsed_seconds < WARMUP_SECONDS):
        raise SmokeFailure("temporal series must render at least 360 frames before a 30-second live warmup")
    captured = [(int(sample), int(frame)) for sample, frame in re.findall(
        r"CsgVisibleSmokeProject: temporal sample (\d+) graphics frame (\d+)", log)]
    expected = [(sample, first_frame + frame_interval * sample) for sample in range(SAMPLE_COUNT)]
    if captured != expected:
        raise SmokeFailure(f"temporal source-frame sequence differs from its deterministic schedule: {captured}")
    if not (log.index("CsgVisibleSmokeProject: temporal warmup started graphics frame")
        < log.index("CsgVisibleSmokeProject: temporal warmup complete elapsed_seconds=")
        < log.index("CsgVisibleSmokeProject: temporal sample 0 graphics frame")):
        raise SmokeFailure("temporal capture must follow the completed live warmup")
    if len(re.findall(r"FramebufferCapture: capture ready", log)) != SAMPLE_COUNT:
        raise SmokeFailure("temporal series did not complete all accepted framebuffer readbacks")
    bounded_motion = "CsgVisibleSmokeProject: front-facing triangle pose from graphics frame" in log
    region_bounds = dict(REGIONS)
    if animated == "0":
        region_bounds.update(STATIC_EXTERIOR_REGIONS)
    else:
        # The static ceiling band crosses real pitched ceiling/wall edges during motion.
        region_bounds.pop("box_upper_band")
        region_bounds.update(ANIMATED_INTERIOR_REGIONS)
    regions = {name: [] for name in region_bounds}
    files = []
    for index, frame in captured:
        path = sample_path(directory, index)
        if path.with_name(path.name + ".partial").exists():
            raise SmokeFailure("temporal series left an incomplete capture")
        width, height, rows = read_bmp_24_rows(path)
        if (width, height) != EXPECTED_EXTENT:
            raise SmokeFailure("temporal series must retain the actual 1280x900 framebuffer")
        for name, (center_x, center_y, half_width, half_height) in region_bounds.items():
            samples = array("B")
            for y in range(center_y - half_height, center_y + half_height, 2):
                for x in range(center_x - half_width, center_x + half_width, 2):
                    samples.extend(rows[y][x])
            regions[name].append(samples)
        files.append({"sample": index, "graphics_frame": frame, "file": path.name,
            "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
            "regions": {name: measure_region_frame(rows, region) for name, region in region_bounds.items()}})
        if view in ("normal", "position"):
            points = {}
            for name, probe in GEOMETRY_PROBES.items():
                x, y = probe["pixel"]
                rgb = rows[y][x]
                decoded = decode_diagnostic(rgb, view)
                points[name] = {"pixel": [x, y], "display_rgb": list(rgb), "decoded": decoded}
                if view == "position":
                    points[name]["component_tolerance"] = position_quantization_tolerance(rgb, decoded)
            files[-1]["diagnostic_points"] = points
        elif view in STATE_CHANNELS:
            points = {}
            for name, (x, y) in CAP_STATE_PROBES.items():
                rgb = rows[y][x]
                decoded = decode_diagnostic(rgb, view)
                points[name] = {"pixel": [x, y], "display_rgb": list(rgb), "decoded": decoded}
                if view in ("event_state", "span_state"):
                    points[name].update({label: round(value * 33) for label, value in zip(STATE_CHANNELS[view], decoded)})
                    if view == "event_state":
                        points[name]["raw_count_overflow"] = points[name]["raw_count"] >= 33
                elif view == "event_data":
                    points[name].update({label: value > .5 for label, value in zip(STATE_CHANNELS[view], decoded)})
                    if not points[name]["span_present"]:
                        points[name]["failed_span_flags"] = round(decoded[0] * 33)
                elif view == "event_order":
                    valid_pair = decoded[1] > .25 and decoded[2] > .25
                    points[name].update({"span_present": decoded[0] > .5, "validated_pair": valid_pair,
                        "first_front": decoded[1] > .75 if valid_pair else None,
                        "second_front": decoded[2] > .75 if valid_pair else None})
                    if not points[name]["span_present"]:
                        points[name]["failed_span_flags"] = round(decoded[0] * 33)
                else:
                    points[name]["state"] = {label: value > .5 for label, value in zip(STATE_CHANNELS[view], decoded)}
            files[-1]["diagnostic_points"] = points
    metrics = {}
    for name, frames in regions.items():
        ranges = []
        deviations = []
        changes = []
        horizontal_changes = []
        row_width = region_bounds[name][2]
        for pixel in range(len(frames[0])):
            values = [frame[pixel] for frame in frames]
            ranges.append(max(values) - min(values))
            deviations.append(statistics.pstdev(values))
            changes.extend(abs(current - previous) for previous, current in zip(values, values[1:]))
        for frame in frames:
            for pixel in range(len(frame) // 3):
                if pixel % row_width:
                    horizontal_changes.extend(abs(frame[pixel * 3 + channel] - frame[(pixel - 1) * 3 + channel])
                        for channel in range(3))
        metrics[name] = {"sampled_pixels": len(frames[0]) // 3,
            "mean_rgb": [statistics.fmean(value for frame in frames for value in frame[channel::3]) for channel in range(3)],
            "mean_temporal_range": statistics.fmean(ranges), "p95_temporal_range": percentile(ranges, .95),
            "max_temporal_range": max(ranges), "mean_temporal_standard_deviation": statistics.fmean(deviations),
            "mean_adjacent_change": statistics.fmean(changes), "p95_adjacent_change": percentile(changes, .95),
            "max_adjacent_change": max(changes), "mean_horizontal_change": statistics.fmean(horizontal_changes),
            "p95_horizontal_change": percentile(horizontal_changes, .95),
            "large_horizontal_change_fraction": sum(change > QUALITY_LIMITS["channel_jump"] for change in horizontal_changes) / len(horizontal_changes),
            "max_frame_horizontal_jump_fraction": max(frame["regions"][name]["horizontal_jump_fraction"] for frame in files),
            "max_frame_vertical_jump_fraction": max(frame["regions"][name]["vertical_jump_fraction"] for frame in files),
            "min_frame_mean_rgb": [min(frame["regions"][name]["mean_rgb"][channel] for frame in files) for channel in range(3)]}
        if animated == "1" and bounded_motion:
            peers = {}
            pairs = []
            for sample, graphics_frame in captured:
                phase = graphics_frame % 240
                pose_tick = min(phase, 240 - phase)
                for previous in peers.get(pose_tick, ()):
                    differences = [abs(current - prior) for current, prior in zip(frames[sample], frames[previous])]
                    pairs.append({"samples": [previous, sample],
                        "graphics_frames": [captured[previous][1], graphics_frame], "pose_tick": pose_tick,
                        "mean_difference": statistics.fmean(differences), "p95_difference": percentile(differences, .95),
                        "max_difference": max(differences)})
                peers.setdefault(pose_tick, []).append(sample)
            metrics[name]["equal_pose_differences"] = pairs
    return {"frames": files, "regions": metrics, "region_bounds": region_bounds, "dimensions": list(EXPECTED_EXTENT),
        "view": view, "motion": "animated" if animated == "1" else "static", "frame_interval": frame_interval,
        "warmup_start_graphics_frame": warmup_start_frame, "warmup_elapsed_seconds": warmup_elapsed_seconds,
        "first_capture_graphics_frame": first_frame,
        "overlapping": bool(overlap_count),
        "route": "software" if "Loader: hardware ray tracing disabled before device creation" in log else "natural",
        "lagged_lighting": "RendererSystem: frame-lagged async lighting active history accepted" in log,
        "pose_contract": ("Static initial phase; unchanged camera and light." if animated == "0" else
            "Front-facing triangle-wave time min(frame%240,240-frame%240)*.3/120; unchanged camera and light."
            if bounded_motion else "Unidentified archived animated pose; equal-pose comparison omitted.")}


def check_quality(result):
    failures = []
    if result["motion"] == "static":
        for name, metrics in result["regions"].items():
            if metrics["p95_temporal_range"] > QUALITY_LIMITS["static_p95_temporal_range"]:
                failures.append(f"{name}: settled temporal range exceeds 6 channel levels")
    for frame in result["frames"]:
        prefix = f"frame {frame['graphics_frame']}"
        background = frame["regions"]["background"]["mean_rgb"]
        if any(abs(value - expected) > QUALITY_LIMITS["background_tolerance"]
            for value, expected in zip(background, (88, 98, 113))):
            failures.append(f"{prefix}: background differs from the fixed scene")
        for name, metrics in frame["regions"].items():
            if max(metrics["horizontal_jump_fraction"], metrics["vertical_jump_fraction"]) > QUALITY_LIMITS["max_frame_jump_fraction"]:
                failures.append(f"{prefix} {name}: spatial channel jumps affect more than 10% of neighboring samples")
        for name in ("box", "sphere", "capsule"):
            red, green, blue = frame["regions"][name]["mean_rgb"]
            if result["view"] == "direct":
                if max(red, green, blue) < QUALITY_LIMITS["direct_core_minimum"]:
                    failures.append(f"{prefix} {name}: direct control cap is nearly black")
                if name in ("box", "capsule") and max(abs(value - backdrop)
                    for value, backdrop in zip((red, green, blue), background)) < QUALITY_LIMITS["direct_background_separation"]:
                    failures.append(f"{prefix} {name}: direct control cap is indistinguishable from background")
                continue
            dominant = green if name == "sphere" else red
            margin = QUALITY_LIMITS["tint_margin"]
            valid_tint = (red >= green + margin and green >= blue + margin) if name == "box" else (
                green >= max(red, blue) + margin if name == "sphere" else red >= max(green, blue) + margin)
            if dominant < QUALITY_LIMITS["colored_core_minimum"] or not valid_tint:
                failures.append(f"{prefix} {name}: expected colored GI contribution is missing")
    return failures


def check_geometry(result):
    if result["motion"] != "static" or result["view"] not in ("normal", "position"):
        raise SmokeFailure("geometry regression requires a static normal or position view")
    failures = []
    for frame in result["frames"]:
        for name, probe in GEOMETRY_PROBES.items():
            point = frame["diagnostic_points"][name]
            expected = probe[result["view"]]
            errors = [abs(value - target) for value, target in zip(point["decoded"], expected)]
            prefix = f"frame {frame['graphics_frame']} {name}"
            if result["view"] == "normal":
                error = sum(value * value for value in errors) ** .5
                if error > GEOMETRY_LIMITS["normal_l2_error"]:
                    failures.append(f"{prefix}: physical cap normal L2 error {error:.6f} exceeds 0.04")
            elif any(error > tolerance for error, tolerance in zip(errors, point["component_tolerance"])):
                failures.append(f"{prefix}: physical cap position component errors {errors} exceed display-quantization tolerances {point['component_tolerance']}")
    return failures


def check_cap_state(result):
    if result["motion"] != "static" or result["view"] != "cap_state":
        raise SmokeFailure("cap-state regression requires a static cap_state view")
    failures = []
    for frame in result["frames"]:
        for name, (x, y) in CAP_STATE_REQUIRED_PROBES.items():
            point = frame["diagnostic_points"][name]
            for channel in STATE_CHANNELS["cap_state"]:
                if not point["state"][channel]:
                    failures.append(f"frame {frame['graphics_frame']} {name} ({x},{y}): {channel} is false")
    return failures


def check_spans(result):
    if result["motion"] != "static" or result["view"] != "span_state" or not result["overlapping"]:
        raise SmokeFailure("span regression requires a static overlapping span_state view")
    failures = []
    for frame in result["frames"]:
        for name, expected in SPAN_EXPECTATIONS.items():
            point = frame["diagnostic_points"][name]
            for channel, target in expected.items():
                if point[channel] != target:
                    failures.append(f"frame {frame['graphics_frame']} {name}: {channel}={point[channel]} expected {target}")
    return failures


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", type=Path)
    parser.add_argument("--working-directory", type=Path)
    parser.add_argument("--logserver-executable", type=Path)
    parser.add_argument("--output-directory", type=Path, required=True)
    parser.add_argument("--route", choices=("natural", "software"), default="natural")
    parser.add_argument("--view", choices=("full", "indirect", "direct", "normal", "position", *STATE_CHANNELS), default="full")
    parser.add_argument("--motion", choices=("static", "animated"), default="static")
    parser.add_argument("--frame-interval", type=int, default=FRAME_INTERVAL,
        help="Use 1 for consecutive frames with serialized readbacks, or 8 for asynchronous longer-term sampling.")
    parser.add_argument("--lagged-lighting", action="store_true")
    parser.add_argument("--overlapping", action="store_true", help="Add a static plane receiver at z+0.125 with the same cutter group.")
    parser.add_argument("--gpu-validation", action="store_true")
    parser.add_argument("--gpu-timing-output", type=Path)
    parser.add_argument("--timeout", type=float, default=150.0)
    parser.add_argument("--analysis-only", action="store_true")
    parser.add_argument("--check-quality", action="store_true")
    parser.add_argument("--check-geometry", action="store_true", help="Check physical cap probes in a static normal or position view.")
    parser.add_argument("--check-cap-state", action="store_true", help="Require produced, visible, depth-owning caps at all static regression probes.")
    parser.add_argument("--check-spans", action="store_true", help="Check independent spans for overlapping closed receivers and three controls.")
    parser.add_argument("--overwrite", action="store_true", help="Replace only this sequence's known capture artifacts.")
    args = parser.parse_args()
    if not 1 <= args.frame_interval <= 64:
        parser.error("--frame-interval must be between 1 and 64")
    if args.check_quality and args.view in ("normal", "position", *STATE_CHANNELS):
        parser.error("diagnostic views do not support the lighting quality gate")
    if args.check_geometry and (args.view not in ("normal", "position") or args.motion != "static"):
        parser.error("--check-geometry requires --motion static and --view normal or position")
    if args.check_cap_state and (args.view != "cap_state" or args.motion != "static"):
        parser.error("--check-cap-state requires --motion static and --view cap_state")
    if args.overlapping and args.motion != "static":
        parser.error("--overlapping requires --motion static")
    if args.check_spans and (args.view != "span_state" or args.motion != "static" or not args.overlapping):
        parser.error("--check-spans requires --motion static, --view span_state, and --overlapping")
    if not args.analysis_only and (not args.executable or not args.working_directory):
        parser.error("acquisition requires --executable and --working-directory")
    try:
        if not args.analysis_only:
            if capture_series(args) == SKIP_EXIT_CODE:
                return SKIP_EXIT_CODE
        result = measure_series(args.output_directory, args.frame_interval)
        for control in ("view", "motion", "route", "lagged_lighting", "overlapping"):
            if result[control] != getattr(args, control):
                raise SmokeFailure(f"logged temporal {control} differs from its requested value")
        result.update({
            "readback_contract": "Interval 1 waits for the accepted readback before advancing; other intervals poll asynchronously.",
            "warmup_contract": "After at least 360 graphics frames, render for at least 30 steady-clock seconds before selecting the first capture frame.",
            "timing_contract": "Interval 1 serializes readbacks for functional coverage; GPU timings describe only the matched capture workload.",
            "control_contract": "Direct view omits displayed GI; identical GI computation and direct lighting remain active."})
        if args.view == "cap_state":
            result["cap_state_contract"] = "RGB: raw removed count > 0, nearest radius-one cap visible, visible cap projected depth exactly equals completed G-buffer depth."
        elif args.view == "interval_state":
            result["interval_state_contract"] = "RGB: receiver span count > 0, any nonzero peel ID in the first four layers, any such peel ID has back-cap alpha > 0."
        elif args.view == "event_state":
            result["event_state_contract"] = "RGB: min(raw receiver event count,33)/33, front count/33, back count/33; scan at most 32 allocated layers and decode round(channel*33). Raw 33 is the overflow sentinel."
        elif args.view == "event_data":
            result["event_data_contract"] = "RGB: 1 when receiver span count > 0, otherwise existing span flags/33; exactly two events with front/back receiver IDs matching the first nonzero peel ID; both matching faces present with finite depths and back depth > front depth. Decode span presence with red > .5 and missing-span flags with round(red*33)."
        elif args.view == "event_order":
            result["event_order_contract"] = "RGB: 1 when receiver span count > 0, otherwise existing span flags/33; raw event indices 0/1 encode FRONT=1 or BACK=.5 only when exactly two events match the first nonzero peel receiver, contain one front and one back, and have finite back depth > front depth. Other pairs encode green/blue=0. Decode valid pairs with both channels > .25, front with channel > .75; invalid face fields are null."
        elif args.view == "span_state":
            result["span_state_contract"] = "RGB: receiver span count/33, existing span flags/33, and raw receiver event count/33. Decode each channel with round(channel*33)."
        if args.overlapping:
            result["overlapping_contract"] = "One additional static plane receiver at the same x/y and z+0.125 shares the plane's group, cutter, and diagnostic material; four receiver event depths are distinct at the interior probe."
        if args.gpu_timing_output:
            result["gpu_timing_file"] = str(args.gpu_timing_output.resolve())
        failures = check_quality(result) if args.check_quality else []
        if args.check_quality:
            result["quality_limits"] = QUALITY_LIMITS
            result["quality_failures"] = failures
        if args.check_geometry:
            geometry_failures = check_geometry(result)
            result["geometry_probes"] = GEOMETRY_PROBES
            result["geometry_limits"] = GEOMETRY_LIMITS
            result["geometry_failures"] = geometry_failures
            failures += geometry_failures
        if args.check_cap_state:
            cap_state_failures = check_cap_state(result)
            result["cap_state_required_probes"] = CAP_STATE_REQUIRED_PROBES
            result["cap_state_gate_contract"] = "Every captured frame must have a produced removed interval, a visible selected cap, and exact completed G-buffer depth ownership at all four central probes and the 12-pixel capsule rim block. Exploratory outer-rim probes are not acceptance requirements."
            result["cap_state_failures"] = cap_state_failures
            failures += cap_state_failures
        if args.check_spans:
            span_failures = check_spans(result)
            result["span_required_probes"] = {name: GEOMETRY_PROBES[name]["pixel"] for name in SPAN_EXPECTATIONS}
            result["span_expectations"] = SPAN_EXPECTATIONS
            result["span_failures"] = span_failures
            failures += span_failures
        (args.output_directory / "manifest.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
        if failures:
            raise SmokeFailure("; ".join(failures[:8]) + f" ({len(failures)} failed checks)")
        print(json.dumps({"samples": SAMPLE_COUNT, "regions": result["regions"]}))
        return 0
    except (SmokeFailure, OSError, ValueError, subprocess.SubprocessError) as error:
        print("FAIL: " + str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
