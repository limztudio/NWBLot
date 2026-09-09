#!/usr/bin/env python3
"""Verify accepted screen-trace feedback, bounded work, and actual framebuffer image equivalence."""

import base64
from dataclasses import asdict, dataclass
import hashlib
import html
import json
import math
from pathlib import Path
import re
import subprocess
import sys

from reflection_smoke import (DEFAULT_RAY_BUDGET, SmokeFailure, analyze_panels, capture_environment,
    in_panel_rectangle, marker_color, validate_frame)
from reflection_roughness_smoke import compare_exact_scene
from reflection_smoke import parse_statistics, validate_statistics
from refraction_gallery_smoke import png_rgb_bytes
from window_capture_smoke import SKIP_EXIT_CODE, read_bmp_24_rows
from smoke_volume_identity import authored_volume_hashes, file_identity


@dataclass(frozen=True)
class FeedbackCapture:
    name: str
    case: str
    enabled: bool
    mode: str = "hybrid"
    hardware_budget: int = DEFAULT_RAY_BUDGET
    accepted_frames: int = 64
    screen_steps: int = 96
    roughness: float = 0.0
    final_state: bool = False
    mutation: bool = False
    extent: str = "native"
    seed: int = 0
    diagnostics: bool = True

    @property
    def dimensions(self):
        return (953, 713) if self.extent == "npot" else (960, 720)


PAIR_CASES = (
    ("offscreen", "offscreen", {}),
    ("floor", "floor", {}),
    ("provisional", "onscreen", {"hardware_budget": 64}),
    ("zero_budget", "floor", {"hardware_budget": 0}),
    ("tight_budget", "floor", {"hardware_budget": 64}),
    ("opaque_glass", "opaque_glass", {}),
    ("npot", "floor", {"extent": "npot"}),
    ("rough_guard", "rough_furnace", {"roughness": 1.0}),
    ("screen_guard", "floor", {"mode": "screen"}),
    ("long_miss", "feedback_long_miss", {"screen_steps": 16}),
)
CAPTURES = tuple(FeedbackCapture(name + ("_feedback" if enabled else "_baseline"), case, enabled, **options)
    for name, case, options in PAIR_CASES for enabled in (False, True)) + (
    FeedbackCapture("long_miss_geometry", "feedback_long_miss", False, mode="screen", screen_steps=96),
    FeedbackCapture("mutation_initial", "feedback_boundary", True),
    FeedbackCapture("mutation_changed", "feedback_mutation", True, mutation=True),
    FeedbackCapture("mutation_fresh", "feedback_mutation", True, mutation=True, final_state=True),
)
DIAGNOSTICS_OFF_CAPTURES = tuple(FeedbackCapture("diagnostics_off_" + case + ("_feedback" if enabled else "_baseline"),
    case, enabled, diagnostics=False) for case in ("offscreen", "floor", "opaque_glass") for enabled in (False, True))


def spec_environment(spec):
    environment = capture_environment(spec.case, spec.mode, spec.hardware_budget)
    environment.update({"NWB_REFLECTION_SMOKE_FEEDBACK": str(int(spec.enabled)),
        "NWB_REFLECTION_SMOKE_SCREEN_STEPS": str(spec.screen_steps),
        "NWB_REFLECTION_SMOKE_HISTORY_SAMPLES": str(spec.accepted_frames),
        "NWB_REFLECTION_SMOKE_ROUGHNESS": str(spec.roughness),
        "NWB_REFLECTION_SMOKE_TEMPORAL": "0", "NWB_REFLECTION_SMOKE_SPATIAL": "0",
        "NWB_REFLECTION_SMOKE_DIAGNOSTICS": str(int(spec.diagnostics)), "NWB_REFLECTION_SMOKE_SEED": str(spec.seed),
        "NWB_REFLECTION_SMOKE_FINAL_STATE": str(int(spec.final_state)),
        "NWB_REFLECTION_SMOKE_POST_RESET_SAMPLES": "1",
        "NWB_REFLECTION_SMOKE_FEEDBACK_CAPTURE": str(int(spec.diagnostics)), "NWB_REFLECTION_SMOKE_EXTENT": spec.extent,
        "NWB_REFLECTION_SMOKE_OPTICAL_QUERIES": "16"})
    return environment


