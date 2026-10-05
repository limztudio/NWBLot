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

LIT_OFFSCREEN = "offscreen"
LIT_MOVED = "moved"
LIT_OPAQUE_GLASS = "opaque_glass"
LIT_ONSCREEN = "onscreen"
LIT_ONSCREEN_MOVED = "onscreen_moved"
LIT_BOUNDARY = "boundary"
LIT_FLOOR = "floor"
LIT_DISABLED = "disabled"
LIT_SCREEN = "screen"
LIT_HARDWARE = "hardware"
LIT_HYBRID = "hybrid"
LIT_SEQUENCE = "sequence"
LIT_GENERATION = "generation"
LIT_FRAME = "frame"
LIT_MODE = "mode"
LIT_WIDTH = "width"
LIT_HEIGHT = "height"
LIT_REQUESTED_BUDGET = "requested_budget"
LIT_EFFECTIVE_BUDGET = "effective_budget"
LIT_QUEUE_CAPACITY = "queue_capacity"
LIT_HARDWARE_REQUESTED = "hardware_requested"
LIT_HARDWARE_AVAILABLE = "hardware_available"
LIT_HARDWARE_READY = "hardware_ready"
LIT_TOKEN_QUEUE = "token_queue"
LIT_TOKEN_VALUE = "token_value"
LIT_PHYSICAL_QUEUE = "physical_queue"
LIT_DEVICE_GENERATION = "device_generation"
LIT_CANDIDATES = "candidates"
LIT_HARDWARE_RAYS = "hardware_rays"
LIT_HARDWARE_HITS = "hardware_hits"
LIT_OPAQUE_PIXELS = "opaque_pixels"
LIT_GLASS_PIXELS = "glass_pixels"
LIT_FALLBACK_PIXELS = "fallback_pixels"
LIT_SCREEN_ATTEMPTS = "screen_attempts"
LIT_SCREEN_HITS = "screen_hits"
LIT_RED = "red"
LIT_GREEN = "green"
LIT_PIXELS = "pixels"
LIT_MINIMUM_PROJECTED_AREA_PIXELS = "minimum_projected_area_pixels"
LIT_CENTROID = "centroid"
LIT_EXPECTED_CENTROID = "expected_centroid"
LIT_MEASURED_MOTION = "measured_motion"
LIT_EXPECTED_MOTION_X = "expected_motion_x"
LIT_ORIGINAL = "original"
LIT_DIRECT = "direct"
LIT_REFLECTION = "reflection"
LIT_EXPECTED = "expected"
LIT_REFLECTIONSMOKESTATISTICS = "ReflectionSmokeStatistics:"
LIT_CASE_MODE = "{case}_{mode}"
LIT_BMP = ".bmp"
LIT_LOG = ".log"
LIT_EXECUTABLE = "--executable"
LIT_WORKING_DIRECTORY = "--working-directory"
LIT_TIMEOUT = "--timeout"
LIT_EXPECT_LOG_MESSAGE = "--expect-log-message"
LIT_LOGSERVER_EXECUTABLE = "--logserver-executable"
LIT_REJECT_LOG_MESSAGE = "--reject-log-message"
LIT_REFLECTION_RESOLVE_HARDWARE = "Reflection resolve: hardware"
LIT_UTF_8 = "utf-8"
LIT_CAPTURES = "captures"
LIT_CASE = "case"
LIT_HARDWARE_BUDGET = "hardware_budget"
LIT_FILE = "file"
LIT_LIMITATIONS = "limitations"
LIT_ALL = "all"
LIT_BASELINE = "baseline"
LIT_BUDGET = "budget"
LIT_ROUGH = "rough"
LIT_TEMPORAL = "temporal"
LIT_OPTICAL = "optical"
LIT_FEEDBACK = "feedback"
LIT_FEEDBACK_DIAGNOSTICS_OFF = "feedback-diagnostics-off"
LIT_CASE_MODE_BMP = "{case}_{mode}.bmp"
LIT_MAIN = "__main__"
LIT_STORE_TRUE = "store_true"
LIT_APPEND = "append"


