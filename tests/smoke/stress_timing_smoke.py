#!/usr/bin/env python3
"""One self-exiting stress presentation measurement using the existing runtime process owners."""

import argparse
import json
import math
from pathlib import Path
import platform
import re
import subprocess
import sys
import tempfile
from types import SimpleNamespace

import renderer_ab_benchmark as ab
from smoke_volume_identity import file_identity
import caustic_quality_smoke
from window_capture_smoke import (
    STRICT_LOG_FAILURE_MESSAGES, SmokeFailure, SmokeSkip, build_launch_environment,
    launch_logserver, launch_testbed, require_normal_process_exit,
    shutdown_logserver_and_collect, terminate_process, validate_expected_log_text, write_status,
)

START = "StressTestSmokeProject: presentation timing warmup_seconds=5 measure_seconds=30 clock=steady accepted_native_present=1"
DONE = "StressTestSmokeProject: presentation measurement complete "
INTERVAL = "StressTestSmokeProject: presentation fps "
SHUTDOWN = "StressTestSmokeProject: shutdown"
GPU_DEBUG = ("Loader: GPU debug validation enabled", "validation layer enabled: yes",
    "Vulkan GPU debug: debug utils messenger installed.")
REQUIRED = (START, SHUTDOWN, "AvboitTimingProbe: in-flight ranges 32",
    "AvboitTimingProbe: render unfocused 1", "AvboitTimingProbe: caustic in-flight ranges 32")


SHADOW_QUALITY_SETTINGS = "ShadowQualitySmoke: requested transparent_sampling="
SHADOW_TEMPORAL_ONE_RECORDED = "RendererSystem: recorded temporal-one transparent shadow sampling "
SHADOW_TRANSPARENT_SAMPLING = {"reference_three": 0, "temporal_one": 1}


SOFTWARE_SHADOW_SETTINGS = "SoftwareShadowSmoke: requested "
SOFTWARE_SHADOW_BACKENDS = {"automatic": 0, "trace": 1, "light_space": 2}
SOFTWARE_SHADOW_COVERAGE = {"reference": 0, "fitted_volume": 1}
SOFTWARE_SHADOW_BLOCKER_SEARCH = {"reference_grid9": 0, "compact_cross5": 1}


WORKLOAD = "StressTestSmokeProject: workload "
SPAWN = "StressTestSmokeProject: spawned "
WORKLOAD_FIELDS = ("characters_per_class", "total", "transparent", "opaque", "layout", "rows", "columns",
    "row_spacing_x", "row_stagger_x", "front_z", "back_z", "body_scale", "camera_x", "camera_y", "camera_z",
    "camera_pitch", "vertical_fov", "near_plane", "far_plane", "aspect")
WORKLOAD_INTEGERS = ("characters_per_class", "total", "transparent", "opaque", "rows", "columns")


def parse_workload(lines, characters_per_class):
    if characters_per_class not in (5, 10):
        raise SmokeFailure("characters per class must select the five or ten character profile")
    records = [line for line in lines if line.startswith(WORKLOAD)]
    spawns = [line for line in lines if line.startswith(SPAWN)]
    if len(records) != 1 or len(spawns) != 1:
        raise SmokeFailure("exactly one workload signature and spawned-character record are required")
    pattern = re.escape(WORKLOAD) + " ".join(re.escape(field) + r"=(\S+)" for field in WORKLOAD_FIELDS)
    match = re.fullmatch(pattern, records[0])
    if not match:
        raise SmokeFailure("malformed stress workload signature")
    row = dict(zip(WORKLOAD_FIELDS, match.groups()))
    try:
        for field in WORKLOAD_INTEGERS:
            if not re.fullmatch(r"[0-9]{1,2}", row[field]):
                raise ValueError("invalid integer")
            row[field] = int(row[field])
        for field in WORKLOAD_FIELDS:
            if field not in WORKLOAD_INTEGERS and field != "layout":
                row[field] = float(row[field])
                if not math.isfinite(row[field]):
                    raise ValueError("nonfinite workload value")
    except ValueError as error:
        raise SmokeFailure("invalid stress workload count or camera/layout number") from error
    comparison = characters_per_class == 5
    expected = dict(characters_per_class=characters_per_class, total=characters_per_class * 2,
        transparent=characters_per_class, opaque=characters_per_class,
        layout="zigzag_v1" if comparison else "two_rows_v1", rows=2, columns=characters_per_class,
        row_spacing_x=1.44 if comparison else .72, row_stagger_x=.36 if comparison else .18,
        front_z=-.55, back_z=.55, body_scale=1., camera_x=0., camera_y=1.8 if comparison else 2.7,
        camera_z=-4.8 if comparison else -7.2, camera_pitch=.2 if comparison else .25,
        vertical_fov=math.pi / 3., near_plane=.001, far_plane=10000., aspect=0.)
    for field, value in expected.items():
        matches = row[field] == value if field in WORKLOAD_INTEGERS or field == "layout" else math.isclose(
            row[field], value, rel_tol=1e-6, abs_tol=1e-6)
        if not matches:
            raise SmokeFailure(f"requested stress workload profile disagrees with observed {field}")
    expected_spawn = (SPAWN + f"{row['total']} spinning characters ({row['transparent']} transparent + "
        f"{row['opaque']} opaque) over ground, directional + point light")
    if spawns[0] != expected_spawn:
        raise SmokeFailure("spawned characters disagree with the requested stress workload")
    first_interval = next((index for index, line in enumerate(lines) if line.startswith(INTERVAL)), len(lines))
    if not lines.index(START) < lines.index(spawns[0]) < lines.index(records[0]) < first_interval:
        raise SmokeFailure("stress workload must be observed after startup and before measurement intervals")
    return {"requested_characters_per_class": characters_per_class, "observed": row, "signature": records[0]}


