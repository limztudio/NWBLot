#!/usr/bin/env python3
"""Balanced GPU A/B acquisition for two explicitly frozen renderer builds.

Workloads cover transparent-multi and fixed reflection routes. Source manifests contain a nonempty
revision and a files object mapping paths (relative to the manifest, or absolute)
to SHA256 values. Keep physical source snapshots available throughout acquisition.
Correctness qualification is separate; this runner never captures framebuffers.
"""

import argparse
from dataclasses import asdict, dataclass
import json
import math
from pathlib import Path
import re
import statistics
import subprocess
import sys
import time
from types import SimpleNamespace
from typing import Callable

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "ab"))
from gpu_timing_parse import load_name_symbols
import reflection_benchmark as reflection
from reflection_benchmark import balanced_orders, paired_statistics, parse_intervals, summarize_intervals
from smoke_volume_identity import authored_volume_hashes, file_identity, runtime_pipeline_cache_paths
from window_capture_smoke import (
    STRICT_LOG_FAILURE_MESSAGES, SmokeFailure, SmokeSkip, build_launch_environment,
    create_capture_backend, ensure_process_running, launch_logserver, launch_testbed,
    require_normal_process_exit, shutdown_logserver_and_collect, terminate_process,
    validate_expected_log_text, write_status,
)


FRAME = "render.frame"
OCCUPANCY = "render.avboit_occupancy"
CONTROLS = ("render.opaque_regular", "render.shadow_visibility", "render.deferred_lighting")
OBSERVATIONS = ("render.deferred_composite", "render.deferred_present")
AVBOIT = ("render.avboit_clear", OCCUPANCY, "render.avboit_depth_warp",
    "render.avboit_extinction", "render.avboit_integration", "render.avboit_accumulate")
SHADOW_ROUTES = {
    "TransparentMultiSmokeProject: natural hybrid shadow route selected on RayQuery-capable hardware": "hybrid",
    "TransparentMultiSmokeProject: natural software-only shadow route selected because RayQuery-capable hardware is unavailable": "software",
}
SHA256 = re.compile(r"[0-9a-f]{64}\Z")
DIMENSIONS = re.compile(r"deferred rendering targets ready \((\d+)x(\d+),")


@dataclass(frozen=True)
class Arm:
    name: str
    executable: Path
    runtime: Path
    source_manifest: Path


@dataclass(frozen=True)
class ReflectionPolicy:
    family: str
    variant: reflection.Variant
    roughness: float
    history_samples: int = 16
    ray_budget: int = 1382400
    optical_queries: int = 16
    screen_steps: int = 96
    sampling_seed: int = 0


@dataclass(frozen=True)
class Workload:
    name: str
    width: int
    height: int
    # The AVBOIT raster timings span a complete pass, including split emulation;
    # they are one range per frame, not one sample per draw or native dispatch.
    scope_multipliers: tuple
    secondary_scope: str
    environment_overrides: tuple
    validate_log: Callable[[str, "Workload", bool], dict]
    reflection_policy: ReflectionPolicy = None
    inactive_scopes: tuple = ()

    @property
    def observed_scopes(self):
        # Decode inactive scope hashes too; otherwise an unexpected scope could evade a zero-coverage check.
        return self.scopes + self.inactive_scopes

    @property
    def scopes(self):
        return tuple(scope for scope, _ in self.scope_multipliers)


def device_material_signature(text):
    devices = re.findall(r"^Vulkan: created device '(.+)'$", text, re.MULTILINE)
    if len(devices) != 1:
        raise SmokeFailure("one actual Vulkan device identity is required")
    material_routes = {}
    for material, route in re.findall(r"^RendererSystem: material '([^']+)' selected (.+)$", text, re.MULTILINE):
        if material in material_routes and material_routes[material] != route:
            raise SmokeFailure("a material changed its native/emulated route within a trial")
        material_routes[material] = route
    if not material_routes:
        raise SmokeFailure("material execution-route evidence is missing")
    return {"device": devices[0], "material_routes": material_routes}


