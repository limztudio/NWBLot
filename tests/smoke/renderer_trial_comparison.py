"""Compare complete paired renderer trials and qualify their evidence."""

import math
import statistics

import reflection_benchmark as reflection
from reflection_benchmark import LIT_POSITIVE_FRAME_REPORTS
from gpu_timing_parse import MAXIMUM_INCONCLUSIVE_POSITIVE_INTERVALS
from experiment_statistics import PairedExperimentAnalysis
from renderer_wall_frame_evidence import WALL_FRAME_POLICY, SYNTHETIC_FRAME_REASON
from renderer_workload_registry import FRAME
from window_capture_outcome import SmokeFailure

LIT_ELIGIBLE = "eligible"
LIT_REASON = "reason"
LIT_FPS = "fps"
LIT_POLICY = "policy"
LIT_MEAN_FRAME_MS = "mean_frame_ms"
LIT_MEAN_FPS = "mean_fps"
LIT_WALL_FRAME = "wall_frame"
LIT_COMPARISON = "comparison"
LIT_GPU_SAMPLES = "gpu_samples"
LIT_MEAN_MS = "mean_ms"
LIT_BLOCK = "block"
LIT_POSITION = "position"
LIT_ARM = "arm"
LIT_BASELINE = "baseline"
LIT_CANDIDATE = "candidate"
LIT_SCOPES = "scopes"
LIT_CI95_MEAN_MS = "ci95_mean_ms"
LIT_TOTAL_MS = "total_ms"
LIT_MEANS_MS = "means_ms"
LIT_SECONDARY_SCOPE = "secondary_scope"
LIT_CONTROLS = "controls"
LIT_UNITS = "units"
LIT_COMPLETED_TRIALS = "completed_trials"