MODES = (LIT_DISABLED, LIT_SCREEN, LIT_HARDWARE, LIT_HYBRID)
BASELINE_CAPTURES = ((LIT_OFFSCREEN, LIT_DISABLED), (LIT_OFFSCREEN, LIT_SCREEN), (LIT_OFFSCREEN, LIT_HARDWARE),
    (LIT_MOVED, LIT_DISABLED), (LIT_MOVED, LIT_HARDWARE), (LIT_OPAQUE_GLASS, LIT_DISABLED), (LIT_OPAQUE_GLASS, LIT_HARDWARE),
    (LIT_OFFSCREEN, LIT_HYBRID))
SCREEN_CAPTURES = ((LIT_ONSCREEN, LIT_DISABLED), (LIT_ONSCREEN, LIT_SCREEN), (LIT_ONSCREEN, LIT_HARDWARE),
    (LIT_ONSCREEN, LIT_HYBRID), (LIT_ONSCREEN_MOVED, LIT_SCREEN), (LIT_BOUNDARY, LIT_DISABLED),
    (LIT_BOUNDARY, LIT_SCREEN), (LIT_BOUNDARY, LIT_HYBRID), (LIT_FLOOR, LIT_DISABLED), (LIT_FLOOR, LIT_SCREEN),
    (LIT_FLOOR, LIT_HARDWARE), (LIT_FLOOR, LIT_HYBRID))
CAPTURES = BASELINE_CAPTURES + SCREEN_CAPTURES
DEFAULT_RAY_BUDGET = 2 * 960 * 720
BUDGET_CAPTURES = ((LIT_FLOOR, LIT_HYBRID, 0), (LIT_FLOOR, LIT_HYBRID, 64))
STATISTICS_FIELDS = (LIT_SEQUENCE, LIT_GENERATION, LIT_FRAME, LIT_MODE, LIT_WIDTH, LIT_HEIGHT, LIT_REQUESTED_BUDGET,
    LIT_EFFECTIVE_BUDGET, LIT_QUEUE_CAPACITY, LIT_HARDWARE_REQUESTED, LIT_HARDWARE_AVAILABLE, LIT_HARDWARE_READY,
    LIT_TOKEN_QUEUE, LIT_TOKEN_VALUE, LIT_PHYSICAL_QUEUE, LIT_DEVICE_GENERATION, LIT_CANDIDATES, LIT_HARDWARE_RAYS,
    LIT_HARDWARE_HITS, LIT_OPAQUE_PIXELS, LIT_GLASS_PIXELS, LIT_FALLBACK_PIXELS, LIT_SCREEN_ATTEMPTS, LIT_SCREEN_HITS)
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
    channel = 0 if color == LIT_RED else 1
    return pixel[channel] >= 40 and all(pixel[channel] >= pixel[other] + 25 for other in range(3) if other != channel)


def blue_foreground(pixel):
    return pixel[2] >= 110 and pixel[2] >= pixel[0] + 45 and pixel[2] >= pixel[1] + 45


def marker_projection(width, height, color, moved=False):
    # Reflect z=-8 through the z=0 plane. The virtual source is 14 units from the camera at z=-6.
    focal_length = height / (2.0 * math.tan(math.pi / 6.0))
    x, y = (-1.6, 1.0) if color == LIT_RED else (1.6, 2.0)
    x += 0.9 if moved else 0.0
    return width * 0.5 + focal_length * x / 14.0, height * 0.5 - focal_length * (y - 1.4) / 14.0


def analyze_markers(frame, moved=False, required=True):
    width, height, rows = validate_frame(frame)
    results = {}
    for color in (LIT_RED, LIT_GREEN):
        points = [(x, y) for y, row in enumerate(rows) for x, pixel in enumerate(row) if marker_color(pixel, color)]
        if not required:
            if len(points) > 16:
                raise SmokeFailure(f"offscreen {color} marker appeared without a hardware reflection hit ({len(points)} pixels)")
            results[color] = {LIT_PIXELS: len(points)}
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
        results[color] = {LIT_PIXELS: len(points), LIT_MINIMUM_PROJECTED_AREA_PIXELS: math.ceil(minimum_pixels),
            LIT_CENTROID: [centroid_x, centroid_y], LIT_EXPECTED_CENTROID: [expected_x, expected_y],
            "outside_predicted_region": outside}
    return results