REFLECTION_ENABLED = "StressTestSmokeProject: reflection diagnostics enabled"
REFLECTION_SAMPLE = "StressReflectionStatistics: "
REFLECTION_FIELDS = ("sequence", "generation", "frame", "graphics_frame", "hardware_ready", "transport_enabled",
    "candidates", "hardware_rays", "exterior_eligible_rays", "hardware_queries", "bootstrap_events",
    "transparent_paths", "unsupported_paths")
REFLECTION_COUNTERS = REFLECTION_FIELDS[6:]
REFLECTION_SCREEN_FIELDS = ("screen_attempts", "screen_hits", "screen_returns", "screen_iterations", "screen_limit_misses")
REFLECTION_QUALITY_SETTINGS = "ReflectionQualitySmoke: requested screen_max_steps="


def parse_reflection_diagnostics(lines, requested):
    records = [(index, line) for index, line in enumerate(lines) if line.startswith(REFLECTION_SAMPLE)]
    if not requested:
        if REFLECTION_ENABLED in lines or records:
            raise SmokeFailure("unrequested reflection diagnostics invalidate the performance-only run")
        return {"requested": False, "status": "not_measured"}
    if lines.count(REFLECTION_ENABLED) != 1 or not records:
        raise SmokeFailure("requested reflection diagnostics require one enable marker and accepted samples")
    enabled_index = lines.index(REFLECTION_ENABLED)
    shutdown_index = lines.index(SHUTDOWN)
    pattern = re.escape(REFLECTION_SAMPLE) + " ".join(re.escape(field) + r"=([0-9]{1,20})" for field in REFLECTION_FIELDS)
    pattern += r"(?: " + " ".join(re.escape(field) + r"=([0-9]{1,20})" for field in REFLECTION_SCREEN_FIELDS) + r")?"
    sums = dict.fromkeys(REFLECTION_COUNTERS, 0)
    screen_sums = dict.fromkeys(REFLECTION_SCREEN_FIELDS, 0)
    screen_samples = 0
    maximum_frame_average_iterations = 0.0
    seen = set()
    ranges = {}
    previous = None
    hardware_ready_samples = transport_enabled_samples = 0
    for index, line in records:
        if not enabled_index < index < shutdown_index:
            raise SmokeFailure("reflection samples must follow enablement and precede shutdown")
        match = re.fullmatch(pattern, line)
        if not match:
            raise SmokeFailure("malformed accepted reflection diagnostic sample")
        row = dict(zip(REFLECTION_FIELDS, map(int, match.groups()[:len(REFLECTION_FIELDS)])))
        if match.groups()[len(REFLECTION_FIELDS)] is not None:
            screen = dict(zip(REFLECTION_SCREEN_FIELDS, map(int, match.groups()[len(REFLECTION_FIELDS):])))
            for field, value in screen.items():
                if value >= 2 ** (64 if field == "screen_iterations" else 32):
                    raise SmokeFailure("screen reflection counter exceeds its unsigned field bounds")
            attempts = screen["screen_attempts"]
            if (screen["screen_hits"] > screen["screen_returns"] or screen["screen_returns"] > attempts
                    or screen["screen_limit_misses"] > attempts or screen["screen_iterations"] > attempts * 256):
                raise SmokeFailure("screen reflection counters exceed their attempted ray population or maximum budget")
            screen_samples += 1
            for field in REFLECTION_SCREEN_FIELDS:
                screen_sums[field] += screen[field]
            maximum_frame_average_iterations = max(maximum_frame_average_iterations,
                screen["screen_iterations"] / attempts if attempts else 0.0)
        for field, value in row.items():
            bits = 64 if field in ("sequence", "generation", "graphics_frame") else 32
            if value >= 2 ** bits or (field in ("sequence", "generation") and value == 0):
                raise SmokeFailure("reflection diagnostic value exceeds its unsigned field bounds")
        if row["hardware_ready"] not in (0, 1) or row["transport_enabled"] not in (0, 1):
            raise SmokeFailure("reflection diagnostic flags must be zero or one")
        if row["transport_enabled"] and not row["hardware_ready"]:
            raise SmokeFailure("optical transport cannot be enabled without accepted hardware readiness")
        rays = row["hardware_rays"]
        if rays > row["candidates"] or any(row[field] > rays for field in
                ("exterior_eligible_rays", "transparent_paths", "unsupported_paths")):
            raise SmokeFailure("reflection path counters cannot exceed their admitted ray population")
        if not row["hardware_ready"] and any(row[field] for field in REFLECTION_COUNTERS if field != "candidates"):
            raise SmokeFailure("reflection work counters require accepted hardware readiness")
        if not row["transport_enabled"] and any(row[field] for field in
                ("exterior_eligible_rays", "bootstrap_events", "transparent_paths", "unsupported_paths")):
            raise SmokeFailure("optical-only counters require optical transport")
        if (rays == 0 and row["hardware_queries"] != 0) or (row["hardware_queries"] == 0
                and (row["bootstrap_events"] != 0 or row["transparent_paths"] != 0)):
            raise SmokeFailure("reflection query-dependent counters require admitted rays and actual queries")
        key = row["generation"], row["sequence"]
        if key in seen:
            raise SmokeFailure("duplicate accepted reflection diagnostic sample")
        seen.add(key)
        if previous is not None and row["generation"] == previous["generation"]:
            if row["sequence"] <= previous["sequence"] or row["graphics_frame"] <= previous["graphics_frame"]:
                raise SmokeFailure("accepted reflection sequence and graphics frame must advance within a generation")
        elif row["generation"] in ranges:
            raise SmokeFailure("accepted reflection diagnostics cannot return to an earlier generation")
        generation = ranges.setdefault(row["generation"], {"generation": row["generation"], "sample_count": 0,
            "sequence_range": [row["sequence"], row["sequence"]], "frame_range": [row["frame"], row["frame"]],
            "graphics_frame_range": [row["graphics_frame"], row["graphics_frame"]]})
        generation["sample_count"] += 1
        for field in ("sequence", "frame", "graphics_frame"):
            bounds = generation[field + "_range"]
            bounds[0], bounds[1] = min(bounds[0], row[field]), max(bounds[1], row[field])
        for field in REFLECTION_COUNTERS:
            sums[field] += row[field]
        hardware_ready_samples += row["hardware_ready"]
        transport_enabled_samples += row["transport_enabled"]
        previous = row
    rays = sums["hardware_rays"]
    if rays > 0 and sums["unsupported_paths"] == rays and sums["hardware_queries"] == 0:
        status = "all_rejected"
    elif sums["unsupported_paths"] > 0:
        status = "unsupported"
    elif sums["hardware_queries"] > 0:
        status = "queries_observed"
    else:
        status = "no_queries"
    return {"requested": True, "status": status, "sample_count": len(records),
        "sample_scope": "accepted_readbacks_including_warmup_not_presentation_counts",
        "ranges_by_generation": list(ranges.values()), "hardware_ready_samples": hardware_ready_samples,
        "transport_enabled_samples": transport_enabled_samples, "sums": sums,
        "unsupported_ratio": sums["unsupported_paths"] / rays if rays else None,
        "exterior_eligible_ratio": sums["exterior_eligible_rays"] / rays if rays else None,
        "queries_per_hardware_ray": sums["hardware_queries"] / rays if rays else None,
        "screen": {"sample_count": screen_samples, "sums": screen_sums,
            "maximum_frame_average_iterations": maximum_frame_average_iterations,
            "iterations_per_attempt": screen_sums["screen_iterations"] / screen_sums["screen_attempts"] if screen_sums["screen_attempts"] else None,
            "limit_miss_ratio": screen_sums["screen_limit_misses"] / screen_sums["screen_attempts"] if screen_sums["screen_attempts"] else None,
            "return_ratio": screen_sums["screen_returns"] / screen_sums["screen_attempts"] if screen_sums["screen_attempts"] else None}}


