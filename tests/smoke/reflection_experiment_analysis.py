"""Paired reflection timing inference and control-drift classification."""

from pathlib import Path
import statistics
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "ab"))
from gpu_timing_parse import MAXIMUM_INCONCLUSIVE_POSITIVE_INTERVALS
from experiment_statistics import PairedExperimentAnalysis
from window_capture_outcome import SmokeFailure

LIT_BLOCK = "block"
LIT_VARIANT = "variant"
LIT_NAME = "name"
LIT_HARDWARE = "hardware"
LIT_HYBRID = "hybrid"
LIT_HARDWARE_RAW = "hardware_raw"
LIT_HYBRID_RAW = "hybrid_raw"
LIT_HYBRID_FEEDBACK = "hybrid_feedback"
LIT_SCOPES = "scopes"
LIT_MEAN_MS = "mean_ms"
LIT_KERNEL_WORK_MS = "kernel_work_ms"
LIT_CI95_MEAN_MS = "ci95_mean_ms"
LIT_MATERIAL_DRIFT = "material_drift"
LIT_EQUIVALENT_WITHIN_TOLERANCE = "equivalent_within_tolerance"
LIT_POSITIVE_FRAME_REPORTS = "positive_frame_reports"


class ReflectionExperimentAnalysis:
    def __init__(self, frame, kernels, controls, practical_ms=.02, practical_fraction=.03, control_floor_ms=.015, seed=0):
        self.frame = frame
        self.kernels = kernels
        self.controls = controls
        self.practical_ms = practical_ms
        self.practical_fraction = practical_fraction
        self.control_floor_ms = control_floor_ms
        self.analysis = PairedExperimentAnalysis(seed)

    def compare(self, trials, variants):
        by_block = {}
        for trial in trials:
            block = by_block.setdefault(trial[LIT_BLOCK], {})
            name = trial[LIT_VARIANT][LIT_NAME]
            if name in block:
                raise SmokeFailure(f"duplicate trial {trial[LIT_BLOCK]}/{name}")
            block[name] = trial
        names = [variant.name for variant in variants]
        if any(set(block) != set(names) for block in by_block.values()):
            raise SmokeFailure("inference refuses incomplete blocks")
        pairs = [(names[0], name) for name in names[1:]]
        if LIT_HARDWARE in names and LIT_HYBRID in names:
            pairs.append((LIT_HARDWARE, LIT_HYBRID))
        if LIT_HARDWARE_RAW in names:
            pairs += [(LIT_HARDWARE_RAW, name) for name in names if name.startswith("hardware_") and name != LIT_HARDWARE_RAW]
            if LIT_HYBRID_RAW in names:
                pairs.append((LIT_HARDWARE_RAW, LIT_HYBRID_RAW))
        if LIT_HYBRID_FEEDBACK in names:
            baseline = LIT_HYBRID_RAW if LIT_HYBRID_RAW in names else LIT_HYBRID
            pairs.append((baseline, LIT_HYBRID_FEEDBACK))
        comparisons = []
        for baseline, candidate in pairs:
            blocks = [by_block[index] for index in sorted(by_block)]
            deltas = [b[candidate][LIT_SCOPES][self.frame][LIT_MEAN_MS] - b[baseline][LIT_SCOPES][self.frame][LIT_MEAN_MS] for b in blocks]
            frame = self.analysis.paired_statistics(deltas)
            kernels = self.analysis.paired_statistics([b[candidate][LIT_KERNEL_WORK_MS] - b[baseline][LIT_KERNEL_WORK_MS] for b in blocks])
            dispatch_scopes = {}
            for scope in self.kernels:
                if all(scope in block[baseline][LIT_SCOPES] and scope in block[candidate][LIT_SCOPES] for block in blocks):
                    dispatch_scopes[scope] = self.analysis.paired_statistics([block[candidate][LIT_SCOPES][scope][LIT_MEAN_MS] - block[baseline][LIT_SCOPES][scope][LIT_MEAN_MS] for block in blocks])
            controls = {}
            unstable = False
            uncertain_controls = False
            for scope in self.controls:
                values = [b[candidate][LIT_SCOPES][scope][LIT_MEAN_MS] - b[baseline][LIT_SCOPES][scope][LIT_MEAN_MS] for b in blocks]
                metric = self.analysis.paired_statistics(values)
                base = statistics.fmean(b[baseline][LIT_SCOPES][scope][LIT_MEAN_MS] for b in blocks)
                limit = max(self.control_floor_ms, self.practical_fraction * base)
                low, high = metric[LIT_CI95_MEAN_MS]
                metric[LIT_MATERIAL_DRIFT] = low > limit or high < -limit
                metric[LIT_EQUIVALENT_WITHIN_TOLERANCE] = low >= -limit and high <= limit
                metric["tolerance_ms"] = limit
                unstable |= metric[LIT_MATERIAL_DRIFT]
                uncertain_controls |= not metric[LIT_EQUIVALENT_WITHIN_TOLERANCE]
                controls[scope] = metric
            base = statistics.fmean(b[baseline][LIT_SCOPES][self.frame][LIT_MEAN_MS] for b in blocks)
            threshold = max(self.practical_ms, self.practical_fraction * base)
            low, high = frame[LIT_CI95_MEAN_MS]
            status = "unresolved"
            if any(min(b[baseline][LIT_POSITIVE_FRAME_REPORTS], b[candidate][LIT_POSITIVE_FRAME_REPORTS]) <= MAXIMUM_INCONCLUSIVE_POSITIVE_INTERVALS for b in blocks):
                status = "inconclusive_interval_count"
            elif unstable:
                status = "control_drift"
            elif uncertain_controls:
                status = "control_uncertain"
            elif high < -threshold:
                status = "resolved_gpu_time_reduction"
            elif low > threshold:
                status = "resolved_gpu_time_increase"
            comparisons.append({"baseline": baseline, "candidate": candidate, "status": status, "practical_threshold_ms": threshold, "frame": frame, "kernel_work": kernels,
                "dispatch_scopes": dispatch_scopes, "dispatch_scope_units": "milliseconds per dispatch; depth is per mip", "controls": controls})
        return comparisons