def compare_marker_motion(original, moved):
    if original[:2] != moved[:2]:
        raise SmokeFailure("marker motion captures have different framebuffer dimensions")
    before = analyze_markers(original)
    after = analyze_markers(moved, moved=True)
    expected_shift = original[1] / (2.0 * math.tan(math.pi / 6.0)) * 0.9 / 14.0
    for color in (LIT_RED, LIT_GREEN):
        dx = after[color][LIT_CENTROID][0] - before[color][LIT_CENTROID][0]
        dy = after[color][LIT_CENTROID][1] - before[color][LIT_CENTROID][1]
        if abs(dx - expected_shift) > original[1] * 0.014 or abs(dy) > original[1] * 0.014:
            raise SmokeFailure(f"{color} reflection did not follow the authored marker movement")
        after[color][LIT_MEASURED_MOTION] = [dx, dy]
        after[color][LIT_EXPECTED_MOTION_X] = expected_shift
    return {LIT_ORIGINAL: before, LIT_MOVED: after}


def panel_projection(width, height, color, case, reflected):
    focal_length = height / (2.0 * math.tan(math.pi / 6.0))
    source_x, source_y, half_height = (-1.7, 1.0, 0.25) if color == LIT_RED else (1.7, 2.0, 0.35)
    if case == LIT_ONSCREEN_MOVED:
        source_x += 0.25 if color == LIT_RED else -0.25
    elif case == LIT_BOUNDARY and color == LIT_GREEN:
        source_x = 3.0
    # Source z=-3 is three units from the camera. Its virtual image at z=+3 is nine units away.
    distance = 9.0 if reflected else 3.0
    if case == LIT_FLOOR:
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
    for color in (LIT_RED, LIT_GREEN):
        points = [(x, y) for y, row in enumerate(rows) for x, pixel in enumerate(row) if marker_color(pixel, color)]
        allowed_regions = []
        color_results = {}
        for label, reflected in ((LIT_DIRECT, False), (LIT_REFLECTION, True)):
            expected = not (case == LIT_BOUNDARY and color == LIT_GREEN) if not reflected else (
                mode != LIT_DISABLED and not (case == LIT_BOUNDARY and color == LIT_GREEN and mode == LIT_SCREEN))
            projection = panel_projection(width, height, color, case, reflected)
            region_points = [point for point in points if in_panel_rectangle(point, projection, height * 0.008)]
            if not expected:
                if len(region_points) > 16:
                    raise SmokeFailure(f"{case}/{mode}: unexpected {color} {label} panel ({len(region_points)} pixels)")
                color_results[label] = {LIT_PIXELS: len(region_points), LIT_EXPECTED: False}
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
            color_results[label] = {LIT_PIXELS: len(region_points), LIT_EXPECTED: True,
                LIT_CENTROID: [cx, cy], LIT_EXPECTED_CENTROID: list(projection[:2]),
                LIT_MINIMUM_PROJECTED_AREA_PIXELS: math.ceil(minimum)}
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
    before = analyze_panels(original, LIT_ONSCREEN, LIT_SCREEN)
    after = analyze_panels(moved, LIT_ONSCREEN_MOVED, LIT_SCREEN)
    focal_length = original[1] / (2.0 * math.tan(math.pi / 6.0))
    for color in (LIT_RED, LIT_GREEN):
        direction = 1.0 if color == LIT_RED else -1.0
        for label, distance in ((LIT_DIRECT, 3.0), (LIT_REFLECTION, 9.0)):
            expected_dx = focal_length * direction * 0.25 / distance
            dx = after[color][label][LIT_CENTROID][0] - before[color][label][LIT_CENTROID][0]
            dy = after[color][label][LIT_CENTROID][1] - before[color][label][LIT_CENTROID][1]
            if abs(dx - expected_dx) > max(1.5, original[1] * 0.006) or abs(dy) > max(1.5, original[1] * 0.006):
                raise SmokeFailure(f"{color} {label} panel did not follow the authored inward movement")
            after[color][label][LIT_MEASURED_MOTION] = [dx, dy]
            after[color][label][LIT_EXPECTED_MOTION_X] = expected_dx
    return {LIT_ORIGINAL: before, LIT_MOVED: after}


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
            old_color = marker_color(before[y][x], LIT_RED) or marker_color(before[y][x], LIT_GREEN)
            new_color = marker_color(after[y][x], LIT_RED) or marker_color(after[y][x], LIT_GREEN)
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
    prefix = LIT_REFLECTIONSMOKESTATISTICS
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


