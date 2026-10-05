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

LIT_OPAQUE = "opaque"
LIT_HYBRID = "hybrid"
LIT_RUNTIME = "runtime"
LIT_GRAPHICS_FRAME = "graphics.frame"
LIT_GRAPHICS_PREPARE_RESOURCES = "graphics.prepare_resources"
LIT_GRAPHICS_RENDER_PASSES = "graphics.render_passes"
LIT_GRAPHICS_RENDER = "graphics.render"
LIT_RENDER_FRAME = "render.frame"
LIT_RENDER_OPAQUE_REGULAR = "render.opaque_regular"
LIT_RENDER_SHADOW_VISIBILITY = "render.shadow_visibility"
LIT_RENDER_DEFERRED_LIGHTING = "render.deferred_lighting"
LIT_RENDER_DEFERRED_COMPOSITE = "render.deferred_composite"
LIT_RENDER_DEFERRED_PRESENT = "render.deferred_present"
LIT_RENDER_AVBOIT_CLEAR = "render.avboit_clear"
LIT_IMPL_ECS_RENDER = "impl/ecs_render/"
LIT_PREPARE = "prepare"
LIT_TASK_GRAPH = "task_graph"
LIT_MATERIAL_PASS_PREPARE = "material_pass_prepare"
LIT_RAY_TRACING_BUILD = "ray_tracing_build"
LIT_SAMPLES = "samples"
LIT_USED = "used"
LIT_RESERVED = "reserved"
LIT_HISTORICAL_ARENA_PEAK = "historical_arena_peak"
LIT_TIMING = "timing"
LIT_MEMORY = "memory"
LIT_TYPE = "type"
LIT_COMPLETE = "complete"
LIT_CONFIGURATION = "configuration"
LIT_CPU = "cpu"
LIT_GPU = "gpu"
LIT_MEMORY_BASELINE = "memory_baseline"
LIT_WORKLOAD = "workload"
LIT_MODE = "mode"
LIT_RUNTIME_RENDERERS = "runtime_renderers"
LIT_RUNTIME_OWNERS = "runtime_owners"
LIT_RENDERERS = "renderers"
LIT_TRANSPARENT_RENDERERS = "transparent_renderers"
LIT_SOURCE_FRAME = "source_frame"
LIT_PUBLISH_FRAME = "publish_frame"
LIT_SCOPES_MS = "scopes_ms"
LIT_TOTAL_MS = "total_ms"
LIT_MEAN_MS = "mean_ms"
LIT_SCOPE = "scope"
LIT_GPU_SAMPLES = "gpu_samples"
LIT_REPORTS = "reports"
LIT_ARENAS = "arenas"
LIT_PRESENT = "present"
LIT_ALLOCATION_DELTAS = "allocation_deltas"
LIT_REALLOCATION_DELTAS = "reallocation_deltas"
LIT_DEALLOCATION_DELTAS = "deallocation_deltas"
LIT_RETAINED_USED_BYTES = "retained_used_bytes"
LIT_RETAINED_RESERVED_BYTES = "retained_reserved_bytes"
LIT_AVAILABLE_FRAMES = "available_frames"
LIT_BASELINE_AVAILABLE = "baseline_available"
LIT_UTF_8 = "utf-8"
LIT_BLOCK = "block"
LIT_POSITION = "position"
LIT_ARM = "arm"
LIT_RESULT = "result"
LIT_STATUS = "status"
LIT_BASELINE = "baseline"
LIT_CANDIDATE = "candidate"
LIT_CI95_MEAN_MS = "ci95_mean_ms"
LIT_GPU_CONTROLS = "gpu_controls"
LIT_NWB = "NWB_"
LIT_NWB_LINUX_BACKEND = "NWB_LINUX_BACKEND"
LIT_COMPILER_STATISTICS = "compiler_statistics"
LIT_EXECUTABLE = "executable"
LIT_VK = "VK_"
LIT_RENDERER_GATHER_BENCHMARK = "renderer gather benchmark"
LIT_PROCESS_TAIL_TXT = "process_tail.txt"
LIT_RUNTIME_LOG = "runtime.log"
LIT_RUNTIME_SIGNATURE = "runtime_signature"
LIT_STORE_TRUE = "store_true"
LIT_RESOURCES = "resources"
LIT_AUTHORED_VOLUME_HASHES = "authored_volume_hashes"
LIT_MAIN = "__main__"

