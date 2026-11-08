#!/usr/bin/env python3
"""Six actual-framebuffer comparisons for the production reflection spatial-radius owner."""

from reflection_statistics import ReflectionStatistics

import argparse
import json
from pathlib import Path
import subprocess
import sys

import renderer_ab_benchmark as ab
from renderer_benchmark_identity import RendererBenchmarkIdentity
from reflection_image_analysis import ReflectionImageAnalysis
from reflection_smoke import capture_environment
from reflection_history_evidence import ReflectionHistoryEvidence
from reflection_spatial_owner_evidence import SELECTIONS, ReflectionSpatialOwnerEvidence
from reflection_spatial_owner_image_evidence import ReflectionSpatialOwnerImageEvidence
from smoke_volume_identity import file_identity
from window_capture_image import read_bmp_24_rows
from window_capture_smoke import SmokeFailure, window_capture_source_paths

LIT_CAPTURE_TIMEOUT_MARGIN_SECONDS = 90
LIT_MAXIMUM_CAPTURE_TIMEOUT_SECONDS = 180

LIT_GPUDBG = "--gpudbg"
LIT_GPUDBG_2 = "--gpudbg="
LIT_ROUGH = "rough"
LIT_HARDWARE = "hardware"
LIT_TIMEOUT = "--timeout"
LIT_EXPECT_LOG_MESSAGE = "--expect-log-message"
LIT_UTF_8 = "utf-8"
LIT_STATUS = "status"
LIT_CAPTURES = "captures"
LIT_QUALIFICATION_ARM_OR_SHARED_INPUT_CHAN = "qualification arm or shared input changed"
LIT_PASSED = "passed"
LIT_MAIN = "__main__"
LIT_APPEND = "append"


GPU_DEBUG_MARKERS = ("Loader: GPU debug validation enabled",)


def validate_gpu_debug(text, application_args):
    requested = any(value == LIT_GPUDBG or value.startswith(LIT_GPUDBG_2) for value in application_args)
    if requested:
        lines = [line.strip() for line in text.splitlines()]
        if any(lines.count(marker) != 1 for marker in GPU_DEBUG_MARKERS):
            raise SmokeFailure("requested GPU validation requires exactly one Loader activation marker")
    return {"requested": requested, "markers": list(GPU_DEBUG_MARKERS) if requested else []}


def capture_environment_for(selection):
    env = capture_environment(LIT_ROUGH, LIT_HARDWARE)
    for suffix, value in {"SPATIAL_OWNER": selection, "ROUGHNESS": ".4", "TEMPORAL": "1", "SPATIAL": "1", "HISTORY_SAMPLES": "1", "DIAGNOSTICS": "1", "FEEDBACK": "0", "SEED": "99", "EXTENT": "native", "OPTICAL_QUERIES": "16"}.items():
        env["NWB_REFLECTION_SMOKE_" + suffix] = value
    return env


