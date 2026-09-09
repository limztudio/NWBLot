#!/usr/bin/env python3
"""Actual framebuffer optical-transport regression with independent geometric/radiometric oracles."""

import base64
from dataclasses import asdict, dataclass
import html
import hashlib
import json
import math
from pathlib import Path
import re
import subprocess
import sys

from reflection_optical_reference import decode_radiance, pixel_reference
from reflection_smoke import capture_environment, parse_statistics, validate_frame, validate_statistics
from refraction_gallery_smoke import png_rgb_bytes
from window_capture_smoke import SKIP_EXIT_CODE, SmokeFailure, read_bmp_24_rows


@dataclass(frozen=True)
class OpticalCapture:
    name: str
    case: str
    queries: int = 16


CASES = ("reference", "clear", "tinted", "tilted", "nested2", "nested3", "priority_a", "priority_b",
    "alpha_before", "alpha_after", "duplicate_identical", "duplicate_group", "duplicate_reverse", "mirrored",
    "disconnected", "same_mesh", "torus", "inside", "inside_nested", "unspecified", "mixed", "overflow", "tir",
    "union_single", "union_same_mesh", "coincident_independent", "priority_tie_a", "priority_tie_b")
CAPTURES = tuple(OpticalCapture("optical_" + name, "optical_" + name) for name in CASES) + (
    OpticalCapture("optical_query_limit", "optical_clear", 1), OpticalCapture("optical_tir_limit", "optical_tir", 3))
OPTICS_FIELDS = ("sequence", "generation", "max_queries", "hardware_queries", "bootstrap_events", "transparent_paths",
    "unsupported_paths", "limited_paths", "ambiguous_paths", "tir_events", "medium_overflow_paths", "transport_enabled")
LIMITATIONS = ("Actual renderer framebuffer pixels; independent float64 plane/box/triangle intersections, exact dielectric Fresnel, "
    "Snell refraction and world-distance Beer absorption predict stripe positions and RGB energy. The model follows one deterministic "
    "transmitted secondary path and continues total internal reflection; secondary non-TIR reflected branches are omitted. "
    "ClosedNested and ClosedPriority are explicit authored volume contracts. Unsupported, ambiguous and exhausted residual paths "
    "are conservatively black. These comparisons do not establish complete path tracing or a GPU speed improvement.")


def parse_optics(log_text):
    samples = []
    for line in log_text.splitlines():
        if "ReflectionSmokeOptics:" not in line:
            continue
        pairs = re.findall(r"([a-z_]+)=(\d+)", line.split("ReflectionSmokeOptics:", 1)[1])
        if len(pairs) != len(OPTICS_FIELDS) or {name for name, _ in pairs} != set(OPTICS_FIELDS):
            raise SmokeFailure("malformed completed optical statistics")
        samples.append({name: int(value) for name, value in pairs})
    if not samples:
        raise SmokeFailure("no completed optical statistics")
    return samples


def validate_optics(log_text, spec):
    statistics = parse_statistics(log_text)
    validate_statistics(statistics, spec.case, "hardware")
    by_key = {(sample["sequence"], sample["generation"]): sample for sample in statistics}
    samples = parse_optics(log_text)
    stable = []
    for optical in samples:
        sample = by_key.get((optical["sequence"], optical["generation"]))
        if sample is None:
            raise SmokeFailure("optical counters lack matching accepted-token source metadata")
        if optical["max_queries"] != spec.queries:
            raise SmokeFailure("optical counters describe a different frozen query budget")
        rays, queries = sample["hardware_rays"], optical["hardware_queries"]
        if not rays <= queries <= rays * spec.queries:
            raise SmokeFailure("actual hardware scene queries violate the per-path query bound")
        for field in ("transparent_paths", "unsupported_paths", "limited_paths", "ambiguous_paths", "medium_overflow_paths"):
            if optical[field] > rays:
                raise SmokeFailure("optical path counter exceeds admitted primary paths: " + field)
        if optical["tir_events"] > queries or optical["bootstrap_events"] > rays * 32:
            raise SmokeFailure("optical event counters exceed their bounded traversal capacity")
        if sample["frame"] >= 3:
            stable.append((sample, optical))
    if not stable:
        raise SmokeFailure("no stable completed optical frame")
    sample, optical = stable[-1]
    if optical["transport_enabled"] != int(spec.case != "optical_reference"):
        raise SmokeFailure("completed snapshot used the wrong plain/optical hardware kernel")
    if spec.case != "optical_reference" and optical["transparent_paths"] == 0:
        raise SmokeFailure("authored optical scene produced no transparent reflected paths")
    required = {"optical_unspecified": "unsupported_paths", "optical_mixed": "ambiguous_paths",
        "optical_overflow": "medium_overflow_paths", "optical_query_limit": "limited_paths",
        "optical_tir_limit": "limited_paths", "optical_coincident_independent": "ambiguous_paths"}.get(spec.name)
    if required and optical[required] == 0:
        raise SmokeFailure("negative optical fixture did not exercise " + required)
    if spec.case.startswith("optical_inside") and optical["bootstrap_events"] == 0:
        raise SmokeFailure("inside-origin fixture did not exercise membership bootstrap")
    if spec.case == "optical_tir" and optical["tir_events"] == 0:
        raise SmokeFailure("prism fixture did not exercise total internal reflection")
    return {"statistics": statistics, "optics": samples, "stable_optics": optical}


