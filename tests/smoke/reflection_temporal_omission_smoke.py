#!/usr/bin/env python3
"""Prove cap-one temporal omission and cap-sixteen retained recording with two GPU launches.

This validates completed timing ranges and the production recording contract. It
does not perform a statistical comparison or claim a frame-time improvement.
"""
from __future__ import annotations

import argparse
from dataclasses import asdict
from pathlib import Path
import subprocess
import sys
import tempfile

import reflection_benchmark as benchmark
from renderer_ab_benchmark import binary_identity, runtime_identity

LIT_VARIANT = "variant"
LIT_UTF_8 = "utf-8"
LIT_REPORTS = "reports"
LIT_GPU_SAMPLES = "gpu_samples"
LIT_COMPLETED_GPU_FRAMES = "completed_gpu_frames"
LIT_TESTS_SMOKE = "tests/smoke"
LIT_EXECUTABLE = "--executable"
LIT_WORKING_DIRECTORY = "--working-directory"
LIT_OUTPUT_DIRECTORY = "--output-directory"
LIT_LOGSERVER_EXECUTABLE = "--logserver-executable"
LIT_ROUGH = "rough"
LIT_TIMEOUT = "--timeout"
LIT_NAMESYM = "--namesym"
LIT_BINARIES = "binaries"
LIT_RUNTIME = "runtime"
LIT_LOGSERVER = "logserver"
LIT_SOURCE = "source"
LIT_EXECUTABLE_2 = "executable"
LIT_WORKING_DIRECTORY_2 = "working_directory"
LIT_LOGSERVER_EXECUTABLE_2 = "logserver_executable"
LIT_REPORT_JSON = "report.json"
LIT_MAIN = "__main__"


CAPS = (1, 16)
VARIANT = benchmark.Variant("hardware_temporal", "hardware", temporal=True)


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


def validate_recording_evidence(trial, history_samples, symbols=None):
    """Reparse retained evidence and reject cap-one work even outside the measured window."""
    if history_samples not in CAPS or trial[LIT_VARIANT] != asdict(VARIANT):
        raise benchmark.SmokeFailure("unexpected temporal omission proof configuration")
    timing = Path(trial["artifacts"]) / "gpu_timing.txt"
    reports = benchmark.parse_intervals(timing.read_text(encoding=LIT_UTF_8), symbols, finalized=True)
    start, end = trial["retained_report_range"]
    if not 0 <= start < end <= len(reports) or end - start != trial[LIT_REPORTS]:
        raise benchmark.SmokeFailure("retained timing report range is inconsistent")
    retained = benchmark.summarize_intervals(reports[start:end])
    if retained != trial["scopes"]:
        raise benchmark.SmokeFailure("raw timing reports differ from the retained trial summary")
    benchmark.validate_coverage(retained, VARIANT, trial[LIT_REPORTS], 100, trial["mip_count"], history_samples)
    frames = retained[benchmark.FRAME][LIT_GPU_SAMPLES]
    # The benchmark already verifies reflection kernels. This proof also rejects
    # selective loss of the common control and presentation timing ranges.
    for scope in benchmark.OBSERVED_CONTROLS:
        if abs(retained[scope][LIT_GPU_SAMPLES] - frames) > max(2, frames * .02):
            raise benchmark.SmokeFailure("control scope coverage differs from completed GPU frames: " + scope)
    all_temporal = sum(report[benchmark.TEMPORAL].gpu_samples for report in reports if benchmark.TEMPORAL in report)
    if history_samples == 1 and all_temporal:
        raise benchmark.SmokeFailure("cap-one temporal work appeared during warm-up, acquisition, or shutdown")
    return {"history_samples": history_samples, LIT_COMPLETED_GPU_FRAMES: frames,
        "retained_temporal_ranges": retained.get(benchmark.TEMPORAL, {}).get(LIT_GPU_SAMPLES, 0),
        "all_completed_temporal_ranges": all_temporal, "finalized_reports": len(reports),
        "expected_temporal_ranges_per_frame": int(history_samples > 1),
        "coverage_tolerance": "max(2 completed ranges, 2% of completed GPU frames)"}


