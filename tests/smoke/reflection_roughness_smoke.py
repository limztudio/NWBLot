#!/usr/bin/env python3
"""Completed-frame GGX and strict-history smoke evidence; images always come from GPU readback."""

import base64
from dataclasses import asdict, dataclass
import html
import json
from pathlib import Path
import re
import subprocess
import sys

from reflection_smoke import (capture_environment, parse_statistics,
    validate_frame, validate_statistics)
from reflection_roughness_reference import analyze_furnace, analyze_roughness, frame_grid
from refraction_gallery_smoke import png_rgb_bytes
from window_capture_smoke import SKIP_EXIT_CODE, SmokeFailure, read_bmp_24_rows

# Shared literals (no inline hardcodes below this block).
LIT_SEQUENCE = "sequence"
LIT_GENERATION = "generation"
LIT_GRAPHICS_FRAME = "graphics_frame"
LIT_EPOCH = "epoch"
LIT_START_GRAPHICS_FRAME = "start_graphics_frame"
LIT_COUNT = "count"
LIT_SAMPLE_INDEX = "sample_index"
LIT_SEED = "seed"
LIT_ELIGIBLE = "eligible"
LIT_REUSED = "reused"
LIT_RESET = "reset"
LIT_REASON = "reason"
LIT_ROUGH = "rough"
LIT_MIRROR = "mirror"
LIT_ROUGH_02 = "rough_02"
LIT_ROUGH_04 = "rough_04"
LIT_ROUGH_04_SEED1 = "rough_04_seed1"
LIT_ROUGH_06 = "rough_06"
LIT_ROUGH_04_N8 = "rough_04_n8"
LIT_ROUGH_04_N256 = "rough_04_n256"
LIT_MIRROR_RAW = "mirror_raw"
LIT_ROUGH_04_SPATIAL_N8 = "rough_04_spatial_n8"
LIT_FURNACE_MIRROR = "furnace_mirror"
LIT_ROUGH_FURNACE = "rough_furnace"
LIT_FURNACE_ROUGH = "furnace_rough"
LIT_FURNACE_ROUGH_RAW = "furnace_rough_raw"
LIT_GLASS_SMOOTH = "glass_smooth"
LIT_ROUGH_GLASS = "rough_glass"
LIT_GLASS_AUTHORED_ROUGH = "glass_authored_rough"
LIT_TEMPORAL = "temporal_"
LIT_DEFORM = "deform"
LIT_CAMERA = "camera"
LIT_TRANSFORM = "transform"
LIT_MATERIAL = "material"
LIT_LIGHT = "light"
LIT_RESET_2 = "_reset"
LIT_FRESH = "_fresh"
LIT_CAMERA_SETTLED = "camera_settled"
LIT_DEFORM_BIND = "deform_bind"
LIT_ROUGH_DEFORM = "rough_deform"
LIT_REFLECTIONSMOKEHISTORY = "ReflectionSmokeHistory:"
LIT_HARDWARE = "hardware"
LIT_BMP = ".bmp"
LIT_EXPECT_LOG_MESSAGE = "--expect-log-message"
LIT_UTF_8 = "utf-8"
LIT_MATCHED_SCENE_CAPTURES_HAVE_DIFFERENT_ = "matched scene captures have different dimensions"
LIT_MEAN_CHANNEL_BYTE_ERROR = "mean_channel_byte_error"
LIT_CHANNELS_DIFFERING_BY_MORE_THAN_TWO = "channels_differing_by_more_than_two"
LIT_NORMALIZED_REFERENCE_ERROR = "normalized_reference_error"
LIT_LIMITATIONS = "limitations"


HISTORY_FIELDS = (LIT_SEQUENCE, LIT_GENERATION, LIT_GRAPHICS_FRAME, LIT_EPOCH, LIT_START_GRAPHICS_FRAME,
    LIT_COUNT, LIT_SAMPLE_INDEX, LIT_SEED, LIT_ELIGIBLE, LIT_REUSED, LIT_RESET, LIT_REASON)


