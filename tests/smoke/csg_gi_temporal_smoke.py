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

from window_capture_smoke import (SKIP_EXIT_CODE, STRICT_LOG_FAILURE_MESSAGES, SmokeFailure, read_bmp_24_rows, validate_expected_log_text)

LIT_CAPTURE_TIMEOUT_MARGIN_SECONDS = 45.0
LIT_MINIMUM_FRAME_INTERVAL = 1
LIT_MAXIMUM_FRAME_INTERVAL = 64

LIT_PARTIAL_SUFFIX = ".partial"
LIT_TEMPORAL_WARMUP_COMPLETE = "CsgVisibleSmokeProject: temporal warmup complete elapsed_seconds="
LIT_TEMPORAL_SERIES_COMPLETE = "CsgVisibleSmokeProject: temporal series complete samples=32"

LIT_BACKGROUND = "background"
LIT_SPHERE = "sphere"
LIT_PIXEL = "pixel"
LIT_NORMAL = "normal"
LIT_POSITION = "position"
LIT_CAPSULE = "capsule"
LIT_EVENT_DATA = "event_data"
LIT_SPAN_PRESENT = "span_present"
LIT_EVENT_ORDER = "event_order"
LIT_FIRST_FRONT = "first_front"
LIT_SECOND_FRONT = "second_front"
LIT_SPAN_STATE = "span_state"
LIT_MANIFEST_JSON = "manifest.json"
LIT_RUNTIME_LOG = "runtime.log"
LIT_NWB_CSG_GI_TEMPORAL = "NWB_CSG_GI_TEMPORAL"
LIT_NWB_GPU_TIMING_FILE = "NWB_GPU_TIMING_FILE"
LIT_ANIMATED = "animated"
LIT_EXECUTABLE = "--executable"
LIT_WORKING_DIRECTORY = "--working-directory"
LIT_TIMEOUT = "--timeout"
LIT_EXPECT_LOG_MESSAGE = "--expect-log-message"
LIT_CSGVISIBLESMOKEPROJECT_TEMPORAL_WARMUP = "CsgVisibleSmokeProject: temporal warmup started graphics frame"
LIT_CSGVISIBLESMOKEPROJECT_SHUTDOWN = "CsgVisibleSmokeProject: shutdown"
LIT_RENDERERSYSTEM_DISPATCHED_SURFEL_GI_RE = "RendererSystem: dispatched surfel GI resolve"
LIT_GPU_VALIDATION = "--gpu-validation"
LIT_SOFTWARE = "software"
LIT_LOADER_HARDWARE_RAY_TRACING_DISABLED_B = "Loader: hardware ray tracing disabled before device creation"
LIT_REJECT_LOG_MESSAGE = "--reject-log-message"
LIT_RENDERERSYSTEM_FRAME_LAGGED_ASYNC_LIGH = "RendererSystem: frame-lagged async lighting active history accepted"
LIT_LOGSERVER_EXECUTABLE = "--logserver-executable"
LIT_CHANNEL_JUMP = "channel_jump"
LIT_MEAN_RGB = "mean_rgb"
LIT_HORIZONTAL_JUMP_FRACTION = "horizontal_jump_fraction"
LIT_VERTICAL_JUMP_FRACTION = "vertical_jump_fraction"
LIT_UTF_8 = "utf-8"
LIT_GRAPHICS_FRAME = "graphics_frame"
LIT_REGIONS = "regions"
LIT_DISPLAY_RGB = "display_rgb"
LIT_DECODED = "decoded"
LIT_COMPONENT_TOLERANCE = "component_tolerance"
LIT_DIAGNOSTIC_POINTS = "diagnostic_points"
LIT_EVENT_STATE = "event_state"
LIT_FAILED_SPAN_FLAGS = "failed_span_flags"
LIT_STATE = "state"
LIT_P95_TEMPORAL_RANGE = "p95_temporal_range"
LIT_SAMPLES = "samples"
LIT_FRAMES = "frames"
LIT_VIEW = "view"
LIT_MOTION = "motion"
LIT_STATIC = "static"
LIT_OVERLAPPING = "overlapping"
LIT_ROUTE = "route"
LIT_NATURAL = "natural"
LIT_LAGGED_LIGHTING = "lagged_lighting"
LIT_BOX = "box"
LIT_DIRECT = "direct"
LIT_CAP_STATE = "cap_state"
LIT_FULL = "full"
LIT_STORE_TRUE = "store_true"


