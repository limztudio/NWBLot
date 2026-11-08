#!/usr/bin/env python3
"""Completed-frame GGX and strict-history smoke evidence; images always come from GPU readback."""

from reflection_statistics import ReflectionStatistics

import base64
from dataclasses import asdict, dataclass
import html
import json
from pathlib import Path
import subprocess
import sys

from reflection_image_analysis import ReflectionImageAnalysis
from reflection_history_evidence import ReflectionHistoryEvidence, LIT_TEMPORAL, LIT_ROUGH_DEFORM, LIT_REFLECTIONSMOKEHISTORY
from reflection_roughness_image_evidence import ReflectionRoughnessImageEvidence
from reflection_smoke import capture_environment
from reflection_roughness_reference import analyze_furnace, analyze_roughness, frame_grid
from framebuffer_png_encoder import FramebufferPngEncoder
from window_capture_image import read_bmp_24_rows
from window_capture_smoke import SKIP_EXIT_CODE, SmokeFailure

LIT_CAPTURE_TIMEOUT_MARGIN_SECONDS = 90

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
LIT_EXPECT_LOG_MESSAGE = "--expect-log-message"
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
    command = [sys.executable, str(Path(__file__).with_name("window_capture_smoke.py")), "--executable", str(args.executable), "--working-directory", str(args.working_directory),
        "--output", str(output), "--application-capture", "--application-capture-frame-count", "1", "--timeout", str(args.timeout), LIT_EXPECT_LOG_MESSAGE, f"ReflectionSmokeProject: case {spec.case} created",
        LIT_EXPECT_LOG_MESSAGE, "ReflectionSmokeProject: reflection mode hardware", LIT_EXPECT_LOG_MESSAGE, "ReflectionSmokeProject: shutdown", LIT_EXPECT_LOG_MESSAGE, "Reflection resolve: hardware",
        LIT_EXPECT_LOG_MESSAGE, LIT_REFLECTIONSMOKEHISTORY, "--log-output", str(log_output), LIT_EXPECT_LOG_MESSAGE if args.require_hardware else "--skip-log-message",
        "ReflectionSmokeProject: hardware available" if args.require_hardware else "ReflectionSmokeProject: hardware unavailable"]
    if args.logserver_executable:
        command += ["--logserver-executable", str(args.logserver_executable)]
    else:
        command.append("--no-logserver")
    if spec.case == LIT_ROUGH_GLASS:
        command += [LIT_EXPECT_LOG_MESSAGE, "AVBOIT refraction resolve:"]
    command.extend("--application-arg=" + argument for argument in args.application_arg)
    print(f"Capturing {spec.name}: r={spec.roughness}, accepted cap={spec.samples}, seed={spec.seed}, temporal={spec.temporal}...", flush=True)
    result = subprocess.run(command, env=spec_environment(spec), check=False, timeout=args.timeout + LIT_CAPTURE_TIMEOUT_MARGIN_SECONDS)
    if result.returncode == SKIP_EXIT_CODE:
        return None
    if result.returncode:
        raise SmokeFailure(f"{spec.name} capture failed with exit {result.returncode}")
    frame = read_bmp_24_rows(output)
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
        return read_bmp_24_rows(args.output_directory / (name + LIT_BMP))
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
    (args.output_directory / "reflection_manifest.json").write_text(json.dumps(metadata, indent=2) + "\n", encoding=LIT_UTF_8)
    cards = []
    for spec in completed:
        bmp = args.output_directory / (spec.name + LIT_BMP)
        png = FramebufferPngEncoder.encode(read_bmp_24_rows(bmp))
        bmp.with_suffix(".png").write_bytes(png)
        name = html.escape(spec.name)
        cards.append(f'<article><h2>{name}</h2><a href="{name}.bmp">Raw BMP</a> / <a href="{name}.log">Completed-frame log</a>' f'<img alt="Actual {name} framebuffer" src="data:image/png;base64,{base64.b64encode(png).decode("ascii")}"></article>')
    document = f'<!doctype html><html lang="en"><meta charset="{LIT_UTF_8}"><title>Reflection roughness and history evidence</title>'
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