@dataclass(frozen=True)
class CaptureSpec:
    name: str
    case: str = LIT_ROUGH
    roughness: float = 0.4
    samples: int = 64
    temporal: bool = True
    spatial: bool = False
    final_state: bool = False
    post_reset_samples: int = 1
    seed: int = 0


ROUGH_CAPTURES = (
    CaptureSpec(LIT_MIRROR, roughness=0), CaptureSpec(LIT_ROUGH_02, roughness=0.2),
    CaptureSpec(LIT_ROUGH_04), CaptureSpec(LIT_ROUGH_04_SEED1, seed=1), CaptureSpec(LIT_ROUGH_06, roughness=0.6),
    CaptureSpec(LIT_ROUGH_04_N8, samples=8), CaptureSpec(LIT_ROUGH_04_N256, samples=256),
    CaptureSpec(LIT_MIRROR_RAW, roughness=0, temporal=False),
    CaptureSpec(LIT_ROUGH_04_SPATIAL_N8, samples=8, spatial=True),
    CaptureSpec(LIT_FURNACE_MIRROR, case=LIT_ROUGH_FURNACE, roughness=0),
    CaptureSpec(LIT_FURNACE_ROUGH, case=LIT_ROUGH_FURNACE, roughness=1, samples=256),
    CaptureSpec(LIT_FURNACE_ROUGH_RAW, case=LIT_ROUGH_FURNACE, roughness=1, temporal=False),
    CaptureSpec(LIT_GLASS_SMOOTH, case=LIT_ROUGH_GLASS, roughness=0),
    CaptureSpec(LIT_GLASS_AUTHORED_ROUGH, case=LIT_ROUGH_GLASS, roughness=0.6),
)
TEMPORAL_CAPTURES = tuple(CaptureSpec(kind + suffix, case=LIT_TEMPORAL + kind,
    roughness=0 if kind == LIT_DEFORM else 0.4, final_state=fresh)
    for kind in (LIT_CAMERA, LIT_TRANSFORM, LIT_MATERIAL, LIT_LIGHT, LIT_DEFORM)
    for suffix, fresh in ((LIT_RESET_2, False), (LIT_FRESH, True))) + (
    CaptureSpec(LIT_CAMERA_SETTLED, case="temporal_camera", post_reset_samples=64),
    CaptureSpec(LIT_DEFORM_BIND, case=LIT_ROUGH_DEFORM, roughness=0),)


def parse_history(log_text):
    history = []
    for line in log_text.splitlines():
        if LIT_REFLECTIONSMOKEHISTORY not in line:
            continue
        fields = line.split(LIT_REFLECTIONSMOKEHISTORY, 1)[1].strip().split()
        sample = {}
        for field in fields:
            match = re.fullmatch(r"([a-z_]+)=([0-9]+)", field)
            if not match or match[1] in sample:
                raise SmokeFailure("malformed completed reflection history field")
            sample[match[1]] = int(match[2])
        if set(sample) != set(HISTORY_FIELDS):
            raise SmokeFailure("completed history metadata is missing fields")
        history.append(sample)
    if not history:
        raise SmokeFailure("no completed reflection history was published")
    return history


