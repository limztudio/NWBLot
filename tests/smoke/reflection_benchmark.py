#!/usr/bin/env python3
"""Measure reflection GPU work without framebuffer readbacks or diagnostic counters.

Each interleaved block is one statistical unit. Publication-window averages and
CPU frame counts are never used as per-frame GPU timings. Correctness belongs to
the separate reflection smoke suites; this runner measures their stationary cases.
"""
from __future__ import annotations

import argparse
from dataclasses import asdict, dataclass
import json
import math
from pathlib import Path
import random
import re
import statistics
import subprocess
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "ab"))
from gpu_timing_parse import load_name_symbols
from smoke_volume_identity import authored_volume_hashes, file_identity, runtime_pipeline_cache_paths
from window_capture_smoke import (
    SKIP_EXIT_CODE, STRICT_LOG_FAILURE_MESSAGES, SmokeFailure, SmokeSkip,
    build_launch_environment, create_capture_backend, ensure_process_running,
    launch_logserver, launch_testbed, require_normal_process_exit,
    shutdown_logserver_and_collect, terminate_process, validate_expected_log_text,
    write_status,
)

FRAME = "render.frame"
CLASSIFY = "render.reflection_classify"
DEPTH = "render.reflection_depth_pyramid"
BUILD_ARGS = "render.reflection_build_args"
HARDWARE = "render.reflection_hardware"
TEMPORAL = "render.reflection_temporal"
SPATIAL = "render.reflection_spatial"
CONTROLS = ("render.opaque_regular", "render.shadow_visibility", "render.deferred_lighting")
OBSERVED_CONTROLS = CONTROLS + ("render.deferred_composite", "render.deferred_present")
KERNELS = (CLASSIFY, DEPTH, BUILD_ARGS, HARDWARE, TEMPORAL, SPATIAL)
KNOWN_SCOPES = (FRAME,) + OBSERVED_CONTROLS + KERNELS
HEADER = re.compile(r"^=== interval: (\d+) frames / ([-+0-9.eE]+)s ===$", re.MULTILINE)
SCOPE = re.compile(r"^  ([^:]+): (.+)$")
DIMENSIONS = re.compile(r"deferred rendering targets ready \((\d+)x(\d+),")
FEEDBACK_PROBE_PERIOD = 16
TIMING_IN_FLIGHT_RANGES = 32


@dataclass(frozen=True)
class Variant:
    name: str
    mode: str
    temporal: bool = False
    spatial: bool = False
    feedback: bool = False


@dataclass(frozen=True)
class ScopeSample:
    total_ms: float
    gpu_samples: int


def variants_for(family, include_screen=False, include_feedback=False):
    if family == "rough":
        variants = (Variant("disabled", "disabled"), Variant("hardware_raw", "hardware"),
            Variant("hardware_temporal", "hardware", True),
            Variant("hardware_spatial", "hardware", False, True),
            Variant("hardware_filtered", "hardware", True, True))
        return variants + ((Variant("hybrid_raw", "hybrid"), Variant("hybrid_feedback", "hybrid", feedback=True))
            if include_feedback else ())
    variants = (Variant("disabled", "disabled"), Variant("hardware", "hardware"), Variant("hybrid", "hybrid"))
    if include_screen:
        variants += (Variant("screen", "screen"),)
    if include_feedback:
        variants += (Variant("hybrid_feedback", "hybrid", feedback=True),)
    return variants


