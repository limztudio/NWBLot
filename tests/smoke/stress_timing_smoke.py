#!/usr/bin/env python3
"""One self-exiting stress presentation measurement using the existing runtime process owners."""

import argparse
import json
import math
from pathlib import Path
import subprocess
import sys
import tempfile
from types import SimpleNamespace

LIT_MINIMUM_TIMEOUT_SECONDS = 35
LIT_MAXIMUM_TIMEOUT_SECONDS = 300
LIT_MAXIMUM_FIXED_DELTA_SECONDS = .25


sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "ab"))
from gpu_timing_parse import MAXIMUM_INCONCLUSIVE_POSITIVE_INTERVALS
from renderer_workload_log_policy import RendererWorkloadLogPolicy
from renderer_benchmark_identity import RendererBenchmarkIdentity
from smoke_volume_identity import file_identity
import caustic_quality_smoke
import surfel_gi_quality_smoke
import smoke_cpu_gpu_timing
from stress_csg_profile import CsgProfileEvidence
from stress_launch_environment import StressLaunchEnvironment
from stress_reflection_diagnostics import ReflectionDiagnostics
from stress_presentation_evidence import PresentationEvidence, INTERVAL, DONE
from stress_shadow_evidence import ShadowEvidence, SHADOW_TRANSPARENT_SAMPLING, SHADOW_RECEIVER_RESOLUTION, SOFTWARE_SHADOW_BACKENDS, SOFTWARE_SHADOW_COVERAGE, SOFTWARE_SHADOW_BLOCKER_SEARCH, SOFTWARE_SHADOW_CAPTURE_CADENCE
from smoke_failure_reporting import write_failure_report
from window_capture_termination import CaptureProcessTermination
from window_capture_smoke import (finish_logserver_cleanup, SmokeFailure, SmokeSkip, build_launch_environment, launch_logserver, launch_testbed, require_normal_process_exit,
    shutdown_logserver_and_collect, write_status,)

LIT_UNSUPPORTED_RATIO = "unsupported_ratio"
LIT_EXTERIOR_ELIGIBLE_RATIO = "exterior_eligible_ratio"
LIT_PHYSICAL_QUERIES_PER_HARDWARE_RAY = "physical_queries_per_hardware_ray"
LIT_INCONCLUSIVE = "inconclusive"

LIT_REFERENCE_THREE = "reference_three"
LIT_QUARTER = "quarter"
LIT_AUTOMATIC = "automatic"
LIT_TRACE = "trace"
LIT_REFERENCE = "reference"
LIT_REFERENCE_GRID9 = "reference_grid9"
LIT_EVERY_FRAME = "every_frame"
LIT_SIGNATURE = "signature"
LIT_REQUESTED = "requested"
LIT_STATUS = "status"
LIT_SAMPLE_COUNT = "sample_count"
LIT_FPS = "fps"
LIT_PRESENTATIONS = "presentations"
LIT_SECONDS = "seconds"
LIT_STRESS_PRESENTATION_TIMING = "stress presentation timing"
LIT_MEASUREMENT = "measurement"
LIT_INTERVALS = "intervals"
LIT_WIDTH = "width"
LIT_HEIGHT = "height"
LIT_WORKLOAD = "workload"
LIT_OPTICAL_REFLECTION = "optical_reflection"
LIT_CSG_PROFILE = "csg_profile"
LIT_NONE = "none"
LIT_WAIST_BANDS = "waist_bands"
LIT_MOTION = "motion"
LIT_NWB = "NWB_"
LIT_GPU_TIMING_TXT = "gpu_timing.txt"
LIT_CPU_DIAGNOSTICS = "cpu_diagnostics"
LIT_CPU_GPU_TIMING_TXT = "cpu_gpu_timing.txt"
LIT_UTF_8 = "utf-8"
LIT_MINIMUM_FPS = "minimum_fps"
LIT_EXECUTABLE = "executable"
LIT_WORKING_DIRECTORY = "working_directory"
LIT_PROCESS_TAIL_TXT = "process_tail.txt"
LIT_RUNTIME_LOG = "runtime.log"
LIT_DIAGNOSTIC_ONLY = "diagnostic_only"
LIT_CPU_GPU_SUMMARY_JSON = "cpu_gpu_summary.json"
LIT_PERFORMANCE_TARGET = "performance_target"
LIT_PASSED = "passed"
LIT_STORE_TRUE = "store_true"
LIT_FAILURE_JSON = "failure.json"
LIT_ERROR = "error"
LIT_MAIN = "__main__"
LIT_APPEND = "append"