def reflection_arguments(workload, require_hardware=False):
    policy = workload.reflection_policy
    return SimpleNamespace(family=policy.family, width=workload.width, height=workload.height,
        mip_count=max(workload.width, workload.height).bit_length(), ray_budget=policy.ray_budget,
        optical_queries=policy.optical_queries, screen_steps=policy.screen_steps,
        sampling_seed=policy.sampling_seed, roughness=policy.roughness, history_samples=policy.history_samples,
        require_hardware=require_hardware or policy.variant.mode in ("hardware", "hybrid"))


def reflection_log(text, workload, require_hardware):
    policy = workload.reflection_policy
    args = reflection_arguments(workload, require_hardware)
    text = text.replace("\r\n", "\n")
    reflection.validate_trial_log(text, args, policy.variant)
    validate_expected_log_text(text, [], ["FramebufferCapture:", "render submission suspended"])
    lines = text.splitlines()
    availability = [line for line in lines if line in (
        "ReflectionSmokeProject: hardware available", "ReflectionSmokeProject: hardware unavailable")]
    if len(availability) != 1:
        raise SmokeFailure("one unambiguous reflection hardware capability marker is required")
    # The shared benchmark currently checks this field for optical_clear. The inside fixture exposes the same
    # startup field and must pin its independent query cap too.
    if policy.family == "optical_inside":
        prefix = "ReflectionSmokeProject: optical query limit "
        if [line[len(prefix):] for line in lines if line.startswith(prefix)] != [str(policy.optical_queries)]:
            raise SmokeFailure("runtime optical query limit differs from the frozen inside workload")
    return {**device_material_signature(text), "extent": [workload.width, workload.height],
        "reflection_route": policy.variant.mode, "hardware_available": availability[0].endswith("hardware available"),
        "timing_in_flight_ranges": reflection.TIMING_IN_FLIGHT_RANGES,
        "timing_depth_mip_count": args.mip_count, "reflection_policy": asdict(policy)}


def transparent_multi_log(text, workload, require_hardware):
    lines = text.replace("\r\n", "\n").splitlines()
    text = "\n".join(lines)
    for prefix, expected in (("AvboitTimingProbe: in-flight ranges ", "32"),
        ("AvboitTimingProbe: render unfocused ", "1")):
        values = [line[len(prefix):] for line in lines if line.startswith(prefix)]
        if values != [expected]:
            raise SmokeFailure(f"timing policy differs from its single frozen value: {prefix}{values!r}")
    required = (
        "TransparentMultiSmokeProject: shared transparent material with three mutable instance overrides created",
        "TransparentMultiSmokeProject: shutdown",
    )
    for message in required:
        if lines.count(message) != 1:
            raise SmokeFailure(f"missing or repeated workload lifecycle message: {message}")
    forbidden = (*STRICT_LOG_FAILURE_MESSAGES, "FramebufferCapture:", "ReflectionSmokeStatistics:",
        "ReflectionSmokeHistory:", "ReflectionSmokeFeedback:", "render submission suspended",
        "FrameLaggedAsyncLightingSmoke:", "CausticSphereSmokeProject:", "TransparentCsgSmokeProject:")
    validate_expected_log_text(text, [], forbidden)
    extents = {(int(width), int(height)) for width, height in DIMENSIONS.findall(text)}
    if extents != {(workload.width, workload.height)}:
        raise SmokeFailure(f"render extent changed or differs from the workload: {sorted(extents)}")
    routes = [SHADOW_ROUTES[line] for line in lines if line in SHADOW_ROUTES]
    if len(routes) != 1 or (require_hardware and routes != ["hybrid"]):
        raise SmokeFailure(f"natural shadow route is missing, contradictory, or unsupported: {routes}")
    return {**device_material_signature(text), "shadow_route": routes[0],
        "extent": list(extents.pop()), "timing_in_flight_ranges": 32}


