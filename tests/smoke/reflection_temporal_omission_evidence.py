"""Validate temporal omission timing and hardware capability evidence."""

from dataclasses import asdict
from pathlib import Path

import reflection_benchmark as benchmark
from reflection_gpu_coverage import ReflectionGpuCoverage
from gpu_timing_evidence import GpuTimingEvidence

LIT_VARIANT = "variant"
LIT_UTF_8 = "utf-8"
LIT_REPORTS = "reports"
LIT_GPU_SAMPLES = "gpu_samples"
LIT_COMPLETED_GPU_FRAMES = "completed_gpu_frames"
CAPS = (1, 16)
VARIANT = benchmark.Variant("hardware_temporal", "hardware", temporal=True)


class ReflectionTemporalOmissionEvidence:
    @staticmethod
    def hardware_unavailable_without_errors(text):
        lines = text.replace("\r\n", "\n").splitlines()
        if lines.count("ReflectionSmokeProject: hardware unavailable") != 1 \
            or "ReflectionSmokeProject: hardware available" in lines:
            return False
        try:
            benchmark.validate_expected_log_text(text, [], benchmark.STRICT_LOG_FAILURE_MESSAGES)
        except benchmark.SmokeFailure:
            return False
        return True

    @staticmethod
    def hardware_skip_is_proven(error, log_path, cleanup_status):
        acquisition_failure = str(error).startswith("GPU benchmark timed out:") \
            or str(error) == "hardware was unavailable for a hardware benchmark route"
        return acquisition_failure and cleanup_status.succeeded and log_path.is_file() \
            and ReflectionTemporalOmissionEvidence.hardware_unavailable_without_errors(log_path.read_text(encoding=LIT_UTF_8))

    @staticmethod
    def validate_recording_evidence(trial, history_samples, symbols=None):
        """Reparse retained evidence and reject cap-one work even outside the measured window."""
        if history_samples not in CAPS or trial[LIT_VARIANT] != asdict(VARIANT):
            raise benchmark.SmokeFailure("unexpected temporal omission proof configuration")
        timing = Path(trial["artifacts"]) / "gpu_timing.txt"
        timing_evidence = GpuTimingEvidence(symbols)
        reports = timing_evidence.parse_intervals(timing.read_text(encoding=LIT_UTF_8), finalized=True)
        start, end = trial["retained_report_range"]
        if not 0 <= start < end <= len(reports) or end - start != trial[LIT_REPORTS]:
            raise benchmark.SmokeFailure("retained timing report range is inconsistent")
        retained = timing_evidence.summarize_intervals(reports[start:end])
        if retained != trial["scopes"]:
            raise benchmark.SmokeFailure("raw timing reports differ from the retained trial summary")
        ReflectionGpuCoverage.validate_coverage(retained, VARIANT, trial[LIT_REPORTS], 100, trial["mip_count"], history_samples)
        frames = retained[benchmark.FRAME][LIT_GPU_SAMPLES]
        # Require common control and presentation timing ranges alongside reflection kernels.
        for scope in benchmark.OBSERVED_CONTROLS:
            if abs(retained[scope][LIT_GPU_SAMPLES] - frames) > max(2, frames * .02):
                raise benchmark.SmokeFailure("control scope coverage differs from completed GPU frames: " + scope)
        all_temporal = sum(report[benchmark.TEMPORAL].gpu_samples for report in reports if benchmark.TEMPORAL in report)
        if history_samples == 1 and all_temporal:
            raise benchmark.SmokeFailure("cap-one temporal work appeared during warm-up, acquisition, or shutdown")
        return {"history_samples": history_samples, LIT_COMPLETED_GPU_FRAMES: frames, "retained_temporal_ranges": retained.get(benchmark.TEMPORAL, {}).get(LIT_GPU_SAMPLES, 0), "all_completed_temporal_ranges": all_temporal, "finalized_reports": len(reports),
            "expected_temporal_ranges_per_frame": int(history_samples > 1), "coverage_tolerance": "max(2 completed ranges, 2% of completed GPU frames)"}
