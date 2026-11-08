#!/usr/bin/env python3
"""Qualify two frozen caustic camera footprints with actual on/off framebuffer readbacks.

This is a separate correctness acquisition. It never emits or consumes benchmark timings.
The receiver mask is derived from the unchanged ground plane and conservative sphere;
the measured footprint is visible positive image gain, not an internal active-tile count.
"""

import argparse
import json
import math
import os
from pathlib import Path
import subprocess
import sys
from types import SimpleNamespace

from smoke_volume_identity import file_identity
from renderer_benchmark_identity import RendererBenchmarkIdentity
from renderer_workload_registry import RendererWorkloadRegistry
from caustic_workload import CausticWorkload, PRESETS, FRAME_COUNT, WIDTH, HEIGHT
from caustic_footprint import (CausticFootprint, LIT_POPULATED, LIT_SPARSE, LIT_CAMERA_HEIGHT, LIT_VERTICAL_FOV_DEGREES, LIT_SPHERE_RADIUS, LIT_SPHERE_EXCLUSION_MARGIN,
    LIT_GROUND_Y, LIT_GROUND_X, LIT_GROUND_Z, LIT_RECEIVER_EDGE_MARGIN, LIT_POSITIVE_CHANNEL_SUM_THRESHOLD, LIT_MINIMUM_POSITIVE_PIXELS, LIT_MINIMUM_CHANNEL_GAIN,
    LIT_TILE_EXTENT, LIT_MAXIMUM_SPARSE_PIXEL_RATIO, LIT_MAXIMUM_SPARSE_TILE_RATIO, LIT_POSITIVE_PIXELS, LIT_POSITIVE_CHANNEL_GAIN, LIT_POSITIVE_TILES)
from window_capture_image import read_bmp_24_rows
from window_capture_smoke import SmokeFailure

LIT_CAPTURE_TIMEOUT_MARGIN_SECONDS = 90

LIT_GPUDBG = "--gpudbg"
LIT_GPUDBG_2 = "--gpudbg="
LIT_TRANSPARENTMULTISMOKEPROJECT_SHUTDOWN = "TransparentMultiSmokeProject: shutdown"
LIT_UTF_8 = "utf-8"
LIT_SCHEMA = "schema"
LIT_POLICY = "policy"
LIT_ARM = "arm"
LIT_ON = "on"
LIT_CAPTURES = "captures"
LIT_QUALIFICATION_TOOL = "qualification_tool"
LIT_LAUNCHER = "launcher"
LIT_WINDOW_CAPTURE_SMOKE_PY = "window_capture_smoke.py"
LIT_LOGSERVER_PATH = "logserver_path"
LIT_LOGSERVER = "logserver"
LIT_LOGSERVER_BINARIES = "logserver_binaries"
LIT_SETTINGS = "settings"
LIT_IMAGE = "image"
LIT_LOG = "log"
LIT_PATH = "path"
LIT_IDENTITY = "identity"
LIT_APPLICATION_ARGS = "application_args"
LIT_RUNTIME = "runtime"
LIT_EXECUTABLE = "executable"
LIT_TIMEOUT_SECONDS = "timeout_seconds"
LIT_COMMAND = "command"
LIT_ON_2 = "_on"
LIT_OFF = "_off"
LIT_METRICS = "metrics"
LIT_LOG_2 = ".log"
LIT_TIMEOUT = "--timeout"
LIT_EXPECT_LOG_MESSAGE = "--expect-log-message"
LIT_FROZEN_CAUSTIC_ARM_OR_LOGGER_DEPENDENC = "frozen caustic arm or logger dependency inventory changed during capture"
LIT_MAIN = "__main__"
LIT_APPEND = "append"


GPU_DEBUG_MARKERS = ("Loader: GPU debug validation enabled",)


def validate_gpu_debug(text, application_args):
    requested = any(value == LIT_GPUDBG or value.startswith(LIT_GPUDBG_2) for value in application_args)
    if requested:
        lines = [line.strip() for line in text.splitlines()]
        if any(lines.count(marker) != 1 for marker in GPU_DEBUG_MARKERS):
            raise SmokeFailure("requested GPU validation requires exactly one Loader activation marker")