def reflection_workload(name, family, mode, roughness, temporal, spatial, target_scope):
    variant = reflection.Variant(name, mode, temporal=temporal, spatial=spatial, feedback=False)
    policy = ReflectionPolicy(family, variant, roughness)
    required = tuple(reflection.required_scopes(variant, policy.history_samples))
    inactive = tuple(scope for scope in reflection.KERNELS if scope not in required)
    workload = Workload(name, 960, 720,
        tuple((scope, 10 if scope == reflection.DEPTH else 1) for scope in required), target_scope,
        (), reflection_log, policy, inactive)
    _, overrides = reflection.timed_environment({}, reflection_arguments(workload), variant, Path("timing.txt"))
    del overrides["NWB_GPU_TIMING_FILE"]
    return Workload(name, workload.width, workload.height, workload.scope_multipliers, target_scope,
        tuple(overrides.items()), reflection_log, policy, inactive)


def workloads():
    scopes = (FRAME, *CONTROLS, *OBSERVATIONS, *AVBOIT)
    result = {"transparent-multi": Workload("transparent-multi", 1280, 900,
        tuple((scope, 1) for scope in scopes), OCCUPANCY,
        (("NWB_AVBOIT_SMOKE_TIMING", "1"), ("NWB_TRANSPARENT_MULTI_SPIN_ANGLE", "0"),
         ("NWB_RENDERER_BASELINE_FIXED_DELTA_SECONDS", "0.016666667")), transparent_multi_log)}
    definitions = (
        ("reflection-rough-spatial", "rough", "hardware", .4, False, True, reflection.SPATIAL),
        ("reflection-mirror-spatial", "rough", "hardware", 0.0, False, True, reflection.SPATIAL),
        ("reflection-rough-filtered", "rough", "hardware", .4, True, True, reflection.SPATIAL),
        ("reflection-screen-depth", "floor", "screen", 0.0, False, False, reflection.DEPTH),
        ("reflection-optical-clear", "optical_clear", "hardware", 0.0, False, False, reflection.HARDWARE),
        ("reflection-optical-inside", "optical_inside", "hardware", 0.0, False, False, reflection.HARDWARE),
    )
    for definition in definitions:
        workload = reflection_workload(*definition)
        result[workload.name] = workload
    return result


def configure_environment(base, workload, timing_file):
    env = dict(base)
    # Preserve a requested validation override as an error, never silently mute it.
    for key in ("VK_INSTANCE_LAYERS", "VK_LOADER_LAYERS_ENABLE"):
        if env.get(key, "").strip():
            raise SmokeFailure(f"explicit validation layer override is incompatible with timing: {key}")
    for key in tuple(env):
        if (key.startswith("NWB_") and ("SMOKE" in key or key.startswith("NWB_TRANSPARENT_"))) \
            or key.startswith("NWB_RENDERER_BASELINE_") or key == "NWB_GPU_TIMING_FILE":
            del env[key]
    if workload.reflection_policy is not None:
        return reflection.timed_environment(env, reflection_arguments(workload),
            workload.reflection_policy.variant, timing_file)
    overrides = dict(workload.environment_overrides)
    overrides["NWB_GPU_TIMING_FILE"] = str(timing_file)
    env.update(overrides)
    return env, overrides


def validate_inactive_scopes(scopes, workload):
    unexpected = [scope for scope in workload.inactive_scopes if scope in scopes]
    if unexpected:
        raise SmokeFailure("inactive reflection scopes recorded GPU work: " + ", ".join(unexpected))


def validate_coverage(scopes, workload, report_count, minimum_frames):
    validate_inactive_scopes(scopes, workload)
    if workload.reflection_policy is not None:
        policy = workload.reflection_policy
        reflection.validate_coverage(scopes, policy.variant, report_count, minimum_frames,
            max(workload.width, workload.height).bit_length(), policy.history_samples)
    missing = [scope for scope in workload.scopes if scope not in scopes]
    if missing:
        raise SmokeFailure("missing completed GPU scopes: " + ", ".join(missing))
    frames = scopes[FRAME]["gpu_samples"]
    if frames < minimum_frames or scopes[FRAME]["mean_ms"] <= 0:
        raise SmokeFailure("insufficient completed GPU frame samples")
    for name, multiplier in workload.scope_multipliers:
        scope = scopes[name]
        if scope["reports"] < math.ceil(report_count * .75):
            raise SmokeFailure(f"scope {name} is absent from too many publications")
        expected = frames * multiplier
        if abs(scope["gpu_samples"] - expected) > max(2 * multiplier, expected * .02):
            raise SmokeFailure(f"scope {name} sample ratio differs from {multiplier} range(s) per frame")