def validate_history(log_text, statistics, spec):
    history = parse_history(log_text)
    anchors = re.findall(r"FramebufferCapture: graphics source frame ([0-9]+)", log_text)
    if len(anchors) != 1:
        raise SmokeFailure("capture lacks one exact graphics source-frame anchor")
    source = int(anchors[0])
    stats_by_identity = {(sample[LIT_SEQUENCE], sample[LIT_GENERATION]): sample for sample in statistics}
    previous = None
    for sample in history:
        if (sample[LIT_SEQUENCE], sample[LIT_GENERATION]) not in stats_by_identity:
            raise SmokeFailure("completed history is detached from its accepted statistics token")
        if any(sample[name] not in (0, 1) for name in (LIT_ELIGIBLE, LIT_REUSED, LIT_RESET)) \
            or sample[LIT_REASON] > 8 or sample[LIT_COUNT] > spec.samples \
            or sample[LIT_START_GRAPHICS_FRAME] > sample[LIT_GRAPHICS_FRAME]:
            raise SmokeFailure("completed history state exceeds its typed contract")
        wide = (LIT_SEQUENCE, LIT_GENERATION, LIT_GRAPHICS_FRAME, LIT_EPOCH, LIT_START_GRAPHICS_FRAME)
        if any(value > (0xffffffffffffffff if name in wide else 0xffffffff) for name, value in sample.items()):
            raise SmokeFailure("completed history metadata exceeds its integer field width")
        if previous and (sample[LIT_SEQUENCE] <= previous[LIT_SEQUENCE]
            or sample[LIT_GRAPHICS_FRAME] <= previous[LIT_GRAPHICS_FRAME] or sample[LIT_EPOCH] < previous[LIT_EPOCH]):
            raise SmokeFailure("completed history source frames or epochs moved backwards")
        if sample[LIT_REUSED] and (not sample[LIT_ELIGIBLE] or sample[LIT_RESET] or sample[LIT_COUNT] < 2):
            raise SmokeFailure("history reported reuse without an eligible prior sample")
        if previous and sample[LIT_EPOCH] == previous[LIT_EPOCH]:
            if sample[LIT_START_GRAPHICS_FRAME] != previous[LIT_START_GRAPHICS_FRAME] \
                or sample[LIT_SAMPLE_INDEX] <= previous[LIT_SAMPLE_INDEX] or sample[LIT_COUNT] < previous[LIT_COUNT]:
                raise SmokeFailure("history epoch did not retain its start, progressive index, and count")
        previous = sample
    covered = [sample for sample in history if sample[LIT_GRAPHICS_FRAME] >= source]
    if not covered:
        raise SmokeFailure("readback quit before completed reflection metadata covered its source frame")
    captured = covered[0]
    if captured[LIT_SEED] != spec.seed:
        raise SmokeFailure("captured reflection used the wrong sampling seed")
    raw = not spec.temporal or spec.case in ("temporal_deform", LIT_ROUGH_DEFORM)
    if raw:
        if any(sample[LIT_ELIGIBLE] or sample[LIT_REUSED] or sample[LIT_COUNT] for sample in history):
            raise SmokeFailure("raw or runtime-deformed geometry reused reflection history")
    elif not captured[LIT_ELIGIBLE]:
        raise SmokeFailure("captured static reflection was ineligible")
    if spec.case.startswith(LIT_TEMPORAL):
        mutations = re.findall(r"ReflectionSmokeMutation: graphics_frame=([0-9]+) fresh_final=([01]) seed=([0-9]+)", log_text)
        if len(mutations) != 1 or int(mutations[0][1]) != int(spec.final_state) or int(mutations[0][2]) != spec.seed:
            raise SmokeFailure("temporal capture lacks the requested single scene mutation")
        mutation = int(mutations[0][0])
        before = [sample for sample in history if sample[LIT_GRAPHICS_FRAME] < mutation]
        if not before or (not raw and max(sample[LIT_COUNT] for sample in before) < 32):
            raise SmokeFailure("temporal mutation occurred before static history was populated")
        if spec.post_reset_samples == 1 and source != mutation:
            raise SmokeFailure("temporal image missed the first post-mutation graphics frame")
        if not raw:
            if captured[LIT_START_GRAPHICS_FRAME] != mutation or captured[LIT_EPOCH] <= before[-1][LIT_EPOCH]:
                raise SmokeFailure("the captured frame retained stale history across the scene mutation")
            if spec.post_reset_samples > 1 and captured[LIT_COUNT] != spec.post_reset_samples:
                raise SmokeFailure("post-reset capture did not reach its accepted history sample count")
    elif not raw:
        if captured[LIT_COUNT] != spec.samples or captured[LIT_START_GRAPHICS_FRAME] > source:
            raise SmokeFailure("static image was captured before its accepted sample-count plateau")
        if not any(sample[LIT_COUNT] == spec.samples and sample[LIT_GRAPHICS_FRAME] < source for sample in history):
            raise SmokeFailure("capture requested convergence without prior completed cap evidence")
    return {"captured_graphics_frame": source, "covering_completed_history": captured,
        "completed_samples": len(history), "history": history}