def long_panel_projection(width, height, color, reflected):
    focal = height / (2.0 * math.tan(math.pi / 6.0))
    x = -1.7 if color == "red" else 1.7
    y = -3.6 if reflected else 3.6
    return (width * 0.5 + focal * x / 12.0, height * 0.5 - focal * (y - 1.4) / 12.0,
        focal * 0.3 / 12.0, focal * 0.35 / 12.0)


def analyze_long_miss_panels(frame):
    width, height, rows = validate_frame(frame)
    results = {}
    for color in ("red", "green"):
        points = [(x, y) for y, row in enumerate(rows) for x, pixel in enumerate(row) if marker_color(pixel, color)]
        projections = [long_panel_projection(width, height, color, reflected) for reflected in (False, True)]
        color_result = {}
        for label, projection in zip(("direct", "reflection"), projections):
            selected = [point for point in points if in_panel_rectangle(point, projection, height * 0.008)]
            minimum = max(32, projection[2] * projection[3])
            if len(selected) < minimum:
                raise SmokeFailure(f"long SSR scene is missing the geometrically predicted {color} {label} panel")
            centroid = (sum(x for x, _ in selected) / len(selected), sum(y for _, y in selected) / len(selected))
            if math.hypot(centroid[0] - projection[0], centroid[1] - projection[1]) > height * 0.012:
                raise SmokeFailure(f"long SSR {color} {label} centroid is outside its projected position")
            color_result[label] = {"pixels": len(selected), "minimum_projected_area_pixels": math.ceil(minimum),
                "centroid": centroid, "expected_centroid": projection[:2]}
        outside = sum(not any(in_panel_rectangle(point, projection, height * 0.008) for projection in projections) for point in points)
        if outside > max(16, len(points) * 0.03):
            raise SmokeFailure(f"long SSR {color} pixels appeared outside the actual/direct reflection geometry")
        color_result["outside_predicted_regions"] = outside
        color_result["projected_traversal_pixels"] = abs(projections[0][1] - projections[1][1])
        results[color] = color_result
    return results


def compare_static_images(baseline, feedback, case):
    if case == "rough_furnace":
        # A raw rough frame has a progressive sample index. Independently integrate its many pixel samples;
        # do not pretend distinct completed source indices should have the same stochastic pixels.
        from reflection_roughness_reference import analyze_furnace
        return {"baseline": analyze_furnace(baseline, 1.0), "feedback": analyze_furnace(feedback, 1.0)}
    if case == "feedback_long_miss":
        before, after = analyze_long_miss_panels(baseline), analyze_long_miss_panels(feedback)
    elif case == "opaque_glass":
        from reflection_smoke import GLASS_REGION, OPAQUE_REGION, FOREGROUND_REGION, blue_foreground, region_pixels
        results = []
        for frame in (baseline, feedback):
            width, height, rows = validate_frame(frame)
            counts = {name: sum(marker_color(rows[y][x], "red") or marker_color(rows[y][x], "green")
                for x, y in region_pixels(width, height, region))
                for name, region in (("opaque", OPAQUE_REGION), ("glass", GLASS_REGION))}
            counts["foreground"] = sum(blue_foreground(rows[y][x]) for x, y in region_pixels(width, height, FOREGROUND_REGION))
            if min(counts.values()) < 100:
                raise SmokeFailure("feedback opaque/glass composition lost a reflected region or its foreground control")
            results.append(counts)
        before, after = results
    elif case in ("floor", "onscreen"):
        before, after = analyze_panels(baseline, case, "hybrid"), analyze_panels(feedback, case, "hybrid")
    else:
        from reflection_smoke import analyze_markers
        before, after = analyze_markers(baseline), analyze_markers(feedback)
    return {"baseline": before, "feedback": after, "image_equivalence": compare_exact_scene(baseline, feedback)}


def compare_mutation_images(initial, changed, fresh):
    before = analyze_panels(initial, "boundary", "hybrid")
    after = analyze_panels(changed, "onscreen", "hybrid")
    fresh_result = analyze_panels(fresh, "onscreen", "hybrid")
    comparison = compare_exact_scene(changed, fresh)
    expected_shift = initial[1] / (2.0 * math.tan(math.pi / 6.0)) * (1.7 - 3.0) / 9.0
    measured_shift = after["green"]["reflection"]["centroid"][0] - before["green"]["reflection"]["centroid"][0]
    if abs(measured_shift - expected_shift) > initial[1] * 0.012:
        raise SmokeFailure("feedback mutation retained the old green reflection instead of its moved geometry")
    for label in ("direct", "reflection"):
        first, last = before["red"][label], after["red"][label]
        if abs(first["pixels"] - last["pixels"]) > max(4, first["pixels"] * 0.01):
            raise SmokeFailure("feedback mutation changed the stationary red control")
    return {"initial": before, "changed": after, "fresh": fresh_result,
        "changed_equals_fresh": comparison, "expected_green_motion_pixels": expected_shift,
        "measured_green_motion_pixels": measured_shift}