def source_identity(manifest_path):
    document = json.loads(manifest_path.read_text(encoding="utf-8"))
    if not isinstance(document, dict):
        raise SmokeFailure("source manifest must be an object")
    files = document.get("files")
    if not isinstance(document.get("revision"), str) or not document["revision"].strip() \
        or not isinstance(files, dict) or not files:
        raise SmokeFailure("source manifest requires a revision and nonempty files-to-SHA256 mapping")
    actual = {}
    for relative, expected in files.items():
        if not isinstance(relative, str) or not isinstance(expected, str) or not SHA256.fullmatch(expected):
            raise SmokeFailure("source manifest contains an invalid path or SHA256")
        path = (manifest_path.parent / relative).resolve()
        if not path.is_file() or file_identity(path)["sha256"] != expected:
            raise SmokeFailure(f"source bytes differ from the manifest: {relative}")
        actual[str(path)] = expected
    return {"manifest": str(manifest_path), "identity": file_identity(manifest_path),
        "revision": document["revision"], "files": actual}


def runtime_identity(runtime):
    resources = runtime / "res"
    if not resources.is_dir():
        raise SmokeFailure(f"runtime lacks a packed res directory: {runtime}")
    mutable = set(runtime_pipeline_cache_paths(runtime))
    files = {path.relative_to(runtime).as_posix(): file_identity(path)
        for path in sorted(resources.rglob("*")) if path.is_file() and path not in mutable}
    volumes = authored_volume_hashes(runtime)
    if not files or not volumes:
        raise SmokeFailure("runtime contains no authored packed volume")
    return {"files": files, "authored_volume_hashes": volumes}


def binary_identity(executable):
    # Freeze local loader dependencies and the crash helper, not unrelated sibling
    # test executables or logger output created at shutdown.
    directory = executable.parent
    dependencies = {path for path in directory.iterdir() if path.is_file()
        and (path.suffix.lower() in (".dll", ".dylib") or ".so" in path.suffixes
             or path.name in ("crash_handler", "crash_handler.exe"))}
    if executable.suffix.lower() == ".exe" and not (directory / "crash_handler.exe").is_file():
        raise SmokeFailure("Windows smoke launcher requires its sibling crash_handler.exe")
    dependencies.add(executable)
    return {path.name: file_identity(path) for path in sorted(dependencies)}


def freeze_arm(arm):
    return {"executable": str(arm.executable), "runtime": str(arm.runtime),
        "binaries": binary_identity(arm.executable), "resources": runtime_identity(arm.runtime),
        "source": source_identity(arm.source_manifest)}


def verify_frozen(arms, identities, shared):
    validate_arm_separation(arms)
    for arm in arms:
        if freeze_arm(arm) != identities[arm.name]:
            raise SmokeFailure(f"frozen source, executable, dependencies, or authored runtime changed: {arm.name}")
    for path, identity in shared.items():
        if file_identity(Path(path)) != identity:
            raise SmokeFailure(f"shared runner, symbols, or logserver changed: {path}")


def cache_identity(runtime):
    return {path.relative_to(runtime).as_posix(): file_identity(path) for path in runtime_pipeline_cache_paths(runtime)}


def validate_arm_separation(arms):
    first, second = arms
    if first.runtime == second.runtime or first.runtime.is_relative_to(second.runtime) \
        or second.runtime.is_relative_to(first.runtime) or (first.runtime / "res").resolve() == (second.runtime / "res").resolve():
        raise SmokeFailure("arms need distinct non-nested runtime/resource directories and separate mutable caches")
    for first_cache in runtime_pipeline_cache_paths(first.runtime):
        for second_cache in runtime_pipeline_cache_paths(second.runtime):
            if first_cache.samefile(second_cache):
                raise SmokeFailure("arm runtime caches alias the same physical file")


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2, sort_keys=True, allow_nan=False) + "\n", encoding="utf-8")


