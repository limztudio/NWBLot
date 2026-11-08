#!/usr/bin/env python3
"""Compare frozen full-frame or tiled reference against tiled refraction.

Acquisition never reads the framebuffer. Four balanced blocks are descriptive;
six or more can additionally use the shared paired bootstrap interval helper.
"""

import argparse
from dataclasses import dataclass, replace
import itertools
import math
from pathlib import Path
import subprocess
import sys
import time
from types import SimpleNamespace

import renderer_ab_benchmark as ab
from renderer_workload_registry import Workload
from renderer_wall_frame_evidence import RendererWallFrameEvidence
from renderer_workload_log_policy import RendererWorkloadLogPolicy, DIMENSIONS
from renderer_benchmark_identity import RendererBenchmarkIdentity
from gpu_timing_evidence import GpuTimingEvidence, HEADER
from gpu_timing_parse import load_name_symbols
from refraction_density_evidence import RefractionDensityEvidence
from refraction_density_comparison import (RefractionDensityComparison, FRAME, RESOLVE, CLASSIFY, CLEAR_ARGUMENTS, COMPOSITE,
    CONTROLS, OBSERVATIONS, AVBOIT, OBSERVED, EFFECT_COST)
from smoke_volume_identity import file_identity
from window_capture_termination import CaptureProcessTermination
from window_capture_smoke import (SmokeFailure, SmokeSkip, STRICT_LOG_FAILURE_MESSAGES, build_launch_environment, create_capture_backend, ensure_process_running,
    collect_log_delta, launch_logserver, launch_testbed, require_normal_process_exit, shutdown_logserver_and_collect, write_status)
from window_capture_validation import validate_expected_log_text

EXTENTS = ("960x720", "1600x900", "1920x1080")
DEFAULT_EXTENTS = ("960x720", "1600x900")
DENSITIES = (0, 1, 10, 50, 100)
ROUTES = ("screen", "hardware")
SCHEDULING_STRATEGIES = ("full-frame", "tiled")
# Compiled C++ sources that define the refraction density fixture.
FIXTURE_SOURCE_FILES = ("tests/smoke/refraction_project.cpp", "tests/smoke/avboit_timing_render_pass.cpp")


@dataclass(frozen=True)
class Cell:
    extent: str
    density: int
    route: str

    @property
    def width(self):
        return int(self.extent.split("x")[0])

    @property
    def height(self):
        return int(self.extent.split("x")[1])

    @property
    def key(self):
        return f"{self.extent}_{self.density:03d}pct_{self.route}"


def write(path, value):
    ab.write_json(path, value)


def scheduling_for(arm):
    strategy = getattr(arm, "scheduling", "tiled" if arm.name == "candidate" else "full-frame")
    if strategy not in SCHEDULING_STRATEGIES or arm.name == "candidate" and strategy != "tiled":
        raise SmokeFailure("invalid density arm scheduling strategy: " + str(strategy))
    return strategy




def workload_for(arm, cell):
    tiled = scheduling_for(arm) == "tiled"
    scopes = (FRAME, RESOLVE, *CONTROLS, *OBSERVATIONS, *AVBOIT) + ((CLASSIFY, CLEAR_ARGUMENTS) if tiled else ())
    return Workload("refraction-density-" + cell.key, cell.width, cell.height, tuple((scope, 1) for scope in scopes), RESOLVE,
        (("NWB_REFRACTION_SMOKE_TIMING", "1"), ("NWB_REFRACTION_SMOKE_HARDWARE", "1" if cell.route == "hardware" else "0"), ("NWB_REFRACTION_SMOKE_ENABLED", "1"),
            ("NWB_REFRACTION_SMOKE_DENSITY", str(cell.density)), ("NWB_REFRACTION_SMOKE_EXTENT", cell.extent),
            ("NWB_RENDERER_BASELINE_FIXED_DELTA_SECONDS", "0.016666667")), None,
        inactive_scopes=() if tiled else (CLASSIFY, CLEAR_ARGUMENTS), control_scopes=CONTROLS)


