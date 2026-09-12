#!/usr/bin/env python3
"""Balanced, source-frozen CPU gathering A/B; memory acquisition is a separate mode."""

import argparse
from dataclasses import asdict
import json
import math
from pathlib import Path
import re
import statistics
import subprocess
import sys
import time
from types import SimpleNamespace

import renderer_ab_benchmark as ab
from reflection_benchmark import balanced_orders, paired_statistics
from smoke_volume_identity import file_identity
from window_capture_smoke import (
    STRICT_LOG_FAILURE_MESSAGES, SmokeFailure, SmokeSkip, build_launch_environment,
    create_capture_backend, launch_logserver, launch_testbed, require_normal_process_exit,
    shutdown_logserver_and_collect, terminate_process, validate_expected_log_text, write_status,
)

WORKLOADS = ("opaque", "hybrid", "shared", "unique", "overrides", "runtime")
CPU = ("graphics.frame", "graphics.frame_preamble", "graphics.prepare_resources", "graphics.render_passes",
    "graphics.render", "frame.project_update", "graphics.present", "graphics.begin_frame")
GPU_BASE = ("render.frame", "render.opaque_regular", "render.shadow_visibility", "render.deferred_lighting",
    "render.deferred_composite", "render.deferred_present", "render.reflection_classify",
    "render.reflection_build_args", "render.reflection_hardware")
GPU_TRANSPARENT = ("render.avboit_clear", "render.avboit_occupancy", "render.avboit_depth_warp",
    "render.avboit_extinction", "render.avboit_integration", "render.avboit_accumulate")
GPU_INACTIVE = ("render.reflection_depth_pyramid", "render.reflection_temporal", "render.reflection_spatial")
GPU_CONTROLS = ("render.frame", "render.opaque_regular", "render.shadow_visibility", "render.deferred_lighting",
    "render.deferred_composite", "render.deferred_present")
ARENAS = tuple("impl/ecs_render/" + name for name in ("prepare", "render", "task_graph", "avboit_transparent_csg",
    "material_pass_prepare", "material_pass_render", "material_instance_mutable", "ray_tracing_build", "ray_tracing_attribute"))
REQUIRED_ARENAS = tuple("impl/ecs_render/" + name for name in ("prepare", "task_graph", "material_pass_prepare", "ray_tracing_build"))
FROZEN_SETTINGS = {"width": 960, "height": 720, "warmup": 96, "samples": 256, "drain": 32,
    "reflection_mode": "hardware", "hardware_budget": 4096, "optical_queries": 16,
    "temporal": False, "spatial": False, "feedback": False, "diagnostics": False,
    "fixed_delta_seconds": .016666667, "sampling_seed": 0, "refraction": True}
COUNTERS = ("allocations", "reallocations", "deallocations")
GAUGES = ("used", "reserved", "historical_arena_peak")


def finite(value, label, nonnegative=True):
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value) \
        or (nonnegative and value < 0):
        raise SmokeFailure(f"invalid finite measurement: {label}")
    return value


def integer(value, label, minimum=0):
    if isinstance(value, bool) or not isinstance(value, int) or value < minimum:
        raise SmokeFailure(f"invalid integer measurement: {label}")
    return value