def validate_statistics(samples, case, mode, budget=DEFAULT_RAY_BUDGET, allow_zero_samples=False, extent=(960, 720)):
    width, height = extent
    stable = []
    previous = None
    for sample in samples:
        if set(sample) != set(STATISTICS_FIELDS) or any(type(value) is not int or value < 0 for value in sample.values()):
            raise SmokeFailure("completed reflection statistics are malformed")
        wide_fields = (LIT_SEQUENCE, LIT_GENERATION, LIT_TOKEN_VALUE)
        if any(value > (0xffffffffffffffff if name in wide_fields else 0xffffffff) for name, value in sample.items()):
            raise SmokeFailure("completed reflection statistics exceed their integer field widths")
        if sample[LIT_MODE] != MODES.index(mode) or (sample[LIT_WIDTH], sample[LIT_HEIGHT]) != (width, height):
            raise SmokeFailure("completed reflection statistics do not match the requested mode and dimensions")
        capacity = sample[LIT_QUEUE_CAPACITY]
        if capacity <= 0 or capacity > 2 * width * height or sample[LIT_REQUESTED_BUDGET] != budget \
            or sample[LIT_EFFECTIVE_BUDGET] != min(budget, capacity):
            raise SmokeFailure("completed reflection statistics do not match the requested budget and capacity")
        if not sample[LIT_SEQUENCE] or not sample[LIT_GENERATION] or not sample[LIT_TOKEN_VALUE] \
            or not 0 < sample[LIT_DEVICE_GENERATION] <= 0xffff or not 0 <= sample[LIT_PHYSICAL_QUEUE] < 0xffff \
            or sample[LIT_TOKEN_QUEUE] > 2:
            raise SmokeFailure("reflection statistics lack an accepted token and device identity")
        if any(sample[name] not in (0, 1) for name in (LIT_HARDWARE_REQUESTED, LIT_HARDWARE_AVAILABLE, LIT_HARDWARE_READY)) \
            or sample[LIT_HARDWARE_REQUESTED] != int(mode in (LIT_HARDWARE, LIT_HYBRID)):
            raise SmokeFailure("reflection statistics have inconsistent hardware route metadata")
        if previous:
            identity = (LIT_GENERATION, LIT_DEVICE_GENERATION, LIT_PHYSICAL_QUEUE, LIT_TOKEN_QUEUE)
            if any(sample[name] != previous[name] for name in identity):
                raise SmokeFailure("static reflection capture mixed statistics generations or physical queues")
            if sample[LIT_SEQUENCE] <= previous[LIT_SEQUENCE] or sample[LIT_FRAME] <= previous[LIT_FRAME] \
                or sample[LIT_TOKEN_VALUE] <= previous[LIT_TOKEN_VALUE]:
                raise SmokeFailure("completed reflection statistics are stale or out of order")
        previous = sample
        eligible = sample[LIT_OPAQUE_PIXELS] + sample[LIT_GLASS_PIXELS]
        if sample[LIT_OPAQUE_PIXELS] > width * height or sample[LIT_GLASS_PIXELS] > width * height \
            or sample[LIT_CANDIDATES] > eligible or sample[LIT_SCREEN_ATTEMPTS] > eligible \
            or sample[LIT_SCREEN_HITS] > sample[LIT_SCREEN_ATTEMPTS]:
            raise SmokeFailure("reflection counters exceed their eligible pixel population")
        if sample[LIT_HARDWARE_RAYS] > min(sample[LIT_EFFECTIVE_BUDGET], sample[LIT_CANDIDATES]) \
            or sample[LIT_HARDWARE_HITS] > sample[LIT_HARDWARE_RAYS]:
            raise SmokeFailure("actual hardware rays or hits exceeded the completed frame budget")
        zero_samples = eligible - sample[LIT_SCREEN_HITS] - sample[LIT_HARDWARE_HITS] - sample[LIT_FALLBACK_PIXELS]
        # GGX samples below the geometric horizon are valid zero radiance, with no trace or fallback.
        if not (0 <= zero_samples <= sample[LIT_OPAQUE_PIXELS] if allow_zero_samples else zero_samples == 0):
            raise SmokeFailure("reflection outcome counters do not partition eligible pixels")
        if mode in (LIT_DISABLED, LIT_SCREEN) and (sample[LIT_HARDWARE_RAYS] or sample[LIT_HARDWARE_HITS] or sample[LIT_CANDIDATES]):
            raise SmokeFailure("a non-hardware reflection route issued hardware work")
        if mode == LIT_DISABLED and any(sample[name] for name in COUNTER_FIELDS):
            raise SmokeFailure("disabled reflection produced nonzero counters")
        if mode == LIT_HARDWARE and (sample[LIT_SCREEN_ATTEMPTS] or sample[LIT_SCREEN_HITS]):
            raise SmokeFailure("hardware-only reflection attempted screen tracing")
        if not sample[LIT_HARDWARE_READY] and (sample[LIT_HARDWARE_RAYS] or sample[LIT_HARDWARE_HITS]):
            raise SmokeFailure("reflection issued hardware rays without a ready hardware route")
        if sample[LIT_FRAME] < 3:
            continue
        if mode != LIT_DISABLED and not sample[LIT_OPAQUE_PIXELS]:
            raise SmokeFailure("stable reflection statistics are missing the opaque fixture")
        if mode in (LIT_SCREEN, LIT_HYBRID) and not sample[LIT_SCREEN_ATTEMPTS]:
            raise SmokeFailure("stable screen route did not attempt screen tracing")
        if mode in (LIT_HARDWARE, LIT_HYBRID):
            if not sample[LIT_HARDWARE_AVAILABLE] or not sample[LIT_HARDWARE_READY]:
                raise SmokeFailure("stable hardware capture has no ready hardware route")
            bounded_queue = min(sample[LIT_CANDIDATES], sample[LIT_EFFECTIVE_BUDGET])
            if sample[LIT_HARDWARE_RAYS] > bounded_queue:
                raise SmokeFailure("ready hardware route exceeded its bounded candidate queue")
            if bounded_queue > 0 and sample[LIT_HARDWARE_RAYS] == 0:
                raise SmokeFailure("ready hardware route executed no rays from its bounded candidate queue")
        if case == LIT_OPAQUE_GLASS and mode != LIT_DISABLED and not sample[LIT_GLASS_PIXELS]:
            raise SmokeFailure("stable reflection statistics are missing primary glass")
        if case == LIT_OFFSCREEN and mode in (LIT_HARDWARE, LIT_HYBRID) and not sample[LIT_HARDWARE_HITS]:
            raise SmokeFailure("offscreen reflection has no completed hardware hits")
        if case == LIT_FLOOR and mode in (LIT_SCREEN, LIT_HYBRID) and not sample[LIT_SCREEN_HITS]:
            raise SmokeFailure("floor reflection has no accepted screen hits")
        if budget < DEFAULT_RAY_BUDGET and (sample[LIT_CANDIDATES] <= sample[LIT_EFFECTIVE_BUDGET] or not sample[LIT_FALLBACK_PIXELS]):
            raise SmokeFailure("limited-budget reflection did not exercise excess candidates and fallback")
        stable.append(sample)
    if not stable:
        raise SmokeFailure("no completed reflection statistics reached stable frame 3")
    return stable