def capture(args, spec):
    output = args.output_directory / (spec.name + ".bmp")
    command = [sys.executable, str(Path(__file__).with_name("window_capture_smoke.py")),
        "--executable", str(args.executable), "--working-directory", str(args.working_directory),
        "--output", str(output), "--application-capture", "--application-capture-frame-count", "1",
        "--timeout", str(args.timeout), "--expect-log-message", "ReflectionSmokeProject: case " + spec.case + " created",
        "--expect-log-message", "ReflectionSmokeProject: reflection mode hardware",
        "--expect-log-message", "ReflectionSmokeProject: optical query limit " + str(spec.queries),
        "--expect-log-message", "ReflectionSmokeProject: shutdown", "--expect-log-message", "Reflection resolve: hardware",
        "--expect-log-message", "ReflectionSmokeOptics:", "--log-output", str(output.with_suffix(".log")),
        "--expect-log-message" if args.require_hardware else "--skip-log-message",
        "ReflectionSmokeProject: hardware available" if args.require_hardware else "ReflectionSmokeProject: hardware unavailable"]
    if args.logserver_executable:
        command += ["--logserver-executable", str(args.logserver_executable)]
    else:
        command.append("--no-logserver")
    command.extend("--application-arg=" + argument for argument in args.application_arg)
    environment = capture_environment(spec.case, "hardware")
    environment.update({"NWB_REFLECTION_SMOKE_OPTICAL_QUERIES": str(spec.queries),
        "NWB_REFLECTION_SMOKE_TEMPORAL": "0", "NWB_REFLECTION_SMOKE_SPATIAL": "0",
        "NWB_REFLECTION_SMOKE_HISTORY_SAMPLES": "16", "NWB_REFLECTION_SMOKE_DIAGNOSTICS": "1"})
    print(f"Capturing {spec.name}: max {spec.queries} queries per admitted optical path...", flush=True)
    result = subprocess.run(command, env=environment, check=False, timeout=args.timeout + 90)
    if result.returncode == SKIP_EXIT_CODE:
        return None
    if result.returncode:
        raise SmokeFailure(f"{spec.name} capture failed with exit {result.returncode}")
    frame = read_bmp_24_rows(output)
    validate_frame(frame)
    if frame[:2] != (960, 720):
        raise SmokeFailure("optical capture is not the actual 960x720 framebuffer")
    from reflection_roughness_smoke import CaptureSpec, validate_history
    log_text = output.with_suffix(".log").read_text(encoding="utf-8")
    evidence = validate_optics(log_text, spec)
    evidence["capture_source"] = validate_history(log_text, evidence["statistics"],
        CaptureSpec(spec.name, case=spec.case, roughness=0, samples=16, temporal=False))
    return evidence


def reference_points(spec):
    if spec.case == "optical_torus":
        return ((x, y) for y in range(332, 389, 4) for x in range(400, 561, 4))
    if spec.case == "optical_tir":
        return ((x, y) for y in range(332, 389, 4) for x in range(448, 513, 4))
    return ((x, y) for y in range(200, 521, 16) for x in range(208, 753, 12))


