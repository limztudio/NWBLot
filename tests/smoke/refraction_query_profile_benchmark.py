#!/usr/bin/env python3
"""Profile frozen, uninstrumented hardware refraction with balanced workload order.

Four density/extent treatments use complete Williams cycles, fixed target-only
warmup, and passive host engine-load observations. This is workload attribution,
not an optimization A/B comparison. Diagnostic counter acquisition is separate.
"""

import argparse
import math
from pathlib import Path
import subprocess
import sys
import time
from types import SimpleNamespace

import refraction_density_benchmark as density
from gpu_load_telemetry import WindowsGpuTelemetry
from refraction_profile_results import ProfileResultAnalysis
from refraction_profile_plan import CELLS, RefractionProfilePlan
from smoke_volume_identity import file_identity

RUNNER_SOURCES = ("refraction_query_profile_benchmark.py", "refraction_density_benchmark.py", "gpu_load_telemetry.py", "gpu_load_analysis.py", "gpu_load_host.py", "refraction_profile_results.py", "refraction_profile_plan.py", "window_capture_termination.py", "window_capture_frame.py", "gpu_timing_evidence.py", "reflection_benchmark.py", "reflection_workload_qualification.py", "reflection_gpu_coverage.py", "reflection_experiment_analysis.py", "renderer_ab_benchmark.py", "renderer_wall_frame_evidence.py", "renderer_workload_log_policy.py", "renderer_workload_registry.py", "renderer_trial_comparison.py", "experiment_statistics.py")






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








def run(args):
    arm = SimpleNamespace(name="candidate", scheduling="tiled", **{name: getattr(args, name) for name in ("executable", "runtime", "logserver", "namesym", "source_manifest")})
    frozen = density.freeze(arm)
    sources = frozen["source"]["files"]
    if any(not any(path.replace("\\", "/").endswith("/source/" + required) for path in sources) for required in density.FIXTURE_SOURCE_FILES):
        raise density.SmokeFailure("profile manifest lacks both compiled fixture source files")
    args.output_directory.mkdir(parents=True, exist_ok=False)
    plan = RefractionProfilePlan.create_plan(args, arm, frozen, runner_identity)
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
        report = ProfileResultAnalysis.summarize(trials, plan, CELLS)
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