@dataclass(frozen=True)
class TraversalEvidence:
    """Measured baseline work, normalized from completed renderer counters after the public schema is finalized."""
    attempts: int
    iterations: int
    step_limit_misses: int
    maximum_steps: int
    bypassed_pixels: int = 0


def qualify_costly_misses(samples, minimum_mean_iterations=12.0, minimum_limit_fraction=0.2):
    if len(samples) < 8:
        raise SmokeFailure("costly-miss qualification needs at least eight completed baseline observations")
    attempts, iterations, limited = 0, 0, 0
    for sample in samples:
        if sample.bypassed_pixels:
            raise SmokeFailure("feedback-bypassed work cannot qualify the baseline SSR workload")
        if sample.maximum_steps <= 0 or sample.maximum_steps > 256 or sample.attempts <= 0:
            raise SmokeFailure("invalid completed SSR traversal extent")
        if not 0 <= sample.step_limit_misses <= sample.attempts:
            raise SmokeFailure("step-limit misses exceed actual SSR attempts")
        if not sample.step_limit_misses * sample.maximum_steps <= sample.iterations <= sample.attempts * sample.maximum_steps:
            raise SmokeFailure("completed iteration counts contradict the actual bounded SSR traversal")
        attempts += sample.attempts
        iterations += sample.iterations
        limited += sample.step_limit_misses
    mean = iterations / attempts
    fraction = limited / attempts
    if mean < minimum_mean_iterations or fraction < minimum_limit_fraction:
        raise SmokeFailure("scene has not demonstrated costly unsuccessful SSR traversal; attempt count alone is insufficient")
    return {"completed_observations": len(samples), "actual_attempts": attempts, "actual_iterations": iterations,
        "mean_iterations_per_attempt": mean, "actual_step_limit_misses": limited, "step_limit_miss_fraction": fraction,
        "qualification": "measured traversal work only; GPU timing comparison remains separate"}


FEEDBACK_FIELDS = ("sequence", "generation", "graphics_frame", "feedback_sequence", "epoch", "start_graphics_frame",
    "probe_index", "requested", "enabled", "reused", "reset", "reason", "scheduling_valid", "potential_receivers",
    "screen_returns", "bypassed_pixels", "probe_tiles", "screen_iterations", "screen_limit_misses")


def parse_feedback(log_text):
    result = []
    for line in log_text.splitlines():
        if "ReflectionSmokeFeedback:" not in line:
            continue
        sample = {}
        for field in line.split("ReflectionSmokeFeedback:", 1)[1].strip().split():
            match = re.fullmatch(r"([a-z_]+)=([0-9]+)", field)
            if not match or match[1] in sample:
                raise SmokeFailure("malformed completed feedback field")
            sample[match[1]] = int(match[2])
        if set(sample) != set(FEEDBACK_FIELDS):
            raise SmokeFailure("completed feedback metadata is missing fields")
        result.append(sample)
    if not result:
        raise SmokeFailure("no completed feedback observation was published")
    return result


