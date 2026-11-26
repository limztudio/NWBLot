#!/usr/bin/env python3
"""Actual framebuffer optical-transport regression with independent geometric/radiometric oracles."""


import argparse
from dataclasses import asdict, dataclass
import json
import math
from pathlib import Path
import subprocess
import sys
import tempfile

from reflection_optical_reference import DENSE_CASES
from reflection_image_analysis import ReflectionImageAnalysis
from reflection_optical_image_evidence import ReflectionOpticalImageEvidence
from reflection_optical_evidence import (ReflectionOpticalEvidence,
    LIT_HARDWARE, LIT_OPTICAL_CSG_CAP, LIT_OPTICAL_CSG_DENSE, LIT_OPTICAL_CSG_GROUP_ENTRY,
    LIT_OPTICAL_CSG_GROUP_GAP, LIT_OPTICAL_CSG_GROUP_GAP_SUB_ULP, LIT_OPTICAL_CSG_OPEN_TAIL, LIT_OPTICAL_CSG_REFERENCE,
    LIT_OPTICAL_CSG_SLIVER, LIT_OPTICAL_CSG_SUB_ULP, LIT_OPTICAL_CSG_UNSPECIFIED_CAP, LIT_OPTICAL_GROUP_ENTRY,
    LIT_OPTICAL_GROUP_GAP, LIT_OPTICAL_GROUP_GAP_SUB_ULP, LIT_OPTICAL_QUERY_LIMIT, LIT_OPTICAL_SLIVER,
    LIT_OPTICAL_SUB_ULP, LIT_OPTICAL_TIR, LIT_OPTICAL_TIR_LIMIT, LIT_OPTICAL_UNSPECIFIED_REFERENCE,
    LIT_QUEUE_COVERAGE, LIT_REFLECTIONSMOKEOPTICS, LIT_STATISTICS)
from reflection_smoke import DEFAULT_RAY_BUDGET, capture_environment
from smoke_capture_suite import (
    build_window_capture_command,
    run_window_capture_subprocess,
    sha256_source_files,
    write_evidence_gallery_report,
)
from window_capture_image_codec import Bmp24Codec
from window_capture_smoke import SKIP_EXIT_CODE, SmokeFailure

LIT_EXECUTABLE = "--executable"
LIT_WORKING_DIRECTORY = "--working-directory"
LIT_TIMEOUT = "--timeout"
LIT_LOGSERVER_EXECUTABLE = "--logserver-executable"
LIT_REFLECTION_HARDWARE_OR_READBACK_UNAVAILABLE = "SKIP: required reflection hardware or framebuffer readback is unavailable"
LIT_FAIL = "FAIL: "
LIT_STORE_TRUE = "store_true"

LIT_DUPLICATE_IDENTICAL = "duplicate_identical"
LIT_DUPLICATE_GROUP = "duplicate_group"
LIT_DUPLICATE_REVERSE = "duplicate_reverse"
LIT_MIRRORED = "mirrored"
LIT_OPTICAL = "optical_"
LIT_BMP = ".bmp"
LIT_LOG = ".log"
LIT_UTF_8 = "utf-8"


@dataclass(frozen=True)
class OpticalCapture:
    name: str
    case: str
    queries: int = 16
    ray_budget: int = DEFAULT_RAY_BUDGET


CASES = ("reference", "clear", "tinted", "tilted", "nested2", "nested3", "priority_a", "priority_b", "alpha_before", "alpha_after", LIT_DUPLICATE_IDENTICAL, LIT_DUPLICATE_GROUP, LIT_DUPLICATE_REVERSE, LIT_MIRRORED,
    "disconnected", "same_mesh", "torus", "inside", "inside_nested", "unspecified", "mixed", "overflow", "tir", "union_single", "union_same_mesh", "coincident_independent", "priority_tie_a", "priority_tie_b",
    "csg_reference", "csg_cap", "csg_cavity", "csg_open_tail", "unspecified_reference", "csg_unspecified_cap", "sliver", "csg_sliver", "sub_ulp", "csg_sub_ulp", "group_gap", "csg_group_gap",
    "group_gap_sub_ulp", "csg_group_gap_sub_ulp", "group_entry", "csg_group_entry")
