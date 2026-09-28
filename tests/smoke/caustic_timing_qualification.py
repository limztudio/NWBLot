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
import re
import subprocess
import sys
from types import SimpleNamespace

from smoke_volume_identity import file_identity
from window_capture_smoke import STRICT_LOG_FAILURE_MESSAGES, SmokeFailure, read_bmp_24_rows, validate_expected_log_text

# Shared literals (no inline hardcodes below this block).
LIT_GPUDBG = "--gpudbg"
LIT_GPUDBG_2 = "--gpudbg="
LIT_POPULATED = "populated"
LIT_SPARSE = "sparse"
LIT_CAMERA_HEIGHT = "camera_height"
LIT_VERTICAL_FOV_DEGREES = "vertical_fov_degrees"
LIT_SPHERE_RADIUS = "sphere_radius"
LIT_SPHERE_EXCLUSION_MARGIN = "sphere_exclusion_margin"
LIT_GROUND_Y = "ground_y"
LIT_GROUND_X = "ground_x"
LIT_GROUND_Z = "ground_z"
LIT_RECEIVER_EDGE_MARGIN = "receiver_edge_margin"
LIT_POSITIVE_CHANNEL_SUM_THRESHOLD = "positive_channel_sum_threshold"
LIT_MINIMUM_POSITIVE_PIXELS = "minimum_positive_pixels"
LIT_MINIMUM_CHANNEL_GAIN = "minimum_channel_gain"
LIT_TILE_EXTENT = "tile_extent"
LIT_MAXIMUM_SPARSE_PIXEL_RATIO = "maximum_sparse_pixel_ratio"
LIT_MAXIMUM_SPARSE_TILE_RATIO = "maximum_sparse_tile_ratio"
LIT_NWB_CAUSTIC_SMOKE_CAMERA_PRESET = "NWB_CAUSTIC_SMOKE_CAMERA_PRESET"
LIT_NWB_CAUSTIC_SMOKE_ENABLED = "NWB_CAUSTIC_SMOKE_ENABLED"
LIT_DISABLED = "disabled"
LIT_CAUSTICSPHERESMOKEPROJECT_CAUSTICS = "CausticSphereSmokeProject: caustics "
LIT_TRANSPARENTMULTISMOKEPROJECT_SHUTDOWN = "TransparentMultiSmokeProject: shutdown"
LIT_HARDWARE = "hardware"
LIT_POSITIVE_PIXELS = "positive_pixels"
LIT_POSITIVE_CHANNEL_GAIN = "positive_channel_gain"
LIT_POSITIVE_TILES = "positive_tiles"
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


GPU_DEBUG_MARKERS = (
    "Loader: GPU debug validation enabled",
    "validation layer enabled: yes",
    "Vulkan GPU debug: debug utils messenger installed.",
)


def validate_gpu_debug(text, application_args):
    requested = any(value == LIT_GPUDBG or value.startswith(LIT_GPUDBG_2) for value in application_args)
    if requested:
        lines = [line.strip() for line in text.splitlines()]
        if any(lines.count(marker) != 1 for marker in GPU_DEBUG_MARKERS):
            raise SmokeFailure("requested GPU validation lacks actual loader/layer/messenger markers")


def validate_output_path(output, protected_roots):
    if output.exists():
        raise SmokeFailure("qualification output must be new; old evidence is never replaced")
    for protected in protected_roots:
        if output == protected or output.is_relative_to(protected) or protected.is_relative_to(output):
            raise SmokeFailure("qualification output overlaps frozen source, binary, runtime or logger inputs")


PHOTONS = "render.caustic_photons"
RESOLVE = "render.caustic_resolve"
PRESETS = {LIT_POPULATED: 2.2, LIT_SPARSE: 4.4}
FRAME_COUNT = 360
WIDTH, HEIGHT = 1280, 900
SCHEMA = "caustic-visible-footprint-v1"
PRODUCER = re.compile(r"^RendererSystem: dispatched hardware caustic producer \((\d+) photons/frame, "
    r"(\d+) temporal phases, (\d+) full-grid budget, (\d+) caustic lights, (\d+) refractive instances\)$", re.MULTILINE)