WORKLOADS = (LIT_OPAQUE, LIT_HYBRID, "shared", "unique", "overrides", LIT_RUNTIME)
CPU = (LIT_GRAPHICS_FRAME, "graphics.frame_preamble", LIT_GRAPHICS_PREPARE_RESOURCES, LIT_GRAPHICS_RENDER_PASSES,
    LIT_GRAPHICS_RENDER, "frame.project_update", "graphics.present", "graphics.begin_frame")
GPU_BASE = (LIT_RENDER_FRAME, LIT_RENDER_OPAQUE_REGULAR, LIT_RENDER_SHADOW_VISIBILITY, LIT_RENDER_DEFERRED_LIGHTING,
    LIT_RENDER_DEFERRED_COMPOSITE, LIT_RENDER_DEFERRED_PRESENT, "render.reflection_classify",
    "render.reflection_build_args", "render.reflection_hardware")
GPU_TRANSPARENT = (LIT_RENDER_AVBOIT_CLEAR, "render.avboit_occupancy", "render.avboit_depth_warp",
    "render.avboit_extinction", "render.avboit_integration", "render.avboit_accumulate")
GPU_INACTIVE = ("render.reflection_depth_pyramid", "render.reflection_temporal", "render.reflection_spatial")
GPU_CONTROLS = (LIT_RENDER_FRAME, LIT_RENDER_OPAQUE_REGULAR, LIT_RENDER_SHADOW_VISIBILITY, LIT_RENDER_DEFERRED_LIGHTING,
    LIT_RENDER_DEFERRED_COMPOSITE, LIT_RENDER_DEFERRED_PRESENT)
ARENAS = tuple(LIT_IMPL_ECS_RENDER + name for name in (LIT_PREPARE, "render", LIT_TASK_GRAPH, "avboit_transparent_csg",
    LIT_MATERIAL_PASS_PREPARE, "material_pass_render", "material_instance_mutable", LIT_RAY_TRACING_BUILD, "ray_tracing_attribute"))
REQUIRED_ARENAS = tuple(LIT_IMPL_ECS_RENDER + name for name in (LIT_PREPARE, LIT_TASK_GRAPH, LIT_MATERIAL_PASS_PREPARE, LIT_RAY_TRACING_BUILD))
FROZEN_SETTINGS = {"width": 960, "height": 720, "warmup": 96, LIT_SAMPLES: 256, "drain": 32,
    "reflection_mode": "hardware", "hardware_budget": 4096, "optical_queries": 16,
    "temporal": False, "spatial": False, "feedback": False, "diagnostics": False,
    "fixed_delta_seconds": .016666667, "sampling_seed": 0, "refraction": True}