def spec_environment(spec):
    env = capture_environment(spec.case, LIT_HARDWARE)
    for key, value in (("ROUGHNESS", spec.roughness), ("HISTORY_SAMPLES", spec.samples),
        ("TEMPORAL", int(spec.temporal)), ("SPATIAL", int(spec.spatial)),
        ("FINAL_STATE", int(spec.final_state)), ("POST_RESET_SAMPLES", spec.post_reset_samples),
        ("SEED", spec.seed), ("DIAGNOSTICS", 1)):
        env["NWB_REFLECTION_SMOKE_" + key] = str(value)
    return env


def capture(args, spec):
    output = args.output_directory / (spec.name + LIT_BMP)
    log_output = output.with_suffix(".log")
    command = [sys.executable, str(Path(__file__).with_name("window_capture_smoke.py")),
        "--executable", str(args.executable), "--working-directory", str(args.working_directory),
        "--output", str(output), "--application-capture", "--application-capture-frame-count", "1",
        "--timeout", str(args.timeout), LIT_EXPECT_LOG_MESSAGE, f"ReflectionSmokeProject: case {spec.case} created",
        LIT_EXPECT_LOG_MESSAGE, "ReflectionSmokeProject: reflection mode hardware",
        LIT_EXPECT_LOG_MESSAGE, "ReflectionSmokeProject: shutdown",
        LIT_EXPECT_LOG_MESSAGE, "Reflection resolve: hardware",
        LIT_EXPECT_LOG_MESSAGE, LIT_REFLECTIONSMOKEHISTORY, "--log-output", str(log_output),
        LIT_EXPECT_LOG_MESSAGE if args.require_hardware else "--skip-log-message",
        "ReflectionSmokeProject: hardware available" if args.require_hardware else "ReflectionSmokeProject: hardware unavailable"]
    if args.logserver_executable:
        command += ["--logserver-executable", str(args.logserver_executable)]
    else:
        command.append("--no-logserver")
    if spec.case == LIT_ROUGH_GLASS:
        command += [LIT_EXPECT_LOG_MESSAGE, "AVBOIT refraction resolve:"]
    command.extend("--application-arg=" + argument for argument in args.application_arg)
    print(f"Capturing {spec.name}: r={spec.roughness}, accepted cap={spec.samples}, seed={spec.seed}, temporal={spec.temporal}...", flush=True)
    result = subprocess.run(command, env=spec_environment(spec), check=False, timeout=args.timeout + 90)
    if result.returncode == SKIP_EXIT_CODE:
        return None
    if result.returncode:
        raise SmokeFailure(f"{spec.name} capture failed with exit {result.returncode}")
    frame = read_bmp_24_rows(output)
    validate_frame(frame)
    if frame[:2] != (960, 720):
        raise SmokeFailure("roughness capture is not the actual 960x720 framebuffer")
    log_text = log_output.read_text(encoding=LIT_UTF_8)
    statistics = parse_statistics(log_text)
    validate_statistics(statistics, spec.case, LIT_HARDWARE, allow_zero_samples=spec.roughness > 0)
    evidence = validate_history(log_text, statistics, spec)
    evidence["statistics"] = statistics
    return evidence