CAPTURES = tuple(OpticalCapture(LIT_OPTICAL + name, LIT_OPTICAL + name) for name in CASES) + (OpticalCapture(LIT_OPTICAL_QUERY_LIMIT, "optical_clear", 1), OpticalCapture(LIT_OPTICAL_TIR_LIMIT, LIT_OPTICAL_TIR, 3))
QUEUE_BOUNDARY_BUDGETS = (0, 1, 63, 64, 65, 8191, 8192, 8193, DEFAULT_RAY_BUDGET)
QUEUE_BOUNDARY_CAPTURES = (OpticalCapture("optical_csg_reference", "optical_csg_reference"),) + tuple(OpticalCapture("optical_csg_cap" if budget == DEFAULT_RAY_BUDGET else f"optical_csg_cap_budget_{budget}",
        LIT_OPTICAL_CSG_CAP, ray_budget=budget) for budget in QUEUE_BOUNDARY_BUDGETS)
DENSE_RECEIVER_CAPTURES = tuple(OpticalCapture(case, case) for case in DENSE_CASES)
CSG_REUSE_CAPTURES = (OpticalCapture("optical_csg_reference", "optical_csg_reference"), OpticalCapture("optical_csg_cap", "optical_csg_cap"), OpticalCapture("optical_csg_open_tail", "optical_csg_open_tail"),
    OpticalCapture("optical_csg_cap_query_limit", LIT_OPTICAL_CSG_CAP, 8), OpticalCapture(LIT_OPTICAL_UNSPECIFIED_REFERENCE, LIT_OPTICAL_UNSPECIFIED_REFERENCE), OpticalCapture(LIT_OPTICAL_CSG_UNSPECIFIED_CAP, LIT_OPTICAL_CSG_UNSPECIFIED_CAP),)
LIMITATIONS = ("Actual renderer framebuffer pixels; independent float64 plane/box/triangle intersections, exact dielectric Fresnel, "
    "Snell refraction and world-distance Beer absorption predict stripe positions and RGB energy. The model follows one deterministic " "transmitted secondary path and continues total internal reflection; secondary non-TIR reflected branches are omitted. "
    "ClosedNested and ClosedPriority are explicit authored volume contracts. Unsupported, ambiguous and exhausted residual paths " "are conservatively black. These comparisons do not establish complete path tracing or a GPU speed improvement.")








def capture(args, spec):
    output = args.output_directory / (spec.name + LIT_BMP)
    log_path = output.with_suffix(LIT_LOG)
    expect_logs = ["ReflectionSmokeProject: case " + spec.case + " created", "ReflectionSmokeProject: reflection mode hardware",
        "ReflectionSmokeProject: optical query limit " + str(spec.queries), "ReflectionSmokeProject: hardware ray budget " + str(spec.ray_budget),
        "ReflectionSmokeProject: shutdown", "Reflection resolve: hardware" if spec.ray_budget else "Reflection resolve: environment", LIT_REFLECTIONSMOKEOPTICS]
    skip_logs = ()
    if args.require_hardware:
        expect_logs.append("ReflectionSmokeProject: hardware available")
    else:
        skip_logs = ("ReflectionSmokeProject: hardware unavailable",)
    command = build_window_capture_command(args, output, frames=1, log_output=log_path, expect_logs=expect_logs, skip_logs=skip_logs)
    environment = capture_environment(spec.case, LIT_HARDWARE, spec.ray_budget)
    environment.update({"NWB_REFLECTION_SMOKE_OPTICAL_QUERIES": str(spec.queries), "NWB_REFLECTION_SMOKE_TEMPORAL": "0", "NWB_REFLECTION_SMOKE_SPATIAL": "0", "NWB_REFLECTION_SMOKE_HISTORY_SAMPLES": "16", "NWB_REFLECTION_SMOKE_DIAGNOSTICS": "1"})
    print(f"Capturing {spec.name}: ray budget {spec.ray_budget}, max {spec.queries} queries per admitted optical path...", flush=True)
    if run_window_capture_subprocess(command, env=environment, timeout=args.timeout, failure_message=f"{spec.name} capture failed with exit {{code}}") is None:
        return None
    frame = Bmp24Codec.read_bmp_24_rows(output)
    ReflectionImageAnalysis.validate_frame(frame)
    if frame[:2] != (960, 720):
        raise SmokeFailure("optical capture is not the actual 960x720 framebuffer")
    from reflection_roughness_smoke import CaptureSpec
    from reflection_history_evidence import ReflectionHistoryEvidence
    log_text = log_path.read_text(encoding=LIT_UTF_8)
    evidence = ReflectionOpticalEvidence.validate_optics(log_text, spec)
    evidence["capture_source"] = ReflectionHistoryEvidence.validate_history(log_text, evidence[LIT_STATISTICS], CaptureSpec(spec.name, case=spec.case, roughness=0, samples=16, temporal=False))
    if spec in CSG_REUSE_CAPTURES[1:]:
        evidence["collection_reuse"] = ReflectionOpticalEvidence.validate_collection_reuse(evidence, spec)
    return evidence