def freeze(arm):
    return {"binaries": RendererBenchmarkIdentity.binary_identity(arm.executable), "runtime": RendererBenchmarkIdentity.runtime_identity(arm.runtime), "logserver": file_identity(arm.logserver),
        "namesym": file_identity(arm.namesym) if arm.namesym else None, "source": RendererBenchmarkIdentity.source_identity(arm.source_manifest)}




def prepare_target_extent(backend, handle, process, cell, read_log, timeout):
    target = (cell.width, cell.height)
    initial = tuple(backend.client_size(handle))
    resized = initial != target
    started = time.monotonic()
    if resized:
        backend.resize_client(handle, *target, allow_outside_work_area=True)
    marker = f"GraphicsRuntime: Back buffer resized to {cell.width}x{cell.height}"
    deadline = started + timeout
    while time.monotonic() < deadline:
        ensure_process_running(process, "while confirming requested density framebuffer extent")
        text = read_log()
        dimensions = [(int(x), int(y)) for x, y in DIMENSIONS.findall(text)]
        if tuple(backend.client_size(handle)) == target and dimensions and dimensions[-1] == target and (not resized or marker in text):
            evidence = RefractionDensityEvidence.validate_extent_history(text, cell)
            return {**evidence, "initial_client_extent": list(initial), "resize_requested": resized, "allow_outside_work_area": resized, "confirmation_seconds": time.monotonic() - started}
        time.sleep(.1)
    raise SmokeFailure("requested density client/framebuffer extent was not confirmed before startup deadline")


def retain_target_reports(reports, startup_headers, warmup_intervals, minimum_warmup_frames, minimum_warmup_seconds=0, report_durations=None):
    # Discard headers present at target confirmation because the last report is still unsealed.
    relative, retained = GpuTimingEvidence().retain_after_warmup(reports[startup_headers:], warmup_intervals, minimum_warmup_frames)
    start = startup_headers + relative
    if minimum_warmup_seconds:
        if report_durations is None or len(report_durations) < len(reports) or any(not math.isfinite(value) or value <= 0 for value in report_durations[:len(reports)]):
            raise SmokeFailure("elapsed target warmup requires positive durations for every sealed report")
        seconds = sum(report_durations[startup_headers:start])
        while start < len(reports) and seconds < minimum_warmup_seconds:
            seconds += report_durations[start]
            start += 1
        retained = reports[start:] if seconds >= minimum_warmup_seconds else []
    return start, retained


def startup_report_boundary(text):
    # Require one extra sealed header in case the file was read mid-write.
    return len(list(HEADER.finditer(text))) + 1


def validate_log(text, cell):
    expected_route = "hardware" if cell.route == "hardware" else "screen-space"
    forbidden_route = "screen-space" if cell.route == "hardware" else "hardware"
    validate_expected_log_text(text, ["RefractionSmokeProject: shutdown", "AvboitTimingProbe: in-flight ranges 32", "AvboitTimingProbe: render unfocused 1",
        "RefractionSmokeProject: refraction enabled", "Loader: project startup complete", "AVBOIT refraction resolve: " + expected_route], [*STRICT_LOG_FAILURE_MESSAGES, "FramebufferCapture:", "RefractionLifecycle:",
        "AVBOIT refraction resolve: " + forbidden_route, "VK_LAYER_KHRONOS_validation", "Vulkan: enabled validation layer", "render submission suspended"])
    RefractionDensityEvidence.validate_extent_history(text, cell)
    return {**RendererWorkloadLogPolicy.device_material_signature(text), "fixture": RefractionDensityEvidence.validate_fixture(text, cell), "trace_route": cell.route}


