#!/usr/bin/env python3
"""Qualify rendered glass density independently of the fixture's admission markers."""

import argparse
import json
import math
import os
from pathlib import Path
import re
import subprocess
import sys
import time
from types import SimpleNamespace

from refraction_density_benchmark import Cell, DEFAULT_EXTENTS, DENSITIES, EXTENTS, ROUTES, validate_fixture
from window_capture_smoke import (SKIP_EXIT_CODE, SmokeFailure, SmokeSkip, STRICT_LOG_FAILURE_MESSAGES, application_capture_partial_path,
    build_launch_environment, collect_log_delta, create_capture_backend, ensure_process_running, launch_logserver, launch_testbed,
    prepare_application_capture_environment, read_bmp_24_rows, require_normal_process_exit, shutdown_logserver_and_collect, terminate_process,
    validate_application_capture_log_text)


BOUNDARY_GUARD = 1
BLOCK_EDGE = 8
FRESNEL_TOLERANCE = 2.0
CAPTURE_PROTOCOL = 2
SANITIZED_ENVIRONMENT = ("NWB_REFRACTION_SMOKE_CASE", "NWB_REFRACTION_SMOKE_GEOMETRY", "NWB_REFRACTION_SMOKE_LIFECYCLE", "NWB_REFRACTION_SMOKE_TIMING", "NWB_REFRACTION_FRAME_GRAPH_SNAPSHOT_PATH", "NWB_GPU_TIMING_FILE", "NWB_RENDERER_BASELINE_CAPTURE_FREEZE_FRAME")


def projected_bounds(width, height, percent):
    if percent not in DENSITIES:
        raise SmokeFailure("unsupported density percentage")
    if not percent:
        return 0, 0, 0, 0
    fraction = math.sqrt(percent / 100)
    covered_width = min(width, 2 * int(width * fraction / 2 + .5))
    covered_height = min(height, 2 * int(height * fraction / 2 + .5))
    left, top = (width - covered_width) // 2, (height - covered_height) // 2
    return left, top, left + covered_width, top + covered_height


def validate_backdrop(frame):
    width, height, rows = frame
    dark = light = bad_color = varying = 0
    for row in rows:
        for x, pixel in enumerate(row):
            neutral = max(pixel) - min(pixel) <= 1
            value = sum(pixel) / 3
            dark += neutral and abs(value - 68) <= 1
            light += neutral and abs(value - 201) <= 1
            bad_color += not neutral or min(abs(value - 68), abs(value - 201)) > 1
            varying += max(abs(a - b) for a, b in zip(pixel, rows[0][x])) > 1
    total = width * height
    if bad_color > max(8, total * .001) or varying > max(8, total * .001) or min(dark, light) < total * .3:
        raise SmokeFailure(f"zero-density backdrop is not the neutral unlit reference: dark={dark}, light={light}, bad_color={bad_color}, vertical_changes={varying}")
    return {"pixels": total, "dark_pixels": dark, "light_pixels": light, "unexpected_color_pixels": bad_color, "vertical_variation_pixels": varying}


def encoded_transmission(level, transmission):
    encoded = level / 255
    linear = encoded / 12.92 if encoded <= .04045 else ((encoded + .055) / 1.055) ** 2.4
    # Inverting then reapplying Reinhard cancels its common exposure/shoulder scale.
    mapped = transmission * linear / (1 - linear + transmission * linear)
    return 255 * (12.92 * mapped if mapped <= .0031308 else 1.055 * mapped ** (1 / 2.4) - .055)


def expected_levels(width, height, x, y, route):
    tangent = math.tan(math.pi / 6)
    ray_x = (2 * (x + .5) / width - 1) * tangent * width / height
    ray_y = (1 - 2 * (y + .5) / height) * tangent
    entry_cosine = 1 / math.sqrt(1 + ray_x * ray_x + ray_y * ray_y)
    exit_cosine = math.sqrt(1 - (1 - entry_cosine * entry_cosine) / 2.25) if route == "hardware" else entry_cosine
    entry_transmission = 1 - (.04 + .96 * (1 - entry_cosine) ** 5)
    exit_transmission = 1 - (.04 + .96 * (1 - exit_cosine) ** 5)
    transmission = entry_transmission * exit_transmission
    return encoded_transmission(68, transmission), encoded_transmission(201, transmission)