POLICY = {"width": WIDTH, "height": HEIGHT, "presentation_count": FRAME_COUNT, LIT_CAMERA_HEIGHT: .85,
    "camera_pitch": 0, LIT_VERTICAL_FOV_DEGREES: 60, "sphere_center": [0, .85, 0], LIT_SPHERE_RADIUS: .7,
    LIT_SPHERE_EXCLUSION_MARGIN: .001,
    LIT_GROUND_Y: -.08, LIT_GROUND_X: [-1.75, 1.75], LIT_GROUND_Z: [-1.47, 1.63], LIT_RECEIVER_EDGE_MARGIN: .02,
    LIT_POSITIVE_CHANNEL_SUM_THRESHOLD: 24, LIT_MINIMUM_POSITIVE_PIXELS: 100, LIT_MINIMUM_CHANNEL_GAIN: 1500,
    LIT_TILE_EXTENT: 16, LIT_MAXIMUM_SPARSE_PIXEL_RATIO: .8, LIT_MAXIMUM_SPARSE_TILE_RATIO: .8,
    "scope": "visible caustic contribution on unoccluded ground; no internal wavelet occupancy claim"}


def environment(preset, enabled=True):
    if preset not in PRESETS:
        raise SmokeFailure("unknown caustic camera preset")
    return {"NWB_CAUSTIC_SMOKE_TIMING": "1", "NWB_AVBOIT_SMOKE_TIMING": "1",
        LIT_NWB_CAUSTIC_SMOKE_CAMERA_PRESET: preset, LIT_NWB_CAUSTIC_SMOKE_ENABLED: "1" if enabled else "0",
        "NWB_REFRACTION_SMOKE_ENABLED": "0", "NWB_REFRACTION_SMOKE_HARDWARE": "1",
        "NWB_REFLECTION_SMOKE_MODE": LIT_DISABLED, "NWB_CAUSTIC_SMOKE_REFLECTION_COMPARISON": "0",
        "NWB_TRANSPARENT_MULTI_SPIN_ANGLE": "0", "NWB_TRANSPARENT_MULTI_SPIN_SPEED": "0",
        "NWB_RENDERER_BASELINE_CAPTURE_FREEZE_FRAME": "0", "NWB_RENDERER_BASELINE_FIXED_DELTA_SECONDS": "0.016666667"}


def single_match(pattern, text, label):
    matches = re.findall(pattern, text, re.MULTILINE)
    if len(matches) != 1:
        raise SmokeFailure("one unambiguous " + label + " is required")
    return matches[0]