def analyze_image(frame, spec):
    width, height, rows = validate_frame(frame)
    if (width, height) != (960, 720):
        raise SmokeFailure("optical geometry oracle requires the fixture's 960x720 camera")
    errors, actual_sum, expected_sum, reasons = [], [0.0] * 3, [0.0] * 3, {}
    stable_points, rejected_edges, transmitted_chart, reentered_chart = 0, 0, 0, 0
    for x, y in reference_points(spec):
        expected, path = pixel_reference(spec.case, x + 0.5, y + 0.5, max_queries=spec.queries)
        # Omit a one-pixel neighborhood of an analytic discontinuity; do not blur screenshots or move expected edges.
        neighbors = [pixel_reference(spec.case, x + dx + 0.5, y + dy + 0.5, max_queries=spec.queries)[0]
            for dx, dy in ((-1, 0), (1, 0), (0, -1), (0, 1))]
        if any(max(abs(a - b) for a, b in zip(expected, neighbor)) > 0.025 for neighbor in neighbors):
            rejected_edges += 1
            continue
        actual = tuple(decode_radiance(value) for value in rows[y][x])
        if not all(math.isfinite(value) for value in actual):
            raise SmokeFailure("optical capture contains clipped or nonfinite decoded radiance")
        errors.extend(abs(a - b) for a, b in zip(actual, expected))
        for channel in range(3):
            actual_sum[channel] += actual[channel]
            expected_sum[channel] += expected[channel]
        reasons[path["reason"]] = reasons.get(path["reason"], 0) + 1
        transmitted_chart += path["reason"] == "chart" and path["crossings"] >= 2
        reentered_chart += path["reason"] == "chart" and path["crossings"] >= 4
        stable_points += 1
    if stable_points < (60 if spec.case == "optical_torus" else 150):
        raise SmokeFailure("too few geometrically stable optical reference samples")
    if spec.case == "optical_torus" and (transmitted_chart < 60 or reentered_chart < 16):
        raise SmokeFailure("torus oracle lacks stable transmitted chart samples and actual re-entry coverage")
    mae = sum(errors) / len(errors)
    percentile = sorted(errors)[math.floor(0.95 * (len(errors) - 1))]
    reference_mean = sum(expected_sum) / (3 * stable_points)
    if mae > 0.025 + reference_mean * 0.02 or percentile > 0.065 + reference_mean * 0.035:
        raise SmokeFailure(f"{spec.name}: reflected stripe transport disagrees with the independent reference "
            f"(linear RGB MAE {mae:.6f}, p95 {percentile:.6f}, expected mean {reference_mean:.6f})")
    for actual, expected in zip(actual_sum, expected_sum):
        if abs(actual - expected) / stable_points > 0.018 + 0.035 * expected / stable_points:
            raise SmokeFailure(spec.name + ": integrated reflected channel energy is outside its analytic bound")
    return {"stable_reference_pixels": stable_points, "omitted_discontinuity_pixels": rejected_edges,
        "linear_rgb_mae": mae, "linear_rgb_p95_error": percentile,
        "actual_mean_rgb": [value / stable_points for value in actual_sum],
        "expected_mean_rgb": [value / stable_points for value in expected_sum], "reference_termination": reasons,
        "transmitted_chart_samples": transmitted_chart, "reentered_chart_samples": reentered_chart}


def compare_invariant(first, second):
    if first[:2] != second[:2]:
        raise SmokeFailure("optical invariance captures have different dimensions")
    errors = [abs(a - b) for y in range(185, 535) for x in range(195, 765)
        for a, b in zip(first[2][y][x], second[2][y][x])]
    mae = sum(errors) / len(errors)
    changed = sum(error > 2 for error in errors)
    if mae > 0.2 or changed > len(errors) * 0.005:
        raise SmokeFailure("equivalent optical volume arrangement changed reflected transport")
    return {"mirror_byte_mae": mae, "channels_differing_by_more_than_two": changed}