def parse_sample(line, prefix, rate_key, positive_count):
    match = re.fullmatch(re.escape(prefix) + re.escape(rate_key)
        + r"=(\S+) presentations=(\d+) seconds=(\S+) first=(\d+) last=(\d+)", line)
    if not match:
        raise SmokeFailure("malformed presentation measurement record")
    try:
        fps, frames, seconds, first, last = (float(match[1]), int(match[2]), float(match[3]), int(match[4]), int(match[5]))
    except ValueError as error:
        raise SmokeFailure("invalid presentation measurement number") from error
    if not math.isfinite(fps) or not math.isfinite(seconds) or fps < 0 or seconds <= 0:
        raise SmokeFailure("presentation rate and elapsed time must be finite and nonnegative")
    if not 0 <= first <= last < 2 ** 64 or frames != last - first or (positive_count and frames == 0):
        raise SmokeFailure("presentation count must equal last minus first and be positive for completion")
    if not math.isclose(fps, frames / seconds, rel_tol=1e-9, abs_tol=1e-9):
        raise SmokeFailure("presentation FPS must be count divided by actual wall seconds")
    return {"fps": fps, "presentations": frames, "seconds": seconds, "first": first, "last": last}


def parse_runtime_log(text, exit_code, application_args=(), reflection_diagnostics=False, characters_per_class=10):
    require_normal_process_exit(exit_code, "", "stress presentation timing")
    validate_expected_log_text(text, list(REQUIRED), list(STRICT_LOG_FAILURE_MESSAGES) + [
        "presentation measurement incomplete", "render submission suspended", "render pass skipped", "device recreation"])
    lines = [line.strip() for line in text.splitlines()]
    if any(lines.count(marker) != 1 for marker in REQUIRED):
        raise SmokeFailure("one exact startup, reservation, fixture and shutdown sequence is required")
    workload = parse_workload(lines, characters_per_class)
    if sum(line.startswith("StressTestSmokeProject: presentation timing ") for line in lines) != 1:
        raise SmokeFailure("multiple or conflicting presentation timing configurations")
    if any(value == "--gpudbg" or value.startswith("--gpudbg=") for value in application_args):
        if any(lines.count(marker) != 1 for marker in GPU_DEBUG):
            raise SmokeFailure("requested GPU validation requires actual loader, layer and messenger markers")
    completions = [line for line in lines if line.startswith(DONE)]
    intervals = [parse_sample(line, INTERVAL, "avg", False) for line in lines if line.startswith(INTERVAL)]
    if len(completions) != 1 or not intervals:
        raise SmokeFailure("exactly one completion and its interval evidence are required")
    total = parse_sample(completions[0], DONE, "fps", True)
    if total["seconds"] < 30.0:
        raise SmokeFailure("measurement must span at least 30 actual wall seconds")
    if not lines.index(START) < lines.index(completions[0]) < lines.index(SHUTDOWN):
        raise SmokeFailure("startup, completion and shutdown order is invalid")
    interval_lines = [index for index, line in enumerate(lines) if line.startswith(INTERVAL)]
    if not lines.index(START) < min(interval_lines) <= max(interval_lines) < lines.index(completions[0]):
        raise SmokeFailure("intervals must occur inside the completed measurement")
    previous = total["first"]
    for interval in intervals:
        if interval["first"] != previous:
            raise SmokeFailure("presentation intervals must form a contiguous count chain")
        previous = interval["last"]
    if previous != total["last"] or sum(row["presentations"] for row in intervals) != total["presentations"]:
        raise SmokeFailure("interval counts do not match completion")
    if not math.isclose(sum(row["seconds"] for row in intervals), total["seconds"], rel_tol=1e-9, abs_tol=1e-9):
        raise SmokeFailure("interval wall times do not match completion")
    pacing = [line for line in lines if line.startswith("StressTestSmokeProject: presentation pacing ")]
    pacing_summary = None
    if len(pacing) == 1:
        pace = re.fullmatch(re.escape("StressTestSmokeProject: presentation pacing ") + r"samples=(\d+) p50ms=(\S+) p95ms=(\S+) maxms=(\S+) stalls50ms=(\d+)", pacing[0])
        if not pace:
            raise SmokeFailure("malformed presentation pacing summary")
        try:
            samples, p50, p95, maxms, stalls = (int(pace[1]), float(pace[2]), float(pace[3]), float(pace[4]), int(pace[5]))
        except ValueError as error:
            raise SmokeFailure("invalid presentation pacing number") from error
        if samples < 0 or not all(math.isfinite(v) for v in (p50, p95, maxms)) or stalls < 0 or p50 < 0 or p95 < 0 or maxms < 0:
            raise SmokeFailure("presentation pacing values must be finite and nonnegative")
        if not p50 <= p95 <= maxms:
            raise SmokeFailure("presentation pacing percentiles must be ordered p50 <= p95 <= max")
        pacing_summary = {"samples": samples, "p50ms": p50, "p95ms": p95, "maxms": maxms, "stalls50ms": stalls}
    elif len(pacing) > 1:
        raise SmokeFailure("exactly one presentation pacing summary is allowed")
    capability = [line for line in lines if line.startswith("StressTestSmokeProject: device capability ")]
    if len(capability) > 1:
        raise SmokeFailure("exactly one device capability report is allowed")
    extents = re.findall(r"deferred rendering targets ready \((\d+)x(\d+),", text)
    if not extents or any(pair != ("1280", "900") for pair in extents):
        raise SmokeFailure("actual stress extent must be 1280x900")
    return {"measurement": total | {"frame_ms": 1000.0 * total["seconds"] / total["presentations"]},
        "intervals": intervals, "width": 1280, "height": 900, "warmup_seconds": 5,
        "requested_measurement_seconds": 30, "clock": "steady", "count": "accepted_native_present",
        "workload": workload, "pacing": pacing_summary, "optical_reflection": parse_reflection_diagnostics(lines, reflection_diagnostics)}