def validate_log(text, settings, capture=False, *, allow_legacy_shadow_route=False):
    text = text.replace("\r\n", "\n")
    lines = text.splitlines()
    preset = settings[LIT_NWB_CAUSTIC_SMOKE_CAMERA_PRESET]
    enabled = settings[LIT_NWB_CAUSTIC_SMOKE_ENABLED] == "1"
    if settings != environment(preset, enabled):
        raise SmokeFailure("caustic settings differ from the complete fixed policy")
    required = ("AvboitTimingProbe: in-flight ranges 32", "AvboitTimingProbe: render unfocused 1",
        "AvboitTimingProbe: caustic in-flight ranges 32", "CausticSphereSmokeProject: reflection mode 0",
        "CausticSphereSmokeProject: camera refraction disabled",
        LIT_CAUSTICSPHERESMOKEPROJECT_CAUSTICS + ("enabled" if enabled else LIT_DISABLED),
        "CausticTimingProbe: scene single-static-sphere-ground-v1", LIT_TRANSPARENTMULTISMOKEPROJECT_SHUTDOWN)
    for marker in required:
        if lines.count(marker) != 1:
            raise SmokeFailure("missing or repeated caustic lifecycle/policy: " + marker)
    route_messages = {
        "TransparentMultiSmokeProject: natural hardware shadow route selected on RayQuery-capable hardware": LIT_HARDWARE,
        "TransparentMultiSmokeProject: natural hybrid shadow route selected on RayQuery-capable hardware": "hybrid",
    }
    routes = [route_messages[line] for line in lines if line in route_messages]
    if len(routes) != 1 or (routes[0] != LIT_HARDWARE and not allow_legacy_shadow_route):
        raise SmokeFailure("one supported caustic shadow route is required")
    if routes == [LIT_HARDWARE]:
        validate_expected_log_text(text, ["RendererSystem: dispatched hardware transparent shadow traversal"], ["RendererSystem: dispatched software shadow traversal"])
    # Reject contradictory values as well as requiring the selected value once.
    for prefix in ("AvboitTimingProbe: in-flight ranges ", "AvboitTimingProbe: render unfocused ",
        "AvboitTimingProbe: caustic in-flight ranges ", "CausticSphereSmokeProject: reflection mode ",
        "CausticSphereSmokeProject: camera refraction ", LIT_CAUSTICSPHERESMOKEPROJECT_CAUSTICS,
        "CausticTimingProbe: scene "):
        if sum(line.startswith(prefix) for line in lines) != 1:
            raise SmokeFailure("contradictory caustic policy: " + prefix)
    disabled = single_match(r"^CausticTimingProbe: reflection diagnostics (\S+) temporal (\S+) spatial (\S+) feedback (\S+)$",
        text, "reflection auxiliary settings")
    if any(value not in ("0", "false") for value in disabled):
        raise SmokeFailure("reflection diagnostics/history/filter/feedback must be disabled")
    camera = single_match(r"^CausticTimingProbe: camera (\S+) distance (\S+) height (\S+)$", text, "camera setting")
    pose = single_match(r"^CausticTimingProbe: fixed delta (\S+) yaw (\S+) sphere scale (\S+)$", text, "fixed scene pose")
    expected = (PRESETS[preset], .85, .016666667, 0.0, .7)
    actual = tuple(float(value) for value in (*camera[1:], *pose))
    if camera[0] != preset or any(not math.isfinite(a) or not math.isclose(a, b, rel_tol=1e-6, abs_tol=1e-8)
        for a, b in zip(actual, expected)):
        raise SmokeFailure("camera/pose/geometry differs from its fixed workload")
    light = tuple(float(value) for value in single_match(
        r"^CausticTimingProbe: directional pitch (\S+) yaw (\S+) intensity (\S+)$", text, "directional light"))
    fov = float(single_match(r"^CausticTimingProbe: vertical FOV radians (\S+)$", text, "camera vertical FOV"))
    if not math.isclose(fov, math.pi / 3, rel_tol=1e-6) \
        or any(not math.isclose(a, b, rel_tol=1e-6) for a, b in zip(light, (.9, .65, 2.0))):
        raise SmokeFailure("actual camera projection or light changed")
    phase = single_match(r"^CausticTimingProbe: photon phases bootstrap (\d+) converged (\d+) warmup (\d+)$",
        text, "photon temporal schedule")
    if tuple(map(int, phase)) != (2, 4, 8):
        raise SmokeFailure("photon temporal schedule changed")
    producers = [tuple(map(int, row)) for row in PRODUCER.findall(text)]
    if enabled:
        if len(producers) != 1 or producers[0][2:] != (262144, 1, 1) or producers[0][1] not in (2, 4) \
            or producers[0][0] * producers[0][1] != 262144:
            raise SmokeFailure("actual hardware photon budget, phase count, light or refractor count changed")
    elif producers:
        raise SmokeFailure("disabled caustics still dispatched photons")
    dimensions = {(int(w), int(h)) for w, h in re.findall(r"deferred rendering targets ready \((\d+)x(\d+),", text)}
    if dimensions != {(WIDTH, HEIGHT)}:
        raise SmokeFailure("caustic render extent changed")
    forbidden = (*STRICT_LOG_FAILURE_MESSAGES, "software caustic producer (", "Reflection resolve: hardware",
        "Reflection resolve: screen-space", "AVBOIT refraction resolve:", "ReflectionSmokeStatistics:",
        "ReflectionSmokeHistory:", "ReflectionSmokeFeedback:", "render submission suspended",
        "FrameLaggedAsyncLightingSmoke:", "natural software-only shadow route", "retaining all-lit visibility")
    if not capture:
        forbidden += ("FramebufferCapture:", "VK_LAYER_KHRONOS_validation", "Vulkan: enabled validation layer")
    validate_expected_log_text(text, [], forbidden)
    result = {"camera_preset": preset, "camera_distance": actual[0], LIT_CAMERA_HEIGHT: actual[1],
        "fixed_delta_seconds": actual[2], "yaw": actual[3], "sphere_scale": actual[4], "extent": [WIDTH, HEIGHT],
        "shadow_route": routes[0], "reflection_mode": LIT_DISABLED, "camera_refraction": False,
        "caustics": enabled, "photon_schedule": list(map(int, phase)), "initial_producer": list(producers[0]) if producers else None,
        "timing_in_flight_ranges": 32, "directional_light": list(light), "vertical_fov_radians": fov}
    if capture:
        if lines.count("FramebufferCapture: capture ready") != 1:
            raise SmokeFailure("one actual completed framebuffer readback is required")
        source = int(single_match(r"^FramebufferCapture: graphics source frame (\d+)$", text, "captured graphics source frame"))
        if source < FRAME_COUNT - 1:
            raise SmokeFailure("capture predates the requested stationary presentation window")
        result["graphics_source_frame"] = source
    return result


