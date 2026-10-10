#!/usr/bin/env python3
"""Actual framebuffer optical-transport regression with independent geometric/radiometric oracles."""

import argparse
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

from reflection_optical_reference import DENSE_CASES, decode_radiance, pixel_reference
from reflection_smoke import DEFAULT_RAY_BUDGET, capture_environment, parse_statistics, validate_frame, validate_statistics
from refraction_gallery_smoke import png_rgb_bytes
from window_capture_smoke import SKIP_EXIT_CODE, SmokeFailure, read_bmp_24_rows

LIT_DUPLICATE_IDENTICAL = "duplicate_identical"
LIT_DUPLICATE_GROUP = "duplicate_group"
LIT_DUPLICATE_REVERSE = "duplicate_reverse"
LIT_MIRRORED = "mirrored"
LIT_OPTICAL = "optical_"
LIT_OPTICAL_QUERY_LIMIT = "optical_query_limit"
LIT_OPTICAL_TIR_LIMIT = "optical_tir_limit"
LIT_OPTICAL_TIR = "optical_tir"
LIT_SEQUENCE = "sequence"
LIT_GENERATION = "generation"
LIT_MAX_QUERIES = "max_queries"
LIT_QUERY_BUDGET_UNITS = "query_budget_units"
LIT_PHYSICAL_QUERIES = "physical_queries"
LIT_BOOTSTRAP_EVENTS = "bootstrap_events"
LIT_TRANSPARENT_PATHS = "transparent_paths"
LIT_UNSUPPORTED_PATHS = "unsupported_paths"
LIT_LIMITED_PATHS = "limited_paths"
LIT_AMBIGUOUS_PATHS = "ambiguous_paths"
LIT_TIR_EVENTS = "tir_events"
LIT_MEDIUM_OVERFLOW_PATHS = "medium_overflow_paths"
LIT_TRANSPORT_ENABLED = "transport_enabled"
LIT_REFLECTIONSMOKEOPTICS = "ReflectionSmokeOptics:"
LIT_HARDWARE = "hardware"
LIT_OPTICAL_REFERENCE = "optical_reference"
LIT_STATISTICS = "statistics"
LIT_BMP = ".bmp"
LIT_EXPECT_LOG_MESSAGE = "--expect-log-message"
LIT_LOG = ".log"
LIT_UTF_8 = "utf-8"
LIT_OPTICAL_TORUS = "optical_torus"
LIT_REASON = "reason"
LIT_CHART = "chart"
LIT_CROSSINGS = "crossings"


@dataclass(frozen=True)
class OpticalCapture:
    name: str
    case: str
    queries: int = 16
    ray_budget: int = DEFAULT_RAY_BUDGET


CASES = ("reference", "clear", "tinted", "tilted", "nested2", "nested3", "priority_a", "priority_b",
    "alpha_before", "alpha_after", LIT_DUPLICATE_IDENTICAL, LIT_DUPLICATE_GROUP, LIT_DUPLICATE_REVERSE, LIT_MIRRORED,
    "disconnected", "same_mesh", "torus", "inside", "inside_nested", "unspecified", "mixed", "overflow", "tir",
    "union_single", "union_same_mesh", "coincident_independent", "priority_tie_a", "priority_tie_b",
    "csg_reference", "csg_cap", "csg_cavity", "csg_open_tail", "unspecified_reference", "csg_unspecified_cap", "sliver", "csg_sliver", "sub_ulp", "csg_sub_ulp", "group_gap", "csg_group_gap",
    "group_gap_sub_ulp", "csg_group_gap_sub_ulp", "group_entry", "csg_group_entry")
CAPTURES = tuple(OpticalCapture(LIT_OPTICAL + name, LIT_OPTICAL + name) for name in CASES) + (
    OpticalCapture(LIT_OPTICAL_QUERY_LIMIT, "optical_clear", 1), OpticalCapture(LIT_OPTICAL_TIR_LIMIT, LIT_OPTICAL_TIR, 3))
AMBIGUOUS_VOLUME_CASES = ("optical_sliver", "optical_csg_sliver", "optical_sub_ulp", "optical_csg_sub_ulp",
    "optical_group_gap", "optical_csg_group_gap", "optical_group_gap_sub_ulp", "optical_csg_group_gap_sub_ulp",
    "optical_group_entry", "optical_csg_group_entry")
