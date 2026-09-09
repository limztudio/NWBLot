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

from reflection_smoke import (DEFAULT_RAY_BUDGET, capture_environment, parse_statistics,
    validate_frame, validate_statistics)
from reflection_roughness_reference import analyze_furnace, analyze_roughness, frame_grid
from refraction_gallery_smoke import png_rgb_bytes
from window_capture_smoke import SKIP_EXIT_CODE, SmokeFailure, read_bmp_24_rows


HISTORY_FIELDS = ("sequence", "generation", "graphics_frame", "epoch", "start_graphics_frame",
    "count", "sample_index", "seed", "eligible", "reused", "reset", "reason")


@dataclass(frozen=True)
class CaptureSpec:
    name: str
    case: str = "rough"
    roughness: float = 0.4
    samples: int = 64
    temporal: bool = True
    spatial: bool = False
    final_state: bool = False
    post_reset_samples: int = 1
    seed: int = 0


ROUGH_CAPTURES = (
    CaptureSpec("mirror", roughness=0), CaptureSpec("rough_02", roughness=0.2),
    CaptureSpec("rough_04"), CaptureSpec("rough_04_seed1", seed=1), CaptureSpec("rough_06", roughness=0.6),
    CaptureSpec("rough_04_n8", samples=8), CaptureSpec("rough_04_n256", samples=256),
    CaptureSpec("mirror_raw", roughness=0, temporal=False),
    CaptureSpec("rough_04_spatial_n8", samples=8, spatial=True),
    CaptureSpec("furnace_mirror", case="rough_furnace", roughness=0),
    CaptureSpec("furnace_rough", case="rough_furnace", roughness=1, samples=256),
    CaptureSpec("furnace_rough_raw", case="rough_furnace", roughness=1, temporal=False),
    CaptureSpec("glass_smooth", case="rough_glass", roughness=0),
    CaptureSpec("glass_authored_rough", case="rough_glass", roughness=0.6),
)
TEMPORAL_CAPTURES = tuple(CaptureSpec(kind + suffix, case="temporal_" + kind,
    roughness=0 if kind == "deform" else 0.4, final_state=fresh)
    for kind in ("camera", "transform", "material", "light", "deform")
    for suffix, fresh in (("_reset", False), ("_fresh", True))) + (
    CaptureSpec("camera_settled", case="temporal_camera", post_reset_samples=64),
    CaptureSpec("deform_bind", case="rough_deform", roughness=0),)


def parse_history(log_text):
    history = []
    for line in log_text.splitlines():
        if "ReflectionSmokeHistory:" not in line:
            continue
        fields = line.split("ReflectionSmokeHistory:", 1)[1].strip().split()
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
    stats_by_identity = {(sample["sequence"], sample["generation"]): sample for sample in statistics}
    previous = None
    for sample in history:
        if (sample["sequence"], sample["generation"]) not in stats_by_identity:
            raise SmokeFailure("completed history is detached from its accepted statistics token")
        if any(sample[name] not in (0, 1) for name in ("eligible", "reused", "reset")) \
            or sample["reason"] > 8 or sample["count"] > spec.samples \
            or sample["start_graphics_frame"] > sample["graphics_frame"]:
            raise SmokeFailure("completed history state exceeds its typed contract")
        wide = ("sequence", "generation", "graphics_frame", "epoch", "start_graphics_frame")
        if any(value > (0xffffffffffffffff if name in wide else 0xffffffff) for name, value in sample.items()):
            raise SmokeFailure("completed history metadata exceeds its integer field width")
        if previous and (sample["sequence"] <= previous["sequence"]
            or sample["graphics_frame"] <= previous["graphics_frame"] or sample["epoch"] < previous["epoch"]):
            raise SmokeFailure("completed history source frames or epochs moved backwards")
        if sample["reused"] and (not sample["eligible"] or sample["reset"] or sample["count"] < 2):
            raise SmokeFailure("history reported reuse without an eligible prior sample")
        if previous and sample["epoch"] == previous["epoch"]:
            if sample["start_graphics_frame"] != previous["start_graphics_frame"] \
                or sample["sample_index"] <= previous["sample_index"] or sample["count"] < previous["count"]:
                raise SmokeFailure("history epoch did not retain its start, progressive index, and count")
        previous = sample
    covered = [sample for sample in history if sample["graphics_frame"] >= source]
    if not covered:
        raise SmokeFailure("readback quit before completed reflection metadata covered its source frame")
    captured = covered[0]
    if captured["seed"] != spec.seed:
        raise SmokeFailure("captured reflection used the wrong sampling seed")
    raw = not spec.temporal or spec.case in ("temporal_deform", "rough_deform")
    if raw:
        if any(sample["eligible"] or sample["reused"] or sample["count"] for sample in history):
            raise SmokeFailure("raw or runtime-deformed geometry reused reflection history")
    elif not captured["eligible"]:
        raise SmokeFailure("captured static reflection was ineligible")
    if spec.case.startswith("temporal_"):
        mutations = re.findall(r"ReflectionSmokeMutation: graphics_frame=([0-9]+) fresh_final=([01]) seed=([0-9]+)", log_text)
        if len(mutations) != 1 or int(mutations[0][1]) != int(spec.final_state) or int(mutations[0][2]) != spec.seed:
            raise SmokeFailure("temporal capture lacks the requested single scene mutation")
        mutation = int(mutations[0][0])
        before = [sample for sample in history if sample["graphics_frame"] < mutation]
        if not before or (not raw and max(sample["count"] for sample in before) < 32):
            raise SmokeFailure("temporal mutation occurred before static history was populated")
        if spec.post_reset_samples == 1 and source != mutation:
            raise SmokeFailure("temporal image missed the first post-mutation graphics frame")
        if not raw:
            if captured["start_graphics_frame"] != mutation or captured["epoch"] <= before[-1]["epoch"]:
                raise SmokeFailure("the captured frame retained stale history across the scene mutation")
            if spec.post_reset_samples > 1 and captured["count"] != spec.post_reset_samples:
                raise SmokeFailure("post-reset capture did not reach its accepted history sample count")
    elif not raw:
        if captured["count"] != spec.samples or captured["start_graphics_frame"] > source:
            raise SmokeFailure("static image was captured before its accepted sample-count plateau")
        if not any(sample["count"] == spec.samples and sample["graphics_frame"] < source for sample in history):
            raise SmokeFailure("capture requested convergence without prior completed cap evidence")
    return {"captured_graphics_frame": source, "covering_completed_history": captured,
        "completed_samples": len(history), "history": history}