def balanced_orders(variants, blocks, seed):
    """Williams rows balance position and preceding treatment over a full cycle."""
    count = len(variants)
    cycle = count if count % 2 == 0 else 2 * count
    if blocks < 5 or blocks % cycle:
        raise SmokeFailure(f"blocks must be at least 5 and a multiple of the balanced cycle ({cycle})")
    permutation = list(variants)
    random.Random(seed).shuffle(permutation)
    base = [0]
    for offset in range(1, count):
        base.append((offset + 1) // 2 if offset % 2 else count - offset // 2)
    rows = [[permutation[(value + shift) % count] for value in base] for shift in range(count)]
    if count % 2:
        rows += [list(reversed(row)) for row in rows]
    return [rows[index % cycle] for index in range(blocks)]


def parse_intervals(text, symbols=None, finalized=False):
    """Only a following header seals a live report; the current tail may be partial."""
    symbols = symbols or {}
    text = text.replace("\r\n", "\n")
    headers = list(HEADER.finditer(text))
    result = []
    count = len(headers) if finalized else max(0, len(headers) - 1)
    for index in range(count):
        match = headers[index]
        seconds = float(match.group(2))
        if not math.isfinite(seconds) or seconds <= 0:
            raise SmokeFailure("invalid timing publication interval")
        end = headers[index + 1].start() if index + 1 < len(headers) else len(text)
        samples = {}
        for line in text[match.end():end].splitlines():
            if not line.strip():
                continue
            parsed = SCOPE.fullmatch(line)
            if not parsed:
                raise SmokeFailure(f"malformed completed GPU timing row: {line}")
            scope = symbols.get(parsed.group(1), parsed.group(1))
            fields = {}
            for field in parsed.group(2).split():
                if "=" not in field:
                    raise SmokeFailure(f"malformed timing field: {field}")
                key, value = field.split("=", 1)
                if key in fields:
                    raise SmokeFailure(f"duplicate timing field: {key}")
                fields[key] = value
            if "total_ms" not in fields or "gpu_samples" not in fields:
                raise SmokeFailure("timing file lacks total_ms/gpu_samples; rebuild the current timing probe")
            try:
                total = float(fields["total_ms"])
                raw_count = fields["gpu_samples"]
                if not raw_count.isdecimal():
                    raise ValueError("sample count is not an unsigned integer")
                gpu_samples = int(raw_count)
            except ValueError as error:
                raise SmokeFailure(f"invalid normalized timing row: {line}") from error
            if not math.isfinite(total) or total < 0 or gpu_samples <= 0:
                raise SmokeFailure(f"nonfinite, negative, or empty GPU timing sample: {line}")
            if scope in samples:
                raise SmokeFailure(f"duplicate timing scope in one report: {scope}")
            samples[scope] = ScopeSample(total, gpu_samples)
        result.append(samples)
    return result


def summarize_intervals(intervals):
    totals = {}
    for interval in intervals:
        for scope, value in interval.items():
            entry = totals.setdefault(scope, {"total_ms": 0.0, "gpu_samples": 0, "reports": 0})
            entry["total_ms"] += value.total_ms
            entry["gpu_samples"] += value.gpu_samples
            entry["reports"] += 1
    for entry in totals.values():
        entry["mean_ms"] = entry["total_ms"] / entry["gpu_samples"]
    return totals


def retain_after_warmup(reports, warmup_intervals, minimum_warmup_frames=0):
    """Feedback warms up by completed GPU observations, not a presumed wall-clock frame rate."""
    start = min(warmup_intervals, len(reports))
    completed = sum(report[FRAME].gpu_samples for report in reports[:start] if FRAME in report)
    while start < len(reports) and completed < minimum_warmup_frames:
        completed += reports[start].get(FRAME, ScopeSample(0.0, 0)).gpu_samples
        start += 1
    return start, reports[start:]


def required_scopes(variant, history_samples=16):
    required = [FRAME, *OBSERVED_CONTROLS, CLASSIFY]
    if variant.mode in ("screen", "hybrid"):
        required.append(DEPTH)
    if variant.mode in ("hardware", "hybrid"):
        required.extend((BUILD_ARGS, HARDWARE))
    if variant.temporal and history_samples > 1:
        required.append(TEMPORAL)
    if variant.spatial:
        required.append(SPATIAL)
    return required


def validate_coverage(summaries, variant, report_count, minimum_frames, mip_count, history_samples=16):
    if variant.temporal and history_samples <= 1 and TEMPORAL in summaries:
        raise SmokeFailure("single-sample temporal history recorded an unexpected native dispatch")
    missing = [scope for scope in required_scopes(variant, history_samples) if scope not in summaries]
    if missing:
        raise SmokeFailure("missing GPU scopes (including possible probe scope-capacity loss): " + ", ".join(missing))
    frame_count = summaries[FRAME]["gpu_samples"]
    if frame_count < minimum_frames:
        raise SmokeFailure(f"only {frame_count} completed GPU frames; {minimum_frames} required")
    for scope in required_scopes(variant, history_samples):
        entry = summaries[scope]
        if entry["gpu_samples"] < minimum_frames // 2:
            raise SmokeFailure(f"scope {scope} has too few completed GPU samples")
        if entry["reports"] < math.ceil(report_count * 0.75):
            raise SmokeFailure(f"scope {scope} is absent in too many timing reports")
        if scope in KERNELS:
            multiplier = mip_count if scope == DEPTH else 1
            expected = frame_count * multiplier
            if abs(entry["gpu_samples"] - expected) > max(2 * multiplier, expected * 0.02):
                raise SmokeFailure(f"scope {scope} sample ratio does not match {multiplier} dispatch(es) per GPU frame")
    if summaries[FRAME]["mean_ms"] <= 0:
        raise SmokeFailure("GPU frame timing is zero")


def kernel_work_ms(summaries, variant, mip_count, history_samples=16):
    return sum(summaries[scope]["mean_ms"] * (mip_count if scope == DEPTH else 1)
        for scope in required_scopes(variant, history_samples) if scope in KERNELS)


def percentile(sorted_values, fraction):
    position = (len(sorted_values) - 1) * fraction
    low = math.floor(position)
    high = math.ceil(position)
    return sorted_values[low] + (sorted_values[high] - sorted_values[low]) * (position - low)


def paired_statistics(differences, seed=0, draws=10000):
    if len(differences) < 5 or any(not math.isfinite(value) for value in differences):
        raise SmokeFailure("paired inference needs at least five finite independent block differences")
    rng = random.Random(seed)
    means = sorted(statistics.fmean(rng.choices(differences, k=len(differences))) for _ in range(draws))
    return {"blocks": len(differences), "mean_ms": statistics.fmean(differences),
        "median_ms": statistics.median(differences), "ci95_mean_ms": [percentile(means, .025), percentile(means, .975)],
        "block_differences_ms": differences}


def compare_trials(trials, variants, practical_ms=.02, practical_fraction=.03, control_floor_ms=.015, seed=0):
    by_block = {}
    for trial in trials:
        block = by_block.setdefault(trial["block"], {})
        name = trial["variant"]["name"]
        if name in block:
            raise SmokeFailure(f"duplicate trial {trial['block']}/{name}")
        block[name] = trial
    names = [variant.name for variant in variants]
    if any(set(block) != set(names) for block in by_block.values()):
        raise SmokeFailure("inference refuses incomplete blocks")
    pairs = [(names[0], name) for name in names[1:]]
    if "hardware" in names and "hybrid" in names:
        pairs.append(("hardware", "hybrid"))
    if "hardware_raw" in names:
        pairs += [("hardware_raw", name) for name in names if name.startswith("hardware_") and name != "hardware_raw"]
        if "hybrid_raw" in names:
            pairs.append(("hardware_raw", "hybrid_raw"))
    if "hybrid_feedback" in names:
        baseline = "hybrid_raw" if "hybrid_raw" in names else "hybrid"
        pairs.append((baseline, "hybrid_feedback"))
    comparisons = []
    for baseline, candidate in pairs:
        blocks = [by_block[index] for index in sorted(by_block)]
        deltas = [b[candidate]["scopes"][FRAME]["mean_ms"] - b[baseline]["scopes"][FRAME]["mean_ms"] for b in blocks]
        frame = paired_statistics(deltas, seed)
        kernels = paired_statistics([b[candidate]["kernel_work_ms"] - b[baseline]["kernel_work_ms"] for b in blocks], seed)
        dispatch_scopes = {}
        for scope in KERNELS:
            if all(scope in block[baseline]["scopes"] and scope in block[candidate]["scopes"] for block in blocks):
                dispatch_scopes[scope] = paired_statistics([
                    block[candidate]["scopes"][scope]["mean_ms"] - block[baseline]["scopes"][scope]["mean_ms"]
                    for block in blocks], seed)
        controls = {}
        unstable = False
        uncertain_controls = False
        for scope in CONTROLS:
            values = [b[candidate]["scopes"][scope]["mean_ms"] - b[baseline]["scopes"][scope]["mean_ms"] for b in blocks]
            metric = paired_statistics(values, seed)
            base = statistics.fmean(b[baseline]["scopes"][scope]["mean_ms"] for b in blocks)
            limit = max(control_floor_ms, practical_fraction * base)
            low, high = metric["ci95_mean_ms"]
            metric["material_drift"] = low > limit or high < -limit
            metric["equivalent_within_tolerance"] = low >= -limit and high <= limit
            metric["tolerance_ms"] = limit
            unstable |= metric["material_drift"]
            uncertain_controls |= not metric["equivalent_within_tolerance"]
            controls[scope] = metric
        base = statistics.fmean(b[baseline]["scopes"][FRAME]["mean_ms"] for b in blocks)
        threshold = max(practical_ms, practical_fraction * base)
        low, high = frame["ci95_mean_ms"]
        status = "unresolved"
        if unstable:
            status = "control_drift"
        elif uncertain_controls:
            status = "control_uncertain"
        elif high < -threshold:
            status = "resolved_gpu_time_reduction"
        elif low > threshold:
            status = "resolved_gpu_time_increase"
        comparisons.append({"baseline": baseline, "candidate": candidate, "status": status,
            "practical_threshold_ms": threshold, "frame": frame, "kernel_work": kernels,
            "dispatch_scopes": dispatch_scopes, "dispatch_scope_units": "milliseconds per dispatch; depth is per mip",
            "controls": controls})
    return comparisons


def timed_environment(base, args, variant, timing_file):
    env = dict(base)
    for name in tuple(env):
        if name.startswith(("NWB_REFLECTION_SMOKE_", "NWB_REFRACTION_SMOKE_", "NWB_SMOKE_FRAMEBUFFER_CAPTURE_")):
            del env[name]
    for name in ("NWB_RENDERER_BASELINE_CAPTURE_FREEZE_FRAME", "NWB_GPU_TIMING_FILE"):
        env.pop(name, None)
    if env.get("VK_INSTANCE_LAYERS"):
        raise SmokeFailure("explicit Vulkan layers must be disabled for the timing benchmark")
    controls = {"NWB_REFLECTION_SMOKE_CASE": args.family, "NWB_REFLECTION_SMOKE_MODE": variant.mode,
        "NWB_REFLECTION_SMOKE_TIMING": "1", "NWB_REFLECTION_SMOKE_DIAGNOSTICS": "0",
        "NWB_REFLECTION_SMOKE_RAY_BUDGET": str(args.ray_budget),
        "NWB_REFLECTION_SMOKE_TEMPORAL": str(int(variant.temporal)),
        "NWB_REFLECTION_SMOKE_SPATIAL": str(int(variant.spatial)),
        "NWB_REFLECTION_SMOKE_FEEDBACK": str(int(variant.feedback)),
        "NWB_REFLECTION_SMOKE_SCREEN_STEPS": str(args.screen_steps),
        "NWB_REFLECTION_SMOKE_HISTORY_SAMPLES": str(args.history_samples),
        "NWB_REFLECTION_SMOKE_ROUGHNESS": str(args.roughness if args.family == "rough" else 0.0),
        "NWB_REFLECTION_SMOKE_SEED": str(args.sampling_seed), "NWB_REFLECTION_SMOKE_DEBUG": "none",
        "NWB_REFLECTION_SMOKE_FINAL_STATE": "0", "NWB_REFLECTION_SMOKE_OPTICAL_QUERIES": str(args.optical_queries),
        "NWB_RENDERER_BASELINE_FIXED_DELTA_SECONDS": "0.016666667", "NWB_GPU_TIMING_FILE": str(timing_file)}
    env.update(controls)
    return env, controls


def validate_trial_log(text, args, variant):
    # Startup values occupy their own logger message lines. Match their complete
    # values, including any contradictory later message, rather than substrings.
    lines = text.replace("\r\n", "\n").splitlines()
    expected_fields = {"case": f"{args.family} created", "reflection mode": variant.mode,
        "hardware ray budget": str(args.ray_budget), "screen feedback": str(int(variant.feedback)),
        "screen steps": str(args.screen_steps), "timing render unfocused": "1",
        "timing in-flight ranges": str(TIMING_IN_FLIGHT_RANGES), "timing depth mip count": str(args.mip_count)}
    if args.family == "optical_clear":
        expected_fields["optical query limit"] = str(args.optical_queries)
    for field, expected in expected_fields.items():
        prefix = "ReflectionSmokeProject: " + field + " "
        values = [line[len(prefix):] for line in lines if line.startswith(prefix)]
        if values != [expected]:
            raise SmokeFailure(f"runtime {field} differs from the single frozen benchmark setting: {values!r}")
    if "ReflectionSmokeProject: shutdown" not in lines:
        raise SmokeFailure("benchmark did not report normal project shutdown")
    route = {"disabled": "disabled", "screen": "screen-space", "hardware": "hardware", "hybrid": "hardware"}[variant.mode]
    routes = {line.removeprefix("Reflection resolve: ") for line in lines if line.startswith("Reflection resolve: ")}
    if routes != {route}:
        raise SmokeFailure(f"reflection execution route changed or differs from {route}: {sorted(routes)}")
    rejected = list(STRICT_LOG_FAILURE_MESSAGES) + ["ReflectionSmokeStatistics:", "ReflectionSmokeHistory:",
        "ReflectionSmokeMutation:", "ReflectionSmokeOptics:", "ReflectionSmokeFeedback:"]
    validate_expected_log_text(text, [], rejected)
    if "ReflectionSmokeProject: hardware unavailable" in text and variant.mode in ("hardware", "hybrid"):
        raise SmokeFailure("hardware was unavailable for a hardware benchmark route")
    if args.require_hardware:
        validate_expected_log_text(text, ["ReflectionSmokeProject: hardware available"], [])
    dimensions = {(int(width), int(height)) for width, height in DIMENSIONS.findall(text)}
    if dimensions != {(args.width, args.height)}:
        raise SmokeFailure(f"render-target dimensions changed or differ from the benchmark contract: {sorted(dimensions)}")


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2, sort_keys=True, allow_nan=False) + "\n", encoding="utf-8")