def compare_hybrid_statistics(hardware, hybrid):
    populations = [sample[LIT_OPAQUE_PIXELS] + sample[LIT_GLASS_PIXELS] for sample in hardware + hybrid]
    if not populations or min(populations) <= 0 or max(populations) - min(populations) > max(16, min(populations) * 0.01):
        raise SmokeFailure("floor hardware and hybrid statistics do not represent matched eligible populations")
    hardware_rays = [sample[LIT_HARDWARE_RAYS] for sample in hardware]
    hybrid_rays = [sample[LIT_HARDWARE_RAYS] for sample in hybrid]
    if not hardware_rays or not hybrid_rays or min(sample[LIT_SCREEN_HITS] for sample in hybrid) <= 0 \
        or max(hybrid_rays) >= min(hardware_rays):
        raise SmokeFailure("floor hybrid did not reduce completed hardware rays while accepting screen hits")
    return {"hardware_ray_range": [min(hardware_rays), max(hardware_rays)],
        "hybrid_hardware_ray_range": [min(hybrid_rays), max(hybrid_rays)],
        "eligible_pixel_range": [min(populations), max(populations)],
        "hybrid_accepted_screen_hit_range": [min(sample[LIT_SCREEN_HITS] for sample in hybrid),
            max(sample[LIT_SCREEN_HITS] for sample in hybrid)]}