# These budgets straddle 64-thread groups and the 8192-ray CSG dispatch bound. The full capture retains the independent image oracle.
QUEUE_BOUNDARY_BUDGETS = (0, 1, 63, 64, 65, 8191, 8192, 8193, DEFAULT_RAY_BUDGET)
QUEUE_BOUNDARY_CAPTURES = (OpticalCapture("optical_csg_reference", "optical_csg_reference"),) + tuple(
    OpticalCapture("optical_csg_cap" if budget == DEFAULT_RAY_BUDGET else f"optical_csg_cap_budget_{budget}",
        "optical_csg_cap", ray_budget=budget) for budget in QUEUE_BOUNDARY_BUDGETS)
DENSE_RECEIVER_CAPTURES = tuple(OpticalCapture(case, case) for case in DENSE_CASES)
CSG_REUSE_CAPTURES = (
    OpticalCapture("optical_csg_reference", "optical_csg_reference"),
    OpticalCapture("optical_csg_cap", "optical_csg_cap"),
    OpticalCapture("optical_csg_open_tail", "optical_csg_open_tail"),
    OpticalCapture("optical_csg_cap_query_limit", "optical_csg_cap", 8),
    OpticalCapture("optical_unspecified_reference", "optical_unspecified_reference"),
    OpticalCapture("optical_csg_unspecified_cap", "optical_csg_unspecified_cap"),
)
DENSE_RECEIVER_CROSSINGS = 18
CSG_TRACE_SLICE_RAYS = 8192
SLICE_PACKET_FIELDS = ("source_frame", "plan_generation", "device_generation", "graphics_queue", "runtime_present",
    "ray_capacity", "expected_slices", "hardware_nodes", "indexed_slices", "compiled_nodes", "unique_packets", "accepted_packets",
    "single_task_packets", "graphics_packets")
OPTICS_FIELDS = (LIT_SEQUENCE, LIT_GENERATION, LIT_MAX_QUERIES, LIT_QUERY_BUDGET_UNITS, LIT_PHYSICAL_QUERIES, LIT_BOOTSTRAP_EVENTS, LIT_TRANSPARENT_PATHS,
    LIT_UNSUPPORTED_PATHS, LIT_LIMITED_PATHS, LIT_AMBIGUOUS_PATHS, LIT_TIR_EVENTS, LIT_MEDIUM_OVERFLOW_PATHS, LIT_TRANSPORT_ENABLED)
LIMITATIONS = ("Actual renderer framebuffer pixels; independent float64 plane/box/triangle intersections, exact dielectric Fresnel, "
    "Snell refraction and world-distance Beer absorption predict stripe positions and RGB energy. The model follows one deterministic "
    "transmitted secondary path and continues total internal reflection; secondary non-TIR reflected branches are omitted. "
    "ClosedNested and ClosedPriority are explicit authored volume contracts. Unsupported, ambiguous and exhausted residual paths "
    "are conservatively black. These comparisons do not establish complete path tracing or a GPU speed improvement.")


def parse_optics(log_text):
    samples = []
    for line in log_text.splitlines():
        if LIT_REFLECTIONSMOKEOPTICS not in line:
            continue
        pairs = re.findall(r"([a-z_]+)=(\d+)", line.split(LIT_REFLECTIONSMOKEOPTICS, 1)[1])
        if len(pairs) != len(OPTICS_FIELDS) or {name for name, _ in pairs} != set(OPTICS_FIELDS):
            raise SmokeFailure("malformed completed optical statistics")
        samples.append({name: int(value) for name, value in pairs})
    if not samples:
        raise SmokeFailure("no completed optical statistics")
    return samples


def validate_queue_coverage(statistics, spec):
    if spec.case not in ("optical_csg_cap", "optical_csg_open_tail", "optical_unspecified_reference", "optical_csg_unspecified_cap", *DENSE_CASES):
        return None
    completed = []
    for sample in statistics:
        if sample["frame"] < 3:
            continue
        admitted = min(sample["candidates"], sample["effective_budget"])
        if sample["hardware_rays"] != admitted:
            raise SmokeFailure("CSG reflection must execute every admitted queue entry exactly once in each stable frame")
        completed.append({name: sample[name] for name in (LIT_SEQUENCE, LIT_GENERATION, "frame", "candidates",
            "effective_budget", "hardware_rays")})
    if not completed:
        raise SmokeFailure("CSG queue coverage lacks a stable accepted frame")
    return {"requested_ray_budget": spec.ray_budget, "completed_frames": completed,
        "contract": "hardware_rays equals min(candidates, effective_budget) in every stable accepted snapshot"}