def summarize_rows(rows, workload, mode):
    if workload not in WORKLOADS or mode not in ("timing", "memory"):
        raise SmokeFailure("unknown workload/capture mode")
    if not rows or rows[-1] != {"type": "complete"}:
        raise SmokeFailure("normal complete result footer is required")
    if sum(row.get("type") == "configuration" for row in rows) != 1 or rows[0].get("type") != "configuration":
        raise SmokeFailure("one leading configuration is required")
    if any(row.get("type") not in ("configuration", "cpu", "gpu", "memory_baseline", "complete") for row in rows):
        raise SmokeFailure("unknown result record")
    if sum(row.get("type") == "complete" for row in rows) != 1:
        raise SmokeFailure("duplicate completion")
    config = rows[0]
    if config.get("schema") != 1 or config.get("workload") != workload or config.get("mode") != mode:
        raise SmokeFailure("actual fixture identity differs from requested trial")
    if any(config.get(key) != value or type(config.get(key)) is not type(value) for key, value in FROZEN_SETTINGS.items()):
        raise SmokeFailure("actual fixed fixture controls changed")
    if config.get("successful_frames") != 384:
        raise SmokeFailure("warm-up, measurement and drain must count successful frames")
    runtime = integer(config.get("runtime_renderers"), "runtime renderer count")
    owners = integer(config.get("runtime_owners"), "runtime owner count")
    renderers = integer(config.get("renderers"), "resolved renderer count", 1)
    transparent = integer(config.get("transparent_renderers"), "transparent renderer count")
    if workload == "runtime":
        if runtime != 8 or owners != 8 or renderers != 65 or transparent != 56:
            raise SmokeFailure("real runtime workload was not resolved")
    elif runtime != 0 or owners != 0 or renderers != 65 or transparent != (0 if workload == "opaque" else 32 if workload == "hybrid" else 64):
        raise SmokeFailure("static fixture count changed")
    samples = [row for row in rows if row.get("type") == "cpu"]
    if len(samples) != 256:
        raise SmokeFailure("exactly 256 complete CPU frames are required")
    for index, row in enumerate(samples):
        source = integer(row.get("source_frame"), "CPU source frame")
        publish = integer(row.get("publish_frame"), "CPU publication frame")
        if index and (source != samples[index - 1]["source_frame"] + 1 or publish != samples[index - 1]["publish_frame"] + 1):
            raise SmokeFailure("CPU samples contain duplicate, skipped, or reordered successful frames")
        if set(row.get("scopes_ms", {})) != set(CPU):
            raise SmokeFailure("CPU callback totals or parent controls are missing")
        for scope, value in row["scopes_ms"].items():
            finite(value, scope)
        measured = row["scopes_ms"]
        if measured["graphics.frame"] <= 0 or measured["graphics.render"] <= 0:
            raise SmokeFailure("timed graphics work is empty")
        # Siblings are chargeable subtotals. The enclosing timer includes scheduling and their instrumentation.
        if measured["graphics.prepare_resources"] + measured["graphics.render_passes"] > measured["graphics.render"] + .005:
            raise SmokeFailure("callback subtotals exceed their measured parent")
        if measured["graphics.render"] > measured["graphics.frame"] + .005:
            raise SmokeFailure("render stage exceeds complete graphics frame")
    cpu = {scope: {"samples": len(samples), "total_ms": sum(row["scopes_ms"][scope] for row in samples),
        "mean_ms": statistics.fmean(row["scopes_ms"][scope] for row in samples),
        "median_ms": statistics.median(row["scopes_ms"][scope] for row in samples)} for scope in CPU}
    start, end = samples[0]["source_frame"], samples[-1]["source_frame"]
    gpu = {}
    seen = set()
    frontier = {}
    for row in (row for row in rows if row.get("type") == "gpu"):
        scope = row.get("scope")
        if scope not in GPU_BASE + GPU_TRANSPARENT + GPU_INACTIVE:
            raise SmokeFailure("unrecognized GPU control scope")
        publish = integer(row.get("publish_frame"), "GPU publication")
        count = integer(row.get("samples"), "completed GPU range count", 1)
        first = integer(row.get("first_source_frame"), "GPU first source")
        last = integer(row.get("last_source_frame"), "GPU last source")
        if first > last or count > last - first + 1:
            raise SmokeFailure("invalid one-range-per-frame GPU source span")
        if scope in GPU_INACTIVE:
            raise SmokeFailure(f"inactive GPU pass ran: {scope}")
        if workload == "opaque" and scope in GPU_TRANSPARENT:
            # The actual AVBOIT owner clears newly created targets once, including an opaque-only warm-up.
            if scope != "render.avboit_clear" or last >= start:
                raise SmokeFailure(f"inactive measured GPU pass ran: {scope}")
        key = (scope, publish)
        if key in seen or (scope in frontier and (publish <= frontier[scope][0] or first <= frontier[scope][1])):
            raise SmokeFailure("duplicate/reordered/overlapping GPU publication")
        seen.add(key)
        frontier[scope] = (publish, last)
        total = finite(row.get("total_ms"), "GPU total milliseconds")
        if first < start or last > end:
            continue  # Never attribute a mixed warm-up/drain publication to measured CPU frames.
        destination = gpu.setdefault(scope, {"total_ms": 0., "gpu_samples": 0, "reports": 0})
        destination["total_ms"] += total
        destination["gpu_samples"] += count
        destination["reports"] += 1
    required = GPU_BASE + (() if workload == "opaque" else GPU_TRANSPARENT)
    if set(gpu) != set(required):
        raise SmokeFailure("completed GPU frame/control/pass coverage is missing")
    frames = gpu["render.frame"]["gpu_samples"]
    if frames < math.ceil(len(samples) * .90):
        raise SmokeFailure("completed GPU frame coverage is below 90% of the exact CPU source window")
    for scope, value in gpu.items():
        if abs(value["gpu_samples"] - frames) > max(2, frames * .02):
            raise SmokeFailure(f"one complete GPU range per frame is not covered for {scope}")
        value["mean_ms"] = value["total_ms"] / value["gpu_samples"]
    memory_baselines = [row for row in rows if row.get("type") == "memory_baseline"]
    memory = {}
    if mode == "timing":
        if memory_baselines or any("arenas" in row for row in samples):
            raise SmokeFailure("arena statistics contaminated the timing mode")
    else:
        if len(memory_baselines) != 1:
            raise SmokeFailure("one warm-up memory baseline is required")
        previous = memory_baselines[0]["arenas"]
        for index, row in enumerate(samples):
            current = row.get("arenas", {})
            if set(previous) != set(ARENAS) or set(current) != set(ARENAS):
                raise SmokeFailure("arena owner coverage is incomplete")
            for scope in ARENAS:
                value, prior = current[scope], previous[scope]
                if not isinstance(value.get("present"), bool) or not isinstance(prior.get("present"), bool):
                    raise SmokeFailure("arena availability must be explicit")
                if scope in REQUIRED_ARENAS and (not value["present"] or not prior["present"]):
                    raise SmokeFailure(f"required actual scratch owner is unavailable: {scope}")
                for field in COUNTERS + GAUGES:
                    integer(value.get(field), f"{scope}/{field}")
                    integer(prior.get(field), f"baseline {scope}/{field}")
                if value["present"] and value.get("frame") != row["publish_frame"]:
                    raise SmokeFailure("arena snapshot comes from another publication")
                result = memory.setdefault(scope, {"allocation_deltas": [], "reallocation_deltas": [],
                    "deallocation_deltas": [], "retained_used_bytes": [], "retained_reserved_bytes": [], "historical_arena_peak": 0,
                    "available_frames": 0, "baseline_available": previous[scope]["present"]})
                result["available_frames"] += int(value["present"])
                for field, key in zip(COUNTERS, ("allocation_deltas", "reallocation_deltas", "deallocation_deltas")):
                    delta = value[field] - prior[field]
                    if delta < 0:
                        raise SmokeFailure("arena cumulative counters reset during measurement")
                    result[key].append(delta)
                result["retained_used_bytes"].append(value["used"])
                result["retained_reserved_bytes"].append(value["reserved"])
                result["historical_arena_peak"] = max(result["historical_arena_peak"], value["historical_arena_peak"])
            previous = current
        for scope, value in memory.items():
            complete = value["baseline_available"] and value["available_frames"] == len(samples)
            memory[scope] = {key: (statistics.fmean(item) if isinstance(item, list) else item) if complete else None
                for key, item in value.items() if key not in ("available_frames", "baseline_available")}
            memory[scope].update(available_frames=value["available_frames"], measured_frames=len(samples), complete=complete)
    return {"configuration": config, "source_window": [start, end], "cpu": cpu, "gpu": gpu, "memory": memory}