def source_identity(namesym):
    root = Path(__file__).resolve().parents[2]
    files = {Path(__file__).resolve()}
    for module in ("reflection_benchmark", "renderer_ab_benchmark", "window_capture_smoke", "smoke_volume_identity",
        "gpu_timing_parse", "name_symbols"):
        files.add(Path(sys.modules[module].__file__).resolve())
    for relative in ("impl/ecs_render/reflection", "impl/assets/graphics/reflection"):
        files.update(path for path in (root / relative).rglob("*") if path.is_file())
    files.update((root / LIT_TESTS_SMOKE).glob("reflection_*.cpp"))
    files.update((root / LIT_TESTS_SMOKE).glob("reflection_*.h"))
    if namesym:
        files.add(namesym.resolve())
    return {str(path): benchmark.file_identity(path) for path in sorted(files)}


def acquisition_args(args, directory, cap):
    argv = [LIT_EXECUTABLE, str(args.executable), LIT_WORKING_DIRECTORY, str(args.working_directory),
        LIT_OUTPUT_DIRECTORY, str(directory), LIT_LOGSERVER_EXECUTABLE, str(args.logserver_executable),
        "--family", LIT_ROUGH, "--width", "960", "--height", "720", "--ray-budget", "1382400",
        "--optical-queries", "16", "--screen-steps", "96", "--sampling-seed", "0", "--roughness", "0.4",
        "--history-samples", str(cap), "--warmup-intervals", "2", "--sample-intervals", "6",
        "--minimum-frame-samples", "100", LIT_TIMEOUT, str(args.timeout), "--require-hardware"]
    if args.namesym:
        argv.extend((LIT_NAMESYM, str(args.namesym)))
    return benchmark.parse_args(argv)