def reserve_output(requested, protected):
    if requested is None:
        return Path(tempfile.mkdtemp(prefix="nwb_stress_presentation_timing_")).resolve()
    output = requested.resolve()
    if output.exists():
        raise SmokeFailure("output must be new; prior evidence is never overwritten")
    for item in protected:
        item = item.resolve()
        if output == item or output.is_relative_to(item) or item.is_relative_to(output):
            raise SmokeFailure("output overlaps a protected input")
    output.mkdir(parents=True, exist_ok=False)
    return output


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n", encoding=LIT_UTF_8)


def identities(args, helpers):
    return {"renderer": RendererBenchmarkIdentity.binary_identity(args.executable), "runtime": RendererBenchmarkIdentity.runtime_identity(args.working_directory), "logger": None if args.no_logserver else RendererBenchmarkIdentity.binary_identity(args.logserver_executable), "python": file_identity(Path(sys.executable)),
        "helpers": {str(path): file_identity(path) for path in helpers}}


def validate_performance_target_request(args):
    minimum = args.minimum_fps
    if minimum is None:
        return
    if not math.isfinite(minimum) or minimum <= 0:
        raise SmokeFailure("minimum FPS must be finite and positive")
    if args.cpu_diagnostics or args.reflection_diagnostics:
        raise SmokeFailure("minimum FPS cannot qualify CPU or reflection diagnostic runs")


def performance_target(measurement, intervals, minimum):
    # Derive qualification from the validated accepted count and wall clock, not rounded log FPS or GPU samples.
    observed = measurement[LIT_PRESENTATIONS] / measurement[LIT_SECONDS]
    # A stalled window leaves few positive intervals, so its target result is inconclusive.
    positive = sum(row[LIT_PRESENTATIONS] > 0 for row in intervals)
    inconclusive = minimum is not None and positive <= MAXIMUM_INCONCLUSIVE_POSITIVE_INTERVALS
    return dict(requested=minimum is not None, minimum_fps=minimum, comparison=">", observed_fps=observed, positive_intervals=positive, inconclusive=inconclusive, source="accepted_native_presentations / steady_clock_seconds",
        passed=None if minimum is None or inconclusive else observed > minimum)


