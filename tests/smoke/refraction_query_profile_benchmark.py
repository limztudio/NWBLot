#!/usr/bin/env python3
"""Profile frozen, uninstrumented hardware refraction with balanced workload order.

Four density/extent treatments use complete Williams cycles, fixed target-only
warmup, and passive host engine-load observations. This is workload attribution,
not an optimization A/B comparison. Diagnostic counter acquisition is separate.
"""

import argparse
from collections import Counter
from datetime import datetime, timezone
import itertools
import math
from pathlib import Path
import statistics
import subprocess
import sys
import time
from types import SimpleNamespace

import refraction_density_benchmark as density
from gpu_load_telemetry import WindowsGpuTelemetry, host_inventory
from smoke_volume_identity import file_identity

CELLS = tuple(density.Cell(extent, coverage, "hardware") for extent, coverage in itertools.product(("960x720", "1600x900"), (50, 100)))
RUNNER_SOURCES = ("refraction_query_profile_benchmark.py", "refraction_density_benchmark.py", "gpu_load_telemetry.py", "reflection_benchmark.py", "renderer_ab_benchmark.py")
METRICS = (density.FRAME, density.RESOLVE, *density.CONTROLS, density.COMPOSITE, *density.AVBOIT)


def orders(blocks, seed):
    return density.timing.balanced_orders(CELLS, blocks, seed)


def order_plan(rows):
    result, previous = [], None
    for block, row in enumerate(rows):
        for position, cell in enumerate(row):
            result.append({"block": block, "position": position, "cell": cell.key, "predecessor": previous,
                "predecessor_within_block": row[position - 1].key if position else None})
            previous = cell.key
    return result


def parse_args(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, allow_abbrev=False)
    for field in ("executable", "runtime", "logserver", "source-manifest", "output-directory"):
        parser.add_argument("--" + field, type=Path, required=True)
    parser.add_argument("--namesym", type=Path)
    parser.add_argument("--blocks", type=int, default=8)
    parser.add_argument("--order-seed", type=int, default=0)
    parser.add_argument("--warmup-minimum-seconds", type=float, default=30)
    parser.add_argument("--warmup-intervals", type=int, default=2)
    parser.add_argument("--warmup-minimum-frames", type=int, default=100)
    parser.add_argument("--sample-intervals", type=int, default=12)
    parser.add_argument("--minimum-frames", type=int, default=100)
    parser.add_argument("--timeout", type=float, default=150)
    parser.add_argument("--stall-threshold-ms", type=float, default=1000)
    parser.add_argument("--plan-only", action="store_true")
    args = parser.parse_args(argv)
    if args.blocks < 8 or args.blocks % len(CELLS) or args.sample_intervals < 12 or args.warmup_intervals < 2 \
        or args.warmup_minimum_frames < 100 or args.minimum_frames < 100:
        parser.error("require >=8 blocks in complete four-treatment Williams cycles, >=12 retained reports, >=2 warmup reports, and >=100 warmup/measured GPU frames")
    if not math.isfinite(args.warmup_minimum_seconds) or args.warmup_minimum_seconds < 30 \
        or any(not math.isfinite(value) or value <= 0 for value in (args.timeout, args.stall_threshold_ms)):
        parser.error("require finite >=30 s target-only warmup and finite positive timeout/stall threshold")
    for name, value in vars(args).copy().items():
        if isinstance(value, Path):
            setattr(args, name, value.resolve())
    args.baseline_scheduling = "tiled"
    args.telemetry_factory = WindowsGpuTelemetry
    return args


def runner_identity():
    directory = Path(__file__).resolve().parent
    return {name: file_identity(directory / name) for name in RUNNER_SOURCES}