def compare_exact_scene(left, right, maximum_mean_error=0.15):
    if left[:2] != right[:2]:
        raise SmokeFailure(LIT_MATCHED_SCENE_CAPTURES_HAVE_DIFFERENT_)
    width, height, rows = validate_frame(left)
    differences = [abs(a - b) for y in range(height) for x in range(width)
        for a, b in zip(rows[y][x], right[2][y][x])]
    mean = sum(differences) / len(differences)
    changed = sum(value > 2 for value in differences)
    if mean > maximum_mean_error or changed > width * height * 3 * 0.005:
        raise SmokeFailure(f"matched final scenes retain a stale or altered reflection ({mean:.4f} byte MAE; {changed} channels)")
    return {LIT_MEAN_CHANNEL_BYTE_ERROR: mean, LIT_CHANNELS_DIFFERING_BY_MORE_THAN_TWO: changed}


def compare_smooth_glass_scene(left, right, maximum_mean_error=0.30):
    # Rough glass keeps the complementary smooth interface: the authored roughness parameter only affects
    # non-glass receivers, so both glass captures must show the same stripe geometry with bounded stochastic
    # GGX sampling noise instead of byte-identical pixels.
    if left[:2] != right[:2]:
        raise SmokeFailure(LIT_MATCHED_SCENE_CAPTURES_HAVE_DIFFERENT_)
    width, height, rows = validate_frame(left)
    differences = [abs(a - b) for y in range(height) for x in range(width)
        for a, b in zip(rows[y][x], right[2][y][x])]
    mean = sum(differences) / len(differences)
    changed = sum(value > 2 for value in differences)
    changed_pixels = sum(
        1 for y in range(height) for x in range(width)
        if max(abs(a - b) for a, b in zip(rows[y][x], right[2][y][x])) > 2
    )
    if mean > maximum_mean_error or changed_pixels > width * height * 0.05:
        raise SmokeFailure(f"matched smooth-glass scenes diverged ({mean:.4f} byte MAE; {changed_pixels} pixels)")
    return {LIT_MEAN_CHANNEL_BYTE_ERROR: mean, LIT_CHANNELS_DIFFERING_BY_MORE_THAN_TWO: changed,
        "pixels_differing_by_more_than_two": changed_pixels}


def high_frequency_energy(frame):
    cells, _, _, _ = frame_grid(frame)
    left, top = cells[0][:2]
    right, bottom = cells[-1][2:]
    from reflection_roughness_reference import DECODED_RADIANCE
    rows = frame[2]
    total = 0.0
    for y in range(top + 1, bottom - 1):
        for x in range(left + 1, right - 1):
            for channel in (0, 1):
                center = DECODED_RADIANCE[rows[y][x][channel]]
                neighbors = sum(DECODED_RADIANCE[rows[yy][xx][channel]]
                    for xx, yy in ((x - 1, y), (x + 1, y), (x, y - 1), (x, y + 1))) * 0.25
                total += (center - neighbors) ** 2
    return total


def compare_deformation(bind, deformed):
    if bind[:2] != deformed[:2]:
        raise SmokeFailure("deformation captures have different dimensions")
    def mask(frame, channel):
        return {(x, y) for y, row in enumerate(frame[2]) for x, pixel in enumerate(row)
            if pixel[channel] >= 40 and all(pixel[channel] >= pixel[other] + 25 for other in range(3) if other != channel)}
    before, after = mask(bind, 0), mask(deformed, 0)
    if min(len(before), len(after)) < 30:
        raise SmokeFailure("deformation comparison is missing a visible reflected red model")
    changed = len(before ^ after)
    if changed < max(12, len(before) * 0.1):
        raise SmokeFailure("skeletal mutation did not change the reflected model footprint")
    green_before, green_after = mask(bind, 1), mask(deformed, 1)
    if min(len(green_before), len(green_after)) < 100 or len(green_before ^ green_after) > max(4, len(green_before) * 0.01):
        raise SmokeFailure("skeletal mutation lost or changed the unrelated green reflection")
    return {"bind_red_pixels": len(before), "posed_red_pixels": len(after),
        "changed_red_footprint_pixels": changed, "retained_green_pixels": len(green_before & green_after)}