def read_result(path, workload, mode):
    return summarize_rows([json.loads(line) for line in path.read_text(encoding="utf-8").splitlines() if line], workload, mode)


def compare(trials, orders, mode, seed=0, practical_ms=.02, practical_fraction=.03, control_floor_ms=.015):
    expected = {(block, position, arm) for block, order in enumerate(orders) for position, arm in enumerate(order)}
    actual = [(row["block"], row["position"], row["arm"]) for row in trials]
    if len(actual) != len(set(actual)) or set(actual) != expected:
        raise SmokeFailure("inference requires all balanced independent blocks")
    if any(row["result"]["configuration"]["mode"] != mode for row in trials):
        raise SmokeFailure("timing and memory trials cannot be pooled")
    blocks = [{row["arm"]: row["result"] for row in trials if row["block"] == index} for index in range(len(orders))]
    if mode == "memory":
        return {"status": "memory_observation_only", "arenas": {scope: {
            key: {arm: (statistics.fmean(block[arm]["memory"][scope][key] for block in blocks)
                if all(block[arm]["memory"][scope][key] is not None for block in blocks) else None) for arm in ("baseline", "candidate")}
            for key in blocks[0]["baseline"]["memory"][scope]} for scope in ARENAS},
            "peak_semantics": "historical maximum of individual arena lifetime peaks, not per-frame or concurrent process peak"}
    cpu = {scope: paired_statistics([block["candidate"]["cpu"][scope]["mean_ms"] - block["baseline"]["cpu"][scope]["mean_ms"]
        for block in blocks], seed) for scope in CPU}
    controls = {}
    for scope in GPU_CONTROLS:
        metric = paired_statistics([block["candidate"]["gpu"][scope]["mean_ms"] - block["baseline"]["gpu"][scope]["mean_ms"]
            for block in blocks], seed)
        baseline = statistics.fmean(block["baseline"]["gpu"][scope]["mean_ms"] for block in blocks)
        tolerance = max(control_floor_ms, practical_fraction * baseline)
        low, high = metric["ci95_mean_ms"]
        metric.update(tolerance_ms=tolerance, equivalent=low >= -tolerance and high <= tolerance,
            material_drift=low > tolerance or high < -tolerance)
        controls[scope] = metric
    baseline = statistics.fmean(block["baseline"]["cpu"]["graphics.render"]["mean_ms"] for block in blocks)
    threshold = max(practical_ms, practical_fraction * baseline)
    low, high = cpu["graphics.render"]["ci95_mean_ms"]
    frame_base = statistics.fmean(block["baseline"]["cpu"]["graphics.frame"]["mean_ms"] for block in blocks)
    frame_limit = max(practical_ms, practical_fraction * frame_base)
    if any(value["material_drift"] for value in controls.values()):
        status = "gpu_control_drift"
    elif not all(value["equivalent"] for value in controls.values()):
        status = "gpu_control_uncertain"
    elif cpu["graphics.frame"]["ci95_mean_ms"][1] > frame_limit:
        status = "whole_cpu_frame_regression_or_uncertain"
    elif high < -threshold:
        status = "resolved_cpu_render_reduction"
    elif low > threshold:
        status = "resolved_cpu_render_increase"
    else:
        status = "cpu_change_unresolved"
    return {"status": status, "primary": "graphics.render", "practical_threshold_ms": threshold,
        "cpu": cpu, "gpu_controls": controls, "whole_cpu_frame_limit_ms": frame_limit,
        "units": "milliseconds per successful CPU frame; complete GPU ranges normalized independently",
        "multiplicity": "per-workload 95% intervals, no simultaneous six-workload claim"}