def validate_feedback(log_text, statistics, spec):
    feedback = parse_feedback(log_text)
    joined = {(sample["sequence"], sample["generation"]): sample for sample in statistics}
    previous = None
    for sample in feedback:
        identity = sample["sequence"], sample["generation"]
        if identity not in joined:
            raise SmokeFailure("completed feedback is detached from its accepted statistics token")
        stats = joined[identity]
        wide = ("sequence", "generation", "graphics_frame", "feedback_sequence", "epoch", "start_graphics_frame", "screen_iterations")
        if any(value > (0xffffffffffffffff if key in wide else 0xffffffff) for key, value in sample.items()) \
            or any(sample[key] not in (0, 1) for key in ("requested", "enabled", "reused", "reset", "scheduling_valid")) \
            or sample["reason"] > 9:
            raise SmokeFailure("completed feedback exceeds its typed metadata contract")
        if sample["requested"] != int(spec.enabled) or not sample["scheduling_valid"]:
            raise SmokeFailure("feedback request or mandatory receiver-bound diagnostics disagree with the capture")
        if sample["start_graphics_frame"] > sample["graphics_frame"]:
            raise SmokeFailure("feedback epoch begins after the completed source frame")
        if (sample["enabled"] and (not spec.enabled or spec.mode != "hybrid" or spec.hardware_budget == 0)) \
            or (sample["reused"] and (not sample["enabled"] or sample["reset"])):
            raise SmokeFailure("feedback writer/reuse is enabled outside its compatible route")
        if sample["bypassed_pixels"] and (not sample["enabled"] or not sample["reused"] or spec.roughness > 0):
            raise SmokeFailure("feedback bypass lacks an eligible previous smooth observation")
        population = stats["opaque_pixels"] + stats["glass_pixels"]
        tile_classes = math.ceil(spec.dimensions[0] / 8) * math.ceil(spec.dimensions[1] / 8) * 2
        if not stats["screen_hits"] <= sample["screen_returns"] <= stats["screen_attempts"] \
            or sample["potential_receivers"] > population or sample["bypassed_pixels"] > sample["potential_receivers"] \
            or sample["probe_tiles"] > tile_classes or sample["screen_limit_misses"] > stats["screen_attempts"] \
            or sample["screen_iterations"] > stats["screen_attempts"] * spec.screen_steps \
            or sample["screen_limit_misses"] * spec.screen_steps > sample["screen_iterations"]:
            raise SmokeFailure("feedback counters contradict eligible populations or bounded actual traversal")
        if previous:
            if sample["sequence"] <= previous["sequence"] or sample["graphics_frame"] <= previous["graphics_frame"] \
                or sample["feedback_sequence"] <= previous["feedback_sequence"] or sample["epoch"] < previous["epoch"]:
                raise SmokeFailure("completed feedback observations moved backwards")
            if sample["epoch"] == previous["epoch"]:
                if sample["start_graphics_frame"] != previous["start_graphics_frame"] \
                    or (sample["enabled"] and sample["probe_index"] <= previous["probe_index"]):
                    raise SmokeFailure("feedback epoch lost its stable start or accepted probe progression")
                if sample["bypassed_pixels"] and previous["potential_receivers"] > stats["effective_budget"]:
                    raise SmokeFailure("feedback bypass violated the compatible receiver upper-bound budget guard")
        previous = sample
    anchors = re.findall(r"FramebufferCapture: graphics source frame ([0-9]+)", log_text)
    if len(anchors) != 1:
        raise SmokeFailure("feedback capture lacks one exact Graphics source frame")
    source = int(anchors[0])
    covered = [sample for sample in feedback if sample["graphics_frame"] >= source]
    if not covered:
        raise SmokeFailure("feedback capture quit before completed metadata covered its image")
    captured = covered[0]
    if spec.mutation:
        changes = re.findall(r"ReflectionSmokeFeedbackMutation: graphics_frame=([0-9]+) fresh_final=([01])", log_text)
        if len(changes) != 1 or int(changes[0][0]) != source or int(changes[0][1]) != int(spec.final_state):
            raise SmokeFailure("feedback image did not capture the first requested mutation frame")
        before = [sample for sample in feedback if sample["graphics_frame"] < source]
        if not before or max(sample["feedback_sequence"] for sample in before) < spec.accepted_frames \
            or (not spec.final_state and not any(sample["bypassed_pixels"] for sample in before)):
            raise SmokeFailure("feedback mutation lacks accepted warm-up and actual dormant-state evidence")
        if captured["start_graphics_frame"] != source or captured["epoch"] <= before[-1]["epoch"]:
            raise SmokeFailure("feedback mutation retained the previous epoch")
        if captured["graphics_frame"] == source and (captured["reused"] or captured["bypassed_pixels"]):
            raise SmokeFailure("first changed frame reused or bypassed stale feedback")
    elif not any(sample["feedback_sequence"] >= spec.accepted_frames and sample["graphics_frame"] < source for sample in feedback):
        raise SmokeFailure("feedback image was captured before accepted warm-up completed")
    stable = [sample for sample in feedback if joined[(sample["sequence"], sample["generation"])]["frame"] >= 32]
    if len(stable) < 8:
        raise SmokeFailure("feedback capture needs at least eight completed steady observations")
    return {"captured_graphics_frame": source, "covering_completed_feedback": captured,
        "exact_first_frame_counters_available": captured["graphics_frame"] == source,
        "feedback": feedback, "stable_feedback": stable, "statistics": statistics}