def run_trial(args, variant, block, position, symbols, executable_identity):
    directory = args.output_directory / f"block_{block:02d}_{position:02d}_{variant.name}"
    directory.mkdir(parents=True, exist_ok=False)
    timing_file = directory / "gpu_timing.txt"
    env, controls = timed_environment(build_launch_environment(args), args, variant, timing_file)
    cache_before = runtime_pipeline_cache_identity(args.working_directory)
    write_json(directory / "launch.json", {"executable": str(args.executable), "working_directory": str(args.working_directory),
        "application_args": args.application_arg, "environment_overrides": controls,
        "executable_identity": executable_identity,
        "runtime_pipeline_cache_before": cache_before,
        "gpu_environment": {key: value for key, value in env.items() if key.startswith("VK_") or key == "NWB_LINUX_BACKEND"},
        "block": block, "position": position, "variant": asdict(variant)})
    runtime_identity = runtime_asset_identity(args.working_directory)
    backend = None
    logserver = None
    runtime = None
    handle = None
    selected = None
    log_directory = None
    baseline = {}
    pattern = ""
    collected_logs = False
    try:
        backend = create_capture_backend()
        logserver, port, log_directory, baseline, pattern = launch_logserver(args, args.executable, env)
        runtime = launch_testbed(args, args.executable, env, port)
        handle = backend.wait_for_window(runtime.pid, min(args.timeout, 30.0))
        if not handle:
            raise SmokeFailure("benchmark render window did not appear")
        backend.focus_window(handle)
        deadline = time.monotonic() + args.timeout
        last_problem = "timing reports have not arrived"
        while time.monotonic() < deadline:
            ensure_process_running(runtime, "during GPU benchmark")
            if timing_file.is_file():
                reports = parse_intervals(timing_file.read_text(encoding="utf-8"), symbols)
                if reports:
                    last_problem = "waiting for required warm-up and measured timing reports" if any(FRAME in report for report in reports) \
                        else "timing reports contain no completed GPU frame samples"
                retained_start, retained = retain_after_warmup(reports, args.warmup_intervals,
                    2 * FEEDBACK_PROBE_PERIOD if args.include_feedback else 0)
                if len(retained) >= args.sample_intervals:
                    summaries = summarize_intervals(retained)
                    try:
                        validate_coverage(summaries, variant, len(retained), args.minimum_frame_samples, args.mip_count, args.history_samples)
                        selected = retained
                        break
                    except SmokeFailure as error:
                        last_problem = str(error)
            time.sleep(.1)
        if selected is None:
            raise SmokeFailure(f"GPU benchmark timed out: {last_problem}")
        exit_code, tail = terminate_process(runtime, "reflection benchmark", handle)
        runtime = None
        (directory / "process_tail.txt").write_text(tail, encoding="utf-8")
        require_normal_process_exit(exit_code, tail, "reflection benchmark")
        log_text = shutdown_logserver_and_collect(logserver, log_directory, baseline, pattern)
        logserver = None
        collected_logs = True
        (directory / "runtime.log").write_text(log_text, encoding="utf-8")
        validate_trial_log(log_text, args, variant)
        if runtime_asset_identity(args.working_directory) != runtime_identity:
            raise SmokeFailure("packed runtime resources changed during benchmark")
        if file_identity(args.executable) != executable_identity:
            raise SmokeFailure("executable changed during benchmark")
        scopes = summarize_intervals(selected)
        result = {"block": block, "position": position, "variant": asdict(variant), "reports": len(selected),
            "retained_report_range": [retained_start, retained_start + len(selected)],
            "scopes": scopes, "kernel_work_ms": kernel_work_ms(scopes, variant, args.mip_count, args.history_samples),
            "mip_count": args.mip_count, "runtime_asset_identity": runtime_identity, "artifacts": str(directory)}
        result["runtime_pipeline_cache_after"] = runtime_pipeline_cache_identity(args.working_directory)
        write_json(directory / "summary.json", result)
        return result
    finally:
        primary_failure = sys.exc_info()[0] is not None
        cleanup_errors = []
        try:
            terminate_process(runtime, "reflection benchmark", handle)
        except (SmokeFailure, OSError, subprocess.SubprocessError) as error:
            cleanup_errors.append(f"application cleanup: {error}")
        if log_directory is not None and not collected_logs:
            try:
                log_text = shutdown_logserver_and_collect(logserver, log_directory, baseline, pattern)
                (directory / "runtime.log").write_text(log_text, encoding="utf-8")
            except (SmokeFailure, OSError, subprocess.SubprocessError) as error:
                cleanup_errors.append(f"log collection: {error}")
            finally:
                try:
                    terminate_process(logserver, "logserver")
                except (SmokeFailure, OSError, subprocess.SubprocessError) as error:
                    cleanup_errors.append(f"logserver cleanup: {error}")
        if backend is not None:
            try:
                backend.close()
            except (SmokeFailure, OSError) as error:
                cleanup_errors.append(f"window backend cleanup: {error}")
        if cleanup_errors:
            detail = "\n".join(cleanup_errors)
            write_status(detail)
            try:
                (directory / "cleanup_error.txt").write_text(detail, encoding="utf-8")
            except OSError as error:
                write_status(f"could not write cleanup error artifact: {error}")
            if not primary_failure:
                raise SmokeFailure(detail)