def environment(base, workload, mode, output):
    for key in ("VK_INSTANCE_LAYERS", "VK_LOADER_LAYERS_ENABLE"):
        if base.get(key, "").strip():
            raise SmokeFailure(f"explicit Vulkan layer override invalidates acquisition: {key}")
    env = {key: value for key, value in base.items() if not key.startswith("NWB_") or key == "NWB_LINUX_BACKEND"}
    env.update(NWB_GATHER_BENCHMARK_WORKLOAD=workload, NWB_GATHER_BENCHMARK_MODE=mode,
        NWB_GATHER_BENCHMARK_OUTPUT=str(output), NWB_RENDERER_BASELINE_FIXED_DELTA_SECONDS="0.016666667",
        NWB_RENDERER_BASELINE_CAPTURE_FREEZE_FRAME="0")
    return env


def acquire(args, arm, block, position):
    directory = args.output_directory / f"block_{block:02d}_{position:02d}_{arm.name}"
    directory.mkdir(parents=True, exist_ok=False)
    result_path = directory / "samples.jsonl"
    launch = SimpleNamespace(**vars(args), working_directory=arm.runtime, executable=arm.executable)
    env = environment(args.frozen_environment, args.workload, args.mode, result_path)
    ab.write_json(directory / "launch.json", {"arm": asdict(arm) | {"executable": str(arm.executable),
        "runtime": str(arm.runtime), "source_manifest": str(arm.source_manifest)},
        "environment": {key: value for key, value in env.items() if key.startswith(("NWB_", "VK_"))},
        "cache_before": ab.cache_identity(arm.runtime)})
    backend = process = logserver = handle = log_directory = None
    baseline, pattern = {}, ""
    logs_collected = False
    try:
        backend = create_capture_backend()
        logserver, port, log_directory, baseline, pattern = launch_logserver(launch, arm.executable, env)
        process = launch_testbed(launch, arm.executable, env, port)
        handle = backend.wait_for_window(process.pid, min(30., args.timeout))
        if not handle:
            raise SmokeFailure("fixed benchmark window never appeared")
        backend.focus_window(handle)
        deadline = time.monotonic() + args.timeout
        while process.poll() is None and time.monotonic() < deadline:
            time.sleep(.1)
        if process.poll() is None:
            raise SmokeFailure("fixture did not finish its successful frame budget")
        code, tail = terminate_process(process, "renderer gather benchmark", handle)
        process = None
        (directory / "process_tail.txt").write_text(tail, encoding="utf-8")
        require_normal_process_exit(code, tail, "renderer gather benchmark")
        text = shutdown_logserver_and_collect(logserver, log_directory, baseline, pattern)
        logserver = None
        (directory / "runtime.log").write_text(text, encoding="utf-8")
        logs_collected = True
        required = [f"RendererGatherBenchmark: workload {args.workload} mode {args.mode} fixed objects 64",
            "RendererGatherBenchmark: hardware reflection 4096 queries 16 refraction 1 diagnostics 0 temporal 0 spatial 0 feedback 0",
            "RendererGatherBenchmark: fixed delta 0.016666667 extent 960x720 async compute requested 1",
            "RendererGatherBenchmark: shutdown successful frames 384", "Reflection resolve: hardware"]
        validate_expected_log_text(text, required, list(STRICT_LOG_FAILURE_MESSAGES) +
            ["FramebufferCapture:", "render submission suspended", "render pass skipped", "device recreation"])
        signature = ab.device_material_signature(text)
        dims = re.findall(r"deferred rendering targets ready \((\d+)x(\d+),", text)
        if not dims or any(pair != ("960", "720") for pair in dims):
            raise SmokeFailure("actual render extent differs")
        vsync = re.findall(r"RendererGatherBenchmark: vsync ([01])", text)
        if len(vsync) != 1:
            raise SmokeFailure("actual vsync evidence is missing")
        signature["vsync"] = vsync[0]
        result = read_result(result_path, args.workload, args.mode)
        signature["actual_counts"] = {key: result["configuration"][key] for key in
            ("renderers", "runtime_renderers", "transparent_renderers", "runtime_owners")}
        return {"arm": arm.name, "block": block, "position": position, "runtime_signature": signature,
            "result": result, "artifacts": str(directory),
            "cache_after": ab.cache_identity(arm.runtime)}
    finally:
        primary_failure = sys.exc_info()[0] is not None
        errors = []
        try:
            if process is not None:
                _, tail = terminate_process(process, "renderer gather benchmark", handle)
                (directory / "process_tail.txt").write_text(tail, encoding="utf-8")
        except Exception as error:
            errors.append(f"renderer cleanup: {error}")
        if not logs_collected and log_directory is not None:
            try:
                text = shutdown_logserver_and_collect(logserver, log_directory, baseline, pattern)
                (directory / "runtime.log").write_text(text, encoding="utf-8")
                logserver = None
            except Exception as error:
                errors.append(f"failure-path log collection: {error}")
        try:
            terminate_process(logserver, "logserver")
        except Exception as error:
            errors.append(f"logserver cleanup: {error}")
        try:
            if backend:
                backend.close()
        except Exception as error:
            errors.append(f"window backend cleanup: {error}")
        if errors:
            ab.write_json(directory / "cleanup_error.json", {"errors": errors})
            if not primary_failure:
                raise SmokeFailure("; ".join(errors))