def compare_sampling_seeds(first, second):
    if first[:2] != second[:2]:
        raise SmokeFailure("sampling-seed captures have different dimensions")
    from reflection_roughness_reference import mirror_cells
    changed, total, absolute_error = 0, 0, 0
    for left, top, right, bottom in mirror_cells(*first[:2]):
        for y in range(top, bottom):
            for x in range(left, right):
                differences = [abs(a - b) for a, b in zip(first[2][y][x], second[2][y][x])]
                changed += int(max(differences) > 2)
                absolute_error += sum(differences)
                total += 1
    if changed < max(64, total * 0.005):
        raise SmokeFailure("independent sampling seeds did not change unresolved rough-reflection samples")
    return {"changed_receiver_pixels": changed, "receiver_pixels": total,
        "mean_receiver_channel_byte_difference": absolute_error / (total * 3)}


def compare_convergence(error8, error64, error256):
    if not (0 <= error256 < error64 < error8) or error256 >= error8 * 0.8:
        raise SmokeFailure("8/64/256 accepted samples did not converge monotonically toward the independent GGX reference")
    return {"accepted_caps": [8, 64, 256], "normalized_reference_errors": [error8, error64, error256]}


def analyze_suite(args):
    def frame(name):
        return read_bmp_24_rows(args.output_directory / (name + LIT_BMP))
    metrics = {}
    if args.suite == LIT_ROUGH:
        for name, roughness in ((LIT_MIRROR, 0), (LIT_ROUGH_02, 0.2), (LIT_ROUGH_04, 0.4), (LIT_ROUGH_04_SEED1, 0.4), (LIT_ROUGH_06, 0.6),
            (LIT_ROUGH_04_N8, 0.4), (LIT_ROUGH_04_N256, 0.4), (LIT_ROUGH_04_SPATIAL_N8, 0.4)):
            metrics[name] = analyze_roughness(frame(name), roughness)
        metrics["independent_seeds"] = compare_sampling_seeds(frame(LIT_ROUGH_04), frame(LIT_ROUGH_04_SEED1))
        error8 = metrics[LIT_ROUGH_04_N8][LIT_NORMALIZED_REFERENCE_ERROR]
        error64 = metrics[LIT_ROUGH_04][LIT_NORMALIZED_REFERENCE_ERROR]
        error256 = metrics[LIT_ROUGH_04_N256][LIT_NORMALIZED_REFERENCE_ERROR]
        metrics["convergence"] = compare_convergence(error8, error64, error256)
        raw_noise = high_frequency_energy(frame(LIT_ROUGH_04_N8))
        spatial_noise = high_frequency_energy(frame(LIT_ROUGH_04_SPATIAL_N8))
        if spatial_noise >= raw_noise * 0.95:
            raise SmokeFailure("separate spatial filter did not reduce unresolved local sample noise")
        metrics["spatial"] = {"raw_high_frequency_energy": raw_noise, "filtered_high_frequency_energy": spatial_noise}
        metrics["mirror_history_identity"] = compare_exact_scene(frame(LIT_MIRROR), frame(LIT_MIRROR_RAW))
        metrics["glass_stays_smooth"] = compare_smooth_glass_scene(frame(LIT_GLASS_SMOOTH), frame(LIT_GLASS_AUTHORED_ROUGH))
        for name, roughness in ((LIT_FURNACE_MIRROR, 0), (LIT_FURNACE_ROUGH, 1), (LIT_FURNACE_ROUGH_RAW, 1)):
            metrics[name] = analyze_furnace(frame(name), roughness)
    else:
        for kind in (LIT_CAMERA, LIT_TRANSFORM, LIT_MATERIAL, LIT_LIGHT, LIT_DEFORM):
            metrics[kind] = compare_exact_scene(frame(kind + LIT_RESET_2), frame(kind + LIT_FRESH))
            if kind in (LIT_TRANSFORM, LIT_MATERIAL, LIT_LIGHT):
                _, grid, _, _ = frame_grid(frame(kind + LIT_RESET_2))
                red, green = sum(value[0] for value in grid), sum(value[1] for value in grid)
                if red > 0.001 or green < 1.0:
                    raise SmokeFailure(f"{kind} mutation retained the removed red reflection or lost its unchanged green control")
                metrics[kind]["remaining_red_cell_radiance"] = red
                metrics[kind]["retained_green_cell_radiance"] = green
        metrics[LIT_CAMERA_SETTLED] = analyze_roughness(frame(LIT_CAMERA_SETTLED), 0.4, camera_x=0.6)
        metrics["deformation_footprint"] = compare_deformation(frame(LIT_DEFORM_BIND), frame("deform_reset"))
    return metrics


