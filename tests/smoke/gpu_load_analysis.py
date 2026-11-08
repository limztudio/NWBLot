"""Analyze GPU engine load and retained publication windows."""

import math
import re
import statistics

from window_capture_smoke import SmokeFailure

PID_INSTANCE = re.compile(r"^pid_(\d+)_(luid_.+_phys_\d+_eng_\d+_engtype_.+)$", re.IGNORECASE)
VALID_STATUSES = (0, 1)


class GpuLoadAnalysis:
    @staticmethod
    def reduce_instances(items, fixture_pid):
        """Keep per-engine sums separate; engines execute concurrently."""
        engines, invalid, unmatched = {}, [], []
        for item in items:
            name, status, value = item["instance"], item["status"], item.get("value")
            if status not in VALID_STATUSES or value is None or not math.isfinite(value) or value < 0:
                invalid.append(item)
                continue
            match = PID_INSTANCE.fullmatch(name)
            if match is None:
                unmatched.append(item)
                continue
            pid, engine = int(match[1]), match[2].lower()
            row = engines.setdefault(engine, {"fixture_percent": 0., "other_processes_percent": 0.})
            row["fixture_percent" if pid == fixture_pid else "other_processes_percent"] += value
        return {"engines": engines, "invalid_instances": invalid, "unmatched_instances": unmatched,
            "fixture_busiest_engine_percent": max((row["fixture_percent"] for row in engines.values()), default=None),
            "other_processes_busiest_engine_percent": max((row["other_processes_percent"] for row in engines.values()), default=None),
            "fixture_instances_present": any(PID_INSTANCE.fullmatch(item["instance"]) and int(PID_INSTANCE.fullmatch(item["instance"])[1]) == fixture_pid
                and item["status"] in VALID_STATUSES and item.get("value") is not None and math.isfinite(item["value"]) and item["value"] >= 0 for item in items)}

    @staticmethod
    def retained_publication_bounds(observations, start, stop, durations):
        if start < 0 or stop <= start or stop > len(observations) or stop > len(durations):
            raise SmokeFailure("telemetry lacks retained publication-header observations")
        if any(not math.isfinite(value) or value <= 0 for value in durations[start:stop]):
            raise SmokeFailure("telemetry retained publication durations must be finite and positive")
        # Log headers arrive after their publication interval.
        return observations[start] - durations[start], observations[stop - 1]

    @staticmethod
    def summarize_samples(samples, bounds):
        start, stop = bounds
        selected = [sample for sample in samples if start <= sample["monotonic_seconds"] <= stop]
        valid = [sample for sample in selected if sample.get("query_status") == 0 and sample.get("fixture_instances_present")]
        result = {"retained_publication_bounds_monotonic_seconds": [start, stop], "retained_samples": len(selected), "valid_fixture_samples": len(valid),
            "eligible": len(valid) >= 2, "interpretation": "1 Hz host observations aligned approximately to timing publication windows. Per-engine process sums may exceed 100%; concurrent engines are not summed into a GPU percentage. No GPU clock or causal DVFS inference."}
        for field in ("fixture_busiest_engine_percent", "other_processes_busiest_engine_percent"):
            values = [sample[field] for sample in valid if sample.get(field) is not None]
            result[field] = {"mean": statistics.fmean(values), "minimum": min(values), "maximum": max(values)} if values else None
        return result