def create_plan(args, arm, frozen):
    sequence = order_plan(orders(args.blocks, args.order_seed))
    positions = Counter((trial["cell"], trial["position"]) for trial in sequence)
    preceding = Counter((trial["predecessor_within_block"], trial["cell"]) for trial in sequence if trial["predecessor_within_block"] is not None)
    inventory = host_inventory()
    no_clock_source = not inventory["gpu_clock_counter_candidates"] and inventory["nvidia_smi"] is None
    return {"created_utc": datetime.now(timezone.utc).isoformat(), "executable": str(arm.executable), "runtime": str(arm.runtime), "identity": frozen,
        "host_inventory": inventory,
        "runner_identity": runner_identity(), "cells": [{"key": cell.key, "extent": cell.extent, "coverage": cell.density, "route": cell.route} for cell in CELLS],
        "blocks": args.blocks, "trial_count": len(sequence), "sequence": sequence, "order_seed": args.order_seed,
        "position_counts": {f"{cell}/position_{position}": count for (cell, position), count in positions.items()},
        "within_block_preceding_counts": {f"{before} -> {after}": count for (before, after), count in preceding.items()},
        "sample_intervals": args.sample_intervals, "warmup_intervals": args.warmup_intervals, "warmup_minimum_frames": args.warmup_minimum_frames,
        "warmup_minimum_seconds": args.warmup_minimum_seconds, "minimum_measured_gpu_frames": args.minimum_frames,
        "minimum_interval_runtime_seconds": len(sequence) * (args.warmup_minimum_seconds + args.sample_intervals * .5),
        "telemetry": {"period_seconds": 1, "provider": "persistent Windows PDH GPU Engine query", "GPU_frequency_available": False,
            "frequency_limitation": "Host inventory found no GPU clock counter candidate or nvidia-smi. Load cannot establish clock state." if no_clock_source else "Clock candidates require separate supported instrumentation; this campaign does not collect frequency values. Load cannot establish clock state.",
            "GPU_temperature_available": False, "temperature_limitation": "ACPI zones are not identified as GPU temperatures."},
        "capture": False, "validation": False, "diagnostic_instrumentation": False,
        "policy": "One frozen renderer; four workloads, eight equal-weight independent blocks. Complete Williams cycles balance position and within-block preceding treatment. Cross-block predecessor is recorded. Fixed target-only warmup, no adaptive plateau or outlier removal, selective repeats, or favorable extension. Telemetry is observational and has approximate publication alignment; no clock/DVFS causal attribution. All raw reports retained. Coverage comparisons change the optical workload, so control movement is described rather than treated as a fixed-scene optimization A/B."}


def trial_metrics(trial):
    frames = trial["scopes"][density.FRAME]["gpu_samples"]
    return {**{scope: trial["scopes"][scope]["mean_ms"] for scope in METRICS},
        "effect_work_per_frame_ms": trial["total_effect_work_per_frame_ms"],
        "resolve_ms_per_million_admitted_pixels": trial["scopes"][density.RESOLVE]["total_ms"] / frames / trial["runtime_signature"]["fixture"]["admitted_pixels"] * 1e6}


def summarize(trials, plan):
    expected = {(row["block"], row["position"], row["cell"]) for row in plan["sequence"]}
    seen = [(trial["block"], trial["position"], trial["cell"]) for trial in trials]
    if len(seen) != len(set(seen)) or set(seen) != expected:
        raise density.SmokeFailure("workload profiling requires every planned trial exactly once")
    cells = {}
    for cell in CELLS:
        selected = [trial for trial in trials if trial["cell"] == cell.key]
        signature = selected[0]["runtime_signature"]
        if any(trial["runtime_signature"] != signature for trial in selected):
            raise density.SmokeFailure("profile cell device, material, coverage, or tracing route changed")
        metrics = [trial_metrics(trial) for trial in selected]
        cells[cell.key] = {"runtime_signature": signature, "blocks": len(selected), "metrics": {name: {
            "mean": statistics.fmean(row[name] for row in metrics), "minimum": min(row[name] for row in metrics), "maximum": max(row[name] for row in metrics),
            "block_values": [row[name] for row in metrics]} for name in metrics[0]},
            "telemetry_eligible": all(trial["telemetry"]["eligible"] for trial in selected), "wall_eligible": all(trial["wall_frame"]["eligible"] for trial in selected)}
    scaling = {}
    for extent in ("960x720", "1600x900"):
        pairs = {block: {trial["runtime_signature"]["fixture"]["requested_percent"]: trial for trial in trials
            if trial["block"] == block and trial["cell"].startswith(extent + "_")} for block in range(plan["blocks"])}
        scaling[extent] = {name: density.timing.paired_statistics([trial_metrics(pairs[block][100])[name] - trial_metrics(pairs[block][50])[name]
            for block in range(plan["blocks"])]) for name in trial_metrics(pairs[0][50])}
    by_position = {}
    for cell in CELLS:
        by_position[cell.key] = {str(position): {name: statistics.fmean(trial_metrics(trial)[name] for trial in trials if trial["cell"] == cell.key and trial["position"] == position)
            for name in (density.FRAME, density.RESOLVE, *density.CONTROLS)} for position in range(len(CELLS))}
    return {"acquisition_passed": True, "telemetry_eligible": all(trial["telemetry"]["eligible"] for trial in trials),
        "wall_eligible": all(trial["wall_frame"]["eligible"] for trial in trials), "cells": cells, "coverage_100_minus_50_paired_statistics": scaling,
        "position_means_ms": by_position, "plan": plan, "trials": trials,
        "interpretation": "Equal-weight block-level workload comparisons, not a renderer speedup. Individual 95% bootstrap intervals are not simultaneous family-wise intervals. Position/load associations are observational; the host provides no GPU clock telemetry."}