SAMPLE_COUNT = 32
MINIMUM_FRAME = 360
WARMUP_SECONDS = 30
FRAME_INTERVAL = 8
EXPECTED_EXTENT = (1280, 900)
REGIONS = {"plane": (454, 324, 32, 32), "box": (826, 324, 32, 32), "sphere": (454, 576, 32, 32), "capsule": (826, 576, 32, 32), "box_upper_band": (836, 290, 40, 8), "sphere_upper_band": (414, 552, 40, 16), "capsule_stripes": (805, 576, 32, 40),
    LIT_BACKGROUND: (640, 450, 32, 32)}
STATIC_EXTERIOR_REGIONS = {"sphere_exterior": (502, 578, 6, 24), "capsule_exterior": (882, 576, 6, 24), "box_exterior": (896, 324, 3, 20)}
ANIMATED_INTERIOR_REGIONS = {"box_inner_wall": (832, 316, 24, 8)}
QUALITY_LIMITS = {"channel_jump": 20, "max_frame_jump_fraction": .10, "static_p95_temporal_range": 6, "colored_core_minimum": 32, "tint_margin": 4, "direct_core_minimum": 16, "direct_background_separation": 8, "background_tolerance": 2}
# Analytical ray/cutter intersections for the static camera (0,.75,-6.5), default near=.001/far=10000, 1280x900 extent, and shape yaw phases 0/.18/.36/.54.
# These distinguish physical caps from enclosing faces.
GEOMETRY_PROBES = { "plane": {"pixel": (430, 320), "normal": (0., 0., -1.), "position": (-1.747126065, 1.829965754, 0.)}, "box": {"pixel": (835, 315), "normal": (-.179029573, 0., -.983843693), "position": (1.721953360, 1.934668680, .365114160)},
    LIT_SPHERE: {LIT_PIXEL: (415, 570), LIT_NORMAL: (.318619304, -.078551107, -.944622392), LIT_POSITION: (-1.881587477, -.259938935, .032526944)},
    LIT_CAPSULE: {LIT_PIXEL: (805, 570), LIT_NORMAL: (-.177317767, 0., -.984153651), LIT_POSITION: (1.361729587, -.241470787, -.086929462)}}
GEOMETRY_LIMITS = {"normal_l2_error": .04, "position_component_max": .06, "position_arithmetic_allowance": .005}
CAP_STATE_REQUIRED_PROBES = {name: probe["pixel"] for name, probe in GEOMETRY_PROBES.items()}
# This interior rim block previously lost its receiver span when raster events arrived BACK first.
CAP_STATE_REQUIRED_PROBES.update({f"capsule_{x}_{y}": (x, y) for y in range(604, 608) for x in range(784, 787)})
CAP_STATE_PROBES = dict(CAP_STATE_REQUIRED_PROBES)
CAP_STATE_PROBES.update({f"capsule_{x}_{y}": (x, y) for y in range(604, 608) for x in (783, 787)})
STATE_CHANNELS = {"cap_state": ("raw_removed_count", "nearest_cap_visible", "owns_gbuffer_depth"), "interval_state": ("receiver_span_count", "peel_id_present", "peel_cap_coverage"), "event_state": ("raw_count", "front_count", "back_count"),
    LIT_EVENT_DATA: (LIT_SPAN_PRESENT, "matching_event_pair", "ordered_event_depths"), LIT_EVENT_ORDER: (LIT_SPAN_PRESENT, LIT_FIRST_FRONT, LIT_SECOND_FRONT), LIT_SPAN_STATE: ("span_count", "span_flags", "event_count")}
EVENT_DATA_FLAGS_MARKER = "CsgVisibleSmokeProject: event-data red stores missing-span flags divided by 33"
EVENT_ORDER_MARKER = "CsgVisibleSmokeProject: event-order RGB stores span/flags and validated raw FRONT=1 BACK=0.5"
SPAN_STATE_MARKER = "CsgVisibleSmokeProject: span-state RGB stores span count, flags, raw event count divided by 33"
OVERLAPPING_MARKER = "CsgVisibleSmokeProject: overlapping plane receiver created z_offset=0.125"
SPAN_EXPECTATIONS = {name: {"span_count": 2 if name == "plane" else 1, "span_flags": 0, "event_count": 4 if name == "plane" else 2} for name in GEOMETRY_PROBES}


def sample_path(directory, sample):
    return directory / ("series.bmp" if sample + 1 == SAMPLE_COUNT else f"series.bmp.{sample}.bmp")