def validate_output_path(output, protected_roots):
    if output.exists():
        raise SmokeFailure("qualification output must be new; old evidence is never replaced")
    for protected in protected_roots:
        if output == protected or output.is_relative_to(protected) or protected.is_relative_to(output):
            raise SmokeFailure("qualification output overlaps frozen source, binary, runtime or logger inputs")


SCHEMA = "caustic-visible-footprint-v1"
POLICY = {"width": WIDTH, "height": HEIGHT, "presentation_count": FRAME_COUNT, LIT_CAMERA_HEIGHT: .85, "camera_pitch": 0, LIT_VERTICAL_FOV_DEGREES: 60, "sphere_center": [0, .85, 0], LIT_SPHERE_RADIUS: .7, LIT_SPHERE_EXCLUSION_MARGIN: .001,
    LIT_GROUND_Y: -.08, LIT_GROUND_X: [-1.75, 1.75], LIT_GROUND_Z: [-1.47, 1.63], LIT_RECEIVER_EDGE_MARGIN: .02, LIT_POSITIVE_CHANNEL_SUM_THRESHOLD: 24, LIT_MINIMUM_POSITIVE_PIXELS: 100, LIT_MINIMUM_CHANNEL_GAIN: 1500,
    LIT_TILE_EXTENT: 16, LIT_MAXIMUM_SPARSE_PIXEL_RATIO: .8, LIT_MAXIMUM_SPARSE_TILE_RATIO: .8, "scope": "visible caustic contribution on unoccluded ground; no internal wavelet occupancy claim"}


def validate_report(path, identity):
    report = json.loads(path.read_text(encoding=LIT_UTF_8))
    if report.get(LIT_SCHEMA) != SCHEMA or report.get(LIT_POLICY) != POLICY or report.get(LIT_ARM) != identity:
        raise SmokeFailure("caustic qualification policy or frozen arm identity changed")
    expected = {preset + "_" + state for preset in PRESETS for state in (LIT_ON, "off")}
    if set(report.get(LIT_CAPTURES, {})) != expected:
        raise SmokeFailure("qualification must retain all four real on/off captures")
    if report.get(LIT_QUALIFICATION_TOOL) != file_identity(Path(__file__)) \
        or report.get(LIT_LAUNCHER) != file_identity(Path(__file__).with_name(LIT_WINDOW_CAPTURE_SMOKE_PY)) \
        or report.get("footprint_analysis") != file_identity(Path(__file__).with_name("caustic_footprint.py")) \
        or report.get("workload_analysis") != file_identity(Path(__file__).with_name("caustic_workload.py")):
        raise SmokeFailure("qualification producer, footprint or workload analysis or framebuffer launcher changed")
    # Commands belong to the qualified arm's physical source tree, even when the other arm replays them.
    source = identity.get("source", {})
    manifest = source.get("manifest")
    if not isinstance(manifest, str):
        raise SmokeFailure("qualification requires a pinned frozen source launcher")
    launcher = (Path(manifest).resolve().parent / "source/tests/smoke/window_capture_smoke.py").resolve()
    if not launcher.is_file() or source.get("files", {}).get(str(launcher)) != file_identity(launcher)["sha256"] \
        or file_identity(launcher) != report.get(LIT_LAUNCHER):
        raise SmokeFailure("qualification frozen launcher is unpinned or its bytes changed")
    frames, metrics, paths = {}, {}, [path]
    logger = Path(report[LIT_LOGSERVER_PATH])
    import renderer_ab_benchmark as benchmark
    logger_binaries = RendererBenchmarkIdentity.binary_identity(logger)
    if file_identity(logger) != report[LIT_LOGSERVER] or logger_binaries != report.get(LIT_LOGSERVER_BINARIES):
        raise SmokeFailure("qualification logserver or dependency inventory changed")
    paths.extend(logger.parent / name for name in logger_binaries)
    for key, record in report[LIT_CAPTURES].items():
        preset, state = key.rsplit("_", 1)
        if record.get(LIT_SETTINGS) != CausticWorkload.environment(preset, state == LIT_ON):
            raise SmokeFailure("capture settings differ from the paired policy")
        files = {}
        for kind in (LIT_IMAGE, LIT_LOG):
            evidence = (path.parent / record[kind][LIT_PATH]).resolve()
            if file_identity(evidence) != record[kind][LIT_IDENTITY]:
                raise SmokeFailure("qualification raw " + kind + " changed")
            paths.append(evidence)
            files[kind] = evidence
        text = files[LIT_LOG].read_text(encoding=LIT_UTF_8)
        validate_gpu_debug(text, report[LIT_APPLICATION_ARGS])
        actual = CausticWorkload(WIDTH, HEIGHT).validate_log(text, record[LIT_SETTINGS], capture=True)
        if actual != record[LIT_RUNTIME]:
            raise SmokeFailure("qualification runtime metadata does not match its actual log")
        args = SimpleNamespace(executable=identity[LIT_EXECUTABLE], runtime=identity[LIT_RUNTIME], logserver_executable=logger, timeout=report[LIT_TIMEOUT_SECONDS], application_arg=report[LIT_APPLICATION_ARGS])
        if record.get(LIT_COMMAND) != capture_command(args, files[LIT_IMAGE], launcher=launcher):
            raise SmokeFailure("qualification launch command changed its exact capture contract")
        frames[key] = read_bmp_24_rows(files[LIT_IMAGE])
        if frames[key][:2] != (WIDTH, HEIGHT):
            raise SmokeFailure("qualification must use the native 1280x900 framebuffer")
    analysis = CausticFootprint(POLICY, PRESETS)
    for preset, distance in PRESETS.items():
        metrics[preset] = analysis.footprint(frames[preset + LIT_ON_2], frames[preset + LIT_OFF], distance)
    analysis.validate_metrics(metrics)
    if metrics != report.get(LIT_METRICS):
        raise SmokeFailure("qualification metrics differ from raw image analysis")
    return {"report": str(path), LIT_IDENTITY: file_identity(path), LIT_METRICS: metrics, LIT_POLICY: POLICY, LIT_CAPTURES: report[LIT_CAPTURES]}, paths