def run(args):
    arm = SimpleNamespace(name="candidate", scheduling="tiled", **{name: getattr(args, name) for name in ("executable", "runtime", "logserver", "namesym", "source_manifest")})
    frozen = density.freeze(arm)
    sources = frozen["source"]["files"]
    if any(not any(path.replace("\\", "/").endswith("/source/" + required) for path in sources) for required in density.FIXTURE_SOURCE_FILES):
        raise density.SmokeFailure("profile manifest lacks both compiled fixture source files")
    args.output_directory.mkdir(parents=True, exist_ok=False)
    plan = create_plan(args, arm, frozen)
    density.write(args.output_directory / "plan.json", plan)
    print(f"Plan: {plan['trial_count']} trials, >= {plan['minimum_interval_runtime_seconds'] / 60:.1f} minutes of target-only warmup and report intervals plus startup", flush=True)
    if args.plan_only:
        return 0
    trials, started = [], time.monotonic()
    try:
        by_key = {cell.key: cell for cell in CELLS}
        for row in plan["sequence"]:
            if density.freeze(arm) != frozen or runner_identity() != plan["runner_identity"]:
                raise density.SmokeFailure("frozen profile renderer or acquisition source changed before trial")
            cell = by_key[row["cell"]]
            print(f"Starting block {row['block'] + 1}/{args.blocks}, position {row['position'] + 1}/4: {cell.key}", flush=True)
            trial = density.acquire(args, arm, cell, row["block"], row["position"])
            trial.update(predecessor=row["predecessor"], predecessor_within_block=row["predecessor_within_block"], campaign_elapsed_seconds=time.monotonic() - started)
            trials.append(trial)
            density.write(args.output_directory / "trials.json", trials)
            if density.freeze(arm) != frozen or runner_identity() != plan["runner_identity"]:
                raise density.SmokeFailure("frozen profile renderer or acquisition source changed during trial")
            if not trial["telemetry"]["eligible"]:
                raise density.SmokeFailure("profile timing saved but passive GPU telemetry is incomplete")
        report = summarize(trials, plan)
        density.write(args.output_directory / "report.json", report)
        print("Completed: " + str(args.output_directory / "report.json"), flush=True)
        return 0
    except (density.SmokeFailure, density.SmokeSkip, OSError, ValueError, subprocess.SubprocessError) as error:
        density.write(args.output_directory / "failure.json", {"status": "incomplete_profile", "error": str(error), "completed_trials": len(trials), "all_acquired_trials_retained": True})
        print("FAIL/INCONCLUSIVE: " + str(error), file=sys.stderr)
        return 77 if isinstance(error, density.SmokeSkip) else 1


def main(argv=None):
    try:
        return run(parse_args(argv))
    except (density.SmokeFailure, density.SmokeSkip, OSError, ValueError, subprocess.SubprocessError) as error:
        print("FAIL/INCONCLUSIVE: " + str(error), file=sys.stderr)
        return 77 if isinstance(error, density.SmokeSkip) else 1


if __name__ == "__main__":
    raise SystemExit(main())