def acquire(args, output):
    validate_performance_target_request(args)
    helpers = sorted({Path(module.__file__).resolve() for module in list(sys.modules.values()) if getattr(module, "__file__", None) and Path(module.__file__).suffix == ".py"
        and ("tests" in Path(module.__file__).parts or Path(module.__file__).resolve() == Path(__file__).resolve())})
    before = identities(args, helpers)
    env = StressLaunchEnvironment.launch_environment(build_launch_environment(args), args, output)
    launch = SimpleNamespace(**vars(args), log_port=0)
    write_json(output / "launch.json", {LIT_EXECUTABLE: str(args.executable), LIT_WORKING_DIRECTORY: str(args.working_directory), "application_args": args.application_arg, "timeout_seconds": args.timeout,
        "environment": {key: value for key, value in env.items() if key.startswith((LIT_NWB, "VK_"))}, "identity_before": before, LIT_MINIMUM_FPS: args.minimum_fps})
    process = logserver = log_directory = None
    baseline, pattern = {}, ""
    collected = False
    code = None
    try:
        logserver, port, log_directory, baseline, pattern = launch_logserver(launch, args.executable, env)
        process = launch_testbed(launch, args.executable, env, port)
        try:
            process.wait(timeout=args.timeout)
        except subprocess.TimeoutExpired as error:
            raise SmokeFailure("stress measurement did not self-exit before timeout") from error
        code, tail = CaptureProcessTermination.terminate_process(process, LIT_STRESS_PRESENTATION_TIMING)
        process = None
        (output / LIT_PROCESS_TAIL_TXT).write_text(tail, encoding=LIT_UTF_8)
        require_normal_process_exit(code, tail, LIT_STRESS_PRESENTATION_TIMING)
        text = shutdown_logserver_and_collect(logserver, log_directory, baseline, pattern)
        logserver = None
        collected = True
        (output / LIT_RUNTIME_LOG).write_text(text, encoding=LIT_UTF_8)
        result = PresentationEvidence(args.application_arg, args.reflection_diagnostics, args.characters_per_class).parse_runtime_log(text, code)
        result[LIT_CPU_DIAGNOSTICS] = smoke_cpu_gpu_timing.verify_capture(text, output / LIT_CPU_GPU_TIMING_TXT, result[LIT_MEASUREMENT], args.cpu_diagnostics, "stress")
        result[LIT_DIAGNOSTIC_ONLY] = args.cpu_diagnostics or args.reflection_diagnostics
        result["performance_qualification"] = not result[LIT_DIAGNOSTIC_ONLY]
        if args.cpu_diagnostics:
            write_json(output / LIT_CPU_GPU_SUMMARY_JSON, result[LIT_CPU_DIAGNOSTICS])
        result["software_shadow_settings"] = ShadowEvidence(args).verify_software(text)
        result[LIT_CSG_PROFILE] = CsgProfileEvidence(args, INTERVAL, DONE).verify(text)
        result["caustic_quality_settings"] = caustic_quality_smoke.verify_settings(text, args.caustic_photon_grid_divisor)
        result["surfel_gi_quality_settings"] = surfel_gi_quality_smoke.verify_settings(text, args.surfel_gi_resolve_resolution, (result[LIT_WIDTH], result[LIT_HEIGHT]))
        result["shadow_quality_settings"] = ShadowEvidence(args).verify_quality(text, (result[LIT_WIDTH], result[LIT_HEIGHT]))
        result["reflection_quality_settings"] = ReflectionDiagnostics.verify_reflection_quality_settings(text, args, result[LIT_OPTICAL_REFLECTION])
        result["runtime_signature"] = RendererWorkloadLogPolicy.device_material_signature(text)
        result[LIT_MOTION] = {"mode": "rotating" if args.animate else "fixed", "simulation_clock": "wall" if args.animate else "fixed_step", "spin_angle": None if args.animate else args.spin_angle,
            "fixed_delta_seconds": None if args.animate else args.fixed_delta_seconds}
        after = identities(args, helpers)
        if before != after:
            raise SmokeFailure("renderer, resources, logger, interpreter or helper identity changed during acquisition")
        result[LIT_PERFORMANCE_TARGET] = performance_target(result[LIT_MEASUREMENT], result[LIT_INTERVALS], args.minimum_fps)
        result.update(schema=1, passed=result[LIT_PERFORMANCE_TARGET][LIT_PASSED] is not False and not result[LIT_PERFORMANCE_TARGET][LIT_INCONCLUSIVE], capture_validated=True, exit_code=code, identity_before=before, identity_after=after,
            raw_files={name: file_identity(output / name) for name in (LIT_RUNTIME_LOG, LIT_PROCESS_TAIL_TXT, LIT_GPU_TIMING_TXT, LIT_CPU_GPU_TIMING_TXT, LIT_CPU_GPU_SUMMARY_JSON) if (output / name).is_file()})
        return result
    finally:
        primary_failure = sys.exc_info()[0] is not None
        errors = []
        if process is not None:
            try:
                _, tail = CaptureProcessTermination.terminate_process(process, LIT_STRESS_PRESENTATION_TIMING)
                (output / LIT_PROCESS_TAIL_TXT).write_text(tail, encoding=LIT_UTF_8)
            except Exception as error:
                errors.append(f"renderer cleanup: {error}")
        if not collected and log_directory is not None:
            try:
                text = shutdown_logserver_and_collect(logserver, log_directory, baseline, pattern)
                (output / LIT_RUNTIME_LOG).write_text(text, encoding=LIT_UTF_8)
                logserver = None
            except Exception as error:
                errors.append(f"failure-path log collection: {error}")
        finish_logserver_cleanup(logserver, output, errors, primary_failure, CaptureProcessTermination.terminate_process, write_json)