def capture(args, selection):
    output = args.output_directory / (selection + ".bmp")
    log = output.with_suffix(".log")
    command = [sys.executable, str(Path(__file__).with_name("window_capture_smoke.py")), "--executable", str(args.executable), "--working-directory", str(args.working_directory),
        "--logserver-executable", str(args.logserver_executable), "--output", str(output), "--application-capture", "--application-capture-frame-count", "1", LIT_TIMEOUT, str(args.timeout), "--log-output", str(log)]
    for expected in ("ReflectionSmokeProject: case rough created", "ReflectionSmokeProject: shutdown", "ReflectionSmokeProject: hardware available", "ReflectionSmokeProject: reflection mode hardware",
            "Reflection resolve: hardware", "ReflectionSpatialOwnerCapture:", "ReflectionSpatialOwnerWork:"):
        command += [LIT_EXPECT_LOG_MESSAGE, expected]
    if any(value == LIT_GPUDBG or value.startswith(LIT_GPUDBG_2) for value in args.application_arg):
        for marker in GPU_DEBUG_MARKERS:
            command += [LIT_EXPECT_LOG_MESSAGE, marker]
    command.extend("--application-arg=" + value for value in args.application_arg)
    print("Capturing production spatial owner " + selection, flush=True)
    result = subprocess.run(command, env=capture_environment_for(selection), check=False, timeout=args.timeout + LIT_CAPTURE_TIMEOUT_MARGIN_SECONDS)
    if result.returncode:
        raise SmokeFailure(selection + " capture failed or skipped: " + str(result.returncode))
    frame = read_bmp_24_rows(output)
    ReflectionImageAnalysis.validate_frame(frame)
    if frame[:2] != (960, 720):
        raise SmokeFailure("owner capture has the wrong extent")
    text = log.read_text(encoding=LIT_UTF_8)
    gpu_debug = validate_gpu_debug(text, args.application_arg)
    statistics = ReflectionStatistics.parse_statistics(text)
    ReflectionStatistics.validate_statistics(statistics, LIT_ROUGH, LIT_HARDWARE, allow_zero_samples=True)
    evidence = ReflectionSpatialOwnerEvidence.validate_owner_evidence(text, selection, ReflectionHistoryEvidence.parse_history(text), statistics)
    return frame, {"selection": selection, "image": file_identity(output), "log": file_identity(log), "command": command, "evidence": evidence, "gpu_debug": gpu_debug}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, allow_abbrev=False)
    for name in ("executable", "working-directory", "logserver-executable", "source-manifest", "output-directory"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument(LIT_TIMEOUT, type=float, default=60)
    parser.add_argument("--application-arg", action=LIT_APPEND, default=[])
    args = parser.parse_args(argv)
    if not 0 < args.timeout <= LIT_MAXIMUM_CAPTURE_TIMEOUT_SECONDS:
        parser.error("timeout must be positive and at most180 seconds per capture")
    for key, value in vars(args).copy().items():
        if isinstance(value, Path):
            setattr(args, key, value.resolve())
    if args.output_directory.exists():
        parser.error("output directory must be new")
    for protected in (args.executable.parent, args.working_directory, args.source_manifest.parent, args.logserver_executable.parent):
        if args.output_directory == protected or args.output_directory.is_relative_to(protected) \
                or protected.is_relative_to(args.output_directory):
            parser.error("output must not overlap frozen source, binary, runtime or logger inputs")
    arm = ab.Arm("qualification", args.executable, args.working_directory, args.source_manifest)
    identity = RendererBenchmarkIdentity.freeze_arm(arm)
    shared = {str(Path(module.__file__).resolve()): file_identity(Path(module.__file__)) for module in (sys.modules[__name__], ab, sys.modules["renderer_wall_frame_evidence"], sys.modules["renderer_workload_log_policy"], sys.modules["renderer_workload_registry"], sys.modules["renderer_trial_comparison"], sys.modules["gpu_timing_evidence"], sys.modules["reflection_smoke"], sys.modules["reflection_statistics"], sys.modules["reflection_image_analysis"], sys.modules["reflection_roughness_oracle"], sys.modules["reflection_history_evidence"], sys.modules["reflection_spatial_owner_evidence"], sys.modules["reflection_spatial_owner_image_evidence"], sys.modules["renderer_benchmark_identity"], sys.modules["framebuffer_png_encoder"],
            sys.modules["window_capture_smoke"], sys.modules["smoke_volume_identity"])}
    shared.update({str(path): file_identity(path) for path in window_capture_source_paths()})
    logger_identity = RendererBenchmarkIdentity.binary_identity(args.logserver_executable)
    for name, saved in logger_identity.items():
        shared[str(args.logserver_executable.parent / name)] = saved
    shared[str(Path(sys.executable).resolve())] = file_identity(Path(sys.executable))
    args.output_directory.mkdir(parents=True)
    report = {LIT_STATUS: "incomplete", "identity": identity, "shared_inputs": shared, "logserver_binaries": logger_identity, LIT_CAPTURES: []}
    try:
        frames = {}
        for selection in SELECTIONS:
            if RendererBenchmarkIdentity.freeze_arm(arm) != identity or RendererBenchmarkIdentity.binary_identity(args.logserver_executable) != logger_identity \
                    or any(file_identity(Path(path)) != saved for path, saved in shared.items()):
                raise SmokeFailure(LIT_QUALIFICATION_ARM_OR_SHARED_INPUT_CHAN)
            frame, evidence = capture(args, selection)
            frames[selection] = frame
            report[LIT_CAPTURES].append(evidence)
        if RendererBenchmarkIdentity.freeze_arm(arm) != identity or RendererBenchmarkIdentity.binary_identity(args.logserver_executable) != logger_identity \
                or any(file_identity(Path(path)) != saved for path, saved in shared.items()):
            raise SmokeFailure(LIT_QUALIFICATION_ARM_OR_SHARED_INPUT_CHAN)
        report["images"] = ReflectionSpatialOwnerImageEvidence.compare_images(frames)
        report[LIT_STATUS] = LIT_PASSED
    except (SmokeFailure, OSError, ValueError, subprocess.TimeoutExpired) as error:
        report["error"] = str(error)
        print("FAIL: " + str(error), file=sys.stderr)
    finally:
        (args.output_directory / "owner_qualification.json").write_text(json.dumps(report, indent=2, sort_keys=True, allow_nan=False) + "\n", encoding=LIT_UTF_8)
    if report[LIT_STATUS] != LIT_PASSED:
        return 1
    print("PASS: six production-owner captures, accepted phases and exact fresh/transition pixels")
    return 0


if __name__ == LIT_MAIN:
    raise SystemExit(main())
