#!/usr/bin/env python3
"""Compare frozen full-frame or tiled reference against tiled refraction.

Acquisition never reads the framebuffer. Four balanced blocks are descriptive;
six or more can additionally use the shared paired bootstrap interval helper.
"""

import argparse
from dataclasses import dataclass, replace
import itertools
import json
import math
from pathlib import Path
import re
import statistics
import subprocess
import sys
import time
from types import SimpleNamespace

import renderer_ab_benchmark as ab
import reflection_benchmark as timing
from gpu_timing_parse import load_name_symbols
from smoke_volume_identity import file_identity
from window_capture_smoke import (SmokeFailure, SmokeSkip, STRICT_LOG_FAILURE_MESSAGES, build_launch_environment, create_capture_backend, ensure_process_running,
    collect_log_delta, launch_logserver, launch_testbed, require_normal_process_exit, shutdown_logserver_and_collect, terminate_process, validate_expected_log_text)

FRAME = "render.frame"
RESOLVE = "render.avboit.refraction_resolve"
CLASSIFY = "render.avboit.refraction_classify_tiles"
CLEAR_ARGUMENTS = "render.avboit.refraction_tile_arguments_clear"
COMPOSITE = "render.deferred_composite"
CONTROLS = ("render.opaque_regular", "render.shadow_visibility", "render.deferred_lighting", "render.deferred_present")
OBSERVATIONS = (COMPOSITE,)
AVBOIT = tuple(ab.AVBOIT)
OBSERVED = (FRAME, RESOLVE, CLASSIFY, CLEAR_ARGUMENTS, *CONTROLS, *OBSERVATIONS, *AVBOIT)
EFFECT_COST = (CLEAR_ARGUMENTS, CLASSIFY, RESOLVE, COMPOSITE)
EXTENTS = ("960x720", "1600x900", "1920x1080")
DEFAULT_EXTENTS = ("960x720", "1600x900")
DENSITIES = (0, 1, 10, 50, 100)
ROUTES = ("screen", "hardware")
SCHEDULING_STRATEGIES = ("full-frame", "tiled")
# Compiled instrumentation used by density scenes. Extend this list if its
# implementation moves into another compiled source; analysis scripts are not
# executable fixture inputs and may evolve independently during acquisition.
FIXTURE_SOURCE_FILES = ("tests/smoke/refraction_project.cpp", "tests/smoke/avboit_timing_render_pass.cpp")
DENSITY_MARKER = re.compile(r"^RefractionDensity: extent (\d+)x(\d+) requested_percent (\d+) admitted_pixels (\d+) total_pixels (\d+) active_tiles (\d+) total_tiles (\d+) glass_instances (\d+) capture_mode (visible|occluded)$", re.MULTILINE)
BOUNDS_MARKER = re.compile(r"^RefractionDensity: pixel_bounds (-?\d+) (-?\d+) (-?\d+) (-?\d+)$", re.MULTILINE)
TRACE_MARKER = re.compile(r"^RefractionDensity: tracing requested (hardware|screen-space) ray_query_supported ([01]) refraction_enabled 1$", re.MULTILINE)
CLAMP_MARKER = re.compile(r"Frame (?:Win32|X11) client size clamped from (\d+)x(\d+) to (\d+)x(\d+) for (?:work area|display) \d+x\d+")


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


def reference_contract(baseline_scheduling):
    if baseline_scheduling == "full-frame":
        return "Surgical full-frame renderer reference with identical AVBOIT/compositor/fixture; frozen source manifest records the exact patch. Candidate uses tile scheduling."
    if baseline_scheduling == "tiled":
        return "Frozen tiled renderer reference versus tiled candidate with identical fixture instrumentation; both arms must publish argument clear and classification. Source manifests record the scheduling difference."
    raise SmokeFailure("invalid baseline scheduling strategy: " + str(baseline_scheduling))


def combined_work_policy(baseline_scheduling):
    admission = "baseline omits tile-only clear/classification" if baseline_scheduling == "full-frame" else "both arms require tile clear/classification"
    return "Argument clear + classification + resolve + compositor; " + admission + "; whole frame includes all work."