def validate_slice_packets(log_text, statistics, spec):
    if not spec.case.startswith("optical_csg_") or spec.case == "optical_csg_reference":
        return None
    from reflection_roughness_smoke import parse_history
    history = parse_history(log_text)
    by_key = {}
    for sample in history:
        key = (sample[LIT_SEQUENCE], sample[LIT_GENERATION])
        if key in by_key:
            raise SmokeFailure("duplicate completed source-frame identity for CSG slice packets")
        by_key[key] = sample
    observations = {}
    for line in log_text.splitlines():
        if "ReflectionSmokeSlicePackets:" not in line:
            continue
        record = {}
        for field in line.split("ReflectionSmokeSlicePackets:", 1)[1].strip().split():
            match = re.fullmatch(r"([a-z_]+)=([0-9]+)", field)
            if not match or match[1] in record:
                raise SmokeFailure("malformed accepted CSG slice-packet observation")
            record[match[1]] = int(match[2])
        if set(record) != set(SLICE_PACKET_FIELDS):
            raise SmokeFailure("accepted CSG slice-packet observation lacks exact fields")
        if record["source_frame"] in observations:
            raise SmokeFailure("duplicate accepted CSG slice-packet source frame")
        observations[record["source_frame"]] = record
    completed = []
    for sample in statistics:
        if sample["frame"] < 3:
            continue
        source = by_key.get((sample[LIT_SEQUENCE], sample[LIT_GENERATION]))
        record = observations.get(source["graphics_frame"]) if source is not None else None
        if record is None:
            raise SmokeFailure("completed CSG snapshot lacks accepted slice-packet evidence for its exact source frame")
        capacity = min(sample["effective_budget"], sample["queue_capacity"])
        expected = (capacity + CSG_TRACE_SLICE_RAYS - 1) // CSG_TRACE_SLICE_RAYS
        if (record["runtime_present"] != 1 or record["plan_generation"] == 0
            or record["device_generation"] != sample["device_generation"]
            or record["device_generation"] == 0 or record["graphics_queue"] >= 65535):
            raise SmokeFailure("CSG slice packets lack current accepted plan and primary Graphics queue identity")
        if record["ray_capacity"] != capacity or record["expected_slices"] != expected:
            raise SmokeFailure("CSG slice-packet count describes a different frozen queue capacity")
        counts = ("hardware_nodes", "indexed_slices", "compiled_nodes", "unique_packets", "accepted_packets", "single_task_packets", "graphics_packets")
        if any(record[name] != expected for name in counts):
            raise SmokeFailure("every CSG hardware slice must accept as a distinct single-task primary Graphics packet")
        completed.append({LIT_SEQUENCE: sample[LIT_SEQUENCE], LIT_GENERATION: sample[LIT_GENERATION], **record})
    if not completed:
        raise SmokeFailure("CSG slice packets lack a stable completed frame")
    return {"slice_ray_limit": CSG_TRACE_SLICE_RAYS, "completed_frames": completed,
        "contract": "Each frozen CSG hardware slice has a distinct accepted single-task primary Graphics packet; zero budget has none. "
            "One complete hardware timing range still spans all slices per frame."}