def parse_measurement(log_text, characters_per_class=10):
    """Replay the complete raw measurement log without requiring a process launch."""
    parsed = parse_runtime_log(log_text, 0, characters_per_class=characters_per_class)
    return parsed["measurement"] | {"intervals": parsed["intervals"]}


def verify_software_shadow_settings(text, args):
    """New acquisitions must prove the requested settings reached the application."""
    records = [line.strip() for line in text.splitlines() if line.strip().startswith(SOFTWARE_SHADOW_SETTINGS)]
    if len(records) != 1:
        raise SmokeFailure("exactly one SoftwareShadowSmoke requested-settings record is required")
    fields = ("backend", "directional_resolution", "point_resolution", "budget_bytes", "coverage", "blocker_search")
    pattern = re.escape(SOFTWARE_SHADOW_SETTINGS) + " ".join(re.escape(field) + r"=([0-9]+)" for field in fields)
    match = re.fullmatch(pattern, records[0])
    if not match:
        raise SmokeFailure("malformed SoftwareShadowSmoke requested-settings record")
    observed = dict(zip(fields, map(int, match.groups())))
    requested = dict(backend=SOFTWARE_SHADOW_BACKENDS[args.software_shadow_backend],
        directional_resolution=args.software_shadow_directional_resolution,
        point_resolution=args.software_shadow_point_resolution, budget_bytes=args.software_shadow_budget_mib * 1024 * 1024,
        coverage=SOFTWARE_SHADOW_COVERAGE[args.software_shadow_coverage],
        blocker_search=SOFTWARE_SHADOW_BLOCKER_SEARCH[args.software_shadow_blocker_search])
    if observed != requested:
        raise SmokeFailure(f"software shadow settings mismatch: requested {requested}, application reported {observed}")
    return {"backend_name": args.software_shadow_backend, "budget_mib": args.software_shadow_budget_mib,
        "blocker_search_name": args.software_shadow_blocker_search, "requested": requested, "observed": observed, "verified": True}