def spec_environment(spec):
    env = capture_environment(spec.case, "hardware")
    for key, value in (("ROUGHNESS", spec.roughness), ("HISTORY_SAMPLES", spec.samples),
        ("TEMPORAL", int(spec.temporal)), ("SPATIAL", int(spec.spatial)),
        ("FINAL_STATE", int(spec.final_state)), ("POST_RESET_SAMPLES", spec.post_reset_samples),
        ("SEED", spec.seed), ("DIAGNOSTICS", 1)):
        env["NWB_REFLECTION_SMOKE_" + key] = str(value)
    return env


def capture(args, spec):
    output = args.output_directory / (spec.name + ".bmp")
    log_output = output.with_suffix(".log")
    command = [sys.executable, str(Path(__file__).with_name("window_capture_smoke.py")),
        "--executable", str(args.executable), "--working-directory", str(args.working_directory),
        "--output", str(output), "--application-capture", "--application-capture-frame-count", "1",
        "--timeout", str(args.timeout), "--expect-log-message", f"ReflectionSmokeProject: case {spec.case} created",
        "--expect-log-message", "ReflectionSmokeProject: reflection mode hardware",
        "--expect-log-message", "ReflectionSmokeProject: shutdown",
        "--expect-log-message", "Reflection resolve: hardware",
        "--expect-log-message", "ReflectionSmokeHistory:", "--log-output", str(log_output),
        "--expect-log-message" if args.require_hardware else "--skip-log-message",
        "ReflectionSmokeProject: hardware available" if args.require_hardware else "ReflectionSmokeProject: hardware unavailable"]
    if args.logserver_executable:
        command += ["--logserver-executable", str(args.logserver_executable)]
    else:
        command.append("--no-logserver")
    if spec.case == "rough_glass":
        command += ["--expect-log-message", "AVBOIT refraction resolve:"]
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
    log_text = log_output.read_text(encoding="utf-8")
    statistics = parse_statistics(log_text)
    validate_statistics(statistics, spec.case, "hardware", allow_zero_samples=spec.roughness > 0)
    evidence = validate_history(log_text, statistics, spec)
    evidence["statistics"] = statistics
    return evidence