def validate_diagnostics_off(log_text, spec):
    if spec.diagnostics or spec.case not in ("offscreen", "floor", "opaque_glass") or spec.mutation or spec.roughness:
        raise SmokeFailure("diagnostics-off image proof requires a supported frozen smooth scene")
    if any(prefix in log_text for prefix in ("ReflectionSmokeStatistics:", "ReflectionSmokeHistory:",
        "ReflectionSmokeFeedback:", "ReflectionSmokeOptics:")):
        raise SmokeFailure("diagnostics-off capture unexpectedly published completed reflection counters")
    for message in (f"ReflectionSmokeProject: screen feedback {int(spec.enabled)}",
        f"ReflectionSmokeProject: screen steps {spec.screen_steps}"):
        if message not in log_text:
            raise SmokeFailure("diagnostics-off capture lacks accepted typed settings")
    anchors = re.findall(r"FramebufferCapture: graphics source frame ([0-9]+)", log_text)
    if len(anchors) != 1 or int(anchors[0]) < max(64, spec.accepted_frames):
        raise SmokeFailure("diagnostics-off readback completed before its bounded Graphics warm-up")
    return {"diagnostics": False, "captured_graphics_frame": int(anchors[0]),
        "requested_prepared_graphics_frames": max(64, spec.accepted_frames) + 1,
        "completed_feedback_observations": None, "completed_feedback_counters": None,
        "completion_evidence": "The framebuffer helper wrote pixels only after its ordered Graphics readback fence completed."
            " This capture has no feedback statistics readback and makes no observation-count or counter claim."}


def capture(args, spec):
    output = args.output_directory / (spec.name + ".bmp")
    command = [sys.executable, str(Path(__file__).with_name("window_capture_smoke.py")),
        "--executable", str(args.executable), "--working-directory", str(args.working_directory),
        "--output", str(output), "--application-capture", "--application-capture-frame-count",
        "1" if spec.diagnostics else str(max(64, spec.accepted_frames) + 1),
        "--timeout", str(args.timeout), "--log-output", str(output.with_suffix(".log")),
        "--expect-log-message", f"ReflectionSmokeProject: case {spec.case} created",
        "--expect-log-message", f"ReflectionSmokeProject: reflection mode {spec.mode}",
        "--expect-log-message", f"ReflectionSmokeProject: screen feedback {int(spec.enabled)}",
        "--expect-log-message", f"ReflectionSmokeProject: screen steps {spec.screen_steps}",
        "--expect-log-message", "ReflectionSmokeProject: shutdown",
        "--expect-log-message", "Reflection resolve: " + ("hardware" if spec.mode == "hybrid" and spec.hardware_budget else "screen-space"),
        "--expect-log-message" if args.require_hardware else "--skip-log-message",
        "ReflectionSmokeProject: hardware available" if args.require_hardware else "ReflectionSmokeProject: hardware unavailable"]
    if spec.diagnostics:
        command += ["--expect-log-message", "ReflectionSmokeFeedback:"]
    else:
        for prefix in ("ReflectionSmokeStatistics:", "ReflectionSmokeHistory:", "ReflectionSmokeFeedback:", "ReflectionSmokeOptics:"):
            command += ["--reject-log-message", prefix]
    if args.logserver_executable:
        command += ["--logserver-executable", str(args.logserver_executable)]
    else:
        command.append("--no-logserver")
    command.extend("--application-arg=" + argument for argument in args.application_arg)
    print(f"Capturing {spec.name}: feedback={spec.enabled}, steps={spec.screen_steps}, budget={spec.hardware_budget}...", flush=True)
    result = subprocess.run(command, env=spec_environment(spec), check=False, timeout=args.timeout + 90)
    if result.returncode == SKIP_EXIT_CODE:
        return None
    if result.returncode:
        raise SmokeFailure(f"{spec.name} capture failed: {result.returncode}")
    frame = read_bmp_24_rows(output)
    validate_frame(frame)
    if frame[:2] != spec.dimensions:
        raise SmokeFailure("feedback readback dimensions do not match the selected native/NPOT preset")
    text = output.with_suffix(".log").read_text(encoding="utf-8")
    if not spec.diagnostics:
        return validate_diagnostics_off(text, spec)
    statistics = parse_statistics(text)
    validate_statistics(statistics, spec.case, spec.mode, spec.hardware_budget,
        allow_zero_samples=spec.roughness > 0, extent=spec.dimensions)
    return validate_feedback(text, statistics, spec)