def verify_shadow_quality_settings(text, args):
    records = [line.strip() for line in text.splitlines() if line.strip().startswith(SHADOW_QUALITY_SETTINGS)]
    if len(records) != 1:
        raise SmokeFailure("exactly one ShadowQualitySmoke requested-settings record is required")
    expected = SHADOW_QUALITY_SETTINGS + str(SHADOW_TRANSPARENT_SAMPLING[args.shadow_transparent_sampling])
    if records[0] != expected:
        raise SmokeFailure("shadow sampling quality mismatch between request and application")
    dispatched = [line.strip() for line in text.splitlines() if line.strip().startswith(SHADOW_TEMPORAL_ONE_RECORDED)]
    hardware = None
    if args.shadow_transparent_sampling == "temporal_one":
        if len(dispatched) != 1:
            raise SmokeFailure("temporal-one acquisition requires exactly one effective dispatch marker")
        match = re.fullmatch(re.escape(SHADOW_TEMPORAL_ONE_RECORDED) + r"samples=1 hardware=([01])", dispatched[0])
        if not match:
            raise SmokeFailure("invalid effective temporal-one dispatch marker")
        hardware = bool(int(match[1]))
    elif dispatched:
        raise SmokeFailure("unrequested temporal-one dispatch was recorded")
    return {"transparent_sampling": args.shadow_transparent_sampling,
        "bootstrap_samples": 3, "accepted_history_samples": 1 if args.shadow_transparent_sampling == "temporal_one" else 3,
        "effective_dispatch_verified": bool(dispatched), "hardware": hardware, "verified": True}