class RendererTrialComparison:
    @staticmethod
    def compare_trials(trials, orders, workload, seed=0, practical_ms=.02, practical_fraction=.03, control_floor_ms=.015):
        analysis = PairedExperimentAnalysis(seed)
        expected = {(block, position, name) for block, row in enumerate(orders) for position, name in enumerate(row)}
        seen = [(trial[LIT_BLOCK], trial[LIT_POSITION], trial[LIT_ARM]) for trial in trials]
        if len(seen) != len(set(seen)) or set(seen) != expected:
            raise SmokeFailure("inference requires every planned trial exactly once; incomplete/duplicate data is refused")
        blocks = {block: {} for block in range(len(orders))}
        for trial in trials:
            if trial[LIT_ARM] in blocks[trial[LIT_BLOCK]]:
                raise SmokeFailure("duplicate arm within a block")
            blocks[trial[LIT_BLOCK]][trial[LIT_ARM]] = trial
        if any(set(block) != {LIT_BASELINE, LIT_CANDIDATE} for block in blocks.values()):
            raise SmokeFailure("each independent block must contain both frozen arms")
        means = {arm: {scope: statistics.fmean(block[arm][LIT_SCOPES][scope][LIT_MEAN_MS] for block in blocks.values()) for scope in workload.scopes} for arm in (LIT_BASELINE, LIT_CANDIDATE)}
        paired = {scope: analysis.paired_statistics([block[LIT_CANDIDATE][LIT_SCOPES][scope][LIT_MEAN_MS] - block[LIT_BASELINE][LIT_SCOPES][scope][LIT_MEAN_MS] for block in blocks.values()]) for scope in workload.scopes}
        controls = {}
        for scope in workload.control_scopes:
            metric = dict(paired[scope])
            tolerance = max(control_floor_ms, practical_fraction * means[LIT_BASELINE][scope])
            low, high = metric[LIT_CI95_MEAN_MS]
            metric.update(tolerance_ms=tolerance, equivalent_within_tolerance=low >= -tolerance and high <= tolerance, material_drift=low > tolerance or high < -tolerance)
            controls[scope] = metric
        threshold = max(practical_ms, practical_fraction * means[LIT_BASELINE][FRAME])
        low, high = paired[FRAME][LIT_CI95_MEAN_MS]
        status = "unresolved"
        if any(trial[LIT_POSITIVE_FRAME_REPORTS] <= MAXIMUM_INCONCLUSIVE_POSITIVE_INTERVALS for trial in trials):
            status = "inconclusive_interval_count"
        elif any(metric["material_drift"] for metric in controls.values()):
            status = "control_drift"
        elif any(not metric["equivalent_within_tolerance"] for metric in controls.values()):
            status = "control_uncertain"
        elif high < -threshold:
            status = "resolved_gpu_time_reduction"
        elif low > threshold:
            status = "resolved_gpu_time_increase"
        secondary_frame_means = {arm: statistics.fmean(block[arm][LIT_SCOPES][workload.secondary_scope][LIT_TOTAL_MS] / block[arm][LIT_SCOPES][FRAME][LIT_GPU_SAMPLES] for block in blocks.values()) for arm in (LIT_BASELINE, LIT_CANDIDATE)}
        secondary_frame_delta = analysis.paired_statistics([block[LIT_CANDIDATE][LIT_SCOPES][workload.secondary_scope][LIT_TOTAL_MS] / block[LIT_CANDIDATE][LIT_SCOPES][FRAME][LIT_GPU_SAMPLES]
            - block[LIT_BASELINE][LIT_SCOPES][workload.secondary_scope][LIT_TOTAL_MS] / block[LIT_BASELINE][LIT_SCOPES][FRAME][LIT_GPU_SAMPLES] for block in blocks.values()])
        wall_frame = {LIT_ELIGIBLE: False, LIT_REASON: SYNTHETIC_FRAME_REASON}
        if workload.reflection_policy is not None:
            if any(not trial.get(LIT_WALL_FRAME, {}).get(LIT_ELIGIBLE) for trial in trials):
                raise SmokeFailure("wall-frame inference requires every planned trial to have qualified reports")
            for trial in trials:
                if any(not math.isfinite(trial[LIT_WALL_FRAME][field]) or trial[LIT_WALL_FRAME][field] <= 0 for field in (LIT_MEAN_FRAME_MS, LIT_MEAN_FPS)):
                    raise SmokeFailure("invalid trial wall-frame metrics")
            wall_frame = {LIT_ELIGIBLE: True, LIT_POLICY: WALL_FRAME_POLICY, "means": {arm: {"frame_ms": statistics.fmean(block[arm][LIT_WALL_FRAME][LIT_MEAN_FRAME_MS] for block in blocks.values()), LIT_FPS: statistics.fmean(block[arm][LIT_WALL_FRAME][LIT_MEAN_FPS]
                    for block in blocks.values())} for arm in (LIT_BASELINE, LIT_CANDIDATE)}, "frame_ms_delta": analysis.paired_statistics([block[LIT_CANDIDATE][LIT_WALL_FRAME][LIT_MEAN_FRAME_MS]
                    - block[LIT_BASELINE][LIT_WALL_FRAME][LIT_MEAN_FRAME_MS] for block in blocks.values()]), "fps_delta": {key.replace("_ms", "_fps"): value for key, value in analysis.paired_statistics([
                    block[LIT_CANDIDATE][LIT_WALL_FRAME][LIT_MEAN_FPS] - block[LIT_BASELINE][LIT_WALL_FRAME][LIT_MEAN_FPS] for block in blocks.values()]).items()},
                LIT_COMPARISON: "candidate minus baseline; equal-weight independent blocks with paired bootstrap 95% intervals"}
        return {"status": status, "practical_threshold_ms": threshold, LIT_MEANS_MS: means, LIT_WALL_FRAME: wall_frame, "frame": paired[FRAME], LIT_SECONDARY_SCOPE: workload.secondary_scope,
            "secondary": paired[workload.secondary_scope], LIT_CONTROLS: controls, "scope_deltas": paired, "secondary_scope_units": "milliseconds per completed mip-reduction range" if workload.secondary_scope == reflection.DEPTH
                else "milliseconds per completed scope range; a range may contain multiple native dispatches", "secondary_per_frame_work": {LIT_MEANS_MS: secondary_frame_means, "delta": secondary_frame_delta,
                LIT_UNITS: "sum of measured secondary-scope milliseconds per completed GPU frame; not a frame-time attribution"}, LIT_COMPLETED_TRIALS: len(trials), "completed_blocks": len(blocks),
            "completed_gpu_frames": sum(t[LIT_SCOPES][FRAME][LIT_GPU_SAMPLES] for t in trials), LIT_UNITS: "milliseconds per completed scope range; frame is the primary endpoint", "multiplicity": "per-comparison 95% intervals; no simultaneous family-wide claim"}