def compare_trials(trials, orders, workload, seed=0, practical_ms=.02, practical_fraction=.03, control_floor_ms=.015):
    expected = {(block, position, name) for block, row in enumerate(orders) for position, name in enumerate(row)}
    seen = [(trial["block"], trial["position"], trial["arm"]) for trial in trials]
    if len(seen) != len(set(seen)) or set(seen) != expected:
        raise SmokeFailure("inference requires every planned trial exactly once; incomplete/duplicate data is refused")
    blocks = {block: {} for block in range(len(orders))}
    for trial in trials:
        if trial["arm"] in blocks[trial["block"]]:
            raise SmokeFailure("duplicate arm within a block")
        blocks[trial["block"]][trial["arm"]] = trial
    if any(set(block) != {"baseline", "candidate"} for block in blocks.values()):
        raise SmokeFailure("each independent block must contain both frozen arms")
    means = {arm: {scope: statistics.fmean(block[arm]["scopes"][scope]["mean_ms"]
        for block in blocks.values()) for scope in workload.scopes} for arm in ("baseline", "candidate")}
    paired = {scope: paired_statistics([block["candidate"]["scopes"][scope]["mean_ms"]
        - block["baseline"]["scopes"][scope]["mean_ms"] for block in blocks.values()], seed)
        for scope in workload.scopes}
    controls = {}
    for scope in CONTROLS:
        metric = dict(paired[scope])
        tolerance = max(control_floor_ms, practical_fraction * means["baseline"][scope])
        low, high = metric["ci95_mean_ms"]
        metric.update(tolerance_ms=tolerance, equivalent_within_tolerance=low >= -tolerance and high <= tolerance,
            material_drift=low > tolerance or high < -tolerance)
        controls[scope] = metric
    threshold = max(practical_ms, practical_fraction * means["baseline"][FRAME])
    low, high = paired[FRAME]["ci95_mean_ms"]
    status = "unresolved"
    if any(metric["material_drift"] for metric in controls.values()):
        status = "control_drift"
    elif any(not metric["equivalent_within_tolerance"] for metric in controls.values()):
        status = "control_uncertain"
    elif high < -threshold:
        status = "resolved_gpu_time_reduction"
    elif low > threshold:
        status = "resolved_gpu_time_increase"
    secondary_frame_means = {arm: statistics.fmean(
        block[arm]["scopes"][workload.secondary_scope]["total_ms"] / block[arm]["scopes"][FRAME]["gpu_samples"]
        for block in blocks.values()) for arm in ("baseline", "candidate")}
    secondary_frame_delta = paired_statistics([
        block["candidate"]["scopes"][workload.secondary_scope]["total_ms"] / block["candidate"]["scopes"][FRAME]["gpu_samples"]
        - block["baseline"]["scopes"][workload.secondary_scope]["total_ms"] / block["baseline"]["scopes"][FRAME]["gpu_samples"]
        for block in blocks.values()], seed)
    return {"status": status, "practical_threshold_ms": threshold, "means_ms": means,
        "frame": paired[FRAME], "secondary_scope": workload.secondary_scope,
        "secondary": paired[workload.secondary_scope], "controls": controls, "scope_deltas": paired,
        "secondary_scope_units": "milliseconds per completed mip-reduction range" if workload.secondary_scope == reflection.DEPTH
            else "milliseconds per completed scope range; a range may contain multiple native dispatches",
        "secondary_per_frame_work": {"means_ms": secondary_frame_means, "delta": secondary_frame_delta,
            "units": "sum of measured secondary-scope milliseconds per completed GPU frame; not a frame-time attribution"},
        "completed_trials": len(trials), "completed_blocks": len(blocks),
        "completed_gpu_frames": sum(t["scopes"][FRAME]["gpu_samples"] for t in trials),
        "units": "milliseconds per completed scope range; frame is the primary endpoint",
        "multiplicity": "per-comparison 95% intervals; no simultaneous family-wide claim"}