def analyze_suite(directory, specs=CAPTURES):
    metrics = {}
    for spec in specs:
        metrics[spec.name] = analyze_image(read_bmp_24_rows(directory / (spec.name + ".bmp")), spec)
    available = {spec.name for spec in specs}
    pairs = [("optical_tinted", "optical_" + name) for name in
        ("duplicate_identical", "duplicate_group", "duplicate_reverse", "mirrored")]
    pairs.append(("optical_disconnected", "optical_same_mesh"))
    pairs.extend((("optical_union_single", "optical_union_same_mesh"),
        ("optical_priority_a", "optical_priority_tie_a"), ("optical_priority_b", "optical_priority_tie_b")))
    for first, second in pairs:
        if first in available and second in available:
            metrics[first + "_equals_" + second] = compare_invariant(
                read_bmp_24_rows(directory / (first + ".bmp")), read_bmp_24_rows(directory / (second + ".bmp")))
    return metrics


def write_report(args, completed, evidence, metrics=None):
    metadata = {"frame_source": "actual application framebuffer readback", "size": [960, 720],
        "presentation": {"exposure": 1, "reinhard_shoulder": 1}, "temporal": False, "spatial_filter": False,
        "captures": [asdict(spec) for spec in completed], "completed_frame_evidence": evidence,
        "reference_source_sha256_lf": hashlib.sha256(Path(__file__).with_name("reflection_optical_reference.py").read_text(encoding="utf-8").encode("utf-8")).hexdigest(),
        "analyzer_source_sha256_lf": hashlib.sha256(Path(__file__).read_text(encoding="utf-8").encode("utf-8")).hexdigest(),
        "metrics": metrics, "limitations": LIMITATIONS}
    (args.output_directory / "reflection_optical_manifest.json").write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")
    cards = []
    for spec in completed:
        path = args.output_directory / (spec.name + ".bmp")
        png = png_rgb_bytes(read_bmp_24_rows(path))
        path.with_suffix(".png").write_bytes(png)
        cards.append(f'<article><h2>{html.escape(spec.name)} / {spec.queries} queries</h2>'
            f'<a href="{spec.name}.bmp">Raw BMP</a> / <a href="{spec.name}.log">Completed-frame log</a>'
            f'<img alt="Actual {html.escape(spec.name)} framebuffer" src="data:image/png;base64,{base64.b64encode(png).decode("ascii")}"></article>')
    document = '<!doctype html><html lang="en"><meta charset="utf-8"><title>Reflected optical transport</title>'
    document += '<style>body{font:16px system-ui;background:#141922;color:#e7edf5;margin:28px}main{display:grid;grid-template-columns:repeat(auto-fit,minmax(440px,1fr));gap:20px}article{padding:16px;background:#202937}img{display:block;width:100%;margin-top:12px}a{color:#8cf}pre{white-space:pre-wrap}</style>'
    document += '<h1>Reflected optical transport — actual framebuffer captures</h1><p>' + html.escape(LIMITATIONS) + '</p>'
    document += '<p>Glass and colored chart are behind the camera. The visible central rectangle is a smooth mirror. PNG conversion preserves every captured RGB pixel; raw BMPs remain available.</p><main>'
    document += ''.join(cards) + '</main><pre>' + html.escape(json.dumps(metrics, indent=2) if metrics else 'Captured evidence; image assertions have not passed yet.') + '</pre></html>'
    (args.output_directory / "reflection_optical.html").write_text(document, encoding="utf-8")


def run_suite(args):
    completed, evidence = [], {}
    try:
        for spec in CAPTURES:
            result = capture(args, spec)
            if result is None:
                print("SKIP: required reflection hardware or framebuffer readback is unavailable", file=sys.stderr)
                return SKIP_EXIT_CODE
            completed.append(spec)
            evidence[spec.name] = result
        write_report(args, completed, evidence)
        metrics = analyze_suite(args.output_directory)
        write_report(args, completed, evidence, metrics)
        print("PASS: optical reflected transport geometry, radiometry, query bounds and invariance\n" + json.dumps(metrics, indent=2), flush=True)
        return 0
    except (SmokeFailure, OSError, subprocess.TimeoutExpired) as exc:
        print("FAIL: " + str(exc), file=sys.stderr)
        return 1
