#!/usr/bin/env python3

from reflection_statistics import ReflectionStatistics

from dataclasses import asdict, dataclass
import json
import subprocess
import sys

from reflection_image_analysis import ReflectionImageAnalysis
from reflection_history_evidence import ReflectionHistoryEvidence, LIT_TEMPORAL, LIT_ROUGH_DEFORM, LIT_REFLECTIONSMOKEHISTORY
from reflection_roughness_image_evidence import ReflectionRoughnessImageEvidence
from reflection_smoke import capture_environment
from reflection_roughness_reference import analyze_furnace, analyze_roughness, frame_grid
from smoke_capture_suite import build_window_capture_command, run_window_capture_subprocess, write_evidence_gallery_report
from window_capture_image_codec import Bmp24Codec
from window_capture_smoke import SKIP_EXIT_CODE, SmokeFailure

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
LIT_DEFORM = "deform"
LIT_CAMERA = "camera"
LIT_TRANSFORM = "transform"
LIT_MATERIAL = "material"
LIT_LIGHT = "light"
LIT_RESET_2 = "_reset"
LIT_FRESH = "_fresh"
LIT_CAMERA_SETTLED = "camera_settled"
LIT_DEFORM_BIND = "deform_bind"
LIT_HARDWARE = "hardware"
LIT_BMP = ".bmp"
LIT_UTF_8 = "utf-8"
LIT_NORMALIZED_REFERENCE_ERROR = "normalized_reference_error"
LIT_LIMITATIONS = "limitations"


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


ROUGH_CAPTURES = (CaptureSpec(LIT_MIRROR, roughness=0), CaptureSpec(LIT_ROUGH_02, roughness=0.2), CaptureSpec(LIT_ROUGH_04), CaptureSpec(LIT_ROUGH_04_SEED1, seed=1), CaptureSpec(LIT_ROUGH_06, roughness=0.6),
    CaptureSpec(LIT_ROUGH_04_N8, samples=8), CaptureSpec(LIT_ROUGH_04_N256, samples=256), CaptureSpec(LIT_MIRROR_RAW, roughness=0, temporal=False), CaptureSpec(LIT_ROUGH_04_SPATIAL_N8, samples=8, spatial=True),
    CaptureSpec(LIT_FURNACE_MIRROR, case=LIT_ROUGH_FURNACE, roughness=0), CaptureSpec(LIT_FURNACE_ROUGH, case=LIT_ROUGH_FURNACE, roughness=1, samples=256), CaptureSpec(LIT_FURNACE_ROUGH_RAW, case=LIT_ROUGH_FURNACE, roughness=1, temporal=False),
    CaptureSpec(LIT_GLASS_SMOOTH, case=LIT_ROUGH_GLASS, roughness=0), CaptureSpec(LIT_GLASS_AUTHORED_ROUGH, case=LIT_ROUGH_GLASS, roughness=0.6),)
TEMPORAL_CAPTURES = tuple(CaptureSpec(kind + suffix, case=LIT_TEMPORAL + kind, roughness=0 if kind == LIT_DEFORM else 0.4, final_state=fresh) for kind in (LIT_CAMERA, LIT_TRANSFORM, LIT_MATERIAL, LIT_LIGHT, LIT_DEFORM)
    for suffix, fresh in ((LIT_RESET_2, False), (LIT_FRESH, True))) + (CaptureSpec(LIT_CAMERA_SETTLED, case="temporal_camera", post_reset_samples=64), CaptureSpec(LIT_DEFORM_BIND, case=LIT_ROUGH_DEFORM, roughness=0),)


def spec_environment(spec):
    env = capture_environment(spec.case, LIT_HARDWARE)
    for key, value in (("ROUGHNESS", spec.roughness), ("HISTORY_SAMPLES", spec.samples), ("TEMPORAL", int(spec.temporal)), ("SPATIAL", int(spec.spatial)), ("FINAL_STATE", int(spec.final_state)), ("POST_RESET_SAMPLES", spec.post_reset_samples),
        ("SEED", spec.seed), ("DIAGNOSTICS", 1)):
        env["NWB_REFLECTION_SMOKE_" + key] = str(value)
    return env


def capture(args, spec):
    output = args.output_directory / (spec.name + LIT_BMP)
    log_output = output.with_suffix(".log")
    expect_logs = [f"ReflectionSmokeProject: case {spec.case} created", "ReflectionSmokeProject: reflection mode hardware",
        "ReflectionSmokeProject: shutdown", "Reflection resolve: hardware", LIT_REFLECTIONSMOKEHISTORY]
    if args.require_hardware:
        expect_logs.append("ReflectionSmokeProject: hardware available")
        skip_logs = ()
    else:
        skip_logs = ("ReflectionSmokeProject: hardware unavailable",)
    if spec.case == LIT_ROUGH_GLASS:
        expect_logs.append("AVBOIT refraction resolve:")
    command = build_window_capture_command(args, output, frames=1, log_output=log_output, expect_logs=expect_logs, skip_logs=skip_logs)
    print(f"Capturing {spec.name}: r={spec.roughness}, accepted cap={spec.samples}, seed={spec.seed}, temporal={spec.temporal}...", flush=True)
    if run_window_capture_subprocess(command, env=spec_environment(spec), timeout=args.timeout, failure_message=f"{spec.name} capture failed with exit {{code}}") is None:
        return None
    frame = Bmp24Codec.read_bmp_24_rows(output)
    ReflectionImageAnalysis.validate_frame(frame)
    if frame[:2] != (960, 720):
        raise SmokeFailure("roughness capture is not the actual 960x720 framebuffer")
    log_text = log_output.read_text(encoding=LIT_UTF_8)
    statistics = ReflectionStatistics.parse_statistics(log_text)
    ReflectionStatistics.validate_statistics(statistics, spec.case, LIT_HARDWARE, allow_zero_samples=spec.roughness > 0)
    evidence = ReflectionHistoryEvidence.validate_history(log_text, statistics, spec)
    evidence["statistics"] = statistics
    return evidence


