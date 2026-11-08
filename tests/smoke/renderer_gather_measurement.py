"""Validate renderer gathering samples and summarize accepted measurements."""

import json
import math
import statistics

from window_capture_smoke import SmokeFailure
from renderer_gather_contract import (
    ARENAS,
    COUNTERS,
    CPU,
    FROZEN_SETTINGS,
    GAUGES,
    GPU_BASE,
    GPU_INACTIVE,
    GPU_TRANSPARENT,
    LIT_ALLOCATION_DELTAS,
    LIT_ARENAS,
    LIT_AVAILABLE_FRAMES,
    LIT_BASELINE_AVAILABLE,
    LIT_COMPLETE,
    LIT_CONFIGURATION,
    LIT_CPU,
    LIT_DEALLOCATION_DELTAS,
    LIT_GPU,
    LIT_GPU_SAMPLES,
    LIT_GRAPHICS_FRAME,
    LIT_GRAPHICS_PREPARE_RESOURCES,
    LIT_GRAPHICS_RENDER,
    LIT_GRAPHICS_RENDER_PASSES,
    LIT_HISTORICAL_ARENA_PEAK,
    LIT_HYBRID,
    LIT_MEAN_MS,
    LIT_MEMORY,
    LIT_MEMORY_BASELINE,
    LIT_MODE,
    LIT_OPAQUE,
    LIT_PRESENT,
    LIT_PUBLISH_FRAME,
    LIT_REALLOCATION_DELTAS,
    LIT_RENDERERS,
    LIT_RENDER_AVBOIT_CLEAR,
    LIT_RENDER_FRAME,
    LIT_REPORTS,
    LIT_RESERVED,
    LIT_RETAINED_RESERVED_BYTES,
    LIT_RETAINED_USED_BYTES,
    LIT_RUNTIME,
    LIT_RUNTIME_OWNERS,
    LIT_RUNTIME_RENDERERS,
    LIT_SAMPLES,
    LIT_SCOPE,
    LIT_SCOPES_MS,
    LIT_SOURCE_FRAME,
    LIT_TIMING,
    LIT_TOTAL_MS,
    LIT_TRANSPARENT_RENDERERS,
    LIT_TYPE,
    LIT_USED,
    LIT_UTF_8,
    LIT_WORKLOAD,
    REQUIRED_ARENAS,
    WORKLOADS,
)