def run(args):
    args.output_directory.mkdir(parents=True, exist_ok=True)
    output = Path(tempfile.mkdtemp(prefix="temporal_omission_", dir=args.output_directory))
    benchmark.write_status("Temporal omission proof artifacts: " + str(output))
    trials = []
    try:
        frozen = {LIT_BINARIES: binary_identity(args.executable), LIT_RUNTIME: runtime_identity(args.working_directory),
            LIT_LOGSERVER: benchmark.file_identity(args.logserver_executable), LIT_SOURCE: source_identity(args.namesym)}
        executable_identity = benchmark.file_identity(args.executable)
        symbols = benchmark.load_name_symbols(args.namesym, benchmark.KNOWN_SCOPES)
        plan = {"purpose": "native temporal recording omission and active-history control; no timing inference",
            LIT_EXECUTABLE_2: str(args.executable), LIT_WORKING_DIRECTORY_2: str(args.working_directory),
            LIT_LOGSERVER_EXECUTABLE_2: str(args.logserver_executable), "frozen": frozen,
            LIT_VARIANT: asdict(VARIANT), "history_sample_caps": list(CAPS), "family": LIT_ROUGH,
            "dimensions": [960, 720], "roughness": .4, "sampling_seed": 0, "ray_budget": 1382400,
            "optical_queries": 16, "screen_steps": 96, "diagnostics": False, "capture": False,
            "spatial": False, "feedback": False, "fixed_delta_seconds": .016666667,
            "warmup_reports": 2, "minimum_retained_reports": 6, "minimum_gpu_frames": 100,
            "timeout_per_trial_seconds": args.timeout, "timing_in_flight_ranges": benchmark.TIMING_IN_FLIGHT_RANGES,
            "artifact_policy": "Every invocation gets a unique directory; failed launches and all raw reports are retained."}
        benchmark.write_json(output / "plan.json", plan)

        def verify_frozen():
            actual = {LIT_BINARIES: binary_identity(args.executable), LIT_RUNTIME: runtime_identity(args.working_directory),
                LIT_LOGSERVER: benchmark.file_identity(args.logserver_executable), LIT_SOURCE: source_identity(args.namesym)}
            if actual != frozen:
                raise benchmark.SmokeFailure("source, executable, dependency, authored asset, symbol, or logserver identity changed")

        evidence = []
        for position, cap in enumerate(CAPS):
            verify_frozen()
            acquisition = acquisition_args(args, output, cap)
            benchmark.write_status(f"Temporal omission proof {position + 1}/2: temporal enabled, history cap {cap}")
            try:
                trial = benchmark.run_trial(acquisition, VARIANT, 0, position, symbols, executable_identity)
            except benchmark.SmokeFailure as error:
                log_path = output / f"block_00_{position:02d}_{VARIANT.name}" / "runtime.log"
                # Only an acquisition failure can become a capability skip. Preserve
                # launch, process-exit, parsing, identity, and validation failures.
                acquisition_failure = str(error).startswith("GPU benchmark timed out:") \
                    or str(error) == "hardware was unavailable for a hardware benchmark route"
                if acquisition_failure and log_path.is_file() and not (log_path.parent / "cleanup_error.txt").exists() \
                    and hardware_unavailable_without_errors(log_path.read_text(encoding=LIT_UTF_8)):
                    verify_frozen()
                    raise benchmark.SmokeSkip("hardware ray queries are unavailable; temporal omission requires the hardware route") from error
                raise
            verify_frozen()
            trials.append(trial)
            benchmark.write_json(output / "trials.json", trials)
            evidence.append(validate_recording_evidence(trial, cap, symbols))
        benchmark.write_json(output / LIT_REPORT_JSON, {"passed": True, "plan": plan, "evidence": evidence,
            LIT_COMPLETED_GPU_FRAMES: sum(item[LIT_COMPLETED_GPU_FRAMES] for item in evidence),
            "interpretation": "Cap one omitted all completed temporal ranges; cap sixteen retained expected coverage after warm-up. No speed comparison was performed."})
        benchmark.write_status("Temporal omission proof passed: " + str(output / LIT_REPORT_JSON))
        return 0
    except (benchmark.SmokeFailure, benchmark.SmokeSkip, OSError, ValueError, KeyError, subprocess.SubprocessError) as error:
        benchmark.write_json(output / "failure.json", {"error": str(error), "completed_trials": len(trials)})
        raise


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, allow_abbrev=False)
    parser.add_argument(LIT_EXECUTABLE, type=Path, required=True)
    parser.add_argument(LIT_WORKING_DIRECTORY, type=Path, required=True)
    parser.add_argument(LIT_LOGSERVER_EXECUTABLE, type=Path, required=True)
    parser.add_argument(LIT_OUTPUT_DIRECTORY, type=Path, required=True)
    parser.add_argument(LIT_NAMESYM, type=Path)
    parser.add_argument(LIT_TIMEOUT, type=float, default=90)
    args = parser.parse_args(argv)
    for field in (LIT_EXECUTABLE_2, LIT_WORKING_DIRECTORY_2, LIT_LOGSERVER_EXECUTABLE_2, "output_directory", "namesym"):
        if getattr(args, field) is not None:
            setattr(args, field, getattr(args, field).resolve())
    # Reuse the production CLI's finite-value validation before creating an artifact directory.
    acquisition_args(args, args.output_directory, CAPS[0])
    try:
        return run(args)
    except benchmark.SmokeSkip as error:
        benchmark.write_status("SKIP: " + str(error))
        return benchmark.SKIP_EXIT_CODE
    except (benchmark.SmokeFailure, OSError, ValueError, KeyError, subprocess.SubprocessError) as error:
        benchmark.write_status("FAIL: " + str(error))
        return 1


if __name__ == LIT_MAIN:
    sys.exit(main())