def analyze_suite(directory, specs=CAPTURES):
    metrics = {}
    for spec in specs:
        metrics[spec.name] = ReflectionOpticalImageEvidence.analyze_image(Bmp24Codec.read_bmp_24_rows(directory / (spec.name + LIT_BMP)), spec)
    available = {spec.name for spec in specs}
    pairs = [("optical_tinted", LIT_OPTICAL + name) for name in (LIT_DUPLICATE_IDENTICAL, LIT_DUPLICATE_GROUP, LIT_DUPLICATE_REVERSE, LIT_MIRRORED)]
    pairs.append(("optical_disconnected", "optical_same_mesh"))
    pairs.extend((("optical_union_single", "optical_union_same_mesh"), ("optical_priority_a", "optical_priority_tie_a"), ("optical_priority_b", "optical_priority_tie_b"),
        (LIT_OPTICAL_CSG_REFERENCE, LIT_OPTICAL_CSG_CAP), (LIT_OPTICAL_CSG_REFERENCE, "optical_csg_cavity"), (LIT_OPTICAL_CSG_CAP, LIT_OPTICAL_CSG_OPEN_TAIL), (LIT_OPTICAL_UNSPECIFIED_REFERENCE, LIT_OPTICAL_CSG_UNSPECIFIED_CAP),
        (LIT_OPTICAL_SLIVER, LIT_OPTICAL_CSG_SLIVER), (LIT_OPTICAL_SUB_ULP, LIT_OPTICAL_CSG_SUB_ULP), (LIT_OPTICAL_GROUP_GAP, LIT_OPTICAL_CSG_GROUP_GAP), (LIT_OPTICAL_GROUP_GAP_SUB_ULP, LIT_OPTICAL_CSG_GROUP_GAP_SUB_ULP),
        (LIT_OPTICAL_GROUP_ENTRY, LIT_OPTICAL_CSG_GROUP_ENTRY), ("optical_dense", LIT_OPTICAL_CSG_DENSE)))
    for first, second in pairs:
        if first in available and second in available:
            metrics[first + "_equals_" + second] = ReflectionOpticalImageEvidence.compare_invariant(Bmp24Codec.read_bmp_24_rows(directory / (first + LIT_BMP)), Bmp24Codec.read_bmp_24_rows(directory / (second + LIT_BMP)))
    return metrics


def write_report(args, completed, evidence, metrics=None):
    here = Path(__file__)
    hashes = sha256_source_files({
        "reference_source_sha256_lf": here.with_name("reflection_optical_reference.py"),
        "geometry_source_sha256_lf": here.with_name("reflection_optical_geometry.py"),
        "analyzer_source_sha256_lf": here,
        "evidence_source_sha256_lf": here.with_name("reflection_optical_evidence.py"),
        "png_encoder_source_sha256_lf": here.with_name("framebuffer_png_encoder.py"),
        "image_evidence_source_sha256_lf": here.with_name("reflection_optical_image_evidence.py"),
    })
    metadata = {"frame_source": "actual application framebuffer readback", "size": [960, 720], "presentation": {"exposure": 1, "reinhard_shoulder": 1}, "temporal": False, "spatial_filter": False,
        "captures": [asdict(spec) for spec in completed], "completed_frame_evidence": evidence, **hashes, "metrics": metrics, "limitations": LIMITATIONS}
    items = [(spec.name, f"{spec.name} / {spec.queries} queries / ray budget {spec.ray_budget}", spec.name + LIT_BMP, spec.name + LIT_LOG,
        "Completed-frame log", f"Actual {spec.name} framebuffer", None) for spec in completed]
    style = "body{font:16px system-ui;background:#141922;color:#e7edf5;margin:28px}main{display:grid;grid-template-columns:repeat(auto-fit,minmax(440px,1fr));gap:20px}article{padding:16px;background:#202937}img{display:block;width:100%;margin-top:12px}a{color:#8cf}pre{white-space:pre-wrap}"
    write_evidence_gallery_report(args.output_directory, manifest_name="reflection_optical_manifest.json", html_name="reflection_optical.html",
        metadata=metadata, title="Reflected optical transport", heading="Reflected optical transport - actual framebuffer captures",
        note=LIMITATIONS, extra_paragraphs=("Glass and colored chart are behind the camera. The visible central rectangle is a smooth mirror. PNG conversion preserves every captured RGB pixel; raw BMPs remain available.",),
        items=items, metrics=metrics, pending_message="Captured evidence; image assertions have not passed yet.", style=style)