def acquire_trial(args, arm, workload, block, position, symbols):
    directory = args.output_directory / f"block_{block:02d}_{position:02d}_{arm.name}"
    directory.mkdir(parents=True, exist_ok=False)
    timing_file = directory / "gpu_timing.txt"
    launch_args = SimpleNamespace(**vars(args))
    launch_args.working_directory = arm.runtime
    launch_args.executable = arm.executable
    env, overrides = configure_environment(build_launch_environment(launch_args), workload, timing_file)
    write_json(directory / "launch.json", {"arm": arm.name, "block": block, "position": position,
        "executable": str(arm.executable), "runtime": str(arm.runtime), "application_args": args.application_arg,
        "environment_overrides": overrides, "cache_before": cache_identity(arm.runtime),
        "gpu_environment": {key: value for key, value in env.items() if key.startswith("VK_") or key == "NWB_LINUX_BACKEND"}})
    backend = logserver = process = handle = log_directory = None
    baseline = {}
    pattern = ""
    logs_collected = False
    try:
        backend = create_capture_backend()
        logserver, port, log_directory, baseline, pattern = launch_logserver(launch_args, arm.executable, env)
        process = launch_testbed(launch_args, arm.executable, env, port)
        handle = backend.wait_for_window(process.pid, min(args.timeout, 30.0))
        if not handle:
            raise SmokeFailure("benchmark render window did not appear")
        backend.focus_window(handle)
        deadline = time.monotonic() + args.timeout
        selected = None
        problem = "timing reports have not arrived"
        while time.monotonic() < deadline:
            ensure_process_running(process, "during A/B benchmark")
            if timing_file.is_file():
                intervals = parse_intervals(timing_file.read_text(encoding="utf-8"), symbols)
                validate_inactive_scopes(summarize_intervals(intervals), workload)
                if intervals:
                    problem = "waiting for warm-up/measured reports" if any(FRAME in value for value in intervals) \
                        else "timing reports contain no completed GPU frame samples"
                retained = intervals[args.warmup_intervals:]
                if len(retained) >= args.sample_intervals:
                    summaries = summarize_intervals(retained)
                    try:
                        validate_coverage(summaries, workload, len(retained), args.minimum_frame_samples)
                        selected = retained
                        break
                    except SmokeFailure as error:
                        problem = str(error)
            time.sleep(.1)
        if selected is None:
            raise SmokeFailure(f"GPU acquisition timed out: {problem}")
        exit_code, tail = terminate_process(process, "renderer A/B benchmark", handle)
        process = None
        (directory / "process_tail.txt").write_text(tail, encoding="utf-8")
        require_normal_process_exit(exit_code, tail, "renderer A/B benchmark")
        text = shutdown_logserver_and_collect(logserver, log_directory, baseline, pattern)
        logserver = None
        logs_collected = True
        (directory / "runtime.log").write_text(text, encoding="utf-8")
        signature = workload.validate_log(text, workload, args.require_hardware)
        finalized = parse_intervals(timing_file.read_text(encoding="utf-8"), symbols, finalized=True)
        validate_inactive_scopes(summarize_intervals(finalized), workload)
        return {"arm": arm.name, "block": block, "position": position, "reports": len(selected),
            "retained_report_range": [args.warmup_intervals, args.warmup_intervals + len(selected)],
            "scopes": summarize_intervals(selected), "runtime_signature": signature,
            "artifacts": str(directory), "cache_after": cache_identity(arm.runtime)}
    finally:
        primary_failure = sys.exc_info()[0] is not None
        errors = []
        try:
            terminate_process(process, "renderer A/B benchmark", handle)
        except Exception as error:
            errors.append(f"application cleanup: {error}")
        if log_directory is not None and not logs_collected:
            try:
                text = shutdown_logserver_and_collect(logserver, log_directory, baseline, pattern)
                (directory / "runtime.log").write_text(text, encoding="utf-8")
                logserver = None
            except Exception as error:
                errors.append(f"log collection: {error}")
        try:
            terminate_process(logserver, "logserver")
        except Exception as error:
            errors.append(f"logserver cleanup: {error}")
        if backend is not None:
            try:
                backend.close()
            except Exception as error:
                errors.append(f"window backend cleanup: {error}")
        if errors:
            write_json(directory / "cleanup_error.json", {"errors": errors})
            if not primary_failure:
                raise SmokeFailure("; ".join(errors))