def validate_optics(log_text, spec):
    statistics = parse_statistics(log_text)
    validate_statistics(statistics, spec.case, LIT_HARDWARE, budget=spec.ray_budget)
    queue_coverage = validate_queue_coverage(statistics, spec)
    slice_packets = validate_slice_packets(log_text, statistics, spec)
    by_key = {(sample[LIT_SEQUENCE], sample[LIT_GENERATION]): sample for sample in statistics}
    samples = parse_optics(log_text)
    stable = []
    for optical in samples:
        sample = by_key.get((optical[LIT_SEQUENCE], optical[LIT_GENERATION]))
        if sample is None:
            raise SmokeFailure("optical counters lack matching accepted-token source metadata")
        if optical[LIT_MAX_QUERIES] != spec.queries:
            raise SmokeFailure("optical counters describe a different frozen query budget")
        rays, queries = sample["hardware_rays"], optical[LIT_QUERY_BUDGET_UNITS]
        if not rays <= queries <= rays * spec.queries:
            raise SmokeFailure("reserved traversal units violate the per-path query bound")
        physical_queries = optical[LIT_PHYSICAL_QUERIES]
        if physical_queries > queries:
            raise SmokeFailure("physical hardware queries exceed reserved traversal units")
        for field in (LIT_TRANSPARENT_PATHS, LIT_UNSUPPORTED_PATHS, LIT_LIMITED_PATHS, LIT_AMBIGUOUS_PATHS, LIT_MEDIUM_OVERFLOW_PATHS):
            if optical[field] > rays:
                raise SmokeFailure("optical path counter exceeds admitted primary paths: " + field)
        if optical[LIT_TIR_EVENTS] > queries or optical[LIT_BOOTSTRAP_EVENTS] > rays * 32:
            raise SmokeFailure("optical event counters exceed their bounded traversal capacity")
        if sample["frame"] >= 3:
            if spec.case in DENSE_CASES:
                expected_queries = rays * (2 if spec.case == "optical_csg_dense" else 1)
                if (not rays or rays != min(sample["candidates"], sample["effective_budget"])
                    or sample["hardware_hits"] != rays or sample["opaque_pixels"] != rays
                    or sample["glass_pixels"] or sample["fallback_pixels"]
                    or queries != expected_queries or physical_queries != expected_queries):
                    raise SmokeFailure("dense opaque receiver lost an admitted surface hit or replayed its hardware query")
                if optical[LIT_TRANSPORT_ENABLED] or any(optical[field] for field in (LIT_TRANSPARENT_PATHS,
                    LIT_BOOTSTRAP_EVENTS, LIT_UNSUPPORTED_PATHS, LIT_LIMITED_PATHS, LIT_AMBIGUOUS_PATHS,
                    LIT_TIR_EVENTS, LIT_MEDIUM_OVERFLOW_PATHS)):
                    raise SmokeFailure("dense opaque receiver used optical transport or reported an unresolved path")
            if spec.case in AMBIGUOUS_VOLUME_CASES and optical[LIT_AMBIGUOUS_PATHS] != rays:
                raise SmokeFailure("negative optical fixture did not report ambiguity for every admitted hardware ray")
            stable.append((sample, optical))
    if not stable:
        raise SmokeFailure("no stable completed optical frame")
    sample, optical = stable[-1]
    if optical[LIT_TRANSPORT_ENABLED] != int(spec.case not in (LIT_OPTICAL_REFERENCE, *DENSE_CASES) and sample["hardware_ready"]):
        raise SmokeFailure("completed snapshot used the wrong plain/optical hardware kernel")
    if spec.ray_budget == DEFAULT_RAY_BUDGET and spec.case not in (LIT_OPTICAL_REFERENCE, *DENSE_CASES) and spec.case not in AMBIGUOUS_VOLUME_CASES and optical[LIT_TRANSPARENT_PATHS] == 0:
        raise SmokeFailure("authored optical scene produced no transparent reflected paths")
    required = {"optical_unspecified": LIT_UNSUPPORTED_PATHS, "optical_mixed": LIT_AMBIGUOUS_PATHS,
        "optical_overflow": LIT_MEDIUM_OVERFLOW_PATHS, LIT_OPTICAL_QUERY_LIMIT: LIT_LIMITED_PATHS,
        LIT_OPTICAL_TIR_LIMIT: LIT_LIMITED_PATHS, "optical_coincident_independent": LIT_AMBIGUOUS_PATHS,
        "optical_sliver": LIT_AMBIGUOUS_PATHS, "optical_csg_sliver": LIT_AMBIGUOUS_PATHS,
        "optical_sub_ulp": LIT_AMBIGUOUS_PATHS, "optical_csg_sub_ulp": LIT_AMBIGUOUS_PATHS,
        "optical_group_gap": LIT_AMBIGUOUS_PATHS, "optical_csg_group_gap": LIT_AMBIGUOUS_PATHS,
        "optical_group_gap_sub_ulp": LIT_AMBIGUOUS_PATHS, "optical_csg_group_gap_sub_ulp": LIT_AMBIGUOUS_PATHS,
        "optical_group_entry": LIT_AMBIGUOUS_PATHS, "optical_csg_group_entry": LIT_AMBIGUOUS_PATHS}.get(spec.name)
    if required and optical[required] == 0:
        raise SmokeFailure("negative optical fixture did not exercise " + required)
    if spec.case.startswith("optical_inside") and optical[LIT_BOOTSTRAP_EVENTS] == 0:
        raise SmokeFailure("inside-origin fixture did not exercise membership bootstrap")
    if spec.case == LIT_OPTICAL_TIR and optical[LIT_TIR_EVENTS] == 0:
        raise SmokeFailure("prism fixture did not exercise total internal reflection")
    return {LIT_STATISTICS: statistics, "optics": samples, "stable_optics": optical, "queue_coverage": queue_coverage,
        "slice_packets": slice_packets}