def capture_series(args):
    args.output_directory.mkdir(parents=True, exist_ok=True)
    output = sample_path(args.output_directory, SAMPLE_COUNT - 1)
    if args.overwrite:
        for sample in range(SAMPLE_COUNT):
            path = sample_path(args.output_directory, sample)
            path.unlink(missing_ok=True)
            path.with_name(path.name + LIT_PARTIAL_SUFFIX).unlink(missing_ok=True)
        for name in (LIT_MANIFEST_JSON, LIT_RUNTIME_LOG):
            (args.output_directory / name).unlink(missing_ok=True)
    if any(sample_path(args.output_directory, index).exists() for index in range(SAMPLE_COUNT)):
        raise SmokeFailure("temporal acquisition requires an empty capture directory")
    environment = os.environ.copy()
    for name in tuple(environment):
        if name.startswith(("NWB_GI_SMOKE_", "NWB_RENDERER_BASELINE_", LIT_NWB_CSG_GI_TEMPORAL)):
            environment.pop(name)
    environment.pop(LIT_NWB_GPU_TIMING_FILE, None)
    if args.gpu_timing_output:
        args.gpu_timing_output.parent.mkdir(parents=True, exist_ok=True)
        if args.overwrite:
            args.gpu_timing_output.unlink(missing_ok=True)
        elif args.gpu_timing_output.exists():
            raise SmokeFailure("temporal acquisition requires a fresh GPU timing file")
        environment[LIT_NWB_GPU_TIMING_FILE] = str(args.gpu_timing_output.resolve())
    environment.update({LIT_NWB_CSG_GI_TEMPORAL: "1", "NWB_CSG_GI_TEMPORAL_VIEW": args.view, "NWB_CSG_GI_TEMPORAL_ANIMATED": str(int(args.motion == LIT_ANIMATED)), "NWB_CSG_GI_TEMPORAL_INTERVAL": str(args.frame_interval),
        "NWB_CSG_GI_TEMPORAL_LAGGED": str(int(args.lagged_lighting)), "NWB_CSG_GI_TEMPORAL_OVERLAPPING": str(int(args.overlapping))})
    command = [sys.executable, str(Path(__file__).with_name("window_capture_smoke.py")), LIT_EXECUTABLE, str(args.executable), LIT_WORKING_DIRECTORY, str(args.working_directory),
        "--output", str(output), "--application-capture", "--application-capture-frame-count", "1", LIT_TIMEOUT, str(args.timeout), "--log-output", str(args.output_directory / LIT_RUNTIME_LOG),
        LIT_EXPECT_LOG_MESSAGE, f"CsgVisibleSmokeProject: temporal scene animated={int(args.motion == LIT_ANIMATED)} view={args.view} minimum_frame=360 warmup_seconds=30 interval={args.frame_interval} samples=32",
        LIT_EXPECT_LOG_MESSAGE, LIT_CSGVISIBLESMOKEPROJECT_TEMPORAL_WARMUP, LIT_EXPECT_LOG_MESSAGE, LIT_TEMPORAL_WARMUP_COMPLETE,
        LIT_EXPECT_LOG_MESSAGE, LIT_TEMPORAL_SERIES_COMPLETE, LIT_EXPECT_LOG_MESSAGE, LIT_CSGVISIBLESMOKEPROJECT_SHUTDOWN, LIT_EXPECT_LOG_MESSAGE, LIT_RENDERERSYSTEM_DISPATCHED_SURFEL_GI_RE,
        LIT_GPU_VALIDATION if args.gpu_validation else "--no-gpu-validation"]
    if args.route == LIT_SOFTWARE:
        command += ["--application-arg=--disable-hardware-ray-tracing", LIT_EXPECT_LOG_MESSAGE, LIT_LOADER_HARDWARE_RAY_TRACING_DISABLED_B, LIT_REJECT_LOG_MESSAGE, "RendererSystem: created surfel HW trace compute pipeline"]
    if args.lagged_lighting:
        command += [LIT_EXPECT_LOG_MESSAGE, "RendererSystem: frame-lagged async lighting bootstrap accepted", LIT_EXPECT_LOG_MESSAGE, LIT_RENDERERSYSTEM_FRAME_LAGGED_ASYNC_LIGH,
            "--skip-log-message", "RendererSystem: frame-lagged async lighting Graphics queue route accepted (no dedicated Compute queue"]
    if args.view == LIT_EVENT_DATA:
        command += [LIT_EXPECT_LOG_MESSAGE, EVENT_DATA_FLAGS_MARKER]
    elif args.view == LIT_EVENT_ORDER:
        command += [LIT_EXPECT_LOG_MESSAGE, EVENT_ORDER_MARKER]
    elif args.view == LIT_SPAN_STATE:
        command += [LIT_EXPECT_LOG_MESSAGE, SPAN_STATE_MARKER]
    if args.overlapping:
        command += [LIT_EXPECT_LOG_MESSAGE, OVERLAPPING_MARKER]
    else:
        command += [LIT_REJECT_LOG_MESSAGE, OVERLAPPING_MARKER]
    if args.logserver_executable:
        command += [LIT_LOGSERVER_EXECUTABLE, str(args.logserver_executable)]
    else:
        command.append("--no-logserver")
    result = subprocess.run(command, env=environment, check=False, timeout=args.timeout + LIT_CAPTURE_TIMEOUT_MARGIN_SECONDS)
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
                    horizontal_jumps += abs(value - rows[y][x - 1][channel]) > QUALITY_LIMITS[LIT_CHANNEL_JUMP]
                if y > center_y - half_height:
                    vertical_jumps += abs(value - rows[y - 1][x][channel]) > QUALITY_LIMITS[LIT_CHANNEL_JUMP]
    return {LIT_MEAN_RGB: [value / (4 * half_width * half_height) for value in total], LIT_HORIZONTAL_JUMP_FRACTION: horizontal_jumps / ((2 * half_width - 1) * 2 * half_height * 3),
        LIT_VERTICAL_JUMP_FRACTION: vertical_jumps / ((2 * half_height - 1) * 2 * half_width * 3)}


