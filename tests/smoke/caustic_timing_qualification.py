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


GPU_DEBUG_MARKERS = (
    "Loader: GPU debug validation enabled",
    "validation layer enabled: yes",
    "Vulkan GPU debug: debug utils messenger installed.",
)


def validate_gpu_debug(text, application_args):
    requested = any(value == "--gpudbg" or value.startswith("--gpudbg=") for value in application_args)
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
PRESETS = {"populated": 2.2, "sparse": 4.4}
FRAME_COUNT = 360
WIDTH, HEIGHT = 1280, 900
SCHEMA = "caustic-visible-footprint-v1"
PRODUCER = re.compile(r"^RendererSystem: dispatched hardware caustic producer \((\d+) photons/frame, "
    r"(\d+) temporal phases, (\d+) full-grid budget, (\d+) caustic lights, (\d+) refractive instances\)$", re.MULTILINE)
POLICY = {"width": WIDTH, "height": HEIGHT, "presentation_count": FRAME_COUNT, "camera_height": .85,
    "camera_pitch": 0, "vertical_fov_degrees": 60, "sphere_center": [0, .85, 0], "sphere_radius": .7,
    "sphere_exclusion_margin": .001,
    "ground_y": -.08, "ground_x": [-1.75, 1.75], "ground_z": [-1.47, 1.63], "receiver_edge_margin": .02,
    "positive_channel_sum_threshold": 24, "minimum_positive_pixels": 100, "minimum_channel_gain": 1500,
    "tile_extent": 16, "maximum_sparse_pixel_ratio": .8, "maximum_sparse_tile_ratio": .8,
    "scope": "visible caustic contribution on unoccluded ground; no internal wavelet occupancy claim"}


def environment(preset, enabled=True):
    if preset not in PRESETS:
        raise SmokeFailure("unknown caustic camera preset")
    return {"NWB_CAUSTIC_SMOKE_TIMING": "1", "NWB_AVBOIT_SMOKE_TIMING": "1",
        "NWB_CAUSTIC_SMOKE_CAMERA_PRESET": preset, "NWB_CAUSTIC_SMOKE_ENABLED": "1" if enabled else "0",
        "NWB_REFRACTION_SMOKE_ENABLED": "0", "NWB_REFRACTION_SMOKE_HARDWARE": "1",
        "NWB_REFLECTION_SMOKE_MODE": "disabled", "NWB_CAUSTIC_SMOKE_REFLECTION_COMPARISON": "0",
        "NWB_TRANSPARENT_MULTI_SPIN_ANGLE": "0", "NWB_TRANSPARENT_MULTI_SPIN_SPEED": "0",
        "NWB_RENDERER_BASELINE_CAPTURE_FREEZE_FRAME": "0", "NWB_RENDERER_BASELINE_FIXED_DELTA_SECONDS": "0.016666667"}


def single_match(pattern, text, label):
    matches = re.findall(pattern, text, re.MULTILINE)
    if len(matches) != 1:
        raise SmokeFailure("one unambiguous " + label + " is required")
    return matches[0]