def resource_file_metadata(paths, directory):
    result = []
    for path in paths:
        identity = path.stat()
        result.append({"path": str(path.relative_to(directory)), "bytes": identity.st_size, "modified_ns": identity.st_mtime_ns})
    return result


def runtime_pipeline_cache_identity(directory):
    return resource_file_metadata(runtime_pipeline_cache_paths(directory), directory)


def runtime_asset_identity(directory):
    resources = directory / "res"
    if not resources.exists():
        raise SmokeFailure("working directory has no packed res resources")
    files = [resources] if resources.is_file() else sorted(path for path in resources.rglob("*") if path.is_file())
    mutable_cache = set(runtime_pipeline_cache_paths(directory))
    files = [path for path in files if path not in mutable_cache]
    if not files:
        raise SmokeFailure("packed runtime resource directory has no authored resources")
    return resource_file_metadata(files, directory)


def validate_long_miss_qualification(args, executable_identity):
    """Accept the semantic runner's evidence only for this exact build and stationary workload."""
    if args.family != "feedback_long_miss":
        return None
    if not args.qualification:
        raise SmokeFailure("feedback_long_miss requires a matching diagnostics-on --qualification manifest")
    try:
        evidence = json.loads(args.qualification.read_text(encoding="utf-8"))
    except (ValueError, OSError) as error:
        raise SmokeFailure(f"cannot read costly-miss qualification: {error}") from error
    if not isinstance(evidence, dict):
        raise SmokeFailure("costly-miss qualification must be a report object")
    build = evidence.get("build_identity", {})
    volumes = authored_volume_hashes(args.working_directory)
    if not isinstance(build, dict) or build.get("executable_sha256") != executable_identity["sha256"] \
            or not volumes or build.get("asset_volumes") != volumes:
        raise SmokeFailure("costly-miss qualification does not match the executable and authored packed volumes")
    if evidence.get("temporal") is not False or evidence.get("spatial_filter") is not False \
            or evidence.get("optical_queries") != args.optical_queries \
            or not isinstance(evidence.get("fixed_delta_seconds"), (int, float)) \
            or not math.isclose(evidence["fixed_delta_seconds"], 1 / 60, abs_tol=1e-9):
        raise SmokeFailure("costly-miss qualification has incompatible stationary estimator settings")
    captures = evidence.get("captures", [])
    if not isinstance(captures, list) or any(not isinstance(capture, dict) for capture in captures):
        raise SmokeFailure("costly-miss qualification captures must be explicit report objects")
    for name, enabled in (("long_miss_baseline", False), ("long_miss_feedback", True)):
        matched = [capture for capture in captures if capture.get("name") == name]
        if len(matched) != 1:
            raise SmokeFailure("costly-miss qualification needs exactly one matching off/on capture pair")
        capture = matched[0]
        expected = {"case": "feedback_long_miss", "mode": "hybrid", "enabled": enabled,
            "hardware_budget": args.ray_budget, "screen_steps": args.screen_steps, "seed": args.sampling_seed,
            "roughness": 0.0, "final_state": False, "mutation": False, "extent": "native"}
        if any(capture.get(key) != value for key, value in expected.items()) or (args.width, args.height) != (960, 720):
            raise SmokeFailure("costly-miss qualification scene, extent, seed or tracing settings differ from timing")
    metrics = evidence.get("metrics", {})
    if not isinstance(metrics, dict) or not metrics.get("long_miss_geometry") or not metrics.get("long_miss"):
        raise SmokeFailure("costly-miss qualification lacks independent geometry and paired image evidence")
    qualified = metrics.get("costly_miss_qualification", {})
    integer_fields = ("completed_observations", "actual_attempts", "actual_iterations", "actual_step_limit_misses")
    if not isinstance(qualified, dict) or any(type(qualified.get(key)) is not int or qualified[key] < 0 for key in integer_fields):
        raise SmokeFailure("costly-miss qualification lacks exact completed work totals")
    attempts, iterations = qualified["actual_attempts"], qualified["actual_iterations"]
    limited = qualified["actual_step_limit_misses"]
    if qualified["completed_observations"] < 8 or attempts <= 0 or limited > attempts \
            or not limited * args.screen_steps <= iterations <= attempts * args.screen_steps \
            or iterations / attempts < 12.0 or limited / attempts < .2:
        raise SmokeFailure("fixture has not established costly bounded SSR misses")
    for key, actual in (("mean_iterations_per_attempt", iterations / attempts), ("step_limit_miss_fraction", limited / attempts)):
        value = qualified.get(key)
        if not isinstance(value, (float, int)) or not math.isfinite(value) or not math.isclose(value, actual, rel_tol=1e-9):
            raise SmokeFailure("costly-miss qualification aggregate ratios contradict its work totals")
    return {"path": str(args.qualification), **file_identity(args.qualification), "metrics": qualified,
        "build_identity": build, "settings_match": True,
        "scope": "separate diagnostics-on workload qualification; no GPU timing or speed claim"}