def wall_frames(log, sidecar, workload, start, stop, stall_threshold_ms):
    # Reuse the shared FpsProbe window alignment and consistency parser.
    normalized = log.replace("RefractionSmokeProject: fps ", "ReflectionSmokeProject: fps ")
    try:
        result = RendererWallFrameEvidence.summarize_wall_frames(normalized, sidecar, replace(workload, reflection_policy=True), [start, stop])
        result["source"] = "RefractionSmokeProject FpsProbe callback delta"
        result["policy"] += "; reject retained callback maxima above the declared stall threshold"
        result["stall_threshold_ms"] = stall_threshold_ms
        if result["max_frame_ms"] > stall_threshold_ms:
            result["eligible"] = False
            result["reason"] = "retained callback stall exceeds threshold; wall comparison rejected, raw GPU and wall reports retained"
        return result
    except SmokeFailure as error:
        return {"eligible": False, "reason": str(error), "source": "RefractionSmokeProject FpsProbe callback delta",
            "interpretation": "FPS/GPU report alignment failed; no wall-frame comparison is inferred."}


def launch_arguments(args, arm):
    return SimpleNamespace(no_logserver=False, logserver_executable=arm.logserver, log_port=0, working_directory=arm.runtime,
        timeout=args.timeout, application_arg=[], software_vulkan="off")


