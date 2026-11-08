"""Summarize refraction workload results across complete planned trials."""

import statistics

import refraction_density_benchmark as density
from experiment_statistics import PairedExperimentAnalysis

METRICS = (density.FRAME, density.RESOLVE, *density.CONTROLS, density.COMPOSITE, *density.AVBOIT)


class ProfileResultAnalysis:
    @staticmethod
    def trial_metrics(trial):
        frames = trial["scopes"][density.FRAME]["gpu_samples"]
        return {**{scope: trial["scopes"][scope]["mean_ms"] for scope in METRICS},
            "effect_work_per_frame_ms": trial["total_effect_work_per_frame_ms"],
            "resolve_ms_per_million_admitted_pixels": trial["scopes"][density.RESOLVE]["total_ms"] / frames / trial["runtime_signature"]["fixture"]["admitted_pixels"] * 1e6}

    @staticmethod
    def summarize(trials, plan, workload_cells):
        expected = {(row["block"], row["position"], row["cell"]) for row in plan["sequence"]}
        seen = [(trial["block"], trial["position"], trial["cell"]) for trial in trials]
        if len(seen) != len(set(seen)) or set(seen) != expected:
            raise density.SmokeFailure("workload profiling requires every planned trial exactly once")
        cells = {}
        for cell in workload_cells:
            selected = [trial for trial in trials if trial["cell"] == cell.key]
            signature = selected[0]["runtime_signature"]
            if any(trial["runtime_signature"] != signature for trial in selected):
                raise density.SmokeFailure("profile cell device, material, coverage, or tracing route changed")
            metrics = [ProfileResultAnalysis.trial_metrics(trial) for trial in selected]
            cells[cell.key] = {"runtime_signature": signature, "blocks": len(selected), "metrics": {name: {
                "mean": statistics.fmean(row[name] for row in metrics), "minimum": min(row[name] for row in metrics), "maximum": max(row[name] for row in metrics),
                "block_values": [row[name] for row in metrics]} for name in metrics[0]},
                "telemetry_eligible": all(trial["telemetry"]["eligible"] for trial in selected), "wall_eligible": all(trial["wall_frame"]["eligible"] for trial in selected)}
        scaling = {}
        for extent in ("960x720", "1600x900"):
            pairs = {block: {trial["runtime_signature"]["fixture"]["requested_percent"]: trial for trial in trials
                if trial["block"] == block and trial["cell"].startswith(extent + "_")} for block in range(plan["blocks"])}
            scaling[extent] = {name: PairedExperimentAnalysis().paired_statistics([ProfileResultAnalysis.trial_metrics(pairs[block][100])[name] - ProfileResultAnalysis.trial_metrics(pairs[block][50])[name]
                for block in range(plan["blocks"])]) for name in ProfileResultAnalysis.trial_metrics(pairs[0][50])}
        by_position = {}
        for cell in workload_cells:
            by_position[cell.key] = {str(position): {name: statistics.fmean(ProfileResultAnalysis.trial_metrics(trial)[name] for trial in trials if trial["cell"] == cell.key and trial["position"] == position)
                for name in (density.FRAME, density.RESOLVE, *density.CONTROLS)} for position in range(len(workload_cells))}
        return {"acquisition_passed": True, "telemetry_eligible": all(trial["telemetry"]["eligible"] for trial in trials),
            "wall_eligible": all(trial["wall_frame"]["eligible"] for trial in trials), "cells": cells, "coverage_100_minus_50_paired_statistics": scaling,
            "position_means_ms": by_position, "plan": plan, "trials": trials,
            "interpretation": "Equal-weight block-level workload comparisons, not a renderer speedup. Individual 95% bootstrap intervals are not simultaneous family-wise intervals. Position/load associations are observational; the host provides no GPU clock telemetry."}