def parse_args(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, allow_abbrev=False)
    for arm in ("baseline", "candidate"):
        for field in ("executable", "runtime", "source-manifest"):
            parser.add_argument(f"--{arm}-{field}", type=Path, required=True)
    parser.add_argument("--logserver-executable", type=Path, required=True)
    parser.add_argument("--output-directory", type=Path, required=True)
    parser.add_argument("--workload", choices=tuple(workloads()), default="transparent-multi")
    parser.add_argument("--blocks", type=int, default=8)
    parser.add_argument("--warmup-intervals", type=int, default=2)
    parser.add_argument("--sample-intervals", type=int, default=6)
    parser.add_argument("--minimum-frame-samples", type=int, default=100)
    parser.add_argument("--timeout", type=float, default=90.0)
    parser.add_argument("--order-seed", type=int, default=0)
    parser.add_argument("--analysis-seed", type=int, default=0)
    parser.add_argument("--practical-ms", type=float, default=.02)
    parser.add_argument("--practical-fraction", type=float, default=.03)
    parser.add_argument("--control-floor-ms", type=float, default=.015)
    parser.add_argument("--baseline-hardware-dispatches-per-range", type=int, choices=(1, 2), default=1)
    parser.add_argument("--candidate-hardware-dispatches-per-range", type=int, choices=(1, 2), default=1)
    parser.add_argument("--namesym", type=Path)
    parser.add_argument("--require-hardware", action="store_true")
    parser.add_argument("--plan-only", action="store_true")
    parser.add_argument("--application-arg", action="append", default=[])
    args = parser.parse_args(argv)
    workload = workloads()[args.workload]
    optical = workload.reflection_policy is not None and workload.reflection_policy.family.startswith("optical_")
    if not optical and (args.baseline_hardware_dispatches_per_range != 1 or args.candidate_hardware_dispatches_per_range != 1):
        parser.error("multiple native hardware dispatches per timing range are supported only for optical workloads")
    if workload.reflection_policy is not None and workload.reflection_policy.variant.mode in ("hardware", "hybrid"):
        args.require_hardware = True
    if args.blocks < 8 or args.blocks % 2:
        parser.error("blocks must be even and at least8, retaining complete AB/BA balance")
    if args.warmup_intervals < 2 or args.sample_intervals < 6 or args.minimum_frame_samples < 100:
        parser.error("at least2 warm-up reports,6 measured reports and100 completed frames are required")
    for field in ("timeout", "practical_ms", "practical_fraction", "control_floor_ms"):
        if not math.isfinite(getattr(args, field)) or getattr(args, field) <= 0:
            parser.error(f"{field} must be finite and positive")
    if any(any(word in value.lower() for word in ("gpudbg", "capture", "benchmark")) for value in args.application_arg):
        parser.error("validation/capture or nested benchmark arguments are incompatible with timing")
    for field, value in vars(args).copy().items():
        if isinstance(value, Path):
            setattr(args, field, value.resolve())
    args.no_logserver = False
    args.log_port = 0
    args.software_vulkan = "off"
    return args