def validate_collection_reuse(evidence, spec):
    if spec.case == "optical_csg_open_tail":
        expected_units, expected_physical, limited = 12, 12, False
    elif spec.case == "optical_csg_cap" and spec.queries == 8:
        expected_units, expected_physical, limited = 8, 5, True
    elif spec.case in ("optical_csg_cap", "optical_csg_unspecified_cap") and spec.queries == 16:
        expected_units, expected_physical, limited = 11, 7, False
    elif spec.case == "optical_unspecified_reference" and spec.queries == 16:
        expected_units, expected_physical, limited = 5, 5, False
    else:
        raise SmokeFailure("collection reuse evidence requires its declared closed, open or query-limit control")
    by_key = {(sample[LIT_SEQUENCE], sample[LIT_GENERATION]): sample for sample in evidence["optics"]}
    completed = []
    for sample in evidence[LIT_STATISTICS]:
        if sample["frame"] < 3:
            continue
        optical = by_key.get((sample[LIT_SEQUENCE], sample[LIT_GENERATION]))
        rays = sample["hardware_rays"]
        if (optical is None or not rays or rays != min(sample["candidates"], sample["effective_budget"])
            or sample["hardware_hits"] != rays or sample["opaque_pixels"] != rays
            or sample["glass_pixels"] or sample["fallback_pixels"]):
            raise SmokeFailure("collection reuse control lost an admitted retained-slab path")
        if (optical[LIT_MAX_QUERIES] != spec.queries or optical[LIT_TRANSPORT_ENABLED] != 1
            or optical[LIT_QUERY_BUDGET_UNITS] != expected_units * rays
            or optical[LIT_PHYSICAL_QUERIES] != expected_physical * rays
            or optical[LIT_TRANSPARENT_PATHS] != rays
            or optical[LIT_LIMITED_PATHS] != (rays if limited else 0)
            or any(optical[field] for field in (LIT_BOOTSTRAP_EVENTS, LIT_UNSUPPORTED_PATHS,
                LIT_AMBIGUOUS_PATHS, LIT_TIR_EVENTS, LIT_MEDIUM_OVERFLOW_PATHS))):
            raise SmokeFailure("collection reuse control changed reserved work, physical queries or conservative termination")
        completed.append({LIT_SEQUENCE: sample[LIT_SEQUENCE], LIT_GENERATION: sample[LIT_GENERATION],
            "frame": sample["frame"], "hardware_rays": rays, LIT_QUERY_BUDGET_UNITS: optical[LIT_QUERY_BUDGET_UNITS],
            LIT_PHYSICAL_QUERIES: optical[LIT_PHYSICAL_QUERIES]})
    if not completed:
        raise SmokeFailure("collection reuse control lacks a stable completed source frame")
    return {"reserved_units_per_ray": expected_units, "physical_queries_per_ray": expected_physical,
        "query_limit_termination": limited, "completed_frames": completed,
        "contract": "Closed receivers reuse exact tie certificates; a positive open event beyond the nearest chart disables reuse. "
            "The extended open root also requires one terminal no-hit collection. Query exhaustion preserves reserved work and black residual radiance. "
            "Valid-air controls author the sole transparent receiver as Unspecified, selecting the normal all-Unspecified optical kernel."}


def capture(args, spec):
    output = args.output_directory / (spec.name + LIT_BMP)
    command = [sys.executable, str(Path(__file__).with_name("window_capture_smoke.py")),
        "--executable", str(args.executable), "--working-directory", str(args.working_directory),
        "--output", str(output), "--application-capture", "--application-capture-frame-count", "1",
        "--timeout", str(args.timeout), LIT_EXPECT_LOG_MESSAGE, "ReflectionSmokeProject: case " + spec.case + " created",
        LIT_EXPECT_LOG_MESSAGE, "ReflectionSmokeProject: reflection mode hardware",
        LIT_EXPECT_LOG_MESSAGE, "ReflectionSmokeProject: optical query limit " + str(spec.queries),
        LIT_EXPECT_LOG_MESSAGE, "ReflectionSmokeProject: hardware ray budget " + str(spec.ray_budget),
        LIT_EXPECT_LOG_MESSAGE, "ReflectionSmokeProject: shutdown", LIT_EXPECT_LOG_MESSAGE,
        "Reflection resolve: hardware" if spec.ray_budget else "Reflection resolve: environment",
        LIT_EXPECT_LOG_MESSAGE, LIT_REFLECTIONSMOKEOPTICS, "--log-output", str(output.with_suffix(LIT_LOG)),
        LIT_EXPECT_LOG_MESSAGE if args.require_hardware else "--skip-log-message",
        "ReflectionSmokeProject: hardware available" if args.require_hardware else "ReflectionSmokeProject: hardware unavailable"]
    if args.logserver_executable:
        command += ["--logserver-executable", str(args.logserver_executable)]
    else:
        command.append("--no-logserver")
    command.extend("--application-arg=" + argument for argument in args.application_arg)
    environment = capture_environment(spec.case, LIT_HARDWARE, spec.ray_budget)
    environment.update({"NWB_REFLECTION_SMOKE_OPTICAL_QUERIES": str(spec.queries),
        "NWB_REFLECTION_SMOKE_TEMPORAL": "0", "NWB_REFLECTION_SMOKE_SPATIAL": "0",
        "NWB_REFLECTION_SMOKE_HISTORY_SAMPLES": "16", "NWB_REFLECTION_SMOKE_DIAGNOSTICS": "1"})
    print(f"Capturing {spec.name}: ray budget {spec.ray_budget}, max {spec.queries} queries per admitted optical path...", flush=True)
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
    log_text = output.with_suffix(LIT_LOG).read_text(encoding=LIT_UTF_8)
    evidence = validate_optics(log_text, spec)
    evidence["capture_source"] = validate_history(log_text, evidence[LIT_STATISTICS],
        CaptureSpec(spec.name, case=spec.case, roughness=0, samples=16, temporal=False))
    if spec in CSG_REUSE_CAPTURES[1:]:
        evidence["collection_reuse"] = validate_collection_reuse(evidence, spec)
    return evidence