def capture_command(args, image, *, launcher=None):
    if launcher is None:
        launcher = Path(__file__).with_name(LIT_WINDOW_CAPTURE_SMOKE_PY)
    command = [sys.executable, str(launcher), "--executable", str(args.executable), "--working-directory", str(args.runtime), "--logserver-executable", str(args.logserver_executable), "--output", str(image),
        "--log-output", str(image.with_suffix(LIT_LOG_2)), "--application-capture", "--application-capture-frame-count", str(FRAME_COUNT), LIT_TIMEOUT, str(args.timeout), LIT_EXPECT_LOG_MESSAGE, LIT_TRANSPARENTMULTISMOKEPROJECT_SHUTDOWN]
    command.extend("--application-arg=" + argument for argument in args.application_arg)
    if any(value == LIT_GPUDBG or value.startswith(LIT_GPUDBG_2) for value in args.application_arg):
        for marker in GPU_DEBUG_MARKERS:
            command.extend((LIT_EXPECT_LOG_MESSAGE, marker))
    return command


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, allow_abbrev=False)
    for name in (LIT_EXECUTABLE, LIT_RUNTIME, "source-manifest", "logserver-executable", "output-directory"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument(LIT_TIMEOUT, type=float, default=90)
    parser.add_argument("--application-arg", action=LIT_APPEND, default=[])
    args = parser.parse_args(argv)
    if not math.isfinite(args.timeout) or args.timeout <= 0:
        parser.error("timeout must be finite and positive")
    for name, value in vars(args).copy().items():
        if isinstance(value, Path):
            setattr(args, name, value.resolve())
    # Delayed import avoids the runner/qualification module cycle and reuses its exact frozen-arm contract.
    import renderer_ab_benchmark as benchmark
    arm = benchmark.Arm("qualification", args.executable, args.runtime, args.source_manifest)
    output = args.output_directory
    owns_output = False
    try:
        validate_output_path(output, (args.executable.parent, args.runtime, args.source_manifest.parent, args.logserver_executable.parent, Path(__file__).resolve().parent))
        if not args.logserver_executable.is_file():
            raise SmokeFailure("explicit logserver executable is missing")
        identity = RendererBenchmarkIdentity.freeze_arm(arm)
        logger_binaries = RendererBenchmarkIdentity.binary_identity(args.logserver_executable)
        output.mkdir(parents=True, exist_ok=False)
        owns_output = True
        report = {LIT_SCHEMA: SCHEMA, LIT_POLICY: POLICY, LIT_ARM: identity, LIT_CAPTURES: {}, LIT_METRICS: {}, LIT_APPLICATION_ARGS: args.application_arg, LIT_LAUNCHER: file_identity(Path(__file__).with_name(LIT_WINDOW_CAPTURE_SMOKE_PY)),
            LIT_QUALIFICATION_TOOL: file_identity(Path(__file__)), "footprint_analysis": file_identity(Path(__file__).with_name("caustic_footprint.py")), "workload_analysis": file_identity(Path(__file__).with_name("caustic_workload.py")), LIT_LOGSERVER: file_identity(args.logserver_executable), LIT_LOGSERVER_PATH: str(args.logserver_executable), LIT_LOGSERVER_BINARIES: logger_binaries, LIT_TIMEOUT_SECONDS: args.timeout}
        benchmark.write_json(output / "plan.json", report)
        frames = {}
        for preset in PRESETS:
            for enabled in (True, False):
                if RendererBenchmarkIdentity.freeze_arm(arm) != identity or RendererBenchmarkIdentity.binary_identity(args.logserver_executable) != logger_binaries:
                    raise SmokeFailure(LIT_FROZEN_CAUSTIC_ARM_OR_LOGGER_DEPENDENC)
                key = preset + (LIT_ON_2 if enabled else LIT_OFF)
                image = output / (key + ".bmp")
                workload = RendererWorkloadRegistry.workloads()["caustic-" + preset]
                env, _ = benchmark.configure_environment(os.environ, workload, output / "unused.txt")
                env.pop("NWB_GPU_TIMING_FILE", None)
                env.update(CausticWorkload.environment(preset, enabled))
                command = capture_command(args, image)
                print("Qualifying actual caustic footprint: " + key, flush=True)
                completed = subprocess.run(command, env=env, timeout=args.timeout + LIT_CAPTURE_TIMEOUT_MARGIN_SECONDS, check=False)
                if completed.returncode:
                    raise SmokeFailure(key + " actual capture failed: " + str(completed.returncode))
                if RendererBenchmarkIdentity.freeze_arm(arm) != identity or RendererBenchmarkIdentity.binary_identity(args.logserver_executable) != logger_binaries:
                    raise SmokeFailure(LIT_FROZEN_CAUSTIC_ARM_OR_LOGGER_DEPENDENC)
                settings = CausticWorkload.environment(preset, enabled)
                log = image.with_suffix(LIT_LOG_2)
                text = log.read_text(encoding=LIT_UTF_8)
                validate_gpu_debug(text, args.application_arg)
                runtime = CausticWorkload(WIDTH, HEIGHT).validate_log(text, settings, capture=True)
                frames[key] = read_bmp_24_rows(image)
                if frames[key][:2] != (WIDTH, HEIGHT):
                    raise SmokeFailure("actual caustic framebuffer has the wrong native extent")
                report[LIT_CAPTURES][key] = {LIT_SETTINGS: settings, LIT_RUNTIME: runtime, LIT_COMMAND: command, LIT_IMAGE: {LIT_PATH: image.name, LIT_IDENTITY: file_identity(image)}, LIT_LOG: {LIT_PATH: log.name, LIT_IDENTITY: file_identity(log)}}
                benchmark.write_json(output / "captures.json", report[LIT_CAPTURES])
        analysis = CausticFootprint(POLICY, PRESETS)
        report[LIT_METRICS] = {preset: analysis.footprint(frames[preset + LIT_ON_2], frames[preset + LIT_OFF], distance) for preset, distance in PRESETS.items()}
        benchmark.write_json(output / "measured_metrics.json", report[LIT_METRICS])
        analysis.validate_metrics(report[LIT_METRICS])
        benchmark.write_json(output / "qualification.json", report)
        print("PASS: actual populated/sparse visible receiver footprints qualified", flush=True)
        return 0
    except (SmokeFailure, OSError, ValueError, subprocess.SubprocessError) as error:
        if owns_output:
            try:
                benchmark.write_json(output / "failure.json", {"error": str(error)})
            except OSError as publication_error:
                print("Failure evidence could not be published: " + str(publication_error), file=sys.stderr)
        print("FAIL: " + str(error), file=sys.stderr)
        return 1


if __name__ == LIT_MAIN:
    sys.exit(main())