def capture_stem(case, mode, budget=DEFAULT_RAY_BUDGET):
    return LIT_CASE_MODE.format(case=case, mode=mode) + (f"_budget_{budget}" if budget != DEFAULT_RAY_BUDGET else "")


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
    output = args.output_directory / (capture_stem(case, mode, budget) + LIT_BMP)
    log_output = output.with_suffix(LIT_LOG)
    command = [sys.executable, str(Path(__file__).with_name("window_capture_smoke.py")),
        LIT_EXECUTABLE, str(args.executable), LIT_WORKING_DIRECTORY, str(args.working_directory),
        "--output", str(output), "--application-capture", "--application-capture-frame-count", str(args.frames),
        LIT_TIMEOUT, str(args.timeout), LIT_EXPECT_LOG_MESSAGE, f"ReflectionSmokeProject: case {case} created",
        LIT_EXPECT_LOG_MESSAGE, f"ReflectionSmokeProject: reflection mode {mode}",
        LIT_EXPECT_LOG_MESSAGE, "ReflectionSmokeProject: shutdown",
        LIT_EXPECT_LOG_MESSAGE, f"ReflectionSmokeProject: hardware ray budget {budget}",
        LIT_EXPECT_LOG_MESSAGE, LIT_REFLECTIONSMOKESTATISTICS, "--log-output", str(log_output)]
    if args.logserver_executable:
        command += [LIT_LOGSERVER_EXECUTABLE, str(args.logserver_executable)]
    else:
        command.append("--no-logserver")
    if mode in (LIT_HARDWARE, LIT_HYBRID):
        if budget == 0:
            route = "screen-space" if mode == LIT_HYBRID else "environment"
            command += [LIT_EXPECT_LOG_MESSAGE, "Reflection resolve: " + route,
                LIT_REJECT_LOG_MESSAGE, LIT_REFLECTION_RESOLVE_HARDWARE]
        else:
            command += [LIT_EXPECT_LOG_MESSAGE, LIT_REFLECTION_RESOLVE_HARDWARE]
        command += [LIT_EXPECT_LOG_MESSAGE if args.require_hardware else "--skip-log-message",
            "ReflectionSmokeProject: hardware available" if args.require_hardware else "ReflectionSmokeProject: hardware unavailable"]
    elif mode == LIT_DISABLED:
        command += [LIT_EXPECT_LOG_MESSAGE, "Reflection resolve: disabled",
            LIT_REJECT_LOG_MESSAGE, LIT_REFLECTION_RESOLVE_HARDWARE]
    elif mode == LIT_SCREEN:
        command += [LIT_EXPECT_LOG_MESSAGE, "Reflection resolve: screen-space",
            LIT_REJECT_LOG_MESSAGE, LIT_REFLECTION_RESOLVE_HARDWARE]
    if case == LIT_OPAQUE_GLASS:
        command += [LIT_EXPECT_LOG_MESSAGE, "AVBOIT refraction resolve:"]
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
    samples = parse_statistics(log_output.read_text(encoding=LIT_UTF_8))
    validate_statistics(samples, case, mode, budget)
    return frame, samples