def reference_points(spec):
    if spec.case in DENSE_CASES:
        # Central rays remain inside every receiver slab through z=-25, beyond the ordinary chart at z=-14.
        return ((x, y) for y in range(280, 441, 8) for x in range(352, 609, 8))
    if spec.case == LIT_OPTICAL_TORUS:
        return ((x, y) for y in range(332, 389, 4) for x in range(400, 561, 4))
    if spec.case == LIT_OPTICAL_TIR:
        return ((x, y) for y in range(332, 389, 4) for x in range(448, 513, 4))
    return ((x, y) for y in range(200, 521, 16) for x in range(208, 753, 12))


def analyze_image(frame, spec):
    width, height, rows = validate_frame(frame)
    if (width, height) != (960, 720):
        raise SmokeFailure("optical geometry oracle requires the fixture's 960x720 camera")
    errors, actual_sum, expected_sum, reasons = [], [0.0] * 3, [0.0] * 3, {}
    channel_deviations, stable_points, rejected_edges, transmitted_chart, reentered_chart = [], 0, 0, 0, 0
    dense_crossings = []
    for x, y in reference_points(spec):
        expected, path = pixel_reference(spec.case, x + 0.5, y + 0.5, max_queries=spec.queries)
        if spec.case in DENSE_CASES:
            if path[LIT_REASON] != "opaque_dense" or path[LIT_CROSSINGS] != DENSE_RECEIVER_CROSSINGS or min(expected) <= 0.:
                raise SmokeFailure("dense receiver oracle lacks eighteen distinct closed crossings and a positive opaque hit")
            dense_crossings.append(path[LIT_CROSSINGS])
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
        channel_deviations.append([abs(a - b) for a, b in zip(actual, expected)])
        for channel in range(3):
            actual_sum[channel] += actual[channel]
            expected_sum[channel] += expected[channel]
        reasons[path[LIT_REASON]] = reasons.get(path[LIT_REASON], 0) + 1
        transmitted_chart += path[LIT_REASON] == LIT_CHART and path[LIT_CROSSINGS] >= 2
        reentered_chart += path[LIT_REASON] == LIT_CHART and path[LIT_CROSSINGS] >= 4
        stable_points += 1
    if stable_points < (60 if spec.case == LIT_OPTICAL_TORUS else 150):
        raise SmokeFailure("too few geometrically stable optical reference samples")
    if spec.case == LIT_OPTICAL_TORUS and (transmitted_chart < 60 or reentered_chart < 16):
        raise SmokeFailure("torus oracle lacks stable transmitted chart samples and actual re-entry coverage")
    mae = sum(errors) / len(errors)
    percentile = sorted(errors)[math.floor(0.95 * (len(errors) - 1))]
    reference_mean = sum(expected_sum) / (3 * stable_points)
    # Same mesh-discretization allowance as the integrated bound below: admit at most 5% isolated
    # near-critical outliers before judging bulk transport, keeping the mean consistent with p95.
    ordered_errors = sorted(errors)
    kept_errors = ordered_errors[:max(len(ordered_errors) - math.ceil(len(ordered_errors) * 0.05), 1)]
    trimmed_mae = sum(kept_errors) / len(kept_errors)
    if trimmed_mae > 0.025 + reference_mean * 0.02 or percentile > 0.065 + reference_mean * 0.035:
        raise SmokeFailure(f"{spec.name}: reflected stripe transport disagrees with the independent reference "
            f"(linear RGB MAE {mae:.6f}, p95 {percentile:.6f}, expected mean {reference_mean:.6f})")
    for channel, (actual, expected) in enumerate(zip(actual_sum, expected_sum)):
        # The analytic oracle models smooth planes in float64 while the renderer traces a triangulated mesh in
        # FP32, so a few near-critical TIR pixels legitimately flip at the query-budget boundary. Admit at most
        # 5% mesh-discretization outliers via the trimmed mean; a bulk energy leak still moves the remaining 95%.
        ordered = sorted(deviation[channel] for deviation in channel_deviations)
        kept = ordered[:max(stable_points - math.ceil(stable_points * 0.05), 1)]
        trimmed = sum(kept) / len(kept)
        if trimmed > 0.018 + 0.035 * expected / stable_points:
            raise SmokeFailure(spec.name + ": integrated reflected channel energy is outside its analytic bound")
    return {"stable_reference_pixels": stable_points, "omitted_discontinuity_pixels": rejected_edges,
        "linear_rgb_mae": mae, "linear_rgb_p95_error": percentile,
        "actual_mean_rgb": [value / stable_points for value in actual_sum],
        "expected_mean_rgb": [value / stable_points for value in expected_sum], "reference_termination": reasons,
        "transmitted_chart_samples": transmitted_chart, "reentered_chart_samples": reentered_chart,
        "dense_receiver_crossings": {"minimum": min(dense_crossings), "maximum": max(dense_crossings),
            "samples": len(dense_crossings)} if dense_crossings else None}


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
        metrics[spec.name] = analyze_image(read_bmp_24_rows(directory / (spec.name + LIT_BMP)), spec)
    available = {spec.name for spec in specs}
    pairs = [("optical_tinted", LIT_OPTICAL + name) for name in
        (LIT_DUPLICATE_IDENTICAL, LIT_DUPLICATE_GROUP, LIT_DUPLICATE_REVERSE, LIT_MIRRORED)]
    pairs.append(("optical_disconnected", "optical_same_mesh"))
    pairs.extend((("optical_union_single", "optical_union_same_mesh"),
        ("optical_priority_a", "optical_priority_tie_a"), ("optical_priority_b", "optical_priority_tie_b"),
        ("optical_csg_reference", "optical_csg_cap"), ("optical_csg_reference", "optical_csg_cavity"),
        ("optical_csg_cap", "optical_csg_open_tail"),
        ("optical_unspecified_reference", "optical_csg_unspecified_cap"),
        ("optical_sliver", "optical_csg_sliver"), ("optical_sub_ulp", "optical_csg_sub_ulp"),
        ("optical_group_gap", "optical_csg_group_gap"),
        ("optical_group_gap_sub_ulp", "optical_csg_group_gap_sub_ulp"),
        ("optical_group_entry", "optical_csg_group_entry"), ("optical_dense", "optical_csg_dense")))
    for first, second in pairs:
        if first in available and second in available:
            metrics[first + "_equals_" + second] = compare_invariant(
                read_bmp_24_rows(directory / (first + LIT_BMP)), read_bmp_24_rows(directory / (second + LIT_BMP)))
    return metrics