def acquire(args, arm, cell, block, position):
    expected_strategy = args.baseline_scheduling if arm.name == "baseline" else "tiled"
    if scheduling_for(arm) != expected_strategy:
        raise SmokeFailure("density acquisition arm strategy differs from the declared comparison")
    directory = args.output_directory / cell.key / f"block_{block:02d}_{position:02d}_{arm.name}"
    directory.mkdir(parents=True, exist_ok=False)
    sidecar = directory / "gpu_timing.txt"
    workload = workload_for(arm, cell)
    symbols = load_name_symbols(arm.namesym, OBSERVED)
    timing_evidence = GpuTimingEvidence(symbols)
    launch = launch_arguments(args, arm)
    env, overrides = ab.configure_environment(build_launch_environment(launch), workload, sidecar)
    for name in ("NWB_SMOKE_FRAMEBUFFER_CAPTURE_PATH", "NWB_SMOKE_FRAMEBUFFER_CAPTURE_FRAME_COUNT", "NWB_REFRACTION_FRAME_GRAPH_SNAPSHOT_PATH"):
        env.pop(name, None)
    write(directory / "launch.json", {"arm": arm.name, "scheduling": scheduling_for(arm), "baseline_scheduling": args.baseline_scheduling,
        "reference_contract": RefractionDensityComparison.reference_contract(args.baseline_scheduling), "block": block, "position": position, "executable": str(arm.executable), "runtime": str(arm.runtime),
        "environment_overrides": overrides, "validation": False, "capture": False, "diagnostic_instrumentation": False, "cell": cell.key, "trace_route": cell.route, "cache_before": RendererBenchmarkIdentity.cache_identity(arm.runtime)})
    backend = logserver = process = handle = log_directory = telemetry = None
    log_baseline, pattern = {}, ""
    selected = None
    collected = False
    started = time.monotonic()
    window_seconds = first_report_seconds = retained_ready_seconds = None
    try:
        backend = create_capture_backend()
        logserver, port, log_directory, log_baseline, pattern = launch_logserver(launch, arm.executable, env)
        process = launch_testbed(launch, arm.executable, env, port)
        telemetry_factory = getattr(args, "telemetry_factory", None)
        if telemetry_factory is not None:
            telemetry = telemetry_factory(directory, process.pid)
        handle = backend.wait_for_window(process.pid, min(30, args.timeout))
        if handle is None:
            raise SmokeFailure("density timing window did not become visible before the startup deadline")
        window_seconds = time.monotonic() - started
        if handle:
            try:
                backend.focus_window(handle)
            except Exception as error:
                write_status(f"could not focus benchmark window: {error}")
        startup_resize = prepare_target_extent(backend, handle, process, cell, lambda: collect_log_delta(log_directory, log_baseline, pattern), args.timeout)
        target_confirmed_seconds = time.monotonic() - started
        startup_text = sidecar.read_text(encoding="utf-8") if sidecar.is_file() else ""
        startup_headers = startup_report_boundary(startup_text)
        write(directory / "startup.json", {"resize": startup_resize, "target_confirmed_after_launch_seconds": target_confirmed_seconds, "discarded_startup_report_headers": startup_headers,
            "boundary_policy": "existing complete headers plus one sealing boundary guard against a concurrently partial header"})
        deadline = time.monotonic() + args.timeout
        problem = "timing reports have not arrived"
        while time.monotonic() < deadline:
            ensure_process_running(process, "during continuous refraction timing")
            if sidecar.is_file():
                timing_text = sidecar.read_text(encoding="utf-8")
                reports = timing_evidence.parse_intervals(timing_text)
                report_durations = [float(match[2]) for match in HEADER.finditer(timing_text)]
                if telemetry is not None:
                    telemetry.observe_timing_headers(report_durations)
                if len(reports) > startup_headers and first_report_seconds is None:
                    first_report_seconds = time.monotonic() - started
                retained_start, warmed = retain_target_reports(reports, startup_headers, args.warmup_intervals, args.warmup_minimum_frames,
                    getattr(args, "warmup_minimum_seconds", 0), report_durations)
                retained = warmed[:args.sample_intervals]
                problem = (f"{len(reports)} sealed reports; {len(retained)}/{args.sample_intervals} retained reports, "
                    f"{timing_evidence.positive_frame_reports(retained)} positive; {startup_headers} startup headers and {retained_start - startup_headers} target warmup reports discarded")
                if len(retained) == args.sample_intervals:
                    try:
                        summary = timing_evidence.summarize_intervals(retained)
                        ab.validate_coverage(summary, workload, len(retained), args.minimum_frames)
                        if timing_evidence.positive_frame_reports(retained) < args.sample_intervals:
                            raise SmokeFailure("retained reports lack the requested positive GPU frame coverage")
                        selected = retained
                        retained_ready_seconds = time.monotonic() - started
                        break
                    except SmokeFailure as error:
                        problem = str(error)
            time.sleep(.1)
        if selected is None:
            raise SmokeFailure("inconclusive timing acquisition: " + problem)
        telemetry_result = telemetry.finish(retained_start, retained_start + len(selected), report_durations) if telemetry is not None else None
        code, tail = CaptureProcessTermination.terminate_process(process, "refraction timing", handle)
        process = None
        require_normal_process_exit(code, tail, "refraction timing")
        (directory / "process_tail.txt").write_text(tail, encoding="utf-8")
        log = shutdown_logserver_and_collect(logserver, log_directory, log_baseline, pattern)
        logserver, collected = None, True
        (directory / "runtime.log").write_text(log, encoding="utf-8")
        signature = validate_log(log, cell)
        text = sidecar.read_text(encoding="utf-8")
        finalized = timing_evidence.parse_intervals(text, finalized=True)
        start, stop = retained_start, retained_start + len(selected)
        summary = timing_evidence.summarize_intervals(finalized[start:stop])
        ab.validate_coverage(summary, workload, len(selected), args.minimum_frames)
        gpu_frames = summary[FRAME]["gpu_samples"]
        retained_durations = [float(match[2]) for match in list(HEADER.finditer(text))[start:stop]]
        wall = wall_frames(log, text, workload, start, stop, args.stall_threshold_ms)
        result = {"arm": arm.name, "scheduling": scheduling_for(arm), "baseline_scheduling": args.baseline_scheduling, "reference_contract": RefractionDensityComparison.reference_contract(args.baseline_scheduling),
            "block": block, "position": position, "cell": cell.key, "trace_route": cell.route, "reports": len(selected), "positive_frame_reports": timing_evidence.positive_frame_reports(selected),
            "retained_report_range": [start, stop], "scopes": summary, "runtime_signature": signature, "wall_frame": wall, "acquisition_clean": wall["eligible"],
            "refraction_work_per_frame_ms": sum(summary.get(scope, {}).get("total_ms", 0) for scope in (CLASSIFY, RESOLVE)) / gpu_frames,
            "total_effect_work_per_frame_ms": sum(summary.get(scope, {}).get("total_ms", 0) for scope in EFFECT_COST) / gpu_frames,
            "combined_work_policy": RefractionDensityComparison.combined_work_policy(args.baseline_scheduling),
            "acquisition": {"window_after_launch_seconds": window_seconds, "first_sealed_report_after_launch_seconds": first_report_seconds,
                "target_confirmed_after_launch_seconds": target_confirmed_seconds, "startup_resize": startup_resize, "discarded_startup_report_headers": startup_headers,
                "measurement_ready_after_launch_seconds": retained_ready_seconds, "retained_gpu_interval_seconds": sum(retained_durations),
                "retained_interval_durations_seconds": retained_durations, "maximum_retained_interval_seconds": max(retained_durations),
                "warmup_minimum_seconds": getattr(args, "warmup_minimum_seconds", 0), "discarded_target_warmup_seconds": sum(float(match[2]) for match in list(HEADER.finditer(text))[startup_headers:start]),
                "startup_policy": "confirm actual target client and renderer extent, discard all existing report headers including the unsealed tail, then satisfy report, elapsed interval, and GPU-frame warmup minima using only new target reports"},
            "cache_after": RendererBenchmarkIdentity.cache_identity(arm.runtime), "artifacts": str(directory)}
        if telemetry_result is not None:
            result["telemetry"] = telemetry_result
        write(directory / "summary.json", result)
        return result
    finally:
        active_error = sys.exc_info()[0] is not None
        failures = []
        if telemetry is not None:
            try:
                telemetry.close()
            except Exception as error:
                failures.append("telemetry cleanup: " + str(error))
        try:
            CaptureProcessTermination.terminate_process(process, "refraction timing", handle)
        except Exception as error:
            failures.append(str(error))
        if log_directory is not None and not collected:
            try:
                log = shutdown_logserver_and_collect(logserver, log_directory, log_baseline, pattern)
                logserver = None
                (directory / "runtime.log").write_text(log, encoding="utf-8")
            except Exception as error:
                failures.append(str(error))
        try:
            CaptureProcessTermination.terminate_process(logserver, "logserver")
        except Exception as error:
            failures.append(str(error))
        if backend is not None:
            try:
                backend.close()
            except Exception as error:
                failures.append(str(error))
        if failures:
            detail = "\n".join(failures)
            write_status(detail)
            try:
                (directory / "cleanup_error.txt").write_text(detail, encoding="utf-8")
            except OSError as error:
                write_status(f"could not write cleanup error artifact: {error}")
            if not active_error:
                raise SmokeFailure("; ".join(failures))