def verify_reflection_quality_settings(text, args, optical):
    records = [line.strip() for line in text.splitlines() if line.strip().startswith(REFLECTION_QUALITY_SETTINGS)]
    if records != [REFLECTION_QUALITY_SETTINGS + str(args.reflection_screen_steps)]:
        raise SmokeFailure("reflection screen step budget mismatch between request and application")
    if args.reflection_diagnostics:
        screen = optical["screen"]
        if screen["sample_count"] != optical["sample_count"]:
            raise SmokeFailure("new reflection diagnostic acquisition requires accepted screen-work counters for every sample")
        if screen["maximum_frame_average_iterations"] > args.reflection_screen_steps:
            raise SmokeFailure("accepted screen iteration count exceeds the requested step budget")
    return {"screen_max_steps": args.reflection_screen_steps, "verified": True,
        "screen_work_measured": args.reflection_diagnostics}


def launch_environment(base, args, output):
    for key in ("VK_INSTANCE_LAYERS", "VK_LOADER_LAYERS_ENABLE"):
        if base.get(key, "").strip():
            raise SmokeFailure(f"unrequested Vulkan layer override: {key}")
    result = {key: value for key, value in base.items() if not key.startswith("NWB_")}
    if platform.system() == "Linux":
        result["NWB_LINUX_BACKEND"] = "x11"
    result.update(NWB_STRESS_SMOKE_TIMING="1",
        NWB_STRESS_CHARACTERS_PER_CLASS=str(args.characters_per_class),
        NWB_REFLECTION_SCREEN_STEPS=str(args.reflection_screen_steps),
        NWB_SOFTWARE_SHADOW_BACKEND=args.software_shadow_backend,
        NWB_SOFTWARE_SHADOW_COVERAGE=args.software_shadow_coverage,
        NWB_SOFTWARE_SHADOW_BLOCKER_SEARCH=args.software_shadow_blocker_search,
        NWB_SHADOW_TRANSPARENT_SAMPLING=args.shadow_transparent_sampling,
        NWB_SOFTWARE_SHADOW_BUDGET_MIB=str(args.software_shadow_budget_mib),
        NWB_SOFTWARE_SHADOW_DIRECTIONAL_RESOLUTION=str(args.software_shadow_directional_resolution),
        NWB_SOFTWARE_SHADOW_POINT_RESOLUTION=str(args.software_shadow_point_resolution),
        NWB_CAUSTIC_PHOTON_GRID_DIVISOR=str(args.caustic_photon_grid_divisor),
        NWB_GPU_TIMING_FILE=str(output / "gpu_timing.txt"))
    if not args.animate:
        result.update(NWB_STRESS_TEST_SPIN_ANGLE=str(args.spin_angle),
            NWB_RENDERER_BASELINE_FIXED_DELTA_SECONDS=str(args.fixed_delta_seconds))
    if args.reflection_diagnostics:
        result["NWB_STRESS_REFLECTION_DIAGNOSTICS"] = "1"
    return result


def reserve_output(requested, protected):
    if requested is None:
        return Path(tempfile.mkdtemp(prefix="nwb_stress_presentation_timing_")).resolve()
    output = requested.resolve()
    if output.exists():
        raise SmokeFailure("output must be new; prior evidence is never overwritten")
    for item in protected:
        item = item.resolve()
        if output == item or output.is_relative_to(item) or item.is_relative_to(output):
            raise SmokeFailure("output overlaps a protected input")
    output.mkdir(parents=True, exist_ok=False)
    return output


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def identities(args, helpers):
    return {"renderer": ab.binary_identity(args.executable), "runtime": ab.runtime_identity(args.working_directory),
        "logger": None if args.no_logserver else ab.binary_identity(args.logserver_executable),
        "python": file_identity(Path(sys.executable)),
        "helpers": {str(path): file_identity(path) for path in helpers}}