def compare_exact_scene(left, right, maximum_mean_error=0.15):
    if left[:2] != right[:2]:
        raise SmokeFailure("matched scene captures have different dimensions")
    width, height, rows = validate_frame(left)
    differences = [abs(a - b) for y in range(height) for x in range(width)
        for a, b in zip(rows[y][x], right[2][y][x])]
    mean = sum(differences) / len(differences)
    changed = sum(value > 2 for value in differences)
    if mean > maximum_mean_error or changed > width * height * 0.005:
        raise SmokeFailure(f"matched final scenes retain a stale or altered reflection ({mean:.4f} byte MAE; {changed} channels)")
    return {"mean_channel_byte_error": mean, "channels_differing_by_more_than_two": changed}


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
        return read_bmp_24_rows(args.output_directory / (name + ".bmp"))
    metrics = {}
    if args.suite == "rough":
        for name, roughness in (("mirror", 0), ("rough_02", 0.2), ("rough_04", 0.4), ("rough_04_seed1", 0.4), ("rough_06", 0.6),
            ("rough_04_n8", 0.4), ("rough_04_n256", 0.4), ("rough_04_spatial_n8", 0.4)):
            metrics[name] = analyze_roughness(frame(name), roughness)
        metrics["independent_seeds"] = compare_sampling_seeds(frame("rough_04"), frame("rough_04_seed1"))
        error8 = metrics["rough_04_n8"]["normalized_reference_error"]
        error64 = metrics["rough_04"]["normalized_reference_error"]
        error256 = metrics["rough_04_n256"]["normalized_reference_error"]
        metrics["convergence"] = compare_convergence(error8, error64, error256)
        raw_noise = high_frequency_energy(frame("rough_04_n8"))
        spatial_noise = high_frequency_energy(frame("rough_04_spatial_n8"))
        if spatial_noise >= raw_noise * 0.95:
            raise SmokeFailure("separate spatial filter did not reduce unresolved local sample noise")
        metrics["spatial"] = {"raw_high_frequency_energy": raw_noise, "filtered_high_frequency_energy": spatial_noise}
        metrics["mirror_history_identity"] = compare_exact_scene(frame("mirror"), frame("mirror_raw"))
        metrics["glass_stays_smooth"] = compare_exact_scene(frame("glass_smooth"), frame("glass_authored_rough"))
        for name, roughness in (("furnace_mirror", 0), ("furnace_rough", 1), ("furnace_rough_raw", 1)):
            metrics[name] = analyze_furnace(frame(name), roughness)
    else:
        for kind in ("camera", "transform", "material", "light", "deform"):
            metrics[kind] = compare_exact_scene(frame(kind + "_reset"), frame(kind + "_fresh"))
            if kind in ("transform", "material", "light"):
                _, grid, _, _ = frame_grid(frame(kind + "_reset"))
                red, green = sum(value[0] for value in grid), sum(value[1] for value in grid)
                if red > 0.001 or green < 1.0:
                    raise SmokeFailure(f"{kind} mutation retained the removed red reflection or lost its unchanged green control")
                metrics[kind]["remaining_red_cell_radiance"] = red
                metrics[kind]["retained_green_cell_radiance"] = green
        metrics["camera_settled"] = analyze_roughness(frame("camera_settled"), 0.4, camera_x=0.6)
        metrics["deformation_footprint"] = compare_deformation(frame("deform_bind"), frame("deform_reset"))
    return metrics


def write_report(args, completed, evidence, metrics=None):
    metadata = {"suite": args.suite, "frame_source": "actual application framebuffer readback",
        "captures": [asdict(spec) for spec in completed], "evidence": evidence, "metrics": metrics,
        "reference": "Independent rectangular-emitter area quadrature of correlated Smith GGX; no production VNDF sampler.",
        "limitations": "Single-bounce opaque GGX with strict static history. Runtime deformation disables history. Clear glass stays smooth. Spatial output is separate from history. Counters and sample counts are completion evidence, not GPU timing."}
    (args.output_directory / "reflection_manifest.json").write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")
    cards = []
    for spec in completed:
        bmp = args.output_directory / (spec.name + ".bmp")
        png = png_rgb_bytes(read_bmp_24_rows(bmp))
        bmp.with_suffix(".png").write_bytes(png)
        name = html.escape(spec.name)
        cards.append(f'<article><h2>{name}</h2><a href="{name}.bmp">Raw BMP</a> / <a href="{name}.log">Completed-frame log</a>'
            f'<img alt="Actual {name} framebuffer" src="data:image/png;base64,{base64.b64encode(png).decode("ascii")}"></article>')
    document = '<!doctype html><html lang="en"><meta charset="utf-8"><title>Reflection roughness and history evidence</title>'
    document += '<style>body{background:#141922;color:#eee;font:16px system-ui;margin:28px}main{display:grid;grid-template-columns:repeat(auto-fit,minmax(440px,1fr));gap:20px}article{background:#202937;padding:16px}img{width:100%}a{color:#88c9ff}pre{white-space:pre-wrap}</style>'
    document += '<h1>Actual reflection framebuffer captures</h1><p>' + html.escape(metadata["limitations"]) + '</p><main>'
    document += ''.join(cards) + '</main><pre>' + html.escape(json.dumps(metrics, indent=2) if metrics else 'Visual analysis pending.') + '</pre></html>'
    (args.output_directory / "reflection.html").write_text(document, encoding="utf-8")


def run_suite(args):
    completed, evidence = [], {}
    try:
        for spec in ROUGH_CAPTURES if args.suite == "rough" else TEMPORAL_CAPTURES:
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