def write_report(args, captures, metrics=None, statistics=None):
    metadata = {"frames": args.frames, "frame_source": "actual application framebuffer readback", "size": [960, 720],
        "hardware_required": args.require_hardware, "application_args": args.application_arg,
        "settings": {"environment_top": [0, 0, 0], "environment_bottom": [0, 0, 0],
            "max_hardware_rays_per_frame": 1382400, "roughness": 0, "glass_ior": 3.8},
        "suite": args.suite,
        LIT_CAPTURES: [{LIT_CASE: case, LIT_MODE: mode, LIT_HARDWARE_BUDGET: budget,
            LIT_FILE: capture_stem(case, mode, budget) + LIT_BMP, "log": capture_stem(case, mode, budget) + LIT_LOG}
            for case, mode, budget in captures], "statistics": statistics,
        "metrics": metrics, LIT_LIMITATIONS: "Smooth single-bounce reflection with on-screen and offscreen geometric marker checks. Wall-mirror screen hits approach marker backs and have provisional confidence; the floor case approaches their fronts. Completed per-frame counters separately check floor hybrid ray reduction and bounded budgets; they do not establish GPU speed. Hardware secondary-hit lighting is approximate; roughness reconstruction and temporal history are not validated here."}
    (args.output_directory / "reflection_manifest.json").write_text(json.dumps(metadata, indent=2) + "\n", encoding=LIT_UTF_8)
    cards = []
    for item in metadata[LIT_CAPTURES]:
        bmp = args.output_directory / item[LIT_FILE]
        png = png_rgb_bytes(read_bmp_24_rows(bmp))
        bmp.with_suffix(".png").write_bytes(png)
        label = html.escape(item[LIT_CASE] + " / " + item[LIT_MODE] + " / budget " + str(item[LIT_HARDWARE_BUDGET]))
        cards.append(f'<article><h2>{label}</h2><a href="{html.escape(item[LIT_FILE])}">Raw BMP</a>'
            f' / <a href="{html.escape(item["log"])}">Completed-frame log</a>'
            f'<img alt="Actual {label} framebuffer" src="data:image/png;base64,{base64.b64encode(png).decode("ascii")}"></article>')
    document = f'<!doctype html><html lang="en"><meta charset="{LIT_UTF_8}"><title>Reflection smoke captures</title>'
    document += '<style>body{background:#141922;color:#e7edf5;font:16px system-ui;margin:28px}main{display:grid;grid-template-columns:repeat(auto-fit,minmax(440px,1fr));gap:20px}article{background:#202937;padding:16px}img{display:block;width:100%;margin-top:12px}a{color:#88c9ff}pre{white-space:pre-wrap}h2{font-size:19px}</style>'
    document += '<h1>Reflection smoke - actual renderer captures</h1><p>' + html.escape(metadata[LIT_LIMITATIONS]) + '</p>'
    document += '<p>Images contain unchanged framebuffer RGB pixels, converted losslessly to PNG. Raw BMPs remain beside this report. Glass uses IOR 3.8 and its matching dielectric Fresnel F0.</p><main>'
    document += ''.join(cards) + '</main><pre>' + html.escape(json.dumps(metrics, indent=2) if metrics else 'Capture evidence; visual assertions have not passed yet.') + '</pre></html>'
    (args.output_directory / "reflection.html").write_text(document, encoding=LIT_UTF_8)


def parse_args(argv):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(LIT_EXECUTABLE, required=True, type=Path)
    parser.add_argument(LIT_WORKING_DIRECTORY, required=True, type=Path)
    parser.add_argument("--output-directory", required=True, type=Path)
    parser.add_argument(LIT_LOGSERVER_EXECUTABLE, type=Path)
    parser.add_argument("--suite", choices=(LIT_ALL, LIT_BASELINE, LIT_SCREEN, LIT_BUDGET, LIT_ROUGH, LIT_TEMPORAL, LIT_OPTICAL, LIT_FEEDBACK, LIT_FEEDBACK_DIAGNOSTICS_OFF), default=LIT_ALL,
        help="Capture the original 22 comparisons, a subset, or the separate roughness, history, optical and feedback suites.")
    parser.add_argument("--frames", type=int, default=16)
    parser.add_argument(LIT_TIMEOUT, type=float, default=60.0)
    parser.add_argument("--require-hardware", action=LIT_STORE_TRUE)
    parser.add_argument("--application-arg", action=LIT_APPEND, default=[])
    parser.add_argument("--feedback-cases", help="Optional comma-separated feedback capture names for a bounded pilot.")
    args = parser.parse_args(argv)
    if args.frames <= 0 or args.timeout <= 0:
        parser.error("frames and timeout must be positive")
    args.output_directory = args.output_directory.resolve()
    return args