def decode_diagnostic(rgb, view):
    display_linear = [value / 255 / 12.92 if value / 255 <= .04045 else ((value / 255 + .055) / 1.055) ** 2.4 for value in rgb]
    # The fixture preserves the renderer's exposure=1, Reinhard shoulder=0.65 presentation defaults.
    if any(value >= 1 for value in display_linear):
        raise SmokeFailure("diagnostic color cannot invert saturated Reinhard presentation")
    encoded = [.65 * value / (1 - value) for value in display_linear]
    if view in STATE_CHANNELS:
        return encoded
    return [value * scale - offset for value, scale, offset in zip(encoded, (2, 2, 2) if view == LIT_NORMAL else (6, 6, 2), (1, 1, 1) if view == LIT_NORMAL else (3, 3, 1))]


def position_quantization_tolerance(rgb, decoded):
    lower = decode_diagnostic([max(value - .5, 0) for value in rgb], LIT_POSITION)
    upper = decode_diagnostic([min(value + .5, 254.5) for value in rgb], LIT_POSITION)
    return [min(GEOMETRY_LIMITS["position_component_max"], max(value - low, high - value) + GEOMETRY_LIMITS["position_arithmetic_allowance"]) for value, low, high in zip(decoded, lower, upper)]


def measure_series(directory, frame_interval=FRAME_INTERVAL):
    log = (directory / LIT_RUNTIME_LOG).read_text(encoding=LIT_UTF_8)
    validate_expected_log_text(log, (LIT_TEMPORAL_SERIES_COMPLETE, LIT_CSGVISIBLESMOKEPROJECT_SHUTDOWN, LIT_RENDERERSYSTEM_DISPATCHED_SURFEL_GI_RE), STRICT_LOG_FAILURE_MESSAGES)
    setups = re.findall(r"CsgVisibleSmokeProject: temporal scene animated=([01]) view=(full|indirect|direct|normal|position|cap_state|interval_state|event_state|event_data|event_order|span_state) minimum_frame=(\d+) warmup_seconds=(\d+) interval=(\d+) samples=(\d+)", log)
    if len(setups) != 1:
        raise SmokeFailure("temporal series must have exactly one logged scene setup")
    animated, view, minimum_frame, warmup_seconds, logged_interval, sample_count = setups[0]
    if view == LIT_EVENT_DATA and log.count(EVENT_DATA_FLAGS_MARKER) != 1:
        raise SmokeFailure("event-data series must identify its missing-span flag encoding")
    if view == LIT_EVENT_ORDER and log.count(EVENT_ORDER_MARKER) != 1:
        raise SmokeFailure("event-order series must identify its validated raw face encoding")
    if view == LIT_SPAN_STATE and log.count(SPAN_STATE_MARKER) != 1:
        raise SmokeFailure("span-state series must identify its count and flag encoding")
    overlap_count = log.count(OVERLAPPING_MARKER)
    if overlap_count > 1 or (overlap_count and animated != "0"):
        raise SmokeFailure("overlapping receiver series requires exactly one static duplicate")
    if (int(minimum_frame), int(warmup_seconds), int(logged_interval), int(sample_count)) != (MINIMUM_FRAME, WARMUP_SECONDS, frame_interval, SAMPLE_COUNT):
        raise SmokeFailure("logged temporal setup differs from the requested sample schedule")
    warmup_starts = re.findall(r"CsgVisibleSmokeProject: temporal warmup started graphics frame (\d+)", log)
    warmup_completions = re.findall(r"CsgVisibleSmokeProject: temporal warmup complete elapsed_seconds=(\d+\.\d+) first_frame=(\d+)", log)
    if len(warmup_starts) != 1 or len(warmup_completions) != 1:
        raise SmokeFailure("temporal series must identify one live warmup start and completion")
    warmup_start_frame = int(warmup_starts[0])
    elapsed, first_frame = warmup_completions[0]
    warmup_elapsed_seconds = float(elapsed)
    first_frame = int(first_frame)
    if (warmup_start_frame < MINIMUM_FRAME or first_frame <= warmup_start_frame or not math.isfinite(warmup_elapsed_seconds) or warmup_elapsed_seconds < WARMUP_SECONDS):
        raise SmokeFailure("temporal series must render at least 360 frames before a 30-second live warmup")
    captured = [(int(sample), int(frame)) for sample, frame in re.findall(r"CsgVisibleSmokeProject: temporal sample (\d+) graphics frame (\d+)", log)]
    expected = [(sample, first_frame + frame_interval * sample) for sample in range(SAMPLE_COUNT)]
    if captured != expected:
        raise SmokeFailure(f"temporal source-frame sequence differs from its deterministic schedule: {captured}")
    if not (log.index(LIT_CSGVISIBLESMOKEPROJECT_TEMPORAL_WARMUP) < log.index(LIT_TEMPORAL_WARMUP_COMPLETE) < log.index("CsgVisibleSmokeProject: temporal sample 0 graphics frame")):
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
        if path.with_name(path.name + LIT_PARTIAL_SUFFIX).exists():
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
        files.append({"sample": index, LIT_GRAPHICS_FRAME: frame, "file": path.name, "sha256": hashlib.sha256(path.read_bytes()).hexdigest(), LIT_REGIONS: {name: measure_region_frame(rows, region) for name, region in region_bounds.items()}})
        if view in (LIT_NORMAL, LIT_POSITION):
            points = {}
            for name, probe in GEOMETRY_PROBES.items():
                x, y = probe[LIT_PIXEL]
                rgb = rows[y][x]
                decoded = decode_diagnostic(rgb, view)
                points[name] = {LIT_PIXEL: [x, y], LIT_DISPLAY_RGB: list(rgb), LIT_DECODED: decoded}
                if view == LIT_POSITION:
                    points[name][LIT_COMPONENT_TOLERANCE] = position_quantization_tolerance(rgb, decoded)
            files[-1][LIT_DIAGNOSTIC_POINTS] = points
        elif view in STATE_CHANNELS:
            points = {}
            for name, (x, y) in CAP_STATE_PROBES.items():
                rgb = rows[y][x]
                decoded = decode_diagnostic(rgb, view)
                points[name] = {LIT_PIXEL: [x, y], LIT_DISPLAY_RGB: list(rgb), LIT_DECODED: decoded}
                if view in (LIT_EVENT_STATE, LIT_SPAN_STATE):
                    points[name].update({label: round(value * 33) for label, value in zip(STATE_CHANNELS[view], decoded)})
                    if view == LIT_EVENT_STATE:
                        points[name]["raw_count_overflow"] = points[name]["raw_count"] >= 33
                elif view == LIT_EVENT_DATA:
                    points[name].update({label: value > .5 for label, value in zip(STATE_CHANNELS[view], decoded)})
                    if not points[name][LIT_SPAN_PRESENT]:
                        points[name][LIT_FAILED_SPAN_FLAGS] = round(decoded[0] * 33)
                elif view == LIT_EVENT_ORDER:
                    valid_pair = decoded[1] > .25 and decoded[2] > .25
                    points[name].update({LIT_SPAN_PRESENT: decoded[0] > .5, "validated_pair": valid_pair, LIT_FIRST_FRONT: decoded[1] > .75 if valid_pair else None, LIT_SECOND_FRONT: decoded[2] > .75 if valid_pair else None})
                    if not points[name][LIT_SPAN_PRESENT]:
                        points[name][LIT_FAILED_SPAN_FLAGS] = round(decoded[0] * 33)
                else:
                    points[name][LIT_STATE] = {label: value > .5 for label, value in zip(STATE_CHANNELS[view], decoded)}
            files[-1][LIT_DIAGNOSTIC_POINTS] = points
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
                    horizontal_changes.extend(abs(frame[pixel * 3 + channel] - frame[(pixel - 1) * 3 + channel]) for channel in range(3))
        metrics[name] = {"sampled_pixels": len(frames[0]) // 3, LIT_MEAN_RGB: [statistics.fmean(value for frame in frames for value in frame[channel::3]) for channel in range(3)],
            "mean_temporal_range": statistics.fmean(ranges), LIT_P95_TEMPORAL_RANGE: percentile(ranges, .95), "max_temporal_range": max(ranges), "mean_temporal_standard_deviation": statistics.fmean(deviations),
            "mean_adjacent_change": statistics.fmean(changes), "p95_adjacent_change": percentile(changes, .95), "max_adjacent_change": max(changes), "mean_horizontal_change": statistics.fmean(horizontal_changes),
            "p95_horizontal_change": percentile(horizontal_changes, .95), "large_horizontal_change_fraction": sum(change > QUALITY_LIMITS[LIT_CHANNEL_JUMP] for change in horizontal_changes) / len(horizontal_changes),
            "max_frame_horizontal_jump_fraction": max(frame[LIT_REGIONS][name][LIT_HORIZONTAL_JUMP_FRACTION] for frame in files), "max_frame_vertical_jump_fraction": max(frame[LIT_REGIONS][name][LIT_VERTICAL_JUMP_FRACTION] for frame in files),
            "min_frame_mean_rgb": [min(frame[LIT_REGIONS][name][LIT_MEAN_RGB][channel] for frame in files) for channel in range(3)]}
        if animated == "1" and bounded_motion:
            peers = {}
            pairs = []
            for sample, graphics_frame in captured:
                phase = graphics_frame % 240
                pose_tick = min(phase, 240 - phase)
                for previous in peers.get(pose_tick, ()):
                    differences = [abs(current - prior) for current, prior in zip(frames[sample], frames[previous])]
                    pairs.append({LIT_SAMPLES: [previous, sample], "graphics_frames": [captured[previous][1], graphics_frame], "pose_tick": pose_tick, "mean_difference": statistics.fmean(differences), "p95_difference": percentile(differences, .95),
                        "max_difference": max(differences)})
                peers.setdefault(pose_tick, []).append(sample)
            metrics[name]["equal_pose_differences"] = pairs
    return {LIT_FRAMES: files, LIT_REGIONS: metrics, "region_bounds": region_bounds, "dimensions": list(EXPECTED_EXTENT), LIT_VIEW: view, LIT_MOTION: LIT_ANIMATED if animated == "1" else LIT_STATIC, "frame_interval": frame_interval,
        "warmup_start_graphics_frame": warmup_start_frame, "warmup_elapsed_seconds": warmup_elapsed_seconds, "first_capture_graphics_frame": first_frame, LIT_OVERLAPPING: bool(overlap_count),
        LIT_ROUTE: LIT_SOFTWARE if LIT_LOADER_HARDWARE_RAY_TRACING_DISABLED_B in log else LIT_NATURAL, LIT_LAGGED_LIGHTING: LIT_RENDERERSYSTEM_FRAME_LAGGED_ASYNC_LIGH in log,
        "pose_contract": ("Static initial phase; unchanged camera and light." if animated == "0" else "Front-facing triangle-wave time min(frame%240,240-frame%240)*.3/120; unchanged camera and light."
            if bounded_motion else "Unidentified archived animated pose; equal-pose comparison omitted.")}