def acquire(args, output):
    helpers = sorted({Path(module.__file__).resolve() for module in list(sys.modules.values())
        if getattr(module, "__file__", None) and Path(module.__file__).suffix == ".py"
        and ("tests" in Path(module.__file__).parts or Path(module.__file__).resolve() == Path(__file__).resolve())})
    before = identities(args, helpers)
    env = launch_environment(build_launch_environment(args), args, output)
    launch = SimpleNamespace(**vars(args), log_port=0)
    write_json(output / "launch.json", {"executable": str(args.executable), "working_directory": str(args.working_directory),
        "application_args": args.application_arg, "timeout_seconds": args.timeout,
        "environment": {key: value for key, value in env.items() if key.startswith(("NWB_", "VK_"))},
        "identity_before": before})
    process = logserver = log_directory = None
    baseline, pattern = {}, ""
    collected = False
    code = None
    try:
        logserver, port, log_directory, baseline, pattern = launch_logserver(launch, args.executable, env)
        process = launch_testbed(launch, args.executable, env, port)
        try:
            process.wait(timeout=args.timeout)
        except subprocess.TimeoutExpired as error:
            raise SmokeFailure("stress measurement did not self-exit before timeout") from error
        code, tail = terminate_process(process, "stress presentation timing")
        process = None
        (output / "process_tail.txt").write_text(tail, encoding="utf-8")
        require_normal_process_exit(code, tail, "stress presentation timing")
        text = shutdown_logserver_and_collect(logserver, log_directory, baseline, pattern)
        logserver = None
        collected = True
        (output / "runtime.log").write_text(text, encoding="utf-8")
        result = parse_runtime_log(text, code, args.application_arg, args.reflection_diagnostics, args.characters_per_class)
        result["software_shadow_settings"] = verify_software_shadow_settings(text, args)
        result["caustic_quality_settings"] = caustic_quality_smoke.verify_settings(text, args.caustic_photon_grid_divisor)
        result["shadow_quality_settings"] = verify_shadow_quality_settings(text, args)
        result["reflection_quality_settings"] = verify_reflection_quality_settings(text, args, result["optical_reflection"])
        result["runtime_signature"] = ab.device_material_signature(text)
        result["motion"] = {"mode": "rotating" if args.animate else "fixed",
            "simulation_clock": "wall" if args.animate else "fixed_step",
            "spin_angle": None if args.animate else args.spin_angle,
            "fixed_delta_seconds": None if args.animate else args.fixed_delta_seconds}
        after = identities(args, helpers)
        if before != after:
            raise SmokeFailure("renderer, resources, logger, interpreter or helper identity changed during acquisition")
        result.update(schema=1, passed=True, exit_code=code, identity_before=before, identity_after=after,
            raw_files={name: file_identity(output / name) for name in ("runtime.log", "process_tail.txt", "gpu_timing.txt")
                if (output / name).is_file()})
        return result
    finally:
        primary_failure = sys.exc_info()[0] is not None
        errors = []
        if process is not None:
            try:
                _, tail = terminate_process(process, "stress presentation timing")
                (output / "process_tail.txt").write_text(tail, encoding="utf-8")
            except Exception as error:
                errors.append(f"renderer cleanup: {error}")
        if not collected and log_directory is not None:
            try:
                text = shutdown_logserver_and_collect(logserver, log_directory, baseline, pattern)
                (output / "runtime.log").write_text(text, encoding="utf-8")
                logserver = None
            except Exception as error:
                errors.append(f"failure-path log collection: {error}")
        try:
            terminate_process(logserver, "logserver")
        except Exception as error:
            errors.append(f"logger cleanup: {error}")
        if errors:
            write_json(output / "cleanup_error.json", {"errors": errors})
            if not primary_failure:
                raise SmokeFailure("; ".join(errors))