def compare_work(baseline, feedback, spec):
    original, optimized = baseline["stable_feedback"], feedback["stable_feedback"]
    if not original or not optimized:
        raise SmokeFailure("paired feedback evidence lacks completed steady observations")
    by_id = lambda evidence: {(sample["sequence"], sample["generation"]): sample for sample in evidence["statistics"]}
    before_stats, after_stats = by_id(baseline), by_id(feedback)
    def total(samples, stats, key):
        return sum(stats[(sample["sequence"], sample["generation"])][key] for sample in samples) / len(samples)
    before_attempts = total(original, before_stats, "screen_attempts")
    after_attempts = total(optimized, after_stats, "screen_attempts")
    bypass = sum(sample["bypassed_pixels"] for sample in optimized)
    guard = spec.mode != "hybrid" or spec.hardware_budget < DEFAULT_RAY_BUDGET or spec.roughness > 0
    if guard and bypass:
        raise SmokeFailure("guarded feedback pair bypassed screen work")
    if spec.name.startswith(("offscreen_", "long_miss_", "npot_")) and (not bypass or after_attempts >= before_attempts):
        raise SmokeFailure("eligible static feedback did not bypass actual repeated screen misses")
    if spec.case in ("floor", "onscreen"):
        if not sum(sample["screen_returns"] for sample in optimized):
            raise SmokeFailure("feedback suppressed every productive or provisional screen return")
        if spec.case == "floor" and not all(after_stats[(sample["sequence"], sample["generation"])]["screen_hits"] for sample in optimized):
            raise SmokeFailure("productive floor lost accepted SSR hits")
    if spec.case == "offscreen":
        for counter in ("candidates", "hardware_rays", "hardware_hits"):
            values = [before_stats[(sample["sequence"], sample["generation"])][counter] for sample in original]
            values += [after_stats[(sample["sequence"], sample["generation"])][counter] for sample in optimized]
            if max(values) != min(values):
                raise SmokeFailure("feedback changed offscreen admitted hardware work")
    if bypass and not any(sample["probe_tiles"] for sample in optimized):
        raise SmokeFailure("dormant feedback never issued a periodic tile probe")
    return {"baseline_mean_attempts": before_attempts, "feedback_mean_attempts": after_attempts,
        "feedback_bypassed_pixels_across_observations": bypass,
        "feedback_probe_classes_across_observations": sum(sample["probe_tiles"] for sample in optimized),
        "baseline_mean_hierarchy_loads": sum(sample["screen_iterations"] for sample in original) / len(original),
        "feedback_mean_hierarchy_loads": sum(sample["screen_iterations"] for sample in optimized) / len(optimized),
        "note": "Completed work counters; these are not GPU timings."}


def analyze_suite(directory, evidence, specs=CAPTURES):
    available = {spec.name: spec for spec in specs}
    frame = lambda name: read_bmp_24_rows(directory / (name + ".bmp"))
    metrics = {}
    for case in ("offscreen", "floor", "opaque_glass"):
        first, second = "diagnostics_off_" + case + "_baseline", "diagnostics_off_" + case + "_feedback"
        if first in available and second in available:
            metrics["diagnostics_off_" + case] = {"images": compare_static_images(frame(first), frame(second), case),
                "diagnostics": False, "work_counters": None,
                "scope": "Frozen smooth image equivalence after completed framebuffer readbacks; no feedback counter evidence."}
    for prefix, case, _ in PAIR_CASES:
        first, second = prefix + "_baseline", prefix + "_feedback"
        if first not in available or second not in available:
            continue
        metrics[prefix] = {"images": compare_static_images(frame(first), frame(second), case),
            "completed_work": compare_work(evidence[first], evidence[second], available[second])}
    if "long_miss_baseline" in available:
        original = evidence["long_miss_baseline"]
        stats = {(sample["sequence"], sample["generation"]): sample for sample in original["statistics"]}
        observations = [TraversalEvidence(stats[(sample["sequence"], sample["generation"])]["screen_attempts"],
            sample["screen_iterations"], sample["screen_limit_misses"], available["long_miss_baseline"].screen_steps)
            for sample in original["stable_feedback"]]
        metrics["costly_miss_qualification"] = qualify_costly_misses(observations)
    if "long_miss_geometry" in available:
        metrics["long_miss_geometry"] = analyze_long_miss_panels(frame("long_miss_geometry"))
    if all(name in available for name in ("mutation_initial", "mutation_changed", "mutation_fresh")):
        metrics["mutation"] = compare_mutation_images(frame("mutation_initial"), frame("mutation_changed"), frame("mutation_fresh"))
    return metrics