def main(argv):
    args = parse_args(argv)
    args.output_directory.mkdir(parents=True, exist_ok=True)
    if args.suite in (LIT_ROUGH, LIT_TEMPORAL):
        from reflection_roughness_smoke import run_suite
        return run_suite(args)
    if args.suite in (LIT_FEEDBACK, LIT_FEEDBACK_DIAGNOSTICS_OFF):
        from reflection_feedback_smoke import run_suite
        return run_suite(args)
    if args.suite == LIT_OPTICAL:
        from reflection_optical_smoke import run_suite
        return run_suite(args)
    completed = []
    statistics = {}
    try:
        visual = BASELINE_CAPTURES if args.suite == LIT_BASELINE else SCREEN_CAPTURES if args.suite == LIT_SCREEN else CAPTURES
        selected = [(case, mode, DEFAULT_RAY_BUDGET) for case, mode in visual] if args.suite != LIT_BUDGET else []
        if args.suite in (LIT_ALL, LIT_BUDGET):
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
        if args.suite in (LIT_ALL, LIT_BASELINE):
            for case, mode in ((LIT_OFFSCREEN, LIT_DISABLED), (LIT_OFFSCREEN, LIT_SCREEN), (LIT_MOVED, LIT_DISABLED)):
                metrics[LIT_CASE_MODE.format(case=case, mode=mode)] = analyze_markers(read_bmp_24_rows(args.output_directory / LIT_CASE_MODE_BMP.format(case=case, mode=mode)), required=False)
            metrics["marker_motion"] = compare_marker_motion(
                read_bmp_24_rows(args.output_directory / "offscreen_hardware.bmp"),
                read_bmp_24_rows(args.output_directory / "moved_hardware.bmp"))
            metrics["offscreen_hybrid"] = analyze_markers(read_bmp_24_rows(args.output_directory / "offscreen_hybrid.bmp"))
            metrics[LIT_OPAQUE_GLASS] = compare_opaque_glass(
                read_bmp_24_rows(args.output_directory / "opaque_glass_disabled.bmp"),
                read_bmp_24_rows(args.output_directory / "opaque_glass_hardware.bmp"))
        if args.suite in (LIT_ALL, LIT_SCREEN):
            for case, mode in SCREEN_CAPTURES:
                metrics[LIT_CASE_MODE.format(case=case, mode=mode)] = analyze_panels(read_bmp_24_rows(args.output_directory / LIT_CASE_MODE_BMP.format(case=case, mode=mode)), case, mode)
            metrics["panel_motion"] = compare_panel_motion(
                read_bmp_24_rows(args.output_directory / "onscreen_screen.bmp"),
                read_bmp_24_rows(args.output_directory / "onscreen_moved_screen.bmp"))
            metrics["floor_completed_ray_comparison"] = compare_hybrid_statistics(
                validate_statistics(statistics["floor_hardware"], LIT_FLOOR, LIT_HARDWARE),
                validate_statistics(statistics["floor_hybrid"], LIT_FLOOR, LIT_HYBRID))
        if args.suite in (LIT_ALL, LIT_BUDGET):
            for case, mode, budget in BUDGET_CAPTURES:
                stem = capture_stem(case, mode, budget)
                metrics[stem] = analyze_panels(read_bmp_24_rows(args.output_directory / (stem + LIT_BMP)), case, mode)
        write_report(args, completed, metrics, statistics)
        print(f"PASS: reflection {args.suite} geometric marker, route, and composition checks\n"
            + json.dumps(metrics, indent=2), flush=True)
        return 0
    except (SmokeFailure, OSError, subprocess.TimeoutExpired) as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        return 1


if __name__ == LIT_MAIN:
    raise SystemExit(main(sys.argv[1:]))