def workload_for(arm, cell):
    tiled = scheduling_for(arm) == "tiled"
    scopes = (FRAME, RESOLVE, *CONTROLS, *OBSERVATIONS, *AVBOIT) + ((CLASSIFY, CLEAR_ARGUMENTS) if tiled else ())
    return ab.Workload("refraction-density-" + cell.key, cell.width, cell.height, tuple((scope, 1) for scope in scopes), RESOLVE,
        (("NWB_REFRACTION_SMOKE_TIMING", "1"), ("NWB_REFRACTION_SMOKE_HARDWARE", "1" if cell.route == "hardware" else "0"), ("NWB_REFRACTION_SMOKE_ENABLED", "1"),
            ("NWB_REFRACTION_SMOKE_DENSITY", str(cell.density)), ("NWB_REFRACTION_SMOKE_EXTENT", cell.extent),
            ("NWB_RENDERER_BASELINE_FIXED_DELTA_SECONDS", "0.016666667")), None,
        inactive_scopes=() if tiled else (CLASSIFY, CLEAR_ARGUMENTS), control_scopes=CONTROLS)


def freeze(arm):
    return {"binaries": ab.binary_identity(arm.executable), "runtime": ab.runtime_identity(arm.runtime), "logserver": file_identity(arm.logserver),
        "namesym": file_identity(arm.namesym) if arm.namesym else None, "source": ab.source_identity(arm.source_manifest)}


