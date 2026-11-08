"""Compare balanced renderer gathering trials and their controls."""

import statistics

from experiment_statistics import PairedExperimentAnalysis
from window_capture_smoke import SmokeFailure
from renderer_gather_contract import (
    ARENAS,
    CPU,
    GPU_CONTROLS,
    LIT_ARENAS,
    LIT_ARM,
    LIT_BASELINE,
    LIT_BLOCK,
    LIT_CANDIDATE,
    LIT_CI95_MEAN_MS,
    LIT_CONFIGURATION,
    LIT_CPU,
    LIT_GPU,
    LIT_GPU_CONTROLS,
    LIT_GRAPHICS_FRAME,
    LIT_GRAPHICS_RENDER,
    LIT_MEAN_MS,
    LIT_MEMORY,
    LIT_MODE,
    LIT_POSITION,
    LIT_RESULT,
    LIT_STATUS,
)


class GatherComparison:
    @staticmethod
    def compare(trials, orders, mode, seed=0, practical_ms=.02, practical_fraction=.03, control_floor_ms=.015):
        analysis = PairedExperimentAnalysis(seed)
        expected = {(block, position, arm) for block, order in enumerate(orders) for position, arm in enumerate(order)}
        actual = [(row[LIT_BLOCK], row[LIT_POSITION], row[LIT_ARM]) for row in trials]
        if len(actual) != len(set(actual)) or set(actual) != expected:
            raise SmokeFailure("inference requires all balanced independent blocks")
        if any(row[LIT_RESULT][LIT_CONFIGURATION][LIT_MODE] != mode for row in trials):
            raise SmokeFailure("timing and memory trials cannot be pooled")
        blocks = [{row[LIT_ARM]: row[LIT_RESULT] for row in trials if row[LIT_BLOCK] == index} for index in range(len(orders))]
        if mode == LIT_MEMORY:
            return {LIT_STATUS: "memory_observation_only", LIT_ARENAS: {scope: { key: {arm: (statistics.fmean(block[arm][LIT_MEMORY][scope][key] for block in blocks)
                    if all(block[arm][LIT_MEMORY][scope][key] is not None for block in blocks) else None) for arm in (LIT_BASELINE, LIT_CANDIDATE)} for key in blocks[0][LIT_BASELINE][LIT_MEMORY][scope]} for scope in ARENAS},
                "peak_semantics": "historical maximum of individual arena lifetime peaks, not per-frame or concurrent process peak"}
        cpu = {scope: analysis.paired_statistics([block[LIT_CANDIDATE][LIT_CPU][scope][LIT_MEAN_MS] - block[LIT_BASELINE][LIT_CPU][scope][LIT_MEAN_MS] for block in blocks]) for scope in CPU}
        controls = {}
        for scope in GPU_CONTROLS:
            metric = analysis.paired_statistics([block[LIT_CANDIDATE][LIT_GPU][scope][LIT_MEAN_MS] - block[LIT_BASELINE][LIT_GPU][scope][LIT_MEAN_MS] for block in blocks])
            baseline = statistics.fmean(block[LIT_BASELINE][LIT_GPU][scope][LIT_MEAN_MS] for block in blocks)
            tolerance = max(control_floor_ms, practical_fraction * baseline)
            low, high = metric[LIT_CI95_MEAN_MS]
            metric.update(tolerance_ms=tolerance, equivalent=low >= -tolerance and high <= tolerance, material_drift=low > tolerance or high < -tolerance)
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
        return {LIT_STATUS: status, "primary": LIT_GRAPHICS_RENDER, "practical_threshold_ms": threshold, LIT_CPU: cpu, LIT_GPU_CONTROLS: controls, "whole_cpu_frame_limit_ms": frame_limit,
            "units": "milliseconds per successful CPU frame; complete GPU ranges normalized independently", "multiplicity": "per-workload 95% intervals, no simultaneous six-workload claim"}