class GatherMeasurement:
    @staticmethod
    def finite(value, label, nonnegative=True):
        if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value) \
            or (nonnegative and value < 0):
            raise SmokeFailure(f"invalid finite measurement: {label}")
        return value

    @staticmethod
    def integer(value, label, minimum=0):
        if isinstance(value, bool) or not isinstance(value, int) or value < minimum:
            raise SmokeFailure(f"invalid integer measurement: {label}")
        return value

    @staticmethod
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
        if config.get("schema") != 2 or config.get(LIT_WORKLOAD) != workload or config.get(LIT_MODE) != mode:
            raise SmokeFailure("actual fixture identity differs from requested trial")
        if any(config.get(key) != value or type(config.get(key)) is not type(value) for key, value in FROZEN_SETTINGS.items()):
            raise SmokeFailure("actual fixed fixture controls changed")
        if config.get("successful_frames") != 384:
            raise SmokeFailure("warm-up, measurement and drain must count successful frames")
        runtime = GatherMeasurement.integer(config.get(LIT_RUNTIME_RENDERERS), "runtime renderer count")
        owners = GatherMeasurement.integer(config.get(LIT_RUNTIME_OWNERS), "runtime owner count")
        renderers = GatherMeasurement.integer(config.get(LIT_RENDERERS), "resolved renderer count", 1)
        transparent = GatherMeasurement.integer(config.get(LIT_TRANSPARENT_RENDERERS), "transparent renderer count")
        if workload == LIT_RUNTIME:
            if runtime != 8 or owners != 8 or renderers != 65 or transparent != 56:
                raise SmokeFailure("real runtime workload was not resolved")
        elif runtime != 0 or owners != 0 or renderers != 65 or transparent != (0 if workload == LIT_OPAQUE else 32 if workload == LIT_HYBRID else 64):
            raise SmokeFailure("static fixture count changed")
        samples = [row for row in rows if row.get(LIT_TYPE) == LIT_CPU]
        if len(samples) != 256:
            raise SmokeFailure("exactly 256 complete CPU frames are required")
        for index, row in enumerate(samples):
            source = GatherMeasurement.integer(row.get(LIT_SOURCE_FRAME), "CPU source frame")
            publish = GatherMeasurement.integer(row.get(LIT_PUBLISH_FRAME), "CPU publication frame")
            if index and (source != samples[index - 1][LIT_SOURCE_FRAME] + 1 or publish != samples[index - 1][LIT_PUBLISH_FRAME] + 1):
                raise SmokeFailure("CPU samples contain duplicate, skipped, or reordered successful frames")
            if set(row.get(LIT_SCOPES_MS, {})) != set(CPU):
                raise SmokeFailure("CPU callback totals or parent controls are missing")
            for scope, value in row[LIT_SCOPES_MS].items():
                GatherMeasurement.finite(value, scope)
            measured = row[LIT_SCOPES_MS]
            if measured[LIT_GRAPHICS_FRAME] <= 0 or measured[LIT_GRAPHICS_RENDER] <= 0:
                raise SmokeFailure("timed graphics work is empty")
            # Sibling subtotals must not exceed their enclosing timer.
            if measured[LIT_GRAPHICS_PREPARE_RESOURCES] + measured[LIT_GRAPHICS_RENDER_PASSES] > measured[LIT_GRAPHICS_RENDER] + .005:
                raise SmokeFailure("callback subtotals exceed their measured parent")
            if measured[LIT_GRAPHICS_RENDER] > measured[LIT_GRAPHICS_FRAME] + .005:
                raise SmokeFailure("render stage exceeds complete graphics frame")
        cpu = {scope: {LIT_SAMPLES: len(samples), LIT_TOTAL_MS: sum(row[LIT_SCOPES_MS][scope] for row in samples), LIT_MEAN_MS: statistics.fmean(row[LIT_SCOPES_MS][scope] for row in samples),
            "median_ms": statistics.median(row[LIT_SCOPES_MS][scope] for row in samples)} for scope in CPU}
        start, end = samples[0][LIT_SOURCE_FRAME], samples[-1][LIT_SOURCE_FRAME]
        gpu = {}
        seen = set()
        frontier = {}
        for row in (row for row in rows if row.get(LIT_TYPE) == LIT_GPU):
            scope = row.get(LIT_SCOPE)
            if scope not in GPU_BASE + GPU_TRANSPARENT + GPU_INACTIVE:
                raise SmokeFailure("unrecognized GPU control scope")
            publish = GatherMeasurement.integer(row.get(LIT_PUBLISH_FRAME), "GPU publication")
            count = GatherMeasurement.integer(row.get(LIT_SAMPLES), "completed GPU range count", 1)
            first = GatherMeasurement.integer(row.get("first_source_frame"), "GPU first source")
            last = GatherMeasurement.integer(row.get("last_source_frame"), "GPU last source")
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
            total = GatherMeasurement.finite(row.get(LIT_TOTAL_MS), "GPU total milliseconds")
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
                        GatherMeasurement.integer(value.get(field), f"{scope}/{field}")
                        GatherMeasurement.integer(prior.get(field), f"baseline {scope}/{field}")
                    if value[LIT_PRESENT] and value.get("frame") != row[LIT_PUBLISH_FRAME]:
                        raise SmokeFailure("arena snapshot comes from another publication")
                    result = memory.setdefault(scope, {LIT_ALLOCATION_DELTAS: [], LIT_REALLOCATION_DELTAS: [], LIT_DEALLOCATION_DELTAS: [], LIT_RETAINED_USED_BYTES: [], LIT_RETAINED_RESERVED_BYTES: [], LIT_HISTORICAL_ARENA_PEAK: 0,
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
                memory[scope] = {key: (statistics.fmean(item) if isinstance(item, list) else item) if complete else None for key, item in value.items() if key not in (LIT_AVAILABLE_FRAMES, LIT_BASELINE_AVAILABLE)}
                memory[scope].update(available_frames=value[LIT_AVAILABLE_FRAMES], measured_frames=len(samples), complete=complete)
        return {LIT_CONFIGURATION: config, "source_window": [start, end], LIT_CPU: cpu, LIT_GPU: gpu, LIT_MEMORY: memory}

    @staticmethod
    def read_result(path, workload, mode):
        return GatherMeasurement.summarize_rows([json.loads(line) for line in path.read_text(encoding=LIT_UTF_8).splitlines() if line], workload, mode)