def validate_fixture(text, cell):
    text = text.replace("\r\n", "\n")
    markers, bounds, traces = DENSITY_MARKER.findall(text), BOUNDS_MARKER.findall(text), TRACE_MARKER.findall(text)
    if len(markers) != 1 or len(bounds) != 1 or len(traces) != 1:
        raise SmokeFailure("density fixture requires one unambiguous extent/coverage, bounds, and tracing marker")
    width, height, percent, pixels, total, tiles, total_tiles, instances = map(int, markers[0][:8])
    x0, y0, x1, y1 = map(int, bounds[0])
    mode = markers[0][8]
    if (width, height, percent) != (cell.width, cell.height, cell.density) or total != width * height or instances != 1:
        raise SmokeFailure("runtime density, framebuffer extent, total pixels, or retained glass instance differs from the frozen cell")
    expected_route = "hardware" if cell.route == "hardware" else "screen-space"
    if traces[0][0] != expected_route or cell.route == "hardware" and traces[0][1] != "1":
        raise SmokeFailure("density tracing settings or required hardware capability differ from the cell")
    full_tiles = ((width + 7) // 8) * ((height + 7) // 8)
    if total_tiles != full_tiles:
        raise SmokeFailure("density total tile count differs from the 8x8 framebuffer partition")
    if cell.density == 0:
        if pixels != 0 or tiles != 0 or mode != "occluded" or (x0, y0, x1, y1) != (0, 0, 0, 0):
            raise SmokeFailure("zero-density fixture must retain occluded glass with zero admitted pixels and tiles")
    else:
        left, top, right, bottom = max(0, x0), max(0, y0), min(width, x1), min(height, y1)
        if mode != "visible" or not (0 <= left < right <= width and 0 <= top < bottom <= height):
            raise SmokeFailure("visible density bounds are invalid or outside the framebuffer")
        expected_pixels = (right - left) * (bottom - top)
        expected_tiles = ((right + 7) // 8 - left // 8) * ((bottom + 7) // 8 - top // 8)
        if pixels != expected_pixels or tiles != expected_tiles:
            raise SmokeFailure("admitted pixel/tile counts disagree with analytical bounds")
        if x0 + x1 != width or y0 + y1 != height:
            raise SmokeFailure("density rectangle must remain centered")
        rounding = 2 / width + 2 / height
        if abs(pixels / total - percent / 100) > rounding or abs((x1 - x0) / width - (y1 - y0) / height) > rounding:
            raise SmokeFailure("admitted density differs beyond even-pixel rectangle rounding")
        if percent == 100 and (pixels != total or tiles != full_tiles):
            raise SmokeFailure("full density did not admit every framebuffer pixel and tile")
    return {"requested_percent": percent, "admitted_pixels": pixels, "total_pixels": total, "active_tiles": tiles, "total_tiles": total_tiles,
        "pixel_bounds": [x0, y0, x1, y1], "capture_mode": mode, "glass_instances": instances, "coverage_source": "analytical pixel-center bounds; not GPU readback"}


def validate_extent_history(text, cell):
    target = (cell.width, cell.height)
    dimensions = [(int(match[1]), int(match[2])) for match in ab.DIMENSIONS.finditer(text)]
    if not dimensions or dimensions[-1] != target:
        raise SmokeFailure("density renderer never confirmed its final target extent")
    clamps = list(CLAMP_MARKER.finditer(text))
    if len(clamps) > 1:
        raise SmokeFailure("multiple initial extent clamps are ambiguous")
    clamped = None
    if clamps:
        requested = tuple(map(int, clamps[0].groups()[:2]))
        clamped = tuple(map(int, clamps[0].groups()[2:]))
        if requested != target or clamped == target or min(clamped) <= 0 or clamped[0] > target[0] or clamped[1] > target[1]:
            raise SmokeFailure("initial clamp marker does not match the requested framebuffer")
        final_resize = f"GraphicsRuntime: Back buffer resized to {cell.width}x{cell.height}"
        resize_offset = text.find(final_resize, clamps[0].end())
        ready_offsets = [match.start() for match in ab.DIMENSIONS.finditer(text) if (int(match[1]), int(match[2])) == target]
        if resize_offset < 0 or not any(offset > resize_offset for offset in ready_offsets):
            raise SmokeFailure("initial clamp lacks a confirmed final framebuffer resize")
    allowed = {target} | ({clamped} if clamped else set())
    if any(extent not in allowed for extent in dimensions):
        raise SmokeFailure("unexpected framebuffer extent is not explained by the explicit initial clamp")
    if clamped and target in dimensions and any(extent != target for extent in dimensions[dimensions.index(target):]):
        raise SmokeFailure("renderer extent changed after confirming the requested target")
    return {"target_extent": list(target), "clamped_initial_extent": list(clamped) if clamped else None}


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
        dimensions = [(int(x), int(y)) for x, y in ab.DIMENSIONS.findall(text)]
        if tuple(backend.client_size(handle)) == target and dimensions and dimensions[-1] == target and (not resized or marker in text):
            evidence = validate_extent_history(text, cell)
            return {**evidence, "initial_client_extent": list(initial), "resize_requested": resized, "allow_outside_work_area": resized, "confirmation_seconds": time.monotonic() - started}
        time.sleep(.1)
    raise SmokeFailure("requested density client/framebuffer extent was not confirmed before startup deadline")


def retain_target_reports(reports, startup_headers, warmup_intervals, minimum_warmup_frames):
    # Headers present at target confirmation include an unsealed report that may
    # mix old-extent GPU observations. Discard it before any target-only warmup.
    relative, retained = timing.retain_after_warmup(reports[startup_headers:], warmup_intervals, minimum_warmup_frames)
    return startup_headers + relative, retained


def startup_report_boundary(text):
    # The append writer can be between bytes of a header during this read. One
    # extra sealing boundary prevents that partial header from becoming target
    # warmup evidence, even though the strict header regex cannot see it yet.
    return len(list(timing.HEADER.finditer(text))) + 1


def validate_log(text, cell):
    expected_route = "hardware" if cell.route == "hardware" else "screen-space"
    forbidden_route = "screen-space" if cell.route == "hardware" else "hardware"
    validate_expected_log_text(text, ["RefractionSmokeProject: shutdown", "AvboitTimingProbe: in-flight ranges 32", "AvboitTimingProbe: render unfocused 1",
        "RefractionSmokeProject: refraction enabled", "Loader: project startup complete", "AVBOIT refraction resolve: " + expected_route], [*STRICT_LOG_FAILURE_MESSAGES, "FramebufferCapture:", "RefractionLifecycle:",
        "AVBOIT refraction resolve: " + forbidden_route, "VK_LAYER_KHRONOS_validation", "Vulkan: enabled validation layer", "render submission suspended"])
    validate_extent_history(text, cell)
    return {**ab.device_material_signature(text), "fixture": validate_fixture(text, cell), "trace_route": cell.route}


def wall_frames(log, sidecar, workload, start, stop, stall_threshold_ms):
    # Both fixtures use the same FpsProbe formatting and real callback delta;
    # reuse the existing checked window alignment and consistency oracle.
    normalized = log.replace("RefractionSmokeProject: fps ", "ReflectionSmokeProject: fps ")
    try:
        result = ab.summarize_wall_frames(normalized, sidecar, replace(workload, reflection_policy=True), [start, stop])
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
    launch = launch_arguments(args, arm)
    env, overrides = ab.configure_environment(build_launch_environment(launch), workload, sidecar)
    for name in ("NWB_SMOKE_FRAMEBUFFER_CAPTURE_PATH", "NWB_SMOKE_FRAMEBUFFER_CAPTURE_FRAME_COUNT", "NWB_REFRACTION_FRAME_GRAPH_SNAPSHOT_PATH"):
        env.pop(name, None)
    write(directory / "launch.json", {"arm": arm.name, "scheduling": scheduling_for(arm), "baseline_scheduling": args.baseline_scheduling,
        "reference_contract": reference_contract(args.baseline_scheduling), "block": block, "position": position, "executable": str(arm.executable), "runtime": str(arm.runtime),
        "environment_overrides": overrides, "validation": False, "capture": False, "cell": cell.key, "trace_route": cell.route, "cache_before": ab.cache_identity(arm.runtime)})
    backend = logserver = process = handle = log_directory = None
    log_baseline, pattern = {}, ""
    selected = None
    collected = False
    started = time.monotonic()
    window_seconds = first_report_seconds = retained_ready_seconds = None
    try:
        backend = create_capture_backend()
        logserver, port, log_directory, log_baseline, pattern = launch_logserver(launch, arm.executable, env)
        process = launch_testbed(launch, arm.executable, env, port)
        handle = backend.wait_for_window(process.pid, min(30, args.timeout))
        if handle is None:
            raise SmokeFailure("density timing window did not become visible before the startup deadline")
        window_seconds = time.monotonic() - started
        if handle:
            try:
                backend.focus_window(handle)
            except Exception:
                pass
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
                reports = timing.parse_intervals(sidecar.read_text(encoding="utf-8"), symbols)
                if len(reports) > startup_headers and first_report_seconds is None:
                    first_report_seconds = time.monotonic() - started
                retained_start, warmed = retain_target_reports(reports, startup_headers, args.warmup_intervals, args.warmup_minimum_frames)
                retained = warmed[:args.sample_intervals]
                problem = (f"{len(reports)} sealed reports; {len(retained)}/{args.sample_intervals} retained reports, "
                    f"{timing.positive_frame_reports(retained)} positive; {startup_headers} startup headers and {retained_start - startup_headers} target warmup reports discarded")
                if len(retained) == args.sample_intervals:
                    try:
                        summary = timing.summarize_intervals(retained)
                        ab.validate_coverage(summary, workload, len(retained), args.minimum_frames)
                        if timing.positive_frame_reports(retained) < args.sample_intervals:
                            raise SmokeFailure("retained reports lack the requested positive GPU frame coverage")
                        selected = retained
                        retained_ready_seconds = time.monotonic() - started
                        break
                    except SmokeFailure as error:
                        problem = str(error)
            time.sleep(.1)
        if selected is None:
            raise SmokeFailure("inconclusive timing acquisition: " + problem)
        code, tail = terminate_process(process, "refraction timing", handle)
        process = None
        require_normal_process_exit(code, tail, "refraction timing")
        (directory / "process_tail.txt").write_text(tail, encoding="utf-8")
        log = shutdown_logserver_and_collect(logserver, log_directory, log_baseline, pattern)
        logserver, collected = None, True
        (directory / "runtime.log").write_text(log, encoding="utf-8")
        signature = validate_log(log, cell)
        text = sidecar.read_text(encoding="utf-8")
        finalized = timing.parse_intervals(text, symbols, finalized=True)
        start, stop = retained_start, retained_start + len(selected)
        summary = timing.summarize_intervals(finalized[start:stop])
        ab.validate_coverage(summary, workload, len(selected), args.minimum_frames)
        gpu_frames = summary[FRAME]["gpu_samples"]
        retained_durations = [float(match[2]) for match in list(timing.HEADER.finditer(text))[start:stop]]
        wall = wall_frames(log, text, workload, start, stop, args.stall_threshold_ms)
        result = {"arm": arm.name, "scheduling": scheduling_for(arm), "baseline_scheduling": args.baseline_scheduling, "reference_contract": reference_contract(args.baseline_scheduling),
            "block": block, "position": position, "cell": cell.key, "trace_route": cell.route, "reports": len(selected), "positive_frame_reports": timing.positive_frame_reports(selected),
            "retained_report_range": [start, stop], "scopes": summary, "runtime_signature": signature, "wall_frame": wall, "acquisition_clean": wall["eligible"],
            "refraction_work_per_frame_ms": sum(summary.get(scope, {}).get("total_ms", 0) for scope in (CLASSIFY, RESOLVE)) / gpu_frames,
            "total_effect_work_per_frame_ms": sum(summary.get(scope, {}).get("total_ms", 0) for scope in EFFECT_COST) / gpu_frames,
            "combined_work_policy": combined_work_policy(args.baseline_scheduling),
            "acquisition": {"window_after_launch_seconds": window_seconds, "first_sealed_report_after_launch_seconds": first_report_seconds,
                "target_confirmed_after_launch_seconds": target_confirmed_seconds, "startup_resize": startup_resize, "discarded_startup_report_headers": startup_headers,
                "measurement_ready_after_launch_seconds": retained_ready_seconds, "retained_gpu_interval_seconds": sum(retained_durations),
                "retained_interval_durations_seconds": retained_durations, "maximum_retained_interval_seconds": max(retained_durations),
                "startup_policy": "confirm actual target client and renderer extent, discard all existing report headers including the unsealed tail, then satisfy report and GPU-frame warmup minima using only new target reports"},
            "cache_after": ab.cache_identity(arm.runtime), "artifacts": str(directory)}
        write(directory / "summary.json", result)
        return result
    finally:
        active_error = sys.exc_info()[0] is not None
        failures = []
        try:
            terminate_process(process, "refraction timing", handle)
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
            terminate_process(logserver, "logserver")
            if backend is not None:
                backend.close()
        except Exception as error:
            failures.append(str(error))
        if failures:
            (directory / "cleanup_error.txt").write_text("\n".join(failures), encoding="utf-8")
            if not active_error:
                raise SmokeFailure("; ".join(failures))


def comparison(trials, blocks, baseline_scheduling="full-frame"):
    contract = reference_contract(baseline_scheduling)
    expected = {(block, arm) for block in range(blocks) for arm in ("baseline", "candidate")}
    observed = [(trial["block"], trial["arm"]) for trial in trials]
    if len(observed) != len(set(observed)) or set(observed) != expected:
        raise SmokeFailure("density comparison refuses duplicate or incomplete paired blocks")
    for trial in trials:
        strategy = baseline_scheduling if trial["arm"] == "baseline" else "tiled"
        if trial.get("scheduling", strategy) != strategy or trial.get("baseline_scheduling", baseline_scheduling) != baseline_scheduling:
            raise SmokeFailure("density comparison refuses mixed or mismatched scheduling strategies")
        tile_scopes = (CLASSIFY, CLEAR_ARGUMENTS)
        if strategy == "tiled" and any(scope not in trial["scopes"] for scope in tile_scopes):
            raise SmokeFailure("tiled density comparison lacks required clear/classification scopes")
        if strategy == "full-frame" and any(scope in trial["scopes"] for scope in tile_scopes):
            raise SmokeFailure("full-frame density comparison contains forbidden tile clear/classification scopes")
    paired = {index: {trial["arm"]: trial for trial in trials if trial["block"] == index} for index in range(blocks)}
    metrics = {"frame_gpu_ms": lambda trial: trial["scopes"][FRAME]["mean_ms"], "refraction_classification_plus_resolve_per_frame_ms": lambda trial: trial["refraction_work_per_frame_ms"],
        "argument_clear_plus_classification_plus_resolve_plus_compositor_per_frame_ms": lambda trial: sum(trial["scopes"].get(scope, {}).get("total_ms", 0) for scope in EFFECT_COST) / trial["scopes"][FRAME]["gpu_samples"]}
    metrics.update({scope: lambda trial, scope=scope: trial["scopes"][scope]["mean_ms"] for scope in (*CONTROLS, *OBSERVATIONS, *AVBOIT, RESOLVE)})
    if all(trial["wall_frame"]["eligible"] for trial in trials):
        metrics["wall_callback_frame_ms"] = lambda trial: trial["wall_frame"]["mean_frame_ms"]
    result = {}
    for name, getter in metrics.items():
        baseline = [getter(paired[index]["baseline"]) for index in range(blocks)]
        candidate = [getter(paired[index]["candidate"]) for index in range(blocks)]
        deltas = [after - before for before, after in zip(baseline, candidate)]
        before, after = statistics.fmean(baseline), statistics.fmean(candidate)
        result[name] = {"baseline": before, "candidate": after, "candidate_minus_baseline": statistics.fmean(deltas),
            "percent_change": 100 * (after - before) / before if before else None, "block_differences": deltas}
        if blocks >= 5:
            result[name]["paired_statistics"] = timing.paired_statistics(deltas)
    wall_rejected = [{"block": trial["block"], "arm": trial["arm"], "reason": trial["wall_frame"].get("reason")} for trial in trials if not trial["wall_frame"]["eligible"]]
    return {"status": f"descriptive_{blocks}_blocks_no_confidence_claim" if blocks < 5 else "paired_intervals_require_control_assessment", "metrics": result,
        "baseline_scheduling": baseline_scheduling, "candidate_scheduling": "tiled", "reference_contract": contract,
        "controls": list(CONTROLS), "affected_observed_scopes": list(OBSERVATIONS),
        "wall_rejected_trials": wall_rejected, "control_assessment": "Report control movement explicitly; host measurement floor is not established by this acquisition.",
        "combined_work_policy": combined_work_policy(baseline_scheduling),
        "interpretation": "Combined work is normalized by completed GPU frames. Compositor is affected, not a control. Wall callback time includes pacing; it is neither isolated CPU cost nor measured presentation cadence. No FPS claim follows from these counters."}


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
        or not math.isfinite(args.timeout) or args.timeout <= 0 or not math.isfinite(args.stall_threshold_ms) or args.stall_threshold_ms <= 0:
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
    ab.validate_arm_separation(tuple(arms.values()))
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
            "baseline_scheduling": args.baseline_scheduling, "candidate_scheduling": "tiled", "reference_contract": reference_contract(args.baseline_scheduling),
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
            reports[cell.key] = {"cell": cell.key, "trials": cell_trials, "acquisition_clean": all(trial["acquisition_clean"] for trial in cell_trials), "comparison": comparison(cell_trials, args.blocks, args.baseline_scheduling)}
            write(args.output_directory / cell.key / "report.json", reports[cell.key])
        write(args.output_directory / "report.json", {"acquisition_passed": True, "acquisition_clean": all(trial["acquisition_clean"] for trial in trials), "cells": reports, "trials": trials, "plan": plan})
        print("Completed: " + str(args.output_directory / "report.json"), flush=True)
        return 0
    except (SmokeFailure, SmokeSkip, OSError, ValueError, subprocess.SubprocessError) as error:
        write(args.output_directory / "failure.json", {"status": "inconclusive_acquisition" if "inconclusive" in str(error) else "failed", "error": str(error), "completed_trials": len(trials)})
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