def parse_args(argv):
    parser = argparse.ArgumentParser(description=__doc__, allow_abbrev=False)
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--working-directory", type=Path, required=True)
    parser.add_argument("--output-directory", type=Path, required=True)
    parser.add_argument("--family", choices=("floor", "offscreen", "rough", "feedback_long_miss", "optical_clear"), default="floor")
    parser.add_argument("--include-screen", action="store_true")
    parser.add_argument("--include-feedback", action="store_true",
        help="Add a matched Hybrid feedback-on trial; every ordinary route explicitly keeps feedback off.")
    parser.add_argument("--screen-steps", type=int,
        help="Exact matched SSR limit; default 16 for the qualified long-miss fixture, 96 otherwise.")
    parser.add_argument("--qualification", type=Path,
        help="Matching reflection_feedback_manifest.json; required for feedback_long_miss.")
    parser.add_argument("--blocks", type=int)
    parser.add_argument("--warmup-intervals", type=int, default=6)
    parser.add_argument("--sample-intervals", type=int, default=16)
    parser.add_argument("--minimum-frame-samples", type=int, default=100)
    parser.add_argument("--timeout", type=float, default=90.0)
    parser.add_argument("--width", type=int, default=960, help="Expected render width, verified from logs; does not resize the window.")
    parser.add_argument("--height", type=int, default=720, help="Expected render height, verified from logs; does not resize the window.")
    parser.add_argument("--ray-budget", type=int, default=2 * 960 * 720)
    parser.add_argument("--history-samples", type=int, default=16)
    parser.add_argument("--roughness", type=float, default=.4)
    parser.add_argument("--sampling-seed", type=int, default=0)
    parser.add_argument("--optical-queries", type=int, default=16)
    parser.add_argument("--order-seed", type=int, default=0)
    parser.add_argument("--analysis-seed", type=int, default=0)
    parser.add_argument("--practical-ms", type=float, default=.02)
    parser.add_argument("--practical-fraction", type=float, default=.03)
    parser.add_argument("--control-floor-ms", type=float, default=.015)
    parser.add_argument("--namesym", type=Path)
    parser.add_argument("--require-hardware", action="store_true")
    parser.add_argument("--logserver-executable")
    parser.add_argument("--no-logserver", action="store_true")
    parser.add_argument("--log-port", type=int, default=0)
    parser.add_argument("--application-arg", action="append", default=[])
    parser.add_argument("--software-vulkan", choices=("off",), default="off")
    args = parser.parse_args(argv)
    if args.screen_steps is None:
        args.screen_steps = 16 if args.family == "feedback_long_miss" else 96
    for name in ("timeout", "practical_ms", "practical_fraction", "control_floor_ms"):
        value = getattr(args, name)
        if not math.isfinite(value) or value <= 0:
            parser.error(f"--{name.replace('_', '-')} must be finite and positive")
    for name in ("width", "height", "sample_intervals", "minimum_frame_samples"):
        if getattr(args, name) <= 0:
            parser.error(f"--{name.replace('_', '-')} must be positive")
    if args.warmup_intervals < 1 or not 1 <= args.ray_budget <= 0xffffffff:
        parser.error("warmup intervals and nonzero u32 ray budget are required")
    if not 1 <= args.history_samples <= 256 or not 1 <= args.optical_queries <= 16:
        parser.error("history samples must be 1..256 and optical queries 1..16")
    if not 8 <= args.screen_steps <= 256:
        parser.error("screen steps must be 8..256")
    if args.include_feedback and args.minimum_frame_samples < 4 * FEEDBACK_PROBE_PERIOD:
        parser.error("feedback comparisons need at least four complete probe periods of GPU frame samples")
    if not math.isfinite(args.roughness) or not 0 < args.roughness <= 1 or not 0 <= args.sampling_seed <= 0xffffffff:
        parser.error("roughness must be finite in (0,1] and seed must be a u32")
    if any("gpudbg" in value.lower() or "capture" in value.lower() for value in args.application_arg):
        parser.error("validation/capture arguments are incompatible with timing")
    if args.family == "rough" and args.include_screen:
        parser.error("--include-screen belongs to smooth route comparisons")
    for name in ("executable", "working_directory", "output_directory"):
        setattr(args, name, getattr(args, name).resolve())
    if args.namesym:
        args.namesym = args.namesym.resolve()
    if args.qualification:
        args.qualification = args.qualification.resolve()
    args.mip_count = max(args.width, args.height).bit_length()
    variants = variants_for(args.family, args.include_screen, args.include_feedback)
    cycle = len(variants) if len(variants) % 2 == 0 else 2 * len(variants)
    args.blocks = args.blocks if args.blocks is not None else max(1, math.ceil(5 / cycle)) * cycle
    balanced_orders(variants, args.blocks, args.order_seed)
    return args