def parse_args(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, allow_abbrev=False)
    for arm in ("baseline", "candidate"):
        for field in ("executable", "runtime", "source-manifest"):
            parser.add_argument(f"--{arm}-{field}", type=Path, required=True)
    parser.add_argument("--logserver-executable", type=Path, required=True)
    parser.add_argument("--output-directory", type=Path, required=True)
    parser.add_argument("--workload", choices=WORKLOADS, required=True)
    parser.add_argument("--mode", choices=("timing", "memory"), default="timing")
    parser.add_argument("--blocks", type=int, default=8)
    parser.add_argument("--timeout", type=float, default=180.)
    parser.add_argument("--order-seed", type=int, default=0)
    parser.add_argument("--analysis-seed", type=int, default=0)
    parser.add_argument("--plan-only", action="store_true")
    args = parser.parse_args(argv)
    if args.blocks < 8 or args.blocks % 2 or not math.isfinite(args.timeout) or args.timeout <= 0:
        parser.error("use at least eight even balanced blocks and a positive finite timeout")
    for key, value in vars(args).copy().items():
        if isinstance(value, Path):
            setattr(args, key, value.resolve())
    args.application_arg = []
    args.no_logserver = False
    args.log_port = 0
    args.software_vulkan = "off"
    return args


def run(args):
    arms = tuple(ab.Arm(name, getattr(args, name + "_executable"), getattr(args, name + "_runtime"),
        getattr(args, name + "_source_manifest")) for name in ("baseline", "candidate"))
    ab.validate_arm_separation(arms)
    identities = {arm.name: ab.freeze_arm(arm) for arm in arms}
    if identities["baseline"]["resources"]["authored_volume_hashes"] != identities["candidate"]["resources"]["authored_volume_hashes"]:
        raise SmokeFailure("CPU-only optimization requires identical authored asset volumes across arms")
    if args.output_directory.exists() and any(args.output_directory.iterdir()):
        raise SmokeFailure("output directory must be empty")
    if not args.logserver_executable.is_file():
        raise SmokeFailure("explicit shared logserver is missing")
    orders = [[arm.name for arm in row] for row in balanced_orders(arms, args.blocks, args.order_seed)]
    args.frozen_environment = environment(build_launch_environment(args), args.workload, args.mode, "<per-trial-output>")
    modules = ("renderer_ab_benchmark", "reflection_benchmark", "window_capture_smoke", "smoke_volume_identity", "gpu_timing_parse", "name_symbols")
    paths = [Path(__file__).resolve(), Path(sys.executable).resolve()]
    paths.extend(args.logserver_executable.parent / name for name in ab.binary_identity(args.logserver_executable))
    paths.extend(Path(sys.modules[name].__file__).resolve() for name in modules)
    shared = {str(path): file_identity(path) for path in paths}
    plan = {"workload": args.workload, "mode": args.mode, "orders": orders, "arms": identities,
        "shared_files": shared, "settings": FROZEN_SETTINGS, "planned_trials": args.blocks * 2,
        "gpu_environment": {key: value for key, value in args.frozen_environment.items() if key.startswith("VK_") or key == "NWB_LINUX_BACKEND"},
        "cpu_primary": "graphics.render", "cpu_subtotals": ["graphics.prepare_resources", "graphics.render_passes"],
        "gpu_controls": GPU_CONTROLS, "memory_policy": "separate acquisition; no timing inference from memory mode",
        "cpu_normalization": "sum scope milliseconds / 256 successful contiguous frames",
        "gpu_normalization": "sum milliseconds / completed range count inside CPU source window, independently per scope",
        "correctness": "qualify both frozen builds separately; benchmark does not replace visual/native tests",
        "source_policy": "instrumentation and fixture must be present in both builds before freezing them"}
    args.output_directory.mkdir(parents=True, exist_ok=True)
    ab.write_json(args.output_directory / "plan.json", plan)
    if args.plan_only:
        return 0
    by_name = {arm.name: arm for arm in arms}
    trials, signature = [], None
    try:
        for block, order in enumerate(orders):
            for position, name in enumerate(order):
                ab.verify_frozen(arms, identities, shared)
                write_status(f"CPU gather {args.workload}/{args.mode} block {block + 1}/{args.blocks}: {name}")
                trial = acquire(args, by_name[name], block, position)
                ab.verify_frozen(arms, identities, shared)
                if signature is not None and trial["runtime_signature"] != signature:
                    raise SmokeFailure("physical device, material route, or vsync changed")
                signature = trial["runtime_signature"]
                trials.append(trial)
                ab.write_json(args.output_directory / "trials.json", trials)
        comparison = compare(trials, orders, args.mode, args.analysis_seed)
        ab.write_json(args.output_directory / "report.json", {"plan": plan, "trials": trials, "comparison": comparison})
        return 0
    except Exception as error:
        ab.write_json(args.output_directory / "failure.json", {"error": str(error), "completed_trials": len(trials)})
        raise


def main(argv=None):
    try:
        return run(parse_args(argv))
    except (SmokeFailure, SmokeSkip, OSError, ValueError, subprocess.SubprocessError) as error:
        write_status(f"FAIL: {error}")
        return 1


if __name__ == "__main__":
    sys.exit(main())
