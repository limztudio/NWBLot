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

from refraction_density_benchmark import Cell, DEFAULT_EXTENTS, DENSITIES, EXTENTS, ROUTES
from refraction_density_evidence import RefractionDensityEvidence
from refraction_density_image_evidence import RefractionDensityImageEvidence
from window_capture_image import read_bmp_24_rows
from window_capture_termination import CaptureProcessTermination
from window_capture_smoke import (SKIP_EXIT_CODE, SmokeFailure, SmokeSkip, STRICT_LOG_FAILURE_MESSAGES, application_capture_partial_path,
    build_launch_environment, collect_log_delta, create_capture_backend, ensure_process_running, launch_logserver, launch_testbed,
    prepare_application_capture_environment, require_normal_process_exit, shutdown_logserver_and_collect, validate_application_capture_log_text)


CAPTURE_PROTOCOL = 2
SANITIZED_ENVIRONMENT = ("NWB_REFRACTION_SMOKE_CASE", "NWB_REFRACTION_SMOKE_GEOMETRY", "NWB_REFRACTION_SMOKE_LIFECYCLE", "NWB_REFRACTION_SMOKE_TIMING", "NWB_REFRACTION_FRAME_GRAPH_SNAPSHOT_PATH", "NWB_GPU_TIMING_FILE", "NWB_RENDERER_BASELINE_CAPTURE_FREEZE_FRAME")


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
        code, tail = CaptureProcessTermination.terminate_process(process, "density capture", handle)
        process = None
        require_normal_process_exit(code, tail, "density capture")
        log_text = shutdown_logserver_and_collect(logserver, log_directory, log_baseline, log_pattern)
        logserver = None
    finally:
        try:
            if process is not None:
                CaptureProcessTermination.terminate_process(process, "density capture", handle)
            if log_directory is not None and not log_text:
                log_text = shutdown_logserver_and_collect(logserver, log_directory, log_baseline, log_pattern)
                logserver = None
        finally:
            if logserver is not None:
                CaptureProcessTermination.terminate_process(logserver, "density capture logserver")
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
    admission = RefractionDensityEvidence.validate_fixture(text, cell)
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
                metrics[cell.key] = {**info, "reference": RefractionDensityImageEvidence.validate_backdrop(reference)}
                for percent in args.densities:
                    if not percent:
                        continue
                    cell = Cell(extent, percent, route)
                    captured = capture_cell(args, cell)
                    if captured is None:
                        return SKIP_EXIT_CODE
                    image, info = captured
                    metrics[cell.key] = {**info, "coverage": RefractionDensityImageEvidence.analyze_density_frame(reference, image, percent, route)}
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