def run(args):
    if not args.executable.is_file() or not args.working_directory.is_dir():
        raise SmokeFailure("executable or packed runtime working directory does not exist")
    if args.output_directory.exists() and any(args.output_directory.iterdir()):
        raise SmokeFailure("output directory must be empty; prior benchmark artifacts are never overwritten")
    args.output_directory.mkdir(parents=True, exist_ok=True)
    variants = variants_for(args.family, args.include_screen, args.include_feedback)
    orders = balanced_orders(variants, args.blocks, args.order_seed)
    identity = file_identity(args.executable)
    qualification = validate_long_miss_qualification(args, identity)
    runtime_identity = runtime_asset_identity(args.working_directory)
    symbols = load_name_symbols(args.namesym, KNOWN_SCOPES)
    namesym_identity = {"path": str(args.namesym), **file_identity(args.namesym)} if args.namesym else None
    plan = {"family": args.family, "dimensions": [args.width, args.height], "blocks": args.blocks,
        "workload_role": {"floor": "productive screen reflection", "offscreen": "offscreen screen-miss control",
            "feedback_long_miss": "separately qualified costly screen misses", "rough": "rough estimator and filter route costs",
            "optical_clear": "bounded secondary transmission through an authored closed clear volume"}[args.family],
        "qualification": qualification,
        "orders": [[variant.name for variant in row] for row in orders], "executable_identity": identity,
        "runtime_asset_identity": runtime_identity, "namesym_identity": namesym_identity,
        "authored_volume_hashes": authored_volume_hashes(args.working_directory),
        "runtime_pipeline_cache_initial": runtime_pipeline_cache_identity(args.working_directory),
        "runtime_resource_policy": "authored files immutable; only canonical runtime_pipeline_cache volume segments may change",
        "scope_decoding": "supplied namesym plus known scope hashes" if args.namesym else "known scope hashes only",
        "settings": {"ray_budget": args.ray_budget, "optical_queries": args.optical_queries,
            "history_samples": args.history_samples, "roughness": args.roughness if args.family == "rough" else 0.0,
            "sampling_seed": args.sampling_seed, "screen_steps": args.screen_steps,
            "feedback_default": False, "feedback_pairs": args.include_feedback, "diagnostics": False, "capture": False},
        "variants": [asdict(variant) for variant in variants],
        "order_seed": args.order_seed, "analysis_seed": args.analysis_seed,
        "warmup_intervals": args.warmup_intervals, "sample_intervals": args.sample_intervals,
        "minimum_warmup_gpu_frames": 2 * FEEDBACK_PROBE_PERIOD if args.include_feedback else 0,
        "minimum_frame_samples": args.minimum_frame_samples, "mip_count": args.mip_count,
        "timing_in_flight_ranges": TIMING_IN_FLIGHT_RANGES,
        "inference": "paired independent blocks; percentile bootstrap 95% CI of mean; no FPS inference",
        "multiplicity": "per-comparison intervals; no family-wide confidence claim",
        "correctness": "separate diagnostics-on reflection suites must pass for this build",
        "external_controls": "same physical GPU, power state and mesh route; no concurrent GPU jobs or builds",
        "feature_cost": "increment above current Disabled route, which retains shared infrastructure/classification",
        "primary_scope": FRAME, "control_scopes": CONTROLS, "observed_control_scopes": OBSERVED_CONTROLS,
        "depth_scope": "per-mip dispatch; kernel estimate multiplies by native mip count",
        "temporal_scope": "one dispatch per stable reused frame when history_samples > 1; zero at cap 1",
        "practical_ms": args.practical_ms, "practical_fraction": args.practical_fraction,
        "control_floor_ms": args.control_floor_ms}
    write_json(args.output_directory / "plan.json", plan)
    trials = []
    try:
        for block, order in enumerate(orders):
            for position, variant in enumerate(order):
                if runtime_asset_identity(args.working_directory) != runtime_identity:
                    raise SmokeFailure("packed runtime resources changed between trials")
                write_status(f"benchmark block {block + 1}/{args.blocks}: {variant.name}")
                trials.append(run_trial(args, variant, block, position, symbols, identity))
                write_json(args.output_directory / "trials.json", trials)
        comparisons = compare_trials(trials, variants, args.practical_ms, args.practical_fraction,
            args.control_floor_ms, args.analysis_seed)
        write_json(args.output_directory / "report.json", {"plan": plan, "trials": trials, "comparisons": comparisons})
        write_status(f"GPU benchmark complete: {args.output_directory / 'report.json'}")
        return 0
    except (SmokeFailure, SmokeSkip, OSError, subprocess.SubprocessError) as error:
        write_json(args.output_directory / "failure.json", {"error": str(error), "completed_trials": len(trials)})
        raise


def main(argv=None):
    try:
        return run(parse_args(sys.argv[1:] if argv is None else argv))
    except SmokeSkip as error:
        write_status(f"SKIP: {error}")
        return SKIP_EXIT_CODE
    except (SmokeFailure, OSError, subprocess.SubprocessError) as error:
        write_status(f"FAIL: {error}")
        return 1


if __name__ == "__main__":
    sys.exit(main())