def parse_args(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", required=True, type=Path)
    parser.add_argument("--working-directory", required=True, type=Path)
    parser.add_argument("--logserver-executable", type=Path)
    parser.add_argument("--no-logserver", action="store_true")
    parser.add_argument("--output-directory", type=Path)
    parser.add_argument("--timeout", type=float, default=90.0)
    parser.add_argument("--characters-per-class", type=int, choices=(5, 10), default=10,
        help="Ten per class is the twenty-body target; five preserves the historical comparison layout/camera.")
    parser.add_argument("--shadow-transparent-sampling", choices=tuple(SHADOW_TRANSPARENT_SAMPLING), default="reference_three",
        help="Temporal-one uses one transparent shadow sample after accepted temporal history, on either HW or SW.")
    parser.add_argument("--software-shadow-backend", choices=tuple(SOFTWARE_SHADOW_BACKENDS), default="automatic")
    parser.add_argument("--software-shadow-coverage", choices=tuple(SOFTWARE_SHADOW_COVERAGE), default="reference",
        help="Fitted-volume coverage uses empty directional margins and retains receivers beyond a complete map's far plane.")
    parser.add_argument("--software-shadow-blocker-search", choices=tuple(SOFTWARE_SHADOW_BLOCKER_SEARCH), default="reference_grid9",
        help="Compact cross uses five blocker taps with cheap off-center opaque plane estimates; the center stays fully checked.")
    parser.add_argument("--software-shadow-budget-mib", type=int, default=256,
        help="Requested shadow-map storage budget in MiB (1 through 4095).")
    parser.add_argument("--software-shadow-directional-resolution", type=int, default=512)
    parser.add_argument("--software-shadow-point-resolution", type=int, default=256)
    motion = parser.add_mutually_exclusive_group()
    motion.add_argument("--spin-angle", type=float, default=0.6)
    motion.add_argument("--animate", action="store_true",
        help="Continuously rotate the bodies using wall-time simulation; excludes fixed yaw and fixed simulation delta.")
    parser.add_argument("--fixed-delta-seconds", type=float)
    parser.add_argument("--application-arg", action="append", default=[])
    caustic_quality_smoke.add_arguments(parser)
    parser.add_argument("--reflection-screen-steps", type=int, default=96,
        help="Maximum SSR hierarchy iterations per attempted surface ray (8 through 256); lower budgets fall back normally.")
    parser.add_argument("--reflection-diagnostics", action="store_true",
        help="Enable accepted reflection-path counters; diagnostic runs are separate from performance comparisons.")
    args = parser.parse_args(argv)
    if not 8 <= args.reflection_screen_steps <= 256:
        parser.error("reflection screen steps must be 8 through 256")
    for key in ("executable", "working_directory", "logserver_executable"):
        if getattr(args, key) is not None:
            setattr(args, key, getattr(args, key).resolve())
    if not args.executable.is_file() or not args.working_directory.is_dir():
        parser.error("existing renderer and runtime paths are required")
    if not args.no_logserver and (args.logserver_executable is None or not args.logserver_executable.is_file()):
        parser.error("an existing logger is required unless --no-logserver is selected")
    if not math.isfinite(args.timeout) or not 35 < args.timeout <= 100:
        parser.error("timeout must be finite and greater than 35 through 100 seconds")
    if not math.isfinite(args.spin_angle) or not 0 <= args.spin_angle < math.tau:
        parser.error("spin angle must be finite in [0, 2pi)")
    if args.animate and args.fixed_delta_seconds is not None:
        parser.error("--animate cannot be combined with --fixed-delta-seconds")
    if args.fixed_delta_seconds is None and not args.animate:
        args.fixed_delta_seconds = .016666667
    if args.fixed_delta_seconds is not None and (not math.isfinite(args.fixed_delta_seconds) or not 0 < args.fixed_delta_seconds <= .25):
        parser.error("fixed simulation delta must be finite in (0, .25]")
    if not 1 <= args.software_shadow_budget_mib <= 4095:
        parser.error("software shadow budget must be from 1 through 4095 MiB")
    for name in ("software_shadow_directional_resolution", "software_shadow_point_resolution"):
        if not 32 <= getattr(args, name) <= 2048:
            parser.error("software shadow resolutions must be from 32 through 2048")
    args.software_vulkan = "off"
    return args


def main(argv=None):
    output = None
    try:
        args = parse_args(argv)
        protected = [args.executable.parent, args.working_directory, Path(__file__).resolve().parent]
        if args.logserver_executable is not None:
            protected.append(args.logserver_executable.parent)
        output = reserve_output(args.output_directory, protected)
        write_status(f"Stress presentation timing artifacts: {output}")
        result = acquire(args, output)
        write_json(output / "result.json", result)
        write_status(result["workload"]["signature"])
        write_status(f"PASS: {result['measurement']['fps']:.4f} accepted presentations/s over {result['measurement']['seconds']:.6f}s")
        optical = result["optical_reflection"]
        write_status(f"Optical reflection: {optical['status']} (query activity alone does not certify optical correctness)")
        if optical["requested"]:
            write_status(f"Reflection samples={optical['sample_count']} unsupported_ratio={optical['unsupported_ratio']} "
                f"exterior_eligible_ratio={optical['exterior_eligible_ratio']} queries_per_hardware_ray={optical['queries_per_hardware_ray']}")
        return 0
    except SmokeSkip as error:
        if output is not None:
            write_json(output / "failure.json", {"passed": False, "skipped": True, "error": str(error)})
        write_status(f"SKIP: {error}")
        return 77
    except (SmokeFailure, OSError) as error:
        if output is not None:
            write_json(output / "failure.json", {"passed": False, "error": str(error)})
        write_status(f"FAIL: {error}")
        return 1


if __name__ == "__main__":
    sys.exit(main())