COUNTERS = ("allocations", "reallocations", "deallocations")
GAUGES = (LIT_USED, LIT_RESERVED, LIT_HISTORICAL_ARENA_PEAK)


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
    if workload not in WORKLOADS or mode not in (LIT_TIMING, LIT_MEMORY):
        raise SmokeFailure("unknown workload/capture mode")
    if not rows or rows[-1] != {LIT_TYPE: LIT_COMPLETE}:
        raise SmokeFailure("normal complete result footer is required")
    if sum(row.get(LIT_TYPE) == LIT_CONFIGURATION for row in rows) != 1 or rows[0].get(LIT_TYPE) != LIT_CONFIGURATION:
        raise SmokeFailure("one leading configuration is required")
    if any(row.get(LIT_TYPE) not in (LIT_CONFIGURATION, LIT_CPU, LIT_GPU, LIT_MEMORY_BASELINE, LIT_COMPLETE) for row in rows):
        raise SmokeFailure("unknown result record")
    if sum(row.get(LIT_TYPE) == LIT_COMPLETE for row in rows) != 1:
        raise SmokeFailure("duplicate completion")
    config = rows[0]
    if config.get("schema") != 1 or config.get(LIT_WORKLOAD) != workload or config.get(LIT_MODE) != mode:
        raise SmokeFailure("actual fixture identity differs from requested trial")
    if any(config.get(key) != value or type(config.get(key)) is not type(value) for key, value in FROZEN_SETTINGS.items()):
        raise SmokeFailure("actual fixed fixture controls changed")
    if config.get("successful_frames") != 384:
        raise SmokeFailure("warm-up, measurement and drain must count successful frames")
    runtime = integer(config.get(LIT_RUNTIME_RENDERERS), "runtime renderer count")
    owners = integer(config.get(LIT_RUNTIME_OWNERS), "runtime owner count")
    renderers = integer(config.get(LIT_RENDERERS), "resolved renderer count", 1)
    transparent = integer(config.get(LIT_TRANSPARENT_RENDERERS), "transparent renderer count")
    if workload == LIT_RUNTIME:
        if runtime != 8 or owners != 8 or renderers != 65 or transparent != 56:
            raise SmokeFailure("real runtime workload was not resolved")
    elif runtime != 0 or owners != 0 or renderers != 65 or transparent != (0 if workload == LIT_OPAQUE else 32 if workload == LIT_HYBRID else 64):
        raise SmokeFailure("static fixture count changed")
    samples = [row for row in rows if row.get(LIT_TYPE) == LIT_CPU]
    if len(samples) != 256:
        raise SmokeFailure("exactly 256 complete CPU frames are required")
    for index, row in enumerate(samples):
        source = integer(row.get(LIT_SOURCE_FRAME), "CPU source frame")
        publish = integer(row.get(LIT_PUBLISH_FRAME), "CPU publication frame")
        if index and (source != samples[index - 1][LIT_SOURCE_FRAME] + 1 or publish != samples[index - 1][LIT_PUBLISH_FRAME] + 1):
            raise SmokeFailure("CPU samples contain duplicate, skipped, or reordered successful frames")
        if set(row.get(LIT_SCOPES_MS, {})) != set(CPU):
            raise SmokeFailure("CPU callback totals or parent controls are missing")
        for scope, value in row[LIT_SCOPES_MS].items():
            finite(value, scope)
        measured = row[LIT_SCOPES_MS]
        if measured[LIT_GRAPHICS_FRAME] <= 0 or measured[LIT_GRAPHICS_RENDER] <= 0:
            raise SmokeFailure("timed graphics work is empty")
        # Siblings are chargeable subtotals. The enclosing timer includes scheduling and their instrumentation.
        if measured[LIT_GRAPHICS_PREPARE_RESOURCES] + measured[LIT_GRAPHICS_RENDER_PASSES] > measured[LIT_GRAPHICS_RENDER] + .005:
            raise SmokeFailure("callback subtotals exceed their measured parent")
        if measured[LIT_GRAPHICS_RENDER] > measured[LIT_GRAPHICS_FRAME] + .005:
            raise SmokeFailure("render stage exceeds complete graphics frame")
    cpu = {scope: {LIT_SAMPLES: len(samples), LIT_TOTAL_MS: sum(row[LIT_SCOPES_MS][scope] for row in samples),
        LIT_MEAN_MS: statistics.fmean(row[LIT_SCOPES_MS][scope] for row in samples),
        "median_ms": statistics.median(row[LIT_SCOPES_MS][scope] for row in samples)} for scope in CPU}
    start, end = samples[0][LIT_SOURCE_FRAME], samples[-1][LIT_SOURCE_FRAME]
    gpu = {}
    seen = set()
    frontier = {}
    for row in (row for row in rows if row.get(LIT_TYPE) == LIT_GPU):
        scope = row.get(LIT_SCOPE)
        if scope not in GPU_BASE + GPU_TRANSPARENT + GPU_INACTIVE:
            raise SmokeFailure("unrecognized GPU control scope")
        publish = integer(row.get(LIT_PUBLISH_FRAME), "GPU publication")
        count = integer(row.get(LIT_SAMPLES), "completed GPU range count", 1)
        first = integer(row.get("first_source_frame"), "GPU first source")
        last = integer(row.get("last_source_frame"), "GPU last source")
        if first > last or count > last - first + 1:
            raise SmokeFailure("invalid one-range-per-frame GPU source span")
        if scope in GPU_INACTIVE:
            raise SmokeFailure(f"inactive GPU pass ran: {scope}")
        if workload == LIT_OPAQUE and scope in GPU_TRANSPARENT:
            # The actual AVBOIT owner clears newly created targets once, including an opaque-only warm-up.
            if scope != LIT_RENDER_AVBOIT_CLEAR or last >= start:
                raise SmokeFailure(f"inactive measured GPU pass ran: {scope}")
        key = (scope, publish)
        if key in seen or (scope in frontier and (publish <= frontier[scope][0] or first <= frontier[scope][1])):
            raise SmokeFailure("duplicate/reordered/overlapping GPU publication")
        seen.add(key)
        frontier[scope] = (publish, last)
        total = finite(row.get(LIT_TOTAL_MS), "GPU total milliseconds")
        if first < start or last > end:
            continue  # Never attribute a mixed warm-up/drain publication to measured CPU frames.
        destination = gpu.setdefault(scope, {LIT_TOTAL_MS: 0., LIT_GPU_SAMPLES: 0, LIT_REPORTS: 0})
        destination[LIT_TOTAL_MS] += total
        destination[LIT_GPU_SAMPLES] += count
        destination[LIT_REPORTS] += 1
    required = GPU_BASE + (() if workload == LIT_OPAQUE else GPU_TRANSPARENT)
    if set(gpu) != set(required):
        raise SmokeFailure("completed GPU frame/control/pass coverage is missing")
    frames = gpu[LIT_RENDER_FRAME][LIT_GPU_SAMPLES]
    if frames < math.ceil(len(samples) * .90):
        raise SmokeFailure("completed GPU frame coverage is below 90% of the exact CPU source window")
    for scope, value in gpu.items():
        if abs(value[LIT_GPU_SAMPLES] - frames) > max(2, frames * .02):
            raise SmokeFailure(f"one complete GPU range per frame is not covered for {scope}")
        value[LIT_MEAN_MS] = value[LIT_TOTAL_MS] / value[LIT_GPU_SAMPLES]
    memory_baselines = [row for row in rows if row.get(LIT_TYPE) == LIT_MEMORY_BASELINE]
    memory = {}
    if mode == LIT_TIMING:
        if memory_baselines or any(LIT_ARENAS in row for row in samples):
            raise SmokeFailure("arena statistics contaminated the timing mode")
    else:
        if len(memory_baselines) != 1:
            raise SmokeFailure("one warm-up memory baseline is required")
        previous = memory_baselines[0][LIT_ARENAS]
        for index, row in enumerate(samples):
            current = row.get(LIT_ARENAS, {})
            if set(previous) != set(ARENAS) or set(current) != set(ARENAS):
                raise SmokeFailure("arena owner coverage is incomplete")
            for scope in ARENAS:
                value, prior = current[scope], previous[scope]
                if not isinstance(value.get(LIT_PRESENT), bool) or not isinstance(prior.get(LIT_PRESENT), bool):
                    raise SmokeFailure("arena availability must be explicit")
                if scope in REQUIRED_ARENAS and (not value[LIT_PRESENT] or not prior[LIT_PRESENT]):
                    raise SmokeFailure(f"required actual scratch owner is unavailable: {scope}")
                for field in COUNTERS + GAUGES:
                    integer(value.get(field), f"{scope}/{field}")
                    integer(prior.get(field), f"baseline {scope}/{field}")
                if value[LIT_PRESENT] and value.get("frame") != row[LIT_PUBLISH_FRAME]:
                    raise SmokeFailure("arena snapshot comes from another publication")
                result = memory.setdefault(scope, {LIT_ALLOCATION_DELTAS: [], LIT_REALLOCATION_DELTAS: [],
                    LIT_DEALLOCATION_DELTAS: [], LIT_RETAINED_USED_BYTES: [], LIT_RETAINED_RESERVED_BYTES: [], LIT_HISTORICAL_ARENA_PEAK: 0,
                    LIT_AVAILABLE_FRAMES: 0, LIT_BASELINE_AVAILABLE: previous[scope][LIT_PRESENT]})
                result[LIT_AVAILABLE_FRAMES] += int(value[LIT_PRESENT])
                for field, key in zip(COUNTERS, (LIT_ALLOCATION_DELTAS, LIT_REALLOCATION_DELTAS, LIT_DEALLOCATION_DELTAS)):
                    delta = value[field] - prior[field]
                    if delta < 0:
                        raise SmokeFailure("arena cumulative counters reset during measurement")
                    result[key].append(delta)
                result[LIT_RETAINED_USED_BYTES].append(value[LIT_USED])
                result[LIT_RETAINED_RESERVED_BYTES].append(value[LIT_RESERVED])
                result[LIT_HISTORICAL_ARENA_PEAK] = max(result[LIT_HISTORICAL_ARENA_PEAK], value[LIT_HISTORICAL_ARENA_PEAK])
            previous = current
        for scope, value in memory.items():
            complete = value[LIT_BASELINE_AVAILABLE] and value[LIT_AVAILABLE_FRAMES] == len(samples)
            memory[scope] = {key: (statistics.fmean(item) if isinstance(item, list) else item) if complete else None
                for key, item in value.items() if key not in (LIT_AVAILABLE_FRAMES, LIT_BASELINE_AVAILABLE)}
            memory[scope].update(available_frames=value[LIT_AVAILABLE_FRAMES], measured_frames=len(samples), complete=complete)
    return {LIT_CONFIGURATION: config, "source_window": [start, end], LIT_CPU: cpu, LIT_GPU: gpu, LIT_MEMORY: memory}