def write_report(args, completed, evidence, metrics=None):
    diagnostics_off = bool(completed) and all(not spec.diagnostics for spec in completed)
    stem = "reflection_feedback_diagnostics_off" if diagnostics_off else "reflection_feedback"
    note = "Actual framebuffer readbacks. Diagnostic captures include completed feedback observations for budget guards, returns, probes, source-change invalidation and actual traversal work. The separate diagnostics-off suite checks frozen smooth images after the framebuffer completion fence and contains no feedback counter evidence. Counter reductions do not establish GPU speed; optimized benchmarks remain separate."
    build_identity = {"executable_sha256": file_identity(args.executable)["sha256"],
        "asset_volumes": authored_volume_hashes(args.working_directory)}
    metadata = {"frame_source": "actual application framebuffer readback", "captures": [asdict(spec) for spec in completed],
        "build_identity": build_identity, "temporal": False, "spatial_filter": False,
        "fixed_delta_seconds": 0.016666667, "optical_queries": 16,
        "completed_frame_evidence": evidence, "metrics": metrics, "scope": note,
        "analyzer_source_sha256_lf": hashlib.sha256(Path(__file__).read_text(encoding="utf-8").encode("utf-8")).hexdigest()}
    (args.output_directory / (stem + "_manifest.json")).write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")
    cards = []
    for spec in completed:
        path = args.output_directory / (spec.name + ".bmp")
        png = png_rgb_bytes(read_bmp_24_rows(path))
        path.with_suffix(".png").write_bytes(png)
        cards.append(f'<article><h2>{html.escape(spec.name)}</h2><a href="{spec.name}.bmp">Raw BMP</a> / '
            f'<a href="{spec.name}.log">Completed-frame log</a><img alt="Actual {html.escape(spec.name)} capture" '
            f'src="data:image/png;base64,{base64.b64encode(png).decode("ascii")}"></article>')
    document = '<!doctype html><html lang="en"><meta charset="utf-8"><title>Reflection feedback evidence</title>'
    document += '<style>body{font:16px system-ui;background:#141922;color:#eee;margin:28px}main{display:grid;grid-template-columns:repeat(auto-fit,minmax(440px,1fr));gap:20px}article{background:#202937;padding:16px}img{width:100%}a{color:#8cf}pre{white-space:pre-wrap}</style>'
    document += '<h1>Reflection feedback — actual captures</h1><p>' + html.escape(note) + '</p><main>' + ''.join(cards) + '</main><pre>'
    document += html.escape(json.dumps(metrics, indent=2) if metrics else 'Captured evidence; assertions pending.') + '</pre></html>'
    (args.output_directory / (stem + ".html")).write_text(document, encoding="utf-8")


def run_suite(args):
    completed, evidence = [], {}
    try:
        universe = DIAGNOSTICS_OFF_CAPTURES if getattr(args, "suite", "feedback") == "feedback-diagnostics-off" else CAPTURES
        selected = getattr(args, "feedback_cases", None)
        names = set(selected.split(",")) if selected else {spec.name for spec in universe}
        if not names <= {spec.name for spec in universe}:
            raise SmokeFailure("unknown feedback capture name")
        specs = tuple(spec for spec in universe if spec.name in names)
        for spec in specs:
            result = capture(args, spec)
            if result is None:
                return SKIP_EXIT_CODE
            completed.append(spec)
            evidence[spec.name] = result
        write_report(args, completed, evidence)
        metrics = analyze_suite(args.output_directory, evidence, specs)
        write_report(args, completed, evidence, metrics)
        label = "diagnostics-off feedback image equivalence" if universe is DIAGNOSTICS_OFF_CAPTURES \
            else "reflection feedback image, accepted-state and traversal checks"
        print("PASS: " + label + "\n" + json.dumps(metrics, indent=2), flush=True)
        return 0
    except (SmokeFailure, OSError, subprocess.TimeoutExpired) as exc:
        print("FAIL: " + str(exc), file=sys.stderr)
        return 1