def write_report(args, completed, evidence, metrics=None):
    metadata = {"frame_source": "actual application framebuffer readback", "size": [960, 720],
        "presentation": {"exposure": 1, "reinhard_shoulder": 1}, "temporal": False, "spatial_filter": False,
        "captures": [asdict(spec) for spec in completed], "completed_frame_evidence": evidence,
        "reference_source_sha256_lf": hashlib.sha256(Path(__file__).with_name("reflection_optical_reference.py").read_text(encoding=LIT_UTF_8).encode(LIT_UTF_8)).hexdigest(),
        "analyzer_source_sha256_lf": hashlib.sha256(Path(__file__).read_text(encoding=LIT_UTF_8).encode(LIT_UTF_8)).hexdigest(),
        "metrics": metrics, "limitations": LIMITATIONS}
    (args.output_directory / "reflection_optical_manifest.json").write_text(json.dumps(metadata, indent=2) + "\n", encoding=LIT_UTF_8)
    cards = []
    for spec in completed:
        path = args.output_directory / (spec.name + LIT_BMP)
        png = png_rgb_bytes(read_bmp_24_rows(path))
        path.with_suffix(".png").write_bytes(png)
        cards.append(f'<article><h2>{html.escape(spec.name)} / {spec.queries} queries / ray budget {spec.ray_budget}</h2>'
            f'<a href="{spec.name}.bmp">Raw BMP</a> / <a href="{spec.name}.log">Completed-frame log</a>'
            f'<img alt="Actual {html.escape(spec.name)} framebuffer" src="data:image/png;base64,{base64.b64encode(png).decode("ascii")}"></article>')
    document = f'<!doctype html><html lang="en"><meta charset="{LIT_UTF_8}"><title>Reflected optical transport</title>'
    document += '<style>body{font:16px system-ui;background:#141922;color:#e7edf5;margin:28px}main{display:grid;grid-template-columns:repeat(auto-fit,minmax(440px,1fr));gap:20px}article{padding:16px;background:#202937}img{display:block;width:100%;margin-top:12px}a{color:#8cf}pre{white-space:pre-wrap}</style>'
    document += '<h1>Reflected optical transport â€” actual framebuffer captures</h1><p>' + html.escape(LIMITATIONS) + '</p>'
    document += '<p>Glass and colored chart are behind the camera. The visible central rectangle is a smooth mirror. PNG conversion preserves every captured RGB pixel; raw BMPs remain available.</p><main>'
    document += ''.join(cards) + '</main><pre>' + html.escape(json.dumps(metrics, indent=2) if metrics else 'Captured evidence; image assertions have not passed yet.') + '</pre></html>'
    (args.output_directory / "reflection_optical.html").write_text(document, encoding=LIT_UTF_8)