def parse_args(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    for arm in ("baseline", "candidate"):
        for field in ("executable", "runtime", "logserver", "source-manifest"):
            parser.add_argument(f"--{arm}-{field}", required=True, type=Path)
        parser.add_argument(f"--{arm}-namesym", type=Path)
    parser.add_argument("--output-directory", required=True, type=Path)
    parser.add_argument("--baseline-scheduling", choices=SCHEDULING_STRATEGIES, default="full-frame", help="Full-frame forbids baseline tile clear/classification; tiled requires those scopes on both arms for a scheduling-only comparison.")
    parser.add_argument("--blocks", type=int, default=4, help="Even balanced block count; 2 is exploratory, 4 is the default, >=6 enables paired bootstrap intervals.")
    parser.add_argument("--sample-intervals", type=int, default=12)
    parser.add_argument("--warmup-intervals", type=int, default=2)
    parser.add_argument("--warmup-minimum-frames", type=int, default=100)
    parser.add_argument("--warmup-minimum-seconds", type=float, default=0, help="Additionally discard this many seconds of sealed, target-only publication intervals before measuring.")
    parser.add_argument("--minimum-frames", type=int, default=100)
    parser.add_argument("--timeout", type=float, default=120)
    parser.add_argument("--stall-threshold-ms", type=float, default=1000, help="Reject wall callback timing if a retained frame exceeds this threshold; retain raw evidence and GPU summaries.")
    parser.add_argument("--require-clean-wall", action="store_true", help="Stop with a saved failure after any rejected wall timing trial.")
    parser.add_argument("--extent", action="append", choices=EXTENTS, help="Repeat to select extents; omission selects 960x720 and 1600x900. Explicit 1920x1080 requires a host whose target allocations are supported.")
    parser.add_argument("--coverage", action="append", type=int, choices=DENSITIES, help="Repeat to select analytical density percentages; omission selects all five.")
    parser.add_argument("--trace-route", action="append", choices=ROUTES, help="Repeat to select tracing routes; omission selects both.")
    parser.add_argument("--plan-only", action="store_true", help="Validate and freeze input identities and write the matrix plan without launching a process.")
    args = parser.parse_args(argv)
    if args.blocks < 2 or args.blocks % 2 or args.sample_intervals < 12 or args.warmup_intervals < 2 or args.minimum_frames < 1 or args.warmup_minimum_frames < 1 \
        or not math.isfinite(args.timeout) or args.timeout <= 0 or not math.isfinite(args.stall_threshold_ms) or args.stall_threshold_ms <= 0 \
        or not math.isfinite(args.warmup_minimum_seconds) or args.warmup_minimum_seconds < 0:
        parser.error("use an even block count >=2, >=12 measured intervals, >=2 warmup intervals, positive frame minima, finite positive timeout and stall threshold")
    for field, defaults in (("extent", DEFAULT_EXTENTS), ("coverage", DENSITIES), ("trace_route", ROUTES)):
        values = getattr(args, field) or list(defaults)
        if len(values) != len(set(values)):
            parser.error("duplicate matrix selection: " + field)
        setattr(args, field, values)
    args.output_directory = args.output_directory.resolve()
    return args


def require_same_fixture_sources(identities):
    fixtures = {}
    for arm, identity in identities.items():
        sources = {path.replace("\\", "/").split("/source/")[-1]: digest for path, digest in identity["source"]["files"].items()}
        missing = [path for path in FIXTURE_SOURCE_FILES if path not in sources]
        if missing:
            raise SmokeFailure("source manifest lacks compiled density fixture instrumentation: " + ", ".join(missing))
        fixtures[arm] = {path: sources[path] for path in FIXTURE_SOURCE_FILES}
    if fixtures["baseline"] != fixtures["candidate"]:
        raise SmokeFailure("frozen arms contain different refraction fixture instrumentation")
    return fixtures["candidate"]


def balanced_orders(blocks, cell_index=0):
    return [("baseline", "candidate") if (block + cell_index) % 2 == 0 else ("candidate", "baseline") for block in range(blocks)]


def run(args):
    arms = {name: SimpleNamespace(name=name, scheduling=args.baseline_scheduling if name == "baseline" else "tiled", **{field: getattr(args, f"{name}_{field}").resolve() if getattr(args, f"{name}_{field}") else None
        for field in ("executable", "runtime", "logserver", "namesym", "source_manifest")}) for name in ("baseline", "candidate")}
    if arms["baseline"].executable == arms["candidate"].executable or arms["baseline"].runtime == arms["candidate"].runtime:
        raise SmokeFailure("baseline and candidate must have distinct frozen executable and runtime paths")
    RendererBenchmarkIdentity.validate_arm_separation(tuple(arms.values()))
    frozen = {name: freeze(arm) for name, arm in arms.items()}
    fixture_sources = require_same_fixture_sources(frozen)
    cells = [Cell(extent, density, route) for extent, density, route in itertools.product(args.extent, args.coverage, args.trace_route)]
    args.output_directory.mkdir(parents=True, exist_ok=False)
    trials = []
    reports = {}
    try:
        trial_count = len(cells) * args.blocks * 2
        plan = {"cells": [{"key": cell.key, "extent": cell.extent, "density": cell.density, "route": cell.route, "orders": balanced_orders(args.blocks, index)} for index, cell in enumerate(cells)],
            "identities": frozen, "fixture_sources": fixture_sources, "blocks": args.blocks, "trial_count": trial_count, "minimum_interval_runtime_seconds": trial_count * (args.sample_intervals + args.warmup_intervals) * .5,
            "startup_estimate_seconds_per_trial": [5, 10], "capture": False, "validation": False, "sample_intervals": args.sample_intervals, "warmup_intervals": args.warmup_intervals,
            "warmup_minimum_frames": args.warmup_minimum_frames, "wall_stall_rejection_ms": args.stall_threshold_ms,
            "warmup_minimum_seconds": args.warmup_minimum_seconds,
            "baseline_scheduling": args.baseline_scheduling, "candidate_scheduling": "tiled", "reference_contract": RefractionDensityComparison.reference_contract(args.baseline_scheduling),
            "policy": "Balanced independent blocks; <6 blocks descriptive only; no CPU, presentation-cadence, portable, or arbitrary-scene performance inference."}
        write(args.output_directory / "plan.json", plan)
        print(f"Plan: {len(cells)} cells, {trial_count} trials; >= {plan['minimum_interval_runtime_seconds'] / 60:.1f} minutes of report intervals plus startup/cache preparation", flush=True)
        if args.plan_only:
            return 0
        for cell_index, cell in enumerate(cells):
            cell_trials = []
            for block, order in enumerate(balanced_orders(args.blocks, cell_index)):
                for position, name in enumerate(order):
                    arm = arms[name]
                    if freeze(arm) != frozen[name]:
                        raise SmokeFailure(f"frozen arm changed before trial: {name}")
                    print(f"Starting {cell.key} block {block + 1}/{args.blocks}: {name}", flush=True)
                    trial = acquire(args, arm, cell, block, position)
                    trials.append(trial)
                    cell_trials.append(trial)
                    if freeze(arm) != frozen[name]:
                        raise SmokeFailure(f"frozen arm changed during trial: {name}")
                    write(args.output_directory / "trials.json", trials)
                    if args.require_clean_wall and not trial["wall_frame"]["eligible"]:
                        raise SmokeFailure("retained GPU evidence saved but contaminated or misaligned wall timing rejects this trial")
            signatures = [trial["runtime_signature"] for trial in cell_trials]
            if any(signature != signatures[0] for signature in signatures[1:]):
                raise SmokeFailure("device, material route, analytical coverage, or tracing route changed between frozen arms")
            reports[cell.key] = {"cell": cell.key, "trials": cell_trials, "acquisition_clean": all(trial["acquisition_clean"] for trial in cell_trials), "comparison": RefractionDensityComparison.comparison(cell_trials, args.blocks, args.baseline_scheduling)}
            write(args.output_directory / cell.key / "report.json", reports[cell.key])
        write(args.output_directory / "report.json", {"acquisition_passed": True, "acquisition_clean": all(trial["acquisition_clean"] for trial in trials), "cells": reports, "trials": trials, "plan": plan})
        print("Completed: " + str(args.output_directory / "report.json"), flush=True)
        return 0
    except (SmokeFailure, SmokeSkip, OSError, ValueError, subprocess.SubprocessError) as error:
        try:
            write(args.output_directory / "failure.json", {"status": "inconclusive_acquisition" if "inconclusive" in str(error) else "failed", "error": str(error), "completed_trials": len(trials)})
        except OSError as report_error:
            write_status(f"could not write failure artifact: {report_error}")
        print("FAIL/INCONCLUSIVE: " + str(error), file=sys.stderr)
        return 77 if isinstance(error, SmokeSkip) else 1


def main(argv=None):
    args = parse_args(argv)
    try:
        return run(args)
    except (SmokeFailure, SmokeSkip, OSError, ValueError, subprocess.SubprocessError) as error:
        print("FAIL/INCONCLUSIVE: " + str(error), file=sys.stderr)
        return 77 if isinstance(error, SmokeSkip) else 1


if __name__ == "__main__":
    raise SystemExit(main())