def physical_core_guard(width, height, bounds):
    left, top, right, bottom = bounds
    tangent = math.tan(math.pi / 6)
    ray_x = max(abs(2 * left / width - 1), abs(2 * right / width - 1)) * tangent * width / height
    ray_y = max(abs(1 - 2 * top / height), abs(1 - 2 * bottom / height)) * tangent
    incident_length = math.sqrt(1 + ray_x * ray_x + ray_y * ray_y)
    inside_z = math.sqrt(1 - (ray_x * ray_x + ray_y * ray_y) / (2.25 * incident_length * incident_length))
    focal_pixels = height / (2 * tangent)
    # Exclude finite-slab side exits from the flat front/back Fresnel oracle only.
    # The entire rectangle, including this band, still participates in coverage checks.
    return max(2, math.ceil(.2 * max(ray_x, ray_y) / (1.5 * incident_length * inside_z) * focal_pixels / 6) + 2)


def analyze_density_frame(reference, candidate, percent, route):
    width, height, rows = reference
    if candidate[:2] != (width, height):
        raise SmokeFailure("density framebuffer extent differs from its zero-density reference")
    if route not in ROUTES or percent not in DENSITIES or percent == 0:
        raise SmokeFailure("density image comparison requires a positive supported density and an explicit route")
    left, top, right, bottom = projected_bounds(width, height, percent)
    inner = left + BOUNDARY_GUARD, top + BOUNDARY_GUARD, right - BOUNDARY_GUARD, bottom - BOUNDARY_GUARD
    core_guard = physical_core_guard(width, height, (left, top, right, bottom))
    core = left + core_guard, top + core_guard, right - core_guard, bottom - core_guard
    blocks_x = (inner[2] - inner[0] + BLOCK_EDGE - 1) // BLOCK_EDGE
    blocks_y = (inner[3] - inner[1] + BLOCK_EDGE - 1) // BLOCK_EDGE
    block_tested, block_changed = [0] * (blocks_x * blocks_y), [0] * (blocks_x * blocks_y)
    block_core_tested, block_core_valid = [0] * (blocks_x * blocks_y), [0] * (blocks_x * blocks_y)
    changed = interior = interior_changed = outside_changed = 0
    core_pixels = core_valid = dark = light = 0
    changed_left, changed_top, changed_right, changed_bottom = width, height, 0, 0
    for y, (before_row, after_row) in enumerate(zip(rows, candidate[2])):
        for x, (before, after) in enumerate(zip(before_row, after_row)):
            different = before != after
            if different:
                changed += 1
                changed_left, changed_top = min(changed_left, x), min(changed_top, y)
                changed_right, changed_bottom = max(changed_right, x + 1), max(changed_bottom, y + 1)
                if not (left - BOUNDARY_GUARD <= x < right + BOUNDARY_GUARD and top - BOUNDARY_GUARD <= y < bottom + BOUNDARY_GUARD):
                    outside_changed += 1
            if inner[0] <= x < inner[2] and inner[1] <= y < inner[3]:
                interior += 1
                interior_changed += different
                block = ((y - inner[1]) // BLOCK_EDGE) * blocks_x + (x - inner[0]) // BLOCK_EDGE
                block_tested[block] += 1
                block_changed[block] += different
            if core[0] <= x < core[2] and core[1] <= y < core[3]:
                core_pixels += 1
                expected_dark, expected_light = expected_levels(width, height, x, y, route)
                value = sum(after) / 3
                is_dark = abs(value - expected_dark) <= FRESNEL_TOLERANCE
                is_light = abs(value - expected_light) <= FRESNEL_TOLERANCE
                valid = max(after) - min(after) <= 1 and min(after) > 0 and (is_dark or is_light)
                core_valid += valid
                dark += valid and is_dark
                light += valid and is_light
                block = ((y - inner[1]) // BLOCK_EDGE) * blocks_x + (x - inner[0]) // BLOCK_EDGE
                block_core_tested[block] += 1
                block_core_valid[block] += valid
    actual_bounds = [changed_left, changed_top, changed_right, changed_bottom] if changed else None
    if outside_changed:
        raise SmokeFailure(f"density output changed outside its projected mask: {outside_changed} pixels, bounds={actual_bounds}")
    if not actual_bounds or any(abs(observed - expected) > BOUNDARY_GUARD for observed, expected in zip(actual_bounds, (left, top, right, bottom))):
        raise SmokeFailure(f"density changed bounds miss the projected rectangle: observed={actual_bounds}, expected={(left, top, right, bottom)}")
    if not interior or interior_changed < interior * .99:
        raise SmokeFailure(f"density interior is incomplete or unchanged: {interior_changed}/{interior} pixels")
    bad_blocks = sum(tested >= 16 and count < tested * .9 for tested, count in zip(block_tested, block_changed))
    if bad_blocks:
        raise SmokeFailure(f"density has missing local coverage: {bad_blocks} incomplete 8x8 blocks")
    if not core_pixels or core_valid < core_pixels * .98 or min(dark, light) < max(12, core_pixels * .05):
        raise SmokeFailure(f"density core lost neutral nonblack Fresnel stripes: accepted={core_valid}/{core_pixels}, dark={dark}, light={light}, core_guard={core_guard}")
    bad_core_blocks = sum(tested >= 16 and count < tested * .9 for tested, count in zip(block_core_tested, block_core_valid))
    if bad_core_blocks:
        raise SmokeFailure(f"density has locally invalid Fresnel output: {bad_core_blocks} incomplete 8x8 core blocks")
    expected_pixels = (right - left) * (bottom - top)
    return {"requested_percent": percent, "analytical_bounds": [left, top, right, bottom], "analytical_pixels": expected_pixels,
        "actual_changed_bounds": actual_bounds, "actual_changed_pixels": changed, "actual_changed_fraction": changed / (width * height),
        "changed_minus_analytical_pixels": changed - expected_pixels, "boundary_guard_pixels": BOUNDARY_GUARD, "outside_changed_pixels": outside_changed,
        "interior_tested_pixels": interior, "interior_changed_pixels": interior_changed, "tested_local_blocks": sum(count >= 16 for count in block_tested),
        "physical_core_guard_pixels": core_guard, "physical_core_pixels": core_pixels, "physical_core_accepted_pixels": core_valid,
        "physical_core_dark_pixels": dark, "physical_core_light_pixels": light, "fresnel_encoded_tolerance": FRESNEL_TOLERANCE}


def validate_extent_evidence(text, target, initial_client):
    matches = list(re.finditer(r"^GraphicsRuntime: Back buffer resized to (\d+)x(\d+)$", text.replace("\r\n", "\n"), re.MULTILINE))
    extents = [tuple(map(int, match.groups())) for match in matches]
    normalized = text.replace("\r\n", "\n")
    ready = normalized.find("FramebufferCapture: capture ready")
    if not matches or extents[-1] != target or any(extent not in (initial_client, target) for extent in extents):
        raise SmokeFailure(f"density framebuffer did not finish at requested extent {target}: runtime extents={extents}")
    if ready < matches[-1].end():
        raise SmokeFailure("density readback completed before the final target framebuffer was confirmed")
    return {"initial_client_extent": list(initial_client), "confirmed_client_extent": list(target), "runtime_framebuffer_extents": [list(extent) for extent in extents],
        "client_resize_requested": initial_client != target, "capture_protocol": CAPTURE_PROTOCOL}


def launch_density_capture(args, cell, output, log_path, environment):
    route = "hardware" if cell.route == "hardware" else "screen-space"
    other = "screen-space" if cell.route == "hardware" else "hardware"
    launch = SimpleNamespace(no_logserver=not bool(args.logserver_executable), logserver_executable=args.logserver_executable, log_port=0,
        working_directory=args.working_directory, timeout=args.timeout, application_arg=args.application_arg, software_vulkan="off", output=output,
        application_capture_frame_count=args.frames, expect_log_message=["RefractionSmokeProject: shutdown", "AVBOIT refraction resolve: " + route],
        reject_log_message=[*STRICT_LOG_FAILURE_MESSAGES, "AVBOIT refraction resolve: " + other], skip_log_message=[], skip_blocking_log_message=list(STRICT_LOG_FAILURE_MESSAGES))
    base_environment = build_launch_environment(launch)
    base_environment.update(environment)
    for name in SANITIZED_ENVIRONMENT:
        base_environment.pop(name, None)
    env = prepare_application_capture_environment(launch, base_environment)
    target = cell.width, cell.height
    backend = logserver = process = handle = log_directory = None
    log_baseline, log_pattern = {}, ""
    initial_client = None
    log_text = ""
    try:
        backend = create_capture_backend()
        logserver, port, log_directory, log_baseline, log_pattern = launch_logserver(launch, args.executable, env)
        process = launch_testbed(launch, args.executable, env, port)
        deadline = time.monotonic() + args.timeout
        handle = backend.wait_for_window(process.pid, min(30, args.timeout))
        if not handle:
            ensure_process_running(process, "before a density capture window became visible")
            raise SmokeFailure("density capture window did not become visible")
        backend.focus_window(handle)
        initial_client = backend.client_size(handle)
        if initial_client != target:
            backend.resize_client(handle, *target, allow_outside_work_area=True)
        marker = f"GraphicsRuntime: Back buffer resized to {cell.width}x{cell.height}"
        while time.monotonic() < deadline:
            ensure_process_running(process, "while confirming density client and framebuffer extent")
            live_text = collect_log_delta(log_directory, log_baseline, log_pattern)
            if backend.client_size(handle) == target and marker in live_text:
                break
            time.sleep(.02)
        else:
            raise SmokeFailure(f"density client/framebuffer resize did not reach exact target {target}")
        while time.monotonic() < deadline:
            ensure_process_running(process, "while waiting for target-sized density readback")
            if backend.client_size(handle) != target:
                raise SmokeFailure("density client extent changed after target confirmation")
            live_text = collect_log_delta(log_directory, log_baseline, log_pattern)
            if output.is_file() and "FramebufferCapture: capture ready" in live_text:
                break
            if "FramebufferCapture: skipped because swap-chain transfer-source usage is unavailable" in live_text:
                raise SmokeSkip("swap-chain framebuffer readback is unavailable")
            time.sleep(.02)
        else:
            raise SmokeFailure("density application did not publish its target-sized framebuffer capture")
        code, tail = terminate_process(process, "density capture", handle)
        process = None
        require_normal_process_exit(code, tail, "density capture")
        log_text = shutdown_logserver_and_collect(logserver, log_directory, log_baseline, log_pattern)
        logserver = None
    finally:
        try:
            if process is not None:
                terminate_process(process, "density capture", handle)
            if log_directory is not None and not log_text:
                log_text = shutdown_logserver_and_collect(logserver, log_directory, log_baseline, log_pattern)
                logserver = None
        finally:
            if logserver is not None:
                terminate_process(logserver, "density capture logserver")
            if backend is not None:
                backend.close()
            log_path.write_text(log_text, encoding="utf-8")
    skip = validate_application_capture_log_text(log_text, launch)
    if skip:
        raise SmokeSkip(skip)
    if not output.is_file() or application_capture_partial_path(output).exists():
        raise SmokeFailure("density framebuffer capture is missing or incomplete")
    return log_text, validate_extent_evidence(log_text, target, initial_client)


def capture_cell(args, cell):
    directory = args.output_directory / cell.extent / cell.route
    directory.mkdir(parents=True, exist_ok=True)
    output, log_path = directory / f"{cell.density:03d}.bmp", directory / f"{cell.density:03d}.log"
    route = "hardware" if cell.route == "hardware" else "screen-space"
    env = os.environ.copy()
    for name in SANITIZED_ENVIRONMENT:
        env.pop(name, None)
    env.update(NWB_REFRACTION_SMOKE_DENSITY=str(cell.density), NWB_REFRACTION_SMOKE_EXTENT=cell.extent, NWB_REFRACTION_SMOKE_ENABLED="1",
        NWB_REFRACTION_SMOKE_HARDWARE="1" if cell.route == "hardware" else "0", NWB_RENDERER_BASELINE_FIXED_DELTA_SECONDS="0.016666667")
    text, extent_evidence = launch_density_capture(args, cell, output, log_path, env)
    admission = validate_fixture(text, cell)
    routes = re.findall(r"^AVBOIT refraction resolve: (hardware|screen-space)$", text.replace("\r\n", "\n"), re.MULTILINE)
    if routes != [route]:
        raise SmokeFailure("density capture requires one actual matching resolve route")
    frame = read_bmp_24_rows(output)
    if frame[:2] != (cell.width, cell.height):
        raise SmokeFailure("captured density framebuffer dimensions differ from the requested cell")
    return frame, {"admission": admission, "image": str(output), "runtime_log": str(log_path), "actual_trace_route": route, "extent_evidence": extent_evidence}


def selection(text, allowed, label):
    values = tuple(part.strip() for part in text.split(","))
    if not values or len(values) != len(set(values)) or any(value not in allowed for value in values):
        raise argparse.ArgumentTypeError(f"{label} must be distinct comma-separated choices from {', '.join(allowed)}")
    return values


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", required=True, type=Path)
    parser.add_argument("--working-directory", required=True, type=Path)
    parser.add_argument("--output-directory", required=True, type=Path)
    parser.add_argument("--logserver-executable", type=Path)
    parser.add_argument("--extents", type=lambda text: selection(text, EXTENTS, "extents"), default=DEFAULT_EXTENTS)
    parser.add_argument("--routes", type=lambda text: selection(text, ROUTES, "routes"), default=ROUTES)
    parser.add_argument("--densities", type=lambda text: tuple(map(int, selection(text, tuple(map(str, DENSITIES)), "densities"))), default=DENSITIES)
    parser.add_argument("--frames", type=int, default=16)
    parser.add_argument("--timeout", type=float, default=120)
    parser.add_argument("--application-arg", action="append", default=[])
    args = parser.parse_args(argv)
    if args.frames <= 0 or not math.isfinite(args.timeout) or args.timeout <= 0:
        parser.error("frames and timeout must be finite and positive")
    args.executable, args.working_directory = args.executable.resolve(), args.working_directory.resolve()
    args.output_directory = args.output_directory.resolve()
    args.output_directory.mkdir(parents=True, exist_ok=True)
    metrics = {}
    try:
        for extent in args.extents:
            for route in args.routes:
                cell = Cell(extent, 0, route)
                captured = capture_cell(args, cell)
                if captured is None:
                    return SKIP_EXIT_CODE
                reference, info = captured
                metrics[cell.key] = {**info, "reference": validate_backdrop(reference)}
                for percent in args.densities:
                    if not percent:
                        continue
                    cell = Cell(extent, percent, route)
                    captured = capture_cell(args, cell)
                    if captured is None:
                        return SKIP_EXIT_CODE
                    image, info = captured
                    metrics[cell.key] = {**info, "coverage": analyze_density_frame(reference, image, percent, route)}
        (args.output_directory / "density_capture_metrics.json").write_text(json.dumps({"passed": True, "capture_protocol": CAPTURE_PROTOCOL, "cells": metrics}, indent=2) + "\n", encoding="utf-8")
        print(f"PASS: independently validated rendered density in {len(metrics)} extent/route/density captures")
        return 0
    except SmokeSkip as error:
        print(f"SKIP: {error}", file=sys.stderr)
        return SKIP_EXIT_CODE
    except (SmokeFailure, OSError, subprocess.TimeoutExpired) as error:
        (args.output_directory / "density_capture_metrics.json").write_text(json.dumps({"passed": False, "capture_protocol": CAPTURE_PROTOCOL, "error": str(error), "completed_cells": metrics}, indent=2) + "\n", encoding="utf-8")
        print(f"FAIL: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