def run_suite(args, specs=CAPTURES):
    completed, evidence = [], {}
    try:
        for spec in specs:
            result = capture(args, spec)
            if result is None:
                print("SKIP: required reflection hardware or framebuffer readback is unavailable", file=sys.stderr)
                return SKIP_EXIT_CODE
            completed.append(spec)
            evidence[spec.name] = result
        write_report(args, completed, evidence)
        metrics = analyze_suite(args.output_directory, specs)
        write_report(args, completed, evidence, metrics)
        print("PASS: optical reflected transport geometry, radiometry, query bounds and invariance\n" + json.dumps(metrics, indent=2), flush=True)
        return 0
    except (SmokeFailure, OSError, subprocess.TimeoutExpired) as exc:
        print("FAIL: " + str(exc), file=sys.stderr)
        return 1


def run_queue_boundary_suite(args):
    completed, evidence = [], {}
    try:
        for spec in QUEUE_BOUNDARY_CAPTURES:
            result = capture(args, spec)
            if result is None:
                print("SKIP: required reflection hardware or framebuffer readback is unavailable", file=sys.stderr)
                return SKIP_EXIT_CODE
            completed.append(spec)
            evidence[spec.name] = result
        write_report(args, completed, evidence)
        full = tuple(spec for spec in completed if spec.ray_budget == DEFAULT_RAY_BUDGET)
        metrics = analyze_suite(args.output_directory, full)
        metrics["queue_boundaries"] = [evidence[spec.name]["queue_coverage"] for spec in completed
            if spec.case == "optical_csg_cap"]
        metrics["coverage_limitations"] = ("Boundary captures validate completed queue coverage; limited-budget images "
            "have no fixed compaction order and are not claimed to match the full image oracle. Full-budget CSG "
            "and retained ordinary captures retain the independent radiometric and image invariance checks.")
        write_report(args, completed, evidence, metrics)
        print("PASS: CSG reflection queue group/slice boundaries, exact accepted-frame ray coverage and full optical oracle", flush=True)
        return 0
    except (SmokeFailure, OSError, subprocess.TimeoutExpired) as exc:
        print("FAIL: " + str(exc), file=sys.stderr)
        return 1


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--working-directory", type=Path, required=True)
    parser.add_argument("--output-directory", type=Path, required=True)
    parser.add_argument("--logserver-executable", type=Path)
    parser.add_argument("--timeout", type=float, default=90)
    parser.add_argument("--application-arg", action="append", default=[])
    focus = parser.add_mutually_exclusive_group()
    focus.add_argument("--queue-boundaries", action="store_true")
    focus.add_argument("--csg-reuse", action="store_true", help="validate closed collection reuse, open-tail retrace and unchanged query-budget exhaustion")
    focus.add_argument("--dense-receiver", action="store_true", help="capture matched opaque receivers with eighteen closed crossings")
    args = parser.parse_args(argv)
    if not math.isfinite(args.timeout) or args.timeout <= 0:
        parser.error("timeout must be finite and positive")
    args.require_hardware = True
    args.output_directory = args.output_directory.resolve()
    if args.output_directory.exists() and any(args.output_directory.iterdir()):
        parser.error("output directory must be empty; completed evidence is never overwritten")
    args.output_directory.mkdir(parents=True, exist_ok=True)
    if args.csg_reuse:
        return run_suite(args, CSG_REUSE_CAPTURES)
    if args.queue_boundaries:
        return run_queue_boundary_suite(args)
    return run_suite(args, DENSE_RECEIVER_CAPTURES if args.dense_receiver else CAPTURES)


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