def read_result(path, workload, mode):
    return summarize_rows([json.loads(line) for line in path.read_text(encoding=LIT_UTF_8).splitlines() if line], workload, mode)


def compare(trials, orders, mode, seed=0, practical_ms=.02, practical_fraction=.03, control_floor_ms=.015):
    expected = {(block, position, arm) for block, order in enumerate(orders) for position, arm in enumerate(order)}
    actual = [(row[LIT_BLOCK], row[LIT_POSITION], row[LIT_ARM]) for row in trials]
    if len(actual) != len(set(actual)) or set(actual) != expected:
        raise SmokeFailure("inference requires all balanced independent blocks")
    if any(row[LIT_RESULT][LIT_CONFIGURATION][LIT_MODE] != mode for row in trials):
        raise SmokeFailure("timing and memory trials cannot be pooled")
    blocks = [{row[LIT_ARM]: row[LIT_RESULT] for row in trials if row[LIT_BLOCK] == index} for index in range(len(orders))]
    if mode == LIT_MEMORY:
        return {LIT_STATUS: "memory_observation_only", LIT_ARENAS: {scope: {
            key: {arm: (statistics.fmean(block[arm][LIT_MEMORY][scope][key] for block in blocks)
                if all(block[arm][LIT_MEMORY][scope][key] is not None for block in blocks) else None) for arm in (LIT_BASELINE, LIT_CANDIDATE)}
            for key in blocks[0][LIT_BASELINE][LIT_MEMORY][scope]} for scope in ARENAS},
            "peak_semantics": "historical maximum of individual arena lifetime peaks, not per-frame or concurrent process peak"}
    cpu = {scope: paired_statistics([block[LIT_CANDIDATE][LIT_CPU][scope][LIT_MEAN_MS] - block[LIT_BASELINE][LIT_CPU][scope][LIT_MEAN_MS]
        for block in blocks], seed) for scope in CPU}
    controls = {}
    for scope in GPU_CONTROLS:
        metric = paired_statistics([block[LIT_CANDIDATE][LIT_GPU][scope][LIT_MEAN_MS] - block[LIT_BASELINE][LIT_GPU][scope][LIT_MEAN_MS]
            for block in blocks], seed)
        baseline = statistics.fmean(block[LIT_BASELINE][LIT_GPU][scope][LIT_MEAN_MS] for block in blocks)
        tolerance = max(control_floor_ms, practical_fraction * baseline)
        low, high = metric[LIT_CI95_MEAN_MS]
        metric.update(tolerance_ms=tolerance, equivalent=low >= -tolerance and high <= tolerance,
            material_drift=low > tolerance or high < -tolerance)
        controls[scope] = metric
    baseline = statistics.fmean(block[LIT_BASELINE][LIT_CPU][LIT_GRAPHICS_RENDER][LIT_MEAN_MS] for block in blocks)
    threshold = max(practical_ms, practical_fraction * baseline)
    low, high = cpu[LIT_GRAPHICS_RENDER][LIT_CI95_MEAN_MS]
    frame_base = statistics.fmean(block[LIT_BASELINE][LIT_CPU][LIT_GRAPHICS_FRAME][LIT_MEAN_MS] for block in blocks)
    frame_limit = max(practical_ms, practical_fraction * frame_base)
    if any(value["material_drift"] for value in controls.values()):
        status = "gpu_control_drift"
    elif not all(value["equivalent"] for value in controls.values()):
        status = "gpu_control_uncertain"
    elif cpu[LIT_GRAPHICS_FRAME][LIT_CI95_MEAN_MS][1] > frame_limit:
        status = "whole_cpu_frame_regression_or_uncertain"
    elif high < -threshold:
        status = "resolved_cpu_render_reduction"
    elif low > threshold:
        status = "resolved_cpu_render_increase"
    else:
        status = "cpu_change_unresolved"
    return {LIT_STATUS: status, "primary": LIT_GRAPHICS_RENDER, "practical_threshold_ms": threshold,
        LIT_CPU: cpu, LIT_GPU_CONTROLS: controls, "whole_cpu_frame_limit_ms": frame_limit,
        "units": "milliseconds per successful CPU frame; complete GPU ranges normalized independently",
        "multiplicity": "per-workload 95% intervals, no simultaneous six-workload claim"}