def write_report(args, completed, evidence, metrics=None):
    metadata = {"suite": args.suite, "frame_source": "actual application framebuffer readback",
        "captures": [asdict(spec) for spec in completed], "evidence": evidence, "metrics": metrics,
        "reference": "Independent rectangular-emitter area quadrature of correlated Smith GGX; no production VNDF sampler.",
        LIT_LIMITATIONS: "Single-bounce opaque GGX with strict static history. Runtime deformation disables history. Clear glass stays smooth. Spatial output is separate from history. Counters and sample counts are completion evidence, not GPU timing."}
    (args.output_directory / "reflection_manifest.json").write_text(json.dumps(metadata, indent=2) + "\n", encoding=LIT_UTF_8)
    cards = []
    for spec in completed:
        bmp = args.output_directory / (spec.name + LIT_BMP)
        png = png_rgb_bytes(read_bmp_24_rows(bmp))
        bmp.with_suffix(".png").write_bytes(png)
        name = html.escape(spec.name)
        cards.append(f'<article><h2>{name}</h2><a href="{name}.bmp">Raw BMP</a> / <a href="{name}.log">Completed-frame log</a>'
            f'<img alt="Actual {name} framebuffer" src="data:image/png;base64,{base64.b64encode(png).decode("ascii")}"></article>')
    document = '<!doctype html><html lang="en"><meta charset=LIT_UTF_8><title>Reflection roughness and history evidence</title>'
    document += '<style>body{background:#141922;color:#eee;font:16px system-ui;margin:28px}main{display:grid;grid-template-columns:repeat(auto-fit,minmax(440px,1fr));gap:20px}article{background:#202937;padding:16px}img{width:100%}a{color:#88c9ff}pre{white-space:pre-wrap}</style>'
    document += '<h1>Actual reflection framebuffer captures</h1><p>' + html.escape(metadata[LIT_LIMITATIONS]) + '</p><main>'
    document += ''.join(cards) + '</main><pre>' + html.escape(json.dumps(metrics, indent=2) if metrics else 'Visual analysis pending.') + '</pre></html>'
    (args.output_directory / "reflection.html").write_text(document, encoding=LIT_UTF_8)


def run_suite(args):
    completed, evidence = [], {}
    try:
        for spec in ROUGH_CAPTURES if args.suite == LIT_ROUGH else TEMPORAL_CAPTURES:
            result = capture(args, spec)
            if result is None:
                print("SKIP: required hardware or framebuffer readback unavailable", file=sys.stderr)
                return SKIP_EXIT_CODE
            completed.append(spec)
            evidence[spec.name] = result
        write_report(args, completed, evidence)
        metrics = analyze_suite(args)
        write_report(args, completed, evidence, metrics)
        print(f"PASS: reflection {args.suite} completed-source, GGX energy, and image checks\n" + json.dumps(metrics, indent=2), flush=True)
        return 0
    except (SmokeFailure, OSError, subprocess.TimeoutExpired) as exc:
        if completed:
            write_report(args, completed, evidence)
        print(f"FAIL: {exc}", file=sys.stderr)
        return 1