def check_quality(result):
    failures = []
    if result[LIT_MOTION] == LIT_STATIC:
        for name, metrics in result[LIT_REGIONS].items():
            if metrics[LIT_P95_TEMPORAL_RANGE] > QUALITY_LIMITS["static_p95_temporal_range"]:
                failures.append(f"{name}: settled temporal range exceeds 6 channel levels")
    for frame in result[LIT_FRAMES]:
        prefix = f"frame {frame[LIT_GRAPHICS_FRAME]}"
        background = frame[LIT_REGIONS][LIT_BACKGROUND][LIT_MEAN_RGB]
        if any(abs(value - expected) > QUALITY_LIMITS["background_tolerance"] for value, expected in zip(background, (88, 98, 113))):
            failures.append(f"{prefix}: background differs from the fixed scene")
        for name, metrics in frame[LIT_REGIONS].items():
            if max(metrics[LIT_HORIZONTAL_JUMP_FRACTION], metrics[LIT_VERTICAL_JUMP_FRACTION]) > QUALITY_LIMITS["max_frame_jump_fraction"]:
                failures.append(f"{prefix} {name}: spatial channel jumps affect more than 10% of neighboring samples")
        for name in (LIT_BOX, LIT_SPHERE, LIT_CAPSULE):
            red, green, blue = frame[LIT_REGIONS][name][LIT_MEAN_RGB]
            if result[LIT_VIEW] == LIT_DIRECT:
                if max(red, green, blue) < QUALITY_LIMITS["direct_core_minimum"]:
                    failures.append(f"{prefix} {name}: direct control cap is nearly black")
                if name in (LIT_BOX, LIT_CAPSULE) and max(abs(value - backdrop) for value, backdrop in zip((red, green, blue), background)) < QUALITY_LIMITS["direct_background_separation"]:
                    failures.append(f"{prefix} {name}: direct control cap is indistinguishable from background")
                continue
            dominant = green if name == LIT_SPHERE else red
            margin = QUALITY_LIMITS["tint_margin"]
            valid_tint = (red >= green + margin and green >= blue + margin) if name == LIT_BOX else (green >= max(red, blue) + margin if name == LIT_SPHERE else red >= max(green, blue) + margin)
            if dominant < QUALITY_LIMITS["colored_core_minimum"] or not valid_tint:
                failures.append(f"{prefix} {name}: expected colored GI contribution is missing")
    return failures