def run(args):
    workload = workloads()[args.workload]
    arms = tuple(Arm(name, getattr(args, name + "_executable"), getattr(args, name + "_runtime"),
        getattr(args, name + "_source_manifest")) for name in ("baseline", "candidate"))
    validate_arm_separation(arms)
    if not args.logserver_executable.is_file():
        raise SmokeFailure("the explicit shared logserver executable is missing")
    if args.output_directory.exists() and any(args.output_directory.iterdir()):
        raise SmokeFailure("output directory must be empty; previous plans/trials are never overwritten")
    for arm in arms:
        if not arm.executable.is_file() or not arm.runtime.is_dir() or not arm.source_manifest.is_file():
            raise SmokeFailure(f"missing executable, runtime, or source manifest for {arm.name}")
    identities = {arm.name: freeze_arm(arm) for arm in arms}
    local_modules = ("reflection_benchmark", "window_capture_smoke", "gpu_timing_parse", "name_symbols", "smoke_volume_identity")
    shared_paths = [Path(__file__), args.logserver_executable]
    shared_paths.extend(Path(sys.modules[name].__file__) for name in local_modules)
    if args.namesym:
        shared_paths.append(args.namesym)
    shared = {str(path.resolve()): file_identity(path) for path in shared_paths}
    orders = [[arm.name for arm in row] for row in balanced_orders(arms, args.blocks, args.order_seed)]
    symbols = load_name_symbols(args.namesym, workload.observed_scopes)
    plan = {"workload": workload.name, "dimensions": [workload.width, workload.height], "orders": orders,
        "arms": identities, "shared_files": shared, "blocks": args.blocks, "planned_trials": args.blocks * 2,
        "settings": dict(workload.environment_overrides), "diagnostics": False, "capture": False,
        "warmup_intervals": args.warmup_intervals, "sample_intervals": args.sample_intervals,
        "minimum_frame_samples": args.minimum_frame_samples, "timeout_seconds": args.timeout,
        "timing_in_flight_ranges": 32, "scope_multipliers": dict(workload.scope_multipliers),
        "inactive_scopes": list(workload.inactive_scopes),
        "reflection_policy": asdict(workload.reflection_policy) if workload.reflection_policy is not None else None,
        "native_hardware_dispatches_per_range": {
            "baseline": args.baseline_hardware_dispatches_per_range,
            "candidate": args.candidate_hardware_dispatches_per_range,
        } if reflection.HARDWARE in workload.scopes else None,
        "hardware_range_policy": "one completed range per frame; optical variants may record two complementary native dispatches over the same admitted queue; declared counts require separate build qualification and never divide measured time",
        "depth_range_policy": "native mip count ranges per frame; per-range means describe one mip, with total measured mip work per frame reported separately",
        "inactive_scope_policy": "no completed inactive scope in warm-up, retained acquisition, or final shutdown reports",
        "scope_coverage": "completed timing ranges; publication skew bounded by max(2 ranges, 2% of expected), scaled by multiplicity",
        "primary_scope": FRAME, "secondary_scope": workload.secondary_scope, "controls": list(CONTROLS),
        "order_seed": args.order_seed, "analysis_seed": args.analysis_seed, "bootstrap_draws": 10000,
        "practical_ms": args.practical_ms, "practical_fraction": args.practical_fraction,
        "control_floor_ms": args.control_floor_ms, "require_hardware": args.require_hardware,
        "application_args": args.application_arg, "normalization": "sum(total_ms)/sum(gpu_samples)",
        "resource_policy": "only canonical contiguous runtime_pipeline_cache segments may mutate",
        "correctness": "qualify each frozen build separately before timing; no image correctness claim from timings"}
    args.output_directory.mkdir(parents=True, exist_ok=True)
    write_json(args.output_directory / "plan.json", plan)
    if args.plan_only:
        write_status("Frozen A/B plan written; use a new output directory for acquisition")
        return 0
    trials = []
    signature = None
    by_name = {arm.name: arm for arm in arms}
    try:
        for block, row in enumerate(orders):
            for position, name in enumerate(row):
                verify_frozen(arms, identities, shared)
                write_status(f"renderer A/B block {block + 1}/{args.blocks}: {name}")
                trial = acquire_trial(args, by_name[name], workload, block, position, symbols)
                verify_frozen(arms, identities, shared)
                if signature is not None and trial["runtime_signature"] != signature:
                    raise SmokeFailure("physical device, material route, shadow route or extent changed between arms/trials")
                signature = trial["runtime_signature"]
                write_json(Path(trial["artifacts"]) / "summary.json", trial)
                trials.append(trial)
                write_json(args.output_directory / "trials.json", trials)
        comparison = compare_trials(trials, orders, workload, args.analysis_seed,
            args.practical_ms, args.practical_fraction, args.control_floor_ms)
        write_json(args.output_directory / "report.json", {"plan": plan, "trials": trials, "comparison": comparison})
        write_status(f"Renderer A/B complete: {args.output_directory / 'report.json'}")
        return 0
    except Exception as error:
        write_json(args.output_directory / "failure.json", {"error": str(error), "completed_trials": len(trials)})
        raise


def main(argv=None):
    try:
        return run(parse_args(argv))
    except (SmokeFailure, SmokeSkip, OSError, ValueError, subprocess.SubprocessError) as error:
        write_status(f"FAIL: {error}")
        return 1


if __name__ == "__main__":
    sys.exit(main())