def parse_args(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", required=True, type=Path)
    parser.add_argument("--working-directory", required=True, type=Path)
    parser.add_argument("--logserver-executable", type=Path)
    parser.add_argument("--no-logserver", action=LIT_STORE_TRUE)
    parser.add_argument("--output-directory", type=Path)
    parser.add_argument("--timeout", type=float, default=90.0, help="Whole-process budget including startup; warmup and measurement durations stay fixed.")
    parser.add_argument("--minimum-fps", type=float, help="Require strictly greater accepted presentation FPS; equality fails. Diagnostic runs cannot qualify, and ten or fewer positive intervals are inconclusive.")
    parser.add_argument("--characters-per-class", type=int, choices=(5, 10), default=10, help="Ten per class is the twenty-body target; five preserves the historical comparison layout/camera.")
    parser.add_argument("--csg-profile", choices=(LIT_NONE, LIT_WAIST_BANDS), default=LIT_NONE, help="Waist bands add two moving box cutters to all twenty mesh children.")
    parser.add_argument("--shadow-transparent-sampling", choices=tuple(SHADOW_TRANSPARENT_SAMPLING), default=LIT_REFERENCE_THREE, help="Temporal-one uses one transparent shadow sample after accepted temporal history, on either HW or SW.")
    parser.add_argument("--shadow-receiver-resolution", choices=tuple(SHADOW_RECEIVER_RESOLUTION), default=LIT_QUARTER, help="Quarter reduces shadow tracing and filtering density; native geometry, final output and light-space map sizes stay unchanged.")
    parser.add_argument("--software-shadow-backend", choices=tuple(SOFTWARE_SHADOW_BACKENDS), default=LIT_AUTOMATIC)
    parser.add_argument("--software-shadow-coverage", choices=tuple(SOFTWARE_SHADOW_COVERAGE), default=LIT_REFERENCE, help="Fitted-volume coverage uses empty directional margins and retains receivers beyond a complete map's far plane.")
    parser.add_argument("--software-shadow-blocker-search", choices=tuple(SOFTWARE_SHADOW_BLOCKER_SEARCH), default=LIT_REFERENCE_GRID9, help="Compact cross uses five blocker taps; center1 uses only the fully checked center and may narrow penumbras.")
    parser.add_argument("--software-shadow-capture-cadence", choices=tuple(SOFTWARE_SHADOW_CAPTURE_CADENCE), default=LIT_EVERY_FRAME, help="Reuse accepted light-space captures for up to one or two frames, including CSG; receiver shading remains current.")
    parser.add_argument("--software-shadow-budget-mib", type=int, default=256, help="Requested shadow-map storage budget in MiB (1 through 4095).")
    parser.add_argument("--software-shadow-directional-resolution", type=int, default=512)
    parser.add_argument("--software-shadow-point-resolution", type=int, default=256)
    motion = parser.add_mutually_exclusive_group()
    motion.add_argument("--spin-angle", type=float, default=0.6)
    motion.add_argument("--animate", action=LIT_STORE_TRUE, help="Continuously rotate the bodies using wall-time simulation; excludes fixed yaw and fixed simulation delta.")
    parser.add_argument("--fixed-delta-seconds", type=float)
    parser.add_argument("--application-arg", action=LIT_APPEND, default=[])
    caustic_quality_smoke.add_arguments(parser)
    surfel_gi_quality_smoke.add_arguments(parser)
    parser.set_defaults(surfel_gi_resolve_resolution=LIT_QUARTER)
    parser.add_argument("--reflection-screen-steps", type=int, default=96, help="Maximum SSR hierarchy iterations per attempted surface ray (8 through 256); lower budgets fall back normally.")
    parser.add_argument("--cpu-diagnostics", action=LIT_STORE_TRUE, help="Capture existing CPU+GPU timing with memory off; buffered publication evidence is diagnostic only, not a performance result.")
    parser.add_argument("--reflection-diagnostics", action=LIT_STORE_TRUE, help="Enable accepted reflection-path counters; diagnostic runs are separate from performance comparisons.")
    args = parser.parse_args(argv)
    if args.csg_profile != LIT_NONE:
        if args.characters_per_class != 10 or args.software_shadow_backend == LIT_TRACE:
            parser.error("waist_bands requires ten bodies per class and automatic or light-space shadows")
    try:
        validate_performance_target_request(args)
    except SmokeFailure as error:
        parser.error(str(error))
    if not 8 <= args.reflection_screen_steps <= 256:
        parser.error("reflection screen steps must be 8 through 256")
    for key in (LIT_EXECUTABLE, LIT_WORKING_DIRECTORY, "logserver_executable"):
        if getattr(args, key) is not None:
            setattr(args, key, getattr(args, key).resolve())
    if not args.executable.is_file() or not args.working_directory.is_dir():
        parser.error("existing renderer and runtime paths are required")
    if not args.no_logserver and (args.logserver_executable is None or not args.logserver_executable.is_file()):
        parser.error("an existing logger is required unless --no-logserver is selected")
    if not math.isfinite(args.timeout) or not LIT_MINIMUM_TIMEOUT_SECONDS < args.timeout <= LIT_MAXIMUM_TIMEOUT_SECONDS:
        parser.error(f"timeout must be finite and greater than {LIT_MINIMUM_TIMEOUT_SECONDS} through {LIT_MAXIMUM_TIMEOUT_SECONDS} seconds")
    if not math.isfinite(args.spin_angle) or not 0 <= args.spin_angle < math.tau:
        parser.error("spin angle must be finite in [0, 2pi)")
    if args.animate and args.fixed_delta_seconds is not None:
        parser.error("--animate cannot be combined with --fixed-delta-seconds")
    if args.fixed_delta_seconds is None and not args.animate:
        args.fixed_delta_seconds = .016666667
    if args.fixed_delta_seconds is not None and (not math.isfinite(args.fixed_delta_seconds) or not 0 < args.fixed_delta_seconds <= LIT_MAXIMUM_FIXED_DELTA_SECONDS):
        parser.error("fixed simulation delta must be finite in (0, .25]")
    if not 1 <= args.software_shadow_budget_mib <= 4095:
        parser.error("software shadow budget must be from 1 through 4095 MiB")
    for name in ("software_shadow_directional_resolution", "software_shadow_point_resolution"):
        if not 32 <= getattr(args, name) <= 2048:
            parser.error("software shadow resolutions must be from 32 through 2048")
    args.software_vulkan = "off"
    return args


def main(argv=None):
    output = None
    try:
        args = parse_args(argv)
        protected = [args.executable.parent, args.working_directory, Path(__file__).resolve().parent]
        if args.logserver_executable is not None:
            protected.append(args.logserver_executable.parent)
        output = reserve_output(args.output_directory, protected)
        write_status(f"Stress presentation timing artifacts: {output}")
        result = acquire(args, output)
        write_json(output / "result.json", result)
        target = result[LIT_PERFORMANCE_TARGET]
        if target[LIT_INCONCLUSIVE]:
            raise SmokeFailure(f"INCONCLUSIVE: {target['positive_intervals']} positive presentation intervals; more than {MAXIMUM_INCONCLUSIVE_POSITIVE_INTERVALS} are required for an FPS target")
        if target[LIT_PASSED] is False:
            raise SmokeFailure(f"accepted presentation FPS {target['observed_fps']:.9g} must exceed {target[LIT_MINIMUM_FPS]:.9g}")
        write_status(result[LIT_WORKLOAD][LIT_SIGNATURE])
        write_status(f"PASS: {result[LIT_MEASUREMENT][LIT_FPS]:.4f} accepted presentations/s over {result[LIT_MEASUREMENT][LIT_SECONDS]:.6f}s")
        if result[LIT_DIAGNOSTIC_ONLY]:
            write_status("DIAGNOSTIC ONLY: profiling overhead is present; use a separate identical GPU-only control for final FPS.")
        optical = result[LIT_OPTICAL_REFLECTION]
        write_status(f"Optical reflection: {optical[LIT_STATUS]} (query activity alone does not certify optical correctness)")
        if optical[LIT_REQUESTED]:
            write_status(f"Reflection samples={optical[LIT_SAMPLE_COUNT]} unsupported_ratio={optical[LIT_UNSUPPORTED_RATIO]} " f"exterior_eligible_ratio={optical[LIT_EXTERIOR_ELIGIBLE_RATIO]} physical_queries_per_hardware_ray={optical[LIT_PHYSICAL_QUERIES_PER_HARDWARE_RAY]}")
        return 0
    except SmokeSkip as error:
        if output is not None:
            write_failure_report(write_json, output / LIT_FAILURE_JSON, {LIT_PASSED: False, "skipped": True, LIT_ERROR: str(error)})
        write_status(f"SKIP: {error}")
        return 77
    except (SmokeFailure, OSError) as error:
        if output is not None:
            write_failure_report(write_json, output / LIT_FAILURE_JSON, {LIT_PASSED: False, LIT_ERROR: str(error)})
        write_status(f"FAIL: {error}")
        return 1


if __name__ == LIT_MAIN:
    sys.exit(main())