def check_geometry(result):
    if result[LIT_MOTION] != LIT_STATIC or result[LIT_VIEW] not in (LIT_NORMAL, LIT_POSITION):
        raise SmokeFailure("geometry regression requires a static normal or position view")
    failures = []
    for frame in result[LIT_FRAMES]:
        for name, probe in GEOMETRY_PROBES.items():
            point = frame[LIT_DIAGNOSTIC_POINTS][name]
            expected = probe[result[LIT_VIEW]]
            errors = [abs(value - target) for value, target in zip(point[LIT_DECODED], expected)]
            prefix = f"frame {frame[LIT_GRAPHICS_FRAME]} {name}"
            if result[LIT_VIEW] == LIT_NORMAL:
                error = sum(value * value for value in errors) ** .5
                if error > GEOMETRY_LIMITS["normal_l2_error"]:
                    failures.append(f"{prefix}: physical cap normal L2 error {error:.6f} exceeds 0.04")
            elif any(error > tolerance for error, tolerance in zip(errors, point[LIT_COMPONENT_TOLERANCE])):
                failures.append(f"{prefix}: physical cap position component errors {errors} exceed display-quantization tolerances {point[LIT_COMPONENT_TOLERANCE]}")
    return failures


def check_cap_state(result):
    if result[LIT_MOTION] != LIT_STATIC or result[LIT_VIEW] != LIT_CAP_STATE:
        raise SmokeFailure("cap-state regression requires a static cap_state view")
    failures = []
    for frame in result[LIT_FRAMES]:
        for name, (x, y) in CAP_STATE_REQUIRED_PROBES.items():
            point = frame[LIT_DIAGNOSTIC_POINTS][name]
            for channel in STATE_CHANNELS[LIT_CAP_STATE]:
                if not point[LIT_STATE][channel]:
                    failures.append(f"frame {frame[LIT_GRAPHICS_FRAME]} {name} ({x},{y}): {channel} is false")
    return failures