def validate_log(text, settings, capture=False):
    text = text.replace("\r\n", "\n")
    lines = text.splitlines()
    preset = settings["NWB_CAUSTIC_SMOKE_CAMERA_PRESET"]
    enabled = settings["NWB_CAUSTIC_SMOKE_ENABLED"] == "1"
    if settings != environment(preset, enabled):
        raise SmokeFailure("caustic settings differ from the complete fixed policy")
    required = ("AvboitTimingProbe: in-flight ranges 32", "AvboitTimingProbe: render unfocused 1",
        "AvboitTimingProbe: caustic in-flight ranges 32", "CausticSphereSmokeProject: reflection mode 0",
        "CausticSphereSmokeProject: camera refraction disabled",
        "CausticSphereSmokeProject: caustics " + ("enabled" if enabled else "disabled"),
        "CausticTimingProbe: scene single-static-sphere-ground-v1", "TransparentMultiSmokeProject: shutdown",
        "TransparentMultiSmokeProject: natural hybrid shadow route selected on RayQuery-capable hardware")
    for marker in required:
        if lines.count(marker) != 1:
            raise SmokeFailure("missing or repeated caustic lifecycle/policy: " + marker)
    # Reject contradictory values as well as requiring the selected value once.
    for prefix in ("AvboitTimingProbe: in-flight ranges ", "AvboitTimingProbe: render unfocused ",
        "AvboitTimingProbe: caustic in-flight ranges ", "CausticSphereSmokeProject: reflection mode ",
        "CausticSphereSmokeProject: camera refraction ", "CausticSphereSmokeProject: caustics ",
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
    result = {"camera_preset": preset, "camera_distance": actual[0], "camera_height": actual[1],
        "fixed_delta_seconds": actual[2], "yaw": actual[3], "sphere_scale": actual[4], "extent": [WIDTH, HEIGHT],
        "shadow_route": "hybrid", "reflection_mode": "disabled", "camera_refraction": False,
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
    tangent = math.tan(math.radians(POLICY["vertical_fov_degrees"]) * .5)
    for y in range(height // 2, height):
        dy = (1 - 2 * (y + .5) / height) * tangent
        if dy >= 0:
            continue
        plane_t = (POLICY["ground_y"] - POLICY["camera_height"]) / dy
        z = -distance + plane_t
        margin = POLICY["receiver_edge_margin"]
        if not POLICY["ground_z"][0] + margin < z < POLICY["ground_z"][1] - margin:
            continue
        for x in range(width):
            dx = (2 * (x + .5) / width - 1) * tangent * width / height
            if not POLICY["ground_x"][0] + margin < dx * plane_t < POLICY["ground_x"][1] - margin:
                continue
            # Sphere center and camera share x/y; roots parameterize direction (dx,dy,1), not a unit vector.
            a = dx * dx + dy * dy + 1
            exclusion_radius = POLICY["sphere_radius"] + POLICY["sphere_exclusion_margin"]
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
        if delta > POLICY["positive_channel_sum_threshold"]:
            changed += 1
            gain += delta
            tiles.add((x // POLICY["tile_extent"], y // POLICY["tile_extent"]))
    return {"receiver_pixels": receiver_count, "positive_pixels": changed,
        "positive_channel_gain": gain, "positive_tiles": len(tiles)}


def validate_metrics(metrics):
    if set(metrics) != set(PRESETS):
        raise SmokeFailure("both populated and sparse camera captures are required")
    for value in metrics.values():
        if value["positive_pixels"] < POLICY["minimum_positive_pixels"] \
            or value["positive_channel_gain"] < POLICY["minimum_channel_gain"]:
            raise SmokeFailure("caustic contribution has insufficient actual positive receiver coverage")
    populated, sparse = metrics["populated"], metrics["sparse"]
    if sparse["positive_pixels"] > populated["positive_pixels"] * POLICY["maximum_sparse_pixel_ratio"] \
        or sparse["positive_tiles"] > populated["positive_tiles"] * POLICY["maximum_sparse_tile_ratio"]:
        raise SmokeFailure("camera pair did not qualify distinct populated/sparse visible footprints")


def validate_warmup(scopes):
    # Require actual completed GPU observations beyond the eight-frame warm-up and one four-phase cycle.
    for name in ("render.frame", PHOTONS):
        value = scopes.get(name)
        if value is None or value["gpu_samples"] < 12 or value["total_ms"] <= 0:
            raise SmokeFailure("caustic warm-up requires at least12 actual completed frame/photon ranges before measurement")


def validate_report(path, identity):
    report = json.loads(path.read_text(encoding="utf-8"))
    if report.get("schema") != SCHEMA or report.get("policy") != POLICY or report.get("arm") != identity:
        raise SmokeFailure("caustic qualification policy or frozen arm identity changed")
    expected = {preset + "_" + state for preset in PRESETS for state in ("on", "off")}
    if set(report.get("captures", {})) != expected:
        raise SmokeFailure("qualification must retain all four real on/off captures")
    if report.get("qualification_tool") != file_identity(Path(__file__)) \
        or report.get("launcher") != file_identity(Path(__file__).with_name("window_capture_smoke.py")):
        raise SmokeFailure("qualification producer or framebuffer launcher changed")
    frames, metrics, paths = {}, {}, [path]
    logger = Path(report["logserver_path"])
    import renderer_ab_benchmark as benchmark
    logger_binaries = benchmark.binary_identity(logger)
    if file_identity(logger) != report["logserver"] or logger_binaries != report.get("logserver_binaries"):
        raise SmokeFailure("qualification logserver or dependency inventory changed")
    paths.extend(logger.parent / name for name in logger_binaries)
    for key, record in report["captures"].items():
        preset, state = key.rsplit("_", 1)
        if record.get("settings") != environment(preset, state == "on"):
            raise SmokeFailure("capture settings differ from the paired policy")
        files = {}
        for kind in ("image", "log"):
            evidence = (path.parent / record[kind]["path"]).resolve()
            if file_identity(evidence) != record[kind]["identity"]:
                raise SmokeFailure("qualification raw " + kind + " changed")
            paths.append(evidence)
            files[kind] = evidence
        text = files["log"].read_text(encoding="utf-8")
        validate_gpu_debug(text, report["application_args"])
        actual = validate_log(text, record["settings"], capture=True)
        if actual != record["runtime"]:
            raise SmokeFailure("qualification runtime metadata does not match its actual log")
        args = SimpleNamespace(executable=identity["executable"], runtime=identity["runtime"],
            logserver_executable=logger, timeout=report["timeout_seconds"], application_arg=report["application_args"])
        if record.get("command") != capture_command(args, files["image"]):
            raise SmokeFailure("qualification launch command changed its exact capture contract")
        frames[key] = read_bmp_24_rows(files["image"])
        if frames[key][:2] != (WIDTH, HEIGHT):
            raise SmokeFailure("qualification must use the native 1280x900 framebuffer")
    for preset, distance in PRESETS.items():
        metrics[preset] = footprint(frames[preset + "_on"], frames[preset + "_off"], distance)
    validate_metrics(metrics)
    if metrics != report.get("metrics"):
        raise SmokeFailure("qualification metrics differ from raw image analysis")
    return {"report": str(path), "identity": file_identity(path), "metrics": metrics,
        "policy": POLICY, "captures": report["captures"]}, paths


def capture_command(args, image):
    command = [sys.executable, str(Path(__file__).with_name("window_capture_smoke.py")),
        "--executable", str(args.executable), "--working-directory", str(args.runtime),
        "--logserver-executable", str(args.logserver_executable), "--output", str(image),
        "--log-output", str(image.with_suffix(".log")), "--application-capture",
        "--application-capture-frame-count", str(FRAME_COUNT), "--timeout", str(args.timeout),
        "--expect-log-message", "TransparentMultiSmokeProject: shutdown"]
    command.extend("--application-arg=" + argument for argument in args.application_arg)
    if any(value == "--gpudbg" or value.startswith("--gpudbg=") for value in args.application_arg):
        for marker in GPU_DEBUG_MARKERS:
            command.extend(("--expect-log-message", marker))
    return command


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, allow_abbrev=False)
    for name in ("executable", "runtime", "source-manifest", "logserver-executable", "output-directory"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--timeout", type=float, default=90)
    parser.add_argument("--application-arg", action="append", default=[])
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
        report = {"schema": SCHEMA, "policy": POLICY, "arm": identity, "captures": {}, "metrics": {},
            "application_args": args.application_arg, "launcher": file_identity(Path(__file__).with_name("window_capture_smoke.py")),
            "qualification_tool": file_identity(Path(__file__)), "logserver": file_identity(args.logserver_executable),
            "logserver_path": str(args.logserver_executable), "logserver_binaries": logger_binaries,
            "timeout_seconds": args.timeout}
        benchmark.write_json(output / "plan.json", report)
        frames = {}
        for preset in PRESETS:
            for enabled in (True, False):
                if benchmark.freeze_arm(arm) != identity or benchmark.binary_identity(args.logserver_executable) != logger_binaries:
                    raise SmokeFailure("frozen caustic arm or logger dependency inventory changed during capture")
                key = preset + ("_on" if enabled else "_off")
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
                    raise SmokeFailure("frozen caustic arm or logger dependency inventory changed during capture")
                settings = environment(preset, enabled)
                log = image.with_suffix(".log")
                text = log.read_text(encoding="utf-8")
                validate_gpu_debug(text, args.application_arg)
                runtime = validate_log(text, settings, capture=True)
                frames[key] = read_bmp_24_rows(image)
                if frames[key][:2] != (WIDTH, HEIGHT):
                    raise SmokeFailure("actual caustic framebuffer has the wrong native extent")
                report["captures"][key] = {"settings": settings, "runtime": runtime, "command": command,
                    "image": {"path": image.name, "identity": file_identity(image)},
                    "log": {"path": log.name, "identity": file_identity(log)}}
                benchmark.write_json(output / "captures.json", report["captures"])
        report["metrics"] = {preset: footprint(frames[preset + "_on"], frames[preset + "_off"], distance)
            for preset, distance in PRESETS.items()}
        benchmark.write_json(output / "measured_metrics.json", report["metrics"])
        validate_metrics(report["metrics"])
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


if __name__ == "__main__":
    sys.exit(main())