def environment(base, workload, mode, output, compiler_statistics_output=None):
    for key in ("VK_INSTANCE_LAYERS", "VK_LOADER_LAYERS_ENABLE"):
        if base.get(key, "").strip():
            raise SmokeFailure(f"explicit Vulkan layer override invalidates acquisition: {key}")
    env = {key: value for key, value in base.items() if not key.startswith(LIT_NWB) or key == LIT_NWB_LINUX_BACKEND}
    env.update(NWB_GATHER_BENCHMARK_WORKLOAD=workload, NWB_GATHER_BENCHMARK_MODE=mode,
        NWB_GATHER_BENCHMARK_OUTPUT=str(output), NWB_RENDERER_BASELINE_FIXED_DELTA_SECONDS="0.016666667",
        NWB_RENDERER_BASELINE_CAPTURE_FREEZE_FRAME="0")
    if compiler_statistics_output is not None:
        env["NWB_GATHER_COMPILER_STATISTICS_FILE"] = str(compiler_statistics_output)
    return env


def acquire(args, arm, block, position):
    directory = args.output_directory / f"block_{block:02d}_{position:02d}_{arm.name}"
    directory.mkdir(parents=True, exist_ok=False)
    result_path = directory / "samples.jsonl"
    launch = SimpleNamespace(**vars(args), working_directory=arm.runtime, executable=arm.executable)
    compiler_output = directory / "compiler_statistics.jsonl" if getattr(args, LIT_COMPILER_STATISTICS, False) else None
    env = environment(args.frozen_environment, args.workload, args.mode, result_path, compiler_output)
    ab.write_json(directory / "launch.json", {LIT_ARM: asdict(arm) | {LIT_EXECUTABLE: str(arm.executable),
        LIT_RUNTIME: str(arm.runtime), "source_manifest": str(arm.source_manifest)},
        "environment": {key: value for key, value in env.items() if key.startswith((LIT_NWB, LIT_VK))},
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
        code, tail = terminate_process(process, LIT_RENDERER_GATHER_BENCHMARK, handle)
        process = None
        (directory / LIT_PROCESS_TAIL_TXT).write_text(tail, encoding=LIT_UTF_8)
        require_normal_process_exit(code, tail, LIT_RENDERER_GATHER_BENCHMARK)
        text = shutdown_logserver_and_collect(logserver, log_directory, baseline, pattern)
        logserver = None
        (directory / LIT_RUNTIME_LOG).write_text(text, encoding=LIT_UTF_8)
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
        signature["actual_counts"] = {key: result[LIT_CONFIGURATION][key] for key in
            (LIT_RENDERERS, LIT_RUNTIME_RENDERERS, LIT_TRANSPARENT_RENDERERS, LIT_RUNTIME_OWNERS)}
        return {LIT_ARM: arm.name, LIT_BLOCK: block, LIT_POSITION: position, LIT_RUNTIME_SIGNATURE: signature,
            LIT_RESULT: result, "artifacts": str(directory),
            "cache_after": ab.cache_identity(arm.runtime)}
    finally:
        primary_failure = sys.exc_info()[0] is not None
        errors = []
        try:
            if process is not None:
                _, tail = terminate_process(process, LIT_RENDERER_GATHER_BENCHMARK, handle)
                (directory / LIT_PROCESS_TAIL_TXT).write_text(tail, encoding=LIT_UTF_8)
        except Exception as error:
            errors.append(f"renderer cleanup: {error}")
        if not logs_collected and log_directory is not None:
            try:
                text = shutdown_logserver_and_collect(logserver, log_directory, baseline, pattern)
                (directory / LIT_RUNTIME_LOG).write_text(text, encoding=LIT_UTF_8)
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
    for arm in (LIT_BASELINE, LIT_CANDIDATE):
        for field in (LIT_EXECUTABLE, LIT_RUNTIME, "source-manifest"):
            parser.add_argument(f"--{arm}-{field}", type=Path, required=True)
    parser.add_argument("--logserver-executable", type=Path, required=True)
    parser.add_argument("--output-directory", type=Path, required=True)
    parser.add_argument("--workload", choices=WORKLOADS, required=True)
    parser.add_argument("--mode", choices=(LIT_TIMING, LIT_MEMORY), default=LIT_TIMING)
    parser.add_argument("--blocks", type=int, default=8)
    parser.add_argument("--timeout", type=float, default=180.)
    parser.add_argument("--order-seed", type=int, default=0)
    parser.add_argument("--analysis-seed", type=int, default=0)
    parser.add_argument("--plan-only", action=LIT_STORE_TRUE)
    parser.add_argument("--compiler-statistics", action=LIT_STORE_TRUE,
        help="opt-in test-domain compiler snapshots; changes observer workload and requires separate diagnostic analysis")
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
        getattr(args, name + "_source_manifest")) for name in (LIT_BASELINE, LIT_CANDIDATE))
    ab.validate_arm_separation(arms)
    identities = {arm.name: ab.freeze_arm(arm) for arm in arms}
    if identities[LIT_BASELINE][LIT_RESOURCES][LIT_AUTHORED_VOLUME_HASHES] != identities[LIT_CANDIDATE][LIT_RESOURCES][LIT_AUTHORED_VOLUME_HASHES]:
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
    plan = {LIT_WORKLOAD: args.workload, LIT_MODE: args.mode, "orders": orders, "arms": identities,
        "shared_files": shared, "settings": FROZEN_SETTINGS, "planned_trials": args.blocks * 2,
        "gpu_environment": {key: value for key, value in args.frozen_environment.items() if key.startswith(LIT_VK) or key == LIT_NWB_LINUX_BACKEND},
        "cpu_primary": LIT_GRAPHICS_RENDER, "cpu_subtotals": [LIT_GRAPHICS_PREPARE_RESOURCES, LIT_GRAPHICS_RENDER_PASSES],
        LIT_GPU_CONTROLS: GPU_CONTROLS, "memory_policy": "separate acquisition; no timing inference from memory mode",
        "cpu_normalization": "sum scope milliseconds / 256 successful contiguous frames",
        "gpu_normalization": "sum milliseconds / completed range count inside CPU source window, independently per scope",
        "correctness": "qualify both frozen builds separately; benchmark does not replace visual/native tests",
        "source_policy": "instrumentation and fixture must be present in both builds before freezing them"}
    if getattr(args, LIT_COMPILER_STATISTICS, False):
        plan["compiler_statistics_diagnostic"] = {
            "enabled": True, "file": "<per-trial-directory>/compiler_statistics.jsonl",
            LIT_SCOPE: "existing compiler wall durations; observer cost is included in graphics.render",
            "validation": "separate compiler_statistics_diagnostic.py required; no change to timing/control gates",
        }
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
                if signature is not None and trial[LIT_RUNTIME_SIGNATURE] != signature:
                    raise SmokeFailure("physical device, material route, or vsync changed")
                signature = trial[LIT_RUNTIME_SIGNATURE]
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


if __name__ == LIT_MAIN:
    sys.exit(main())