def check_spans(result):
    if result[LIT_MOTION] != LIT_STATIC or result[LIT_VIEW] != LIT_SPAN_STATE or not result[LIT_OVERLAPPING]:
        raise SmokeFailure("span regression requires a static overlapping span_state view")
    failures = []
    for frame in result[LIT_FRAMES]:
        for name, expected in SPAN_EXPECTATIONS.items():
            point = frame[LIT_DIAGNOSTIC_POINTS][name]
            for channel, target in expected.items():
                if point[channel] != target:
                    failures.append(f"frame {frame[LIT_GRAPHICS_FRAME]} {name}: {channel}={point[channel]} expected {target}")
    return failures


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(LIT_EXECUTABLE, type=Path)
    parser.add_argument(LIT_WORKING_DIRECTORY, type=Path)
    parser.add_argument(LIT_LOGSERVER_EXECUTABLE, type=Path)
    parser.add_argument("--output-directory", type=Path, required=True)
    parser.add_argument("--route", choices=(LIT_NATURAL, LIT_SOFTWARE), default=LIT_NATURAL)
    parser.add_argument("--view", choices=(LIT_FULL, "indirect", LIT_DIRECT, LIT_NORMAL, LIT_POSITION, *STATE_CHANNELS), default=LIT_FULL)
    parser.add_argument("--motion", choices=(LIT_STATIC, LIT_ANIMATED), default=LIT_STATIC)
    parser.add_argument("--frame-interval", type=int, default=FRAME_INTERVAL, help="Use 1 for consecutive frames with serialized readbacks, or 8 for asynchronous longer-term sampling.")
    parser.add_argument("--lagged-lighting", action=LIT_STORE_TRUE)
    parser.add_argument("--overlapping", action=LIT_STORE_TRUE, help="Add a static plane receiver at z+0.125 with the same cutter group.")
    parser.add_argument(LIT_GPU_VALIDATION, action=argparse.BooleanOptionalAction, default=False, help="Request graphics validation; use --no-gpu-validation for a fin application.")
    parser.add_argument("--gpu-timing-output", type=Path)
    parser.add_argument(LIT_TIMEOUT, type=float, default=150.0)
    parser.add_argument("--analysis-only", action=LIT_STORE_TRUE)
    parser.add_argument("--check-quality", action=LIT_STORE_TRUE)
    parser.add_argument("--check-geometry", action=LIT_STORE_TRUE, help="Check physical cap probes in a static normal or position view.")
    parser.add_argument("--check-cap-state", action=LIT_STORE_TRUE, help="Require produced, visible, depth-owning caps at all static regression probes.")
    parser.add_argument("--check-spans", action=LIT_STORE_TRUE, help="Check independent spans for overlapping closed receivers and three controls.")
    parser.add_argument("--overwrite", action=LIT_STORE_TRUE, help="Replace only this sequence's known capture artifacts.")
    args = parser.parse_args()
    if not LIT_MINIMUM_FRAME_INTERVAL <= args.frame_interval <= LIT_MAXIMUM_FRAME_INTERVAL:
        parser.error("--frame-interval must be between 1 and 64")
    if args.check_quality and args.view in (LIT_NORMAL, LIT_POSITION, *STATE_CHANNELS):
        parser.error("diagnostic views do not support the lighting quality gate")
    if args.check_geometry and (args.view not in (LIT_NORMAL, LIT_POSITION) or args.motion != LIT_STATIC):
        parser.error("--check-geometry requires --motion static and --view normal or position")
    if args.check_cap_state and (args.view != LIT_CAP_STATE or args.motion != LIT_STATIC):
        parser.error("--check-cap-state requires --motion static and --view cap_state")
    if args.overlapping and args.motion != LIT_STATIC:
        parser.error("--overlapping requires --motion static")
    if args.check_spans and (args.view != LIT_SPAN_STATE or args.motion != LIT_STATIC or not args.overlapping):
        parser.error("--check-spans requires --motion static, --view span_state, and --overlapping")
    if not args.analysis_only and (not args.executable or not args.working_directory):
        parser.error("acquisition requires --executable and --working-directory")
    try:
        if not args.analysis_only:
            if capture_series(args) == SKIP_EXIT_CODE:
                return SKIP_EXIT_CODE
        result = measure_series(args.output_directory, args.frame_interval)
        for control in (LIT_VIEW, LIT_MOTION, LIT_ROUTE, LIT_LAGGED_LIGHTING, LIT_OVERLAPPING):
            if result[control] != getattr(args, control):
                raise SmokeFailure(f"logged temporal {control} differs from its requested value")
        result.update({ "readback_contract": "Interval 1 waits for the accepted readback before advancing; other intervals poll asynchronously.",
            "warmup_contract": "After at least 360 graphics frames, render for at least 30 steady-clock seconds before selecting the first capture frame.",
            "timing_contract": "Interval 1 serializes readbacks for functional coverage; GPU timings describe only the matched capture workload.",
            "control_contract": "Direct view omits displayed GI; identical GI computation and direct lighting remain active."})
        if args.view == LIT_CAP_STATE:
            result["cap_state_contract"] = "RGB: raw removed count > 0, nearest radius-one cap visible, visible cap projected depth exactly equals completed G-buffer depth."
        elif args.view == "interval_state":
            result["interval_state_contract"] = "RGB: receiver span count > 0, any nonzero peel ID in the first four layers, any such peel ID has back-cap alpha > 0."
        elif args.view == LIT_EVENT_STATE:
            result["event_state_contract"] = "RGB: min(raw receiver event count,33)/33, front count/33, back count/33; scan at most 32 allocated layers and decode round(channel*33). Raw 33 is the overflow sentinel."
        elif args.view == LIT_EVENT_DATA:
            result["event_data_contract"] = "RGB: 1 when receiver span count > 0, otherwise existing span flags/33; exactly two events with front/back receiver IDs matching the first nonzero peel ID; both matching faces present with finite depths and back depth > front depth. Decode span presence with red > .5 and missing-span flags with round(red*33)."
        elif args.view == LIT_EVENT_ORDER:
            result["event_order_contract"] = "RGB: 1 when receiver span count > 0, otherwise existing span flags/33; raw event indices 0/1 encode FRONT=1 or BACK=.5 only when exactly two events match the first nonzero peel receiver, contain one front and one back, and have finite back depth > front depth. Other pairs encode green/blue=0. Decode valid pairs with both channels > .25, front with channel > .75; invalid face fields are null."
        elif args.view == LIT_SPAN_STATE:
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
            result["span_required_probes"] = {name: GEOMETRY_PROBES[name][LIT_PIXEL] for name in SPAN_EXPECTATIONS}
            result["span_expectations"] = SPAN_EXPECTATIONS
            result["span_failures"] = span_failures
            failures += span_failures
        (args.output_directory / LIT_MANIFEST_JSON).write_text(json.dumps(result, indent=2) + "\n", encoding=LIT_UTF_8)
        if failures:
            raise SmokeFailure("; ".join(failures[:8]) + f" ({len(failures)} failed checks)")
        print(json.dumps({LIT_SAMPLES: SAMPLE_COUNT, LIT_REGIONS: result[LIT_REGIONS]}))
        return 0
    except (SmokeFailure, OSError, ValueError, subprocess.SubprocessError) as error:
        print("FAIL: " + str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