def receiver_pixels(width, height, distance):
    """Pinhole ray/plane test excludes a conservative enclosing sphere and ground edges."""
    tangent = math.tan(math.radians(POLICY[LIT_VERTICAL_FOV_DEGREES]) * .5)
    for y in range(height // 2, height):
        dy = (1 - 2 * (y + .5) / height) * tangent
        if dy >= 0:
            continue
        plane_t = (POLICY[LIT_GROUND_Y] - POLICY[LIT_CAMERA_HEIGHT]) / dy
        z = -distance + plane_t
        margin = POLICY[LIT_RECEIVER_EDGE_MARGIN]
        if not POLICY[LIT_GROUND_Z][0] + margin < z < POLICY[LIT_GROUND_Z][1] - margin:
            continue
        for x in range(width):
            dx = (2 * (x + .5) / width - 1) * tangent * width / height
            if not POLICY[LIT_GROUND_X][0] + margin < dx * plane_t < POLICY[LIT_GROUND_X][1] - margin:
                continue
            # Sphere center and camera share x/y; roots parameterize direction (dx,dy,1), not a unit vector.
            a = dx * dx + dy * dy + 1
            exclusion_radius = POLICY[LIT_SPHERE_RADIUS] + POLICY[LIT_SPHERE_EXCLUSION_MARGIN]
            discriminant = distance * distance - a * (distance * distance - exclusion_radius ** 2)
            if discriminant >= 0:
                entry = (distance - math.sqrt(discriminant)) / a
                if 0 < entry < plane_t:
                    continue
            yield x, y


def footprint(on, off, distance):
    width, height, rows = on
    if off[:2] != on[:2] or len(rows) != height or len(off[2]) != height \
        or any(len(row) != width for row in (*rows, *off[2])):
        raise SmokeFailure("caustic footprint images have inconsistent shapes")
    receiver_count = changed = gain = 0
    tiles = set()
    for x, y in receiver_pixels(width, height, distance):
        receiver_count += 1
        delta = sum(a - b for a, b in zip(rows[y][x], off[2][y][x]))
        if delta > POLICY[LIT_POSITIVE_CHANNEL_SUM_THRESHOLD]:
            changed += 1
            gain += delta
            tiles.add((x // POLICY[LIT_TILE_EXTENT], y // POLICY[LIT_TILE_EXTENT]))
    return {"receiver_pixels": receiver_count, LIT_POSITIVE_PIXELS: changed,
        LIT_POSITIVE_CHANNEL_GAIN: gain, LIT_POSITIVE_TILES: len(tiles)}


def validate_metrics(metrics):
    if set(metrics) != set(PRESETS):
        raise SmokeFailure("both populated and sparse camera captures are required")
    for value in metrics.values():
        if value[LIT_POSITIVE_PIXELS] < POLICY[LIT_MINIMUM_POSITIVE_PIXELS] \
            or value[LIT_POSITIVE_CHANNEL_GAIN] < POLICY[LIT_MINIMUM_CHANNEL_GAIN]:
            raise SmokeFailure("caustic contribution has insufficient actual positive receiver coverage")
    populated, sparse = metrics[LIT_POPULATED], metrics[LIT_SPARSE]
    if sparse[LIT_POSITIVE_PIXELS] > populated[LIT_POSITIVE_PIXELS] * POLICY[LIT_MAXIMUM_SPARSE_PIXEL_RATIO] \
        or sparse[LIT_POSITIVE_TILES] > populated[LIT_POSITIVE_TILES] * POLICY[LIT_MAXIMUM_SPARSE_TILE_RATIO]:
        raise SmokeFailure("camera pair did not qualify distinct populated/sparse visible footprints")


def validate_warmup(scopes):
    # Require actual completed GPU observations beyond the eight-frame warm-up and one four-phase cycle.
    for name in ("render.frame", PHOTONS):
        value = scopes.get(name)
        if value is None or value["gpu_samples"] < 12 or value["total_ms"] <= 0:
            raise SmokeFailure("caustic warm-up requires at least12 actual completed frame/photon ranges before measurement")


def validate_report(path, identity):
    report = json.loads(path.read_text(encoding=LIT_UTF_8))
    if report.get(LIT_SCHEMA) != SCHEMA or report.get(LIT_POLICY) != POLICY or report.get(LIT_ARM) != identity:
        raise SmokeFailure("caustic qualification policy or frozen arm identity changed")
    expected = {preset + "_" + state for preset in PRESETS for state in (LIT_ON, "off")}
    if set(report.get(LIT_CAPTURES, {})) != expected:
        raise SmokeFailure("qualification must retain all four real on/off captures")
    if report.get(LIT_QUALIFICATION_TOOL) != file_identity(Path(__file__)) \
        or report.get(LIT_LAUNCHER) != file_identity(Path(__file__).with_name(LIT_WINDOW_CAPTURE_SMOKE_PY)):
        raise SmokeFailure("qualification producer or framebuffer launcher changed")
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
    logger_binaries = benchmark.binary_identity(logger)
    if file_identity(logger) != report[LIT_LOGSERVER] or logger_binaries != report.get(LIT_LOGSERVER_BINARIES):
        raise SmokeFailure("qualification logserver or dependency inventory changed")
    paths.extend(logger.parent / name for name in logger_binaries)
    for key, record in report[LIT_CAPTURES].items():
        preset, state = key.rsplit("_", 1)
        if record.get(LIT_SETTINGS) != environment(preset, state == LIT_ON):
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
        actual = validate_log(text, record[LIT_SETTINGS], capture=True)
        if actual != record[LIT_RUNTIME]:
            raise SmokeFailure("qualification runtime metadata does not match its actual log")
        args = SimpleNamespace(executable=identity[LIT_EXECUTABLE], runtime=identity[LIT_RUNTIME],
            logserver_executable=logger, timeout=report[LIT_TIMEOUT_SECONDS], application_arg=report[LIT_APPLICATION_ARGS])
        if record.get(LIT_COMMAND) != capture_command(args, files[LIT_IMAGE], launcher=launcher):
            raise SmokeFailure("qualification launch command changed its exact capture contract")
        frames[key] = read_bmp_24_rows(files[LIT_IMAGE])
        if frames[key][:2] != (WIDTH, HEIGHT):
            raise SmokeFailure("qualification must use the native 1280x900 framebuffer")
    for preset, distance in PRESETS.items():
        metrics[preset] = footprint(frames[preset + LIT_ON_2], frames[preset + LIT_OFF], distance)
    validate_metrics(metrics)
    if metrics != report.get(LIT_METRICS):
        raise SmokeFailure("qualification metrics differ from raw image analysis")
    return {"report": str(path), LIT_IDENTITY: file_identity(path), LIT_METRICS: metrics,
        LIT_POLICY: POLICY, LIT_CAPTURES: report[LIT_CAPTURES]}, paths


def capture_command(args, image, *, launcher=None):
    if launcher is None:
        launcher = Path(__file__).with_name(LIT_WINDOW_CAPTURE_SMOKE_PY)
    command = [sys.executable, str(launcher),
        "--executable", str(args.executable), "--working-directory", str(args.runtime),
        "--logserver-executable", str(args.logserver_executable), "--output", str(image),
        "--log-output", str(image.with_suffix(LIT_LOG_2)), "--application-capture",
        "--application-capture-frame-count", str(FRAME_COUNT), LIT_TIMEOUT, str(args.timeout),
        LIT_EXPECT_LOG_MESSAGE, LIT_TRANSPARENTMULTISMOKEPROJECT_SHUTDOWN]
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
        validate_output_path(output, (args.executable.parent, args.runtime, args.source_manifest.parent,
            args.logserver_executable.parent, Path(__file__).resolve().parent))
        if not args.logserver_executable.is_file():
            raise SmokeFailure("explicit logserver executable is missing")
        identity = benchmark.freeze_arm(arm)
        logger_binaries = benchmark.binary_identity(args.logserver_executable)
        output.mkdir(parents=True, exist_ok=False)
        owns_output = True
        report = {LIT_SCHEMA: SCHEMA, LIT_POLICY: POLICY, LIT_ARM: identity, LIT_CAPTURES: {}, LIT_METRICS: {},
            LIT_APPLICATION_ARGS: args.application_arg, LIT_LAUNCHER: file_identity(Path(__file__).with_name(LIT_WINDOW_CAPTURE_SMOKE_PY)),
            LIT_QUALIFICATION_TOOL: file_identity(Path(__file__)), LIT_LOGSERVER: file_identity(args.logserver_executable),
            LIT_LOGSERVER_PATH: str(args.logserver_executable), LIT_LOGSERVER_BINARIES: logger_binaries,
            LIT_TIMEOUT_SECONDS: args.timeout}
        benchmark.write_json(output / "plan.json", report)
        frames = {}
        for preset in PRESETS:
            for enabled in (True, False):
                if benchmark.freeze_arm(arm) != identity or benchmark.binary_identity(args.logserver_executable) != logger_binaries:
                    raise SmokeFailure(LIT_FROZEN_CAUSTIC_ARM_OR_LOGGER_DEPENDENC)
                key = preset + (LIT_ON_2 if enabled else LIT_OFF)
                image = output / (key + ".bmp")
                workload = benchmark.workloads()["caustic-" + preset]
                env, _ = benchmark.configure_environment(os.environ, workload, output / "unused.txt")
                env.pop("NWB_GPU_TIMING_FILE", None)
                env.update(environment(preset, enabled))
                command = capture_command(args, image)
                print("Qualifying actual caustic footprint: " + key, flush=True)
                completed = subprocess.run(command, env=env, timeout=args.timeout + 90, check=False)
                if completed.returncode:
                    raise SmokeFailure(key + " actual capture failed: " + str(completed.returncode))
                if benchmark.freeze_arm(arm) != identity or benchmark.binary_identity(args.logserver_executable) != logger_binaries:
                    raise SmokeFailure(LIT_FROZEN_CAUSTIC_ARM_OR_LOGGER_DEPENDENC)
                settings = environment(preset, enabled)
                log = image.with_suffix(LIT_LOG_2)
                text = log.read_text(encoding=LIT_UTF_8)
                validate_gpu_debug(text, args.application_arg)
                runtime = validate_log(text, settings, capture=True)
                frames[key] = read_bmp_24_rows(image)
                if frames[key][:2] != (WIDTH, HEIGHT):
                    raise SmokeFailure("actual caustic framebuffer has the wrong native extent")
                report[LIT_CAPTURES][key] = {LIT_SETTINGS: settings, LIT_RUNTIME: runtime, LIT_COMMAND: command,
                    LIT_IMAGE: {LIT_PATH: image.name, LIT_IDENTITY: file_identity(image)},
                    LIT_LOG: {LIT_PATH: log.name, LIT_IDENTITY: file_identity(log)}}
                benchmark.write_json(output / "captures.json", report[LIT_CAPTURES])
        report[LIT_METRICS] = {preset: footprint(frames[preset + LIT_ON_2], frames[preset + LIT_OFF], distance)
            for preset, distance in PRESETS.items()}
        benchmark.write_json(output / "measured_metrics.json", report[LIT_METRICS])
        validate_metrics(report[LIT_METRICS])
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