def run_suite(args, specs=CAPTURES):
    completed, evidence = [], {}
    try:
        for spec in specs:
            result = capture(args, spec)
            if result is None:
                print(LIT_REFLECTION_HARDWARE_OR_READBACK_UNAVAILABLE, file=sys.stderr)
                return SKIP_EXIT_CODE
            completed.append(spec)
            evidence[spec.name] = result
        write_report(args, completed, evidence)
        metrics = analyze_suite(args.output_directory, specs)
        write_report(args, completed, evidence, metrics)
        print("PASS: optical reflected transport geometry, radiometry, query bounds and invariance\n" + json.dumps(metrics, indent=2), flush=True)
        return 0
    except (SmokeFailure, OSError, subprocess.TimeoutExpired) as exc:
        print(LIT_FAIL + str(exc), file=sys.stderr)
        return 1


def run_queue_boundary_suite(args):
    completed, evidence = [], {}
    try:
        for spec in QUEUE_BOUNDARY_CAPTURES:
            result = capture(args, spec)
            if result is None:
                print(LIT_REFLECTION_HARDWARE_OR_READBACK_UNAVAILABLE, file=sys.stderr)
                return SKIP_EXIT_CODE
            completed.append(spec)
            evidence[spec.name] = result
        write_report(args, completed, evidence)
        full = tuple(spec for spec in completed if spec.ray_budget == DEFAULT_RAY_BUDGET)
        metrics = analyze_suite(args.output_directory, full)
        metrics["queue_boundaries"] = [evidence[spec.name][LIT_QUEUE_COVERAGE] for spec in completed if spec.case == LIT_OPTICAL_CSG_CAP]
        metrics["coverage_limitations"] = ("Boundary captures validate completed queue coverage; limited-budget images " "have no fixed compaction order and are not claimed to match the full image oracle. Full-budget CSG "
            "and retained ordinary captures retain the independent radiometric and image invariance checks.")
        write_report(args, completed, evidence, metrics)
        print("PASS: CSG reflection queue group/slice boundaries, exact accepted-frame ray coverage and full optical oracle", flush=True)
        return 0
    except (SmokeFailure, OSError, subprocess.TimeoutExpired) as exc:
        print(LIT_FAIL + str(exc), file=sys.stderr)
        return 1


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(LIT_EXECUTABLE, type=Path, required=True)
    parser.add_argument(LIT_WORKING_DIRECTORY, type=Path, required=True)
    output = parser.add_mutually_exclusive_group(required=True)
    output.add_argument("--output-directory", type=Path, help="Use this exact empty evidence directory.")
    output.add_argument("--output-root", type=Path, help="Preserve prior evidence and create a unique child directory for this run.")
    parser.add_argument(LIT_LOGSERVER_EXECUTABLE, type=Path)
    parser.add_argument(LIT_TIMEOUT, type=float, default=90)
    parser.add_argument("--application-arg", action="append", default=[])
    focus = parser.add_mutually_exclusive_group()
    focus.add_argument("--queue-boundaries", action=LIT_STORE_TRUE)
    focus.add_argument("--csg-reuse", action=LIT_STORE_TRUE, help="validate closed collection reuse, open-tail retrace and unchanged query-budget exhaustion")
    focus.add_argument("--dense-receiver", action=LIT_STORE_TRUE, help="capture matched opaque receivers with eighteen closed crossings")
    args = parser.parse_args(argv)
    if not math.isfinite(args.timeout) or args.timeout <= 0:
        parser.error("timeout must be finite and positive")
    args.require_hardware = True
    if args.output_root is not None:
        root = args.output_root.resolve()
        root.mkdir(parents=True, exist_ok=True)
        args.output_directory = Path(tempfile.mkdtemp(prefix="reflection_optical_", dir=root))
        print(f"Reflection optical capture evidence: {args.output_directory}", flush=True)
    else:
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