def analyze_suite(args):
    def frame(name):
        return Bmp24Codec.read_bmp_24_rows(args.output_directory / (name + LIT_BMP))
    metrics = {}
    if args.suite == LIT_ROUGH:
        for name, roughness in ((LIT_MIRROR, 0), (LIT_ROUGH_02, 0.2), (LIT_ROUGH_04, 0.4), (LIT_ROUGH_04_SEED1, 0.4), (LIT_ROUGH_06, 0.6), (LIT_ROUGH_04_N8, 0.4), (LIT_ROUGH_04_N256, 0.4), (LIT_ROUGH_04_SPATIAL_N8, 0.4)):
            metrics[name] = analyze_roughness(frame(name), roughness)
        metrics["independent_seeds"] = ReflectionRoughnessImageEvidence.compare_sampling_seeds(frame(LIT_ROUGH_04), frame(LIT_ROUGH_04_SEED1))
        error8 = metrics[LIT_ROUGH_04_N8][LIT_NORMALIZED_REFERENCE_ERROR]
        error64 = metrics[LIT_ROUGH_04][LIT_NORMALIZED_REFERENCE_ERROR]
        error256 = metrics[LIT_ROUGH_04_N256][LIT_NORMALIZED_REFERENCE_ERROR]
        metrics["convergence"] = ReflectionRoughnessImageEvidence.compare_convergence(error8, error64, error256)
        raw_noise = ReflectionRoughnessImageEvidence.high_frequency_energy(frame(LIT_ROUGH_04_N8))
        spatial_noise = ReflectionRoughnessImageEvidence.high_frequency_energy(frame(LIT_ROUGH_04_SPATIAL_N8))
        if spatial_noise >= raw_noise * 0.95:
            raise SmokeFailure("separate spatial filter did not reduce unresolved local sample noise")
        metrics["spatial"] = {"raw_high_frequency_energy": raw_noise, "filtered_high_frequency_energy": spatial_noise}
        metrics["mirror_history_identity"] = ReflectionRoughnessImageEvidence.compare_exact_scene(frame(LIT_MIRROR), frame(LIT_MIRROR_RAW))
        metrics["glass_stays_smooth"] = ReflectionRoughnessImageEvidence.compare_smooth_glass_scene(frame(LIT_GLASS_SMOOTH), frame(LIT_GLASS_AUTHORED_ROUGH))
        for name, roughness in ((LIT_FURNACE_MIRROR, 0), (LIT_FURNACE_ROUGH, 1), (LIT_FURNACE_ROUGH_RAW, 1)):
            metrics[name] = analyze_furnace(frame(name), roughness)
    else:
        for kind in (LIT_CAMERA, LIT_TRANSFORM, LIT_MATERIAL, LIT_LIGHT, LIT_DEFORM):
            metrics[kind] = ReflectionRoughnessImageEvidence.compare_exact_scene(frame(kind + LIT_RESET_2), frame(kind + LIT_FRESH))
            if kind in (LIT_TRANSFORM, LIT_MATERIAL, LIT_LIGHT):
                _, grid, _, _ = frame_grid(frame(kind + LIT_RESET_2))
                red, green = sum(value[0] for value in grid), sum(value[1] for value in grid)
                if red > 0.001 or green < 1.0:
                    raise SmokeFailure(f"{kind} mutation retained the removed red reflection or lost its unchanged green control")
                metrics[kind]["remaining_red_cell_radiance"] = red
                metrics[kind]["retained_green_cell_radiance"] = green
        metrics[LIT_CAMERA_SETTLED] = analyze_roughness(frame(LIT_CAMERA_SETTLED), 0.4, camera_x=0.6)
        metrics["deformation_footprint"] = ReflectionRoughnessImageEvidence.compare_deformation(frame(LIT_DEFORM_BIND), frame("deform_reset"))
    return metrics


def write_report(args, completed, evidence, metrics=None):
    metadata = {"suite": args.suite, "frame_source": "actual application framebuffer readback", "captures": [asdict(spec) for spec in completed], "evidence": evidence, "metrics": metrics,
        "reference": "Independent rectangular-emitter area quadrature of correlated Smith GGX; no production VNDF sampler.",
        LIT_LIMITATIONS: "Single-bounce opaque GGX with strict static history. Runtime deformation disables history. Clear glass stays smooth. Spatial output is separate from history. Counters and sample counts are completion evidence, not GPU timing."}
    items = [(spec.name, spec.name, spec.name + LIT_BMP, spec.name + ".log", "Completed-frame log", f"Actual {spec.name} framebuffer", None) for spec in completed]
    style = "body{background:#141922;color:#eee;font:16px system-ui;margin:28px}main{display:grid;grid-template-columns:repeat(auto-fit,minmax(440px,1fr));gap:20px}article{background:#202937;padding:16px}img{width:100%}a{color:#88c9ff}pre{white-space:pre-wrap}"
    write_evidence_gallery_report(args.output_directory, manifest_name="reflection_manifest.json", html_name="reflection.html",
        metadata=metadata, title="Reflection roughness and history evidence", heading="Actual reflection framebuffer captures",
        note=metadata[LIT_LIMITATIONS], items=items, metrics=metrics, pending_message="Visual analysis pending.", style=style)


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
