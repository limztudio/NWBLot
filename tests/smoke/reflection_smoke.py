#!/usr/bin/env python3
"""Validate screen-space and hardware reflection from actual application framebuffer readbacks."""

import argparse
import base64
import html
import json
import os
from pathlib import Path
import subprocess
import sys

from reflection_image_analysis import ReflectionImageAnalysis, LIT_BOUNDARY, LIT_MOVED, LIT_ONSCREEN, LIT_ONSCREEN_MOVED
from reflection_statistics import (
    ReflectionStatistics,
    DEFAULT_RAY_BUDGET,
    LIT_DISABLED,
    LIT_FLOOR,
    LIT_HARDWARE,
    LIT_HYBRID,
    LIT_MODE,
    LIT_OFFSCREEN,
    LIT_OPAQUE_GLASS,
    LIT_REFLECTIONSMOKESTATISTICS,
    LIT_SCREEN,
)
from framebuffer_png_encoder import FramebufferPngEncoder
from window_capture_image import read_bmp_24_rows
from window_capture_smoke import SKIP_EXIT_CODE, SmokeFailure

LIT_CAPTURE_TIMEOUT_MARGIN_SECONDS = 90

LIT_LOG_2 = "log"

LIT_CASE_MODE = "{case}_{mode}"
LIT_BMP = ".bmp"
LIT_LOG = ".log"
LIT_EXECUTABLE = "--executable"
LIT_WORKING_DIRECTORY = "--working-directory"
LIT_TIMEOUT = "--timeout"
LIT_EXPECT_LOG_MESSAGE = "--expect-log-message"
LIT_LOGSERVER_EXECUTABLE = "--logserver-executable"
LIT_REJECT_LOG_MESSAGE = "--reject-log-message"
LIT_REFLECTION_RESOLVE_HARDWARE = "Reflection resolve: hardware"
LIT_UTF_8 = "utf-8"
LIT_CAPTURES = "captures"
LIT_CASE = "case"
LIT_HARDWARE_BUDGET = "hardware_budget"
LIT_FILE = "file"
LIT_LIMITATIONS = "limitations"
LIT_ALL = "all"
LIT_BASELINE = "baseline"
LIT_BUDGET = "budget"
LIT_ROUGH = "rough"
LIT_TEMPORAL = "temporal"
LIT_OPTICAL = "optical"
LIT_FEEDBACK = "feedback"
LIT_FEEDBACK_DIAGNOSTICS_OFF = "feedback-diagnostics-off"
LIT_CASE_MODE_BMP = "{case}_{mode}.bmp"
LIT_MAIN = "__main__"
LIT_STORE_TRUE = "store_true"
LIT_APPEND = "append"


BASELINE_CAPTURES = ((LIT_OFFSCREEN, LIT_DISABLED), (LIT_OFFSCREEN, LIT_SCREEN), (LIT_OFFSCREEN, LIT_HARDWARE), (LIT_MOVED, LIT_DISABLED), (LIT_MOVED, LIT_HARDWARE), (LIT_OPAQUE_GLASS, LIT_DISABLED), (LIT_OPAQUE_GLASS, LIT_HARDWARE),
    (LIT_OFFSCREEN, LIT_HYBRID))
SCREEN_CAPTURES = ((LIT_ONSCREEN, LIT_DISABLED), (LIT_ONSCREEN, LIT_SCREEN), (LIT_ONSCREEN, LIT_HARDWARE), (LIT_ONSCREEN, LIT_HYBRID), (LIT_ONSCREEN_MOVED, LIT_SCREEN), (LIT_BOUNDARY, LIT_DISABLED),
    (LIT_BOUNDARY, LIT_SCREEN), (LIT_BOUNDARY, LIT_HYBRID), (LIT_FLOOR, LIT_DISABLED), (LIT_FLOOR, LIT_SCREEN), (LIT_FLOOR, LIT_HARDWARE), (LIT_FLOOR, LIT_HYBRID))
CAPTURES = BASELINE_CAPTURES + SCREEN_CAPTURES
BUDGET_CAPTURES = ((LIT_FLOOR, LIT_HYBRID, 0), (LIT_FLOOR, LIT_HYBRID, 64))


def capture_stem(case, mode, budget=DEFAULT_RAY_BUDGET):
    return LIT_CASE_MODE.format(case=case, mode=mode) + (f"_budget_{budget}" if budget != DEFAULT_RAY_BUDGET else "")


def capture_environment(case, mode, budget=DEFAULT_RAY_BUDGET):
    env = os.environ.copy()
    for name in tuple(env):
        if name.startswith("NWB_REFLECTION_SMOKE_") or name.startswith("NWB_REFRACTION_SMOKE_"):
            env.pop(name)
    env.pop("NWB_RENDERER_BASELINE_CAPTURE_FREEZE_FRAME", None)
    env.pop("NWB_GPU_TIMING_FILE", None)
    env["NWB_REFLECTION_SMOKE_CASE"] = case
    env["NWB_REFLECTION_SMOKE_MODE"] = mode
    env["NWB_REFLECTION_SMOKE_RAY_BUDGET"] = str(budget)
    env["NWB_RENDERER_BASELINE_FIXED_DELTA_SECONDS"] = "0.016666667"
    return env


def capture(args, case, mode, budget=DEFAULT_RAY_BUDGET):
    output = args.output_directory / (capture_stem(case, mode, budget) + LIT_BMP)
    log_output = output.with_suffix(LIT_LOG)
    command = [sys.executable, str(Path(__file__).with_name("window_capture_smoke.py")), LIT_EXECUTABLE, str(args.executable), LIT_WORKING_DIRECTORY, str(args.working_directory),
        "--output", str(output), "--application-capture", "--application-capture-frame-count", str(args.frames), LIT_TIMEOUT, str(args.timeout), LIT_EXPECT_LOG_MESSAGE, f"ReflectionSmokeProject: case {case} created",
        LIT_EXPECT_LOG_MESSAGE, f"ReflectionSmokeProject: reflection mode {mode}", LIT_EXPECT_LOG_MESSAGE, "ReflectionSmokeProject: shutdown", LIT_EXPECT_LOG_MESSAGE, f"ReflectionSmokeProject: hardware ray budget {budget}",
        LIT_EXPECT_LOG_MESSAGE, LIT_REFLECTIONSMOKESTATISTICS, "--log-output", str(log_output)]
    if args.logserver_executable:
        command += [LIT_LOGSERVER_EXECUTABLE, str(args.logserver_executable)]
    else:
        command.append("--no-logserver")
    if mode in (LIT_HARDWARE, LIT_HYBRID):
        if budget == 0:
            route = "screen-space" if mode == LIT_HYBRID else "environment"
            command += [LIT_EXPECT_LOG_MESSAGE, "Reflection resolve: " + route, LIT_REJECT_LOG_MESSAGE, LIT_REFLECTION_RESOLVE_HARDWARE]
        else:
            command += [LIT_EXPECT_LOG_MESSAGE, LIT_REFLECTION_RESOLVE_HARDWARE]
        command += [LIT_EXPECT_LOG_MESSAGE if args.require_hardware else "--skip-log-message", "ReflectionSmokeProject: hardware available" if args.require_hardware else "ReflectionSmokeProject: hardware unavailable"]
    elif mode == LIT_DISABLED:
        command += [LIT_EXPECT_LOG_MESSAGE, "Reflection resolve: disabled", LIT_REJECT_LOG_MESSAGE, LIT_REFLECTION_RESOLVE_HARDWARE]
    elif mode == LIT_SCREEN:
        command += [LIT_EXPECT_LOG_MESSAGE, "Reflection resolve: screen-space", LIT_REJECT_LOG_MESSAGE, LIT_REFLECTION_RESOLVE_HARDWARE]
    if case == LIT_OPAQUE_GLASS:
        command += [LIT_EXPECT_LOG_MESSAGE, "AVBOIT refraction resolve:"]
    command.extend("--application-arg=" + argument for argument in args.application_arg)
    print(f"Capturing {case}/{mode} budget={budget} ({args.frames} presentation frames)...", flush=True)
    # The child owns app/logserver cleanup: leave time for startup, graceful exit, kill fallback, and log drain.
    result = subprocess.run(command, env=capture_environment(case, mode, budget), check=False, timeout=args.timeout + LIT_CAPTURE_TIMEOUT_MARGIN_SECONDS)
    if result.returncode == SKIP_EXIT_CODE:
        return None
    if result.returncode:
        raise SmokeFailure(f"reflection {case}/{mode} capture failed with exit {result.returncode}")
    frame = read_bmp_24_rows(output)
    ReflectionImageAnalysis.validate_frame(frame)
    if frame[:2] != (960, 720):
        raise SmokeFailure(f"expected actual 960x720 framebuffer, got {frame[0]}x{frame[1]}")
    samples = ReflectionStatistics.parse_statistics(log_output.read_text(encoding=LIT_UTF_8))
    ReflectionStatistics.validate_statistics(samples, case, mode, budget)
    return frame, samples


def write_report(args, captures, metrics=None, statistics=None):
    metadata = {"frames": args.frames, "frame_source": "actual application framebuffer readback", "size": [960, 720], "hardware_required": args.require_hardware, "application_args": args.application_arg,
        "settings": {"environment_top": [0, 0, 0], "environment_bottom": [0, 0, 0], "max_hardware_rays_per_frame": 1382400, "roughness": 0, "glass_ior": 3.8}, "suite": args.suite, LIT_CAPTURES: [{LIT_CASE: case, LIT_MODE: mode, LIT_HARDWARE_BUDGET: budget,
            LIT_FILE: capture_stem(case, mode, budget) + LIT_BMP, LIT_LOG_2: capture_stem(case, mode, budget) + LIT_LOG} for case, mode, budget in captures], "statistics": statistics,
        "metrics": metrics, LIT_LIMITATIONS: "Smooth single-bounce reflection with on-screen and offscreen geometric marker checks. Wall-mirror screen hits approach marker backs and have provisional confidence; the floor case approaches their fronts. Completed per-frame counters separately check floor hybrid ray reduction and bounded budgets; they do not establish GPU speed. Hardware secondary-hit lighting is approximate; roughness reconstruction and temporal history are not validated here."}
    (args.output_directory / "reflection_manifest.json").write_text(json.dumps(metadata, indent=2) + "\n", encoding=LIT_UTF_8)
    cards = []
    for item in metadata[LIT_CAPTURES]:
        bmp = args.output_directory / item[LIT_FILE]
        png = FramebufferPngEncoder.encode(read_bmp_24_rows(bmp))
        bmp.with_suffix(".png").write_bytes(png)
        label = html.escape(item[LIT_CASE] + " / " + item[LIT_MODE] + " / budget " + str(item[LIT_HARDWARE_BUDGET]))
        cards.append(f'<article><h2>{label}</h2><a href="{html.escape(item[LIT_FILE])}">Raw BMP</a>' f' / <a href="{html.escape(item[LIT_LOG_2])}">Completed-frame log</a>'
            f'<img alt="Actual {label} framebuffer" src="data:image/png;base64,{base64.b64encode(png).decode("ascii")}"></article>')
    document = f'<!doctype html><html lang="en"><meta charset="{LIT_UTF_8}"><title>Reflection smoke captures</title>'
    document += '<style>body{background:#141922;color:#e7edf5;font:16px system-ui;margin:28px}main{display:grid;grid-template-columns:repeat(auto-fit,minmax(440px,1fr));gap:20px}article{background:#202937;padding:16px}img{display:block;width:100%;margin-top:12px}a{color:#88c9ff}pre{white-space:pre-wrap}h2{font-size:19px}</style>'
    document += '<h1>Reflection smoke - actual renderer captures</h1><p>' + html.escape(metadata[LIT_LIMITATIONS]) + '</p>'
    document += '<p>Images contain unchanged framebuffer RGB pixels, converted losslessly to PNG. Raw BMPs remain beside this report. Glass uses IOR 3.8 and its matching dielectric Fresnel F0.</p><main>'
    document += ''.join(cards) + '</main><pre>' + html.escape(json.dumps(metrics, indent=2) if metrics else 'Capture evidence; visual assertions have not passed yet.') + '</pre></html>'
    (args.output_directory / "reflection.html").write_text(document, encoding=LIT_UTF_8)


def parse_args(argv):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(LIT_EXECUTABLE, required=True, type=Path)
    parser.add_argument(LIT_WORKING_DIRECTORY, required=True, type=Path)
    parser.add_argument("--output-directory", required=True, type=Path)
    parser.add_argument(LIT_LOGSERVER_EXECUTABLE, type=Path)
    parser.add_argument("--suite", choices=(LIT_ALL, LIT_BASELINE, LIT_SCREEN, LIT_BUDGET, LIT_ROUGH, LIT_TEMPORAL, LIT_OPTICAL, LIT_FEEDBACK, LIT_FEEDBACK_DIAGNOSTICS_OFF), default=LIT_ALL,
        help="Capture the original 22 comparisons, a subset, or the separate roughness, history, optical and feedback suites.")
    parser.add_argument("--frames", type=int, default=16)
    parser.add_argument(LIT_TIMEOUT, type=float, default=180.0)
    parser.add_argument("--require-hardware", action=LIT_STORE_TRUE)
    parser.add_argument("--application-arg", action=LIT_APPEND, default=[])
    parser.add_argument("--feedback-cases", help="Optional comma-separated feedback capture names for a bounded pilot.")
    args = parser.parse_args(argv)
    if args.frames <= 0 or args.timeout <= 0:
        parser.error("frames and timeout must be positive")
    args.output_directory = args.output_directory.resolve()
    return args


def main(argv):
    args = parse_args(argv)
    args.output_directory.mkdir(parents=True, exist_ok=True)
    if args.suite in (LIT_ROUGH, LIT_TEMPORAL):
        from reflection_roughness_smoke import run_suite
        return run_suite(args)
    if args.suite in (LIT_FEEDBACK, LIT_FEEDBACK_DIAGNOSTICS_OFF):
        from reflection_feedback_smoke import run_suite
        return run_suite(args)
    if args.suite == LIT_OPTICAL:
        from reflection_optical_smoke import run_suite
        return run_suite(args)
    completed = []
    statistics = {}
    try:
        visual = BASELINE_CAPTURES if args.suite == LIT_BASELINE else SCREEN_CAPTURES if args.suite == LIT_SCREEN else CAPTURES
        selected = [(case, mode, DEFAULT_RAY_BUDGET) for case, mode in visual] if args.suite != LIT_BUDGET else []
        if args.suite in (LIT_ALL, LIT_BUDGET):
            selected.extend(BUDGET_CAPTURES)
        for case, mode, budget in selected:
            result = capture(args, case, mode, budget)
            if result is None:
                print("SKIP: required reflection hardware or framebuffer readback is unavailable", file=sys.stderr)
                return SKIP_EXIT_CODE
            frame, samples = result
            del result
            completed.append((case, mode, budget))
            statistics[capture_stem(case, mode, budget)] = samples
            del frame
        write_report(args, completed, statistics=statistics)
        metrics = {}
        if args.suite in (LIT_ALL, LIT_BASELINE):
            for case, mode in ((LIT_OFFSCREEN, LIT_DISABLED), (LIT_OFFSCREEN, LIT_SCREEN), (LIT_MOVED, LIT_DISABLED)):
                metrics[LIT_CASE_MODE.format(case=case, mode=mode)] = ReflectionImageAnalysis.analyze_markers(read_bmp_24_rows(args.output_directory / LIT_CASE_MODE_BMP.format(case=case, mode=mode)), required=False)
            metrics["marker_motion"] = ReflectionImageAnalysis.compare_marker_motion(read_bmp_24_rows(args.output_directory / "offscreen_hardware.bmp"), read_bmp_24_rows(args.output_directory / "moved_hardware.bmp"))
            metrics["offscreen_hybrid"] = ReflectionImageAnalysis.analyze_markers(read_bmp_24_rows(args.output_directory / "offscreen_hybrid.bmp"))
            metrics[LIT_OPAQUE_GLASS] = ReflectionImageAnalysis.compare_opaque_glass(read_bmp_24_rows(args.output_directory / "opaque_glass_disabled.bmp"), read_bmp_24_rows(args.output_directory / "opaque_glass_hardware.bmp"))
        if args.suite in (LIT_ALL, LIT_SCREEN):
            for case, mode in SCREEN_CAPTURES:
                metrics[LIT_CASE_MODE.format(case=case, mode=mode)] = ReflectionImageAnalysis.analyze_panels(read_bmp_24_rows(args.output_directory / LIT_CASE_MODE_BMP.format(case=case, mode=mode)), case, mode)
            metrics["panel_motion"] = ReflectionImageAnalysis.compare_panel_motion(read_bmp_24_rows(args.output_directory / "onscreen_screen.bmp"), read_bmp_24_rows(args.output_directory / "onscreen_moved_screen.bmp"))
            metrics["floor_completed_ray_comparison"] = ReflectionStatistics.compare_hybrid_statistics(ReflectionStatistics.validate_statistics(statistics["floor_hardware"], LIT_FLOOR, LIT_HARDWARE), ReflectionStatistics.validate_statistics(statistics["floor_hybrid"], LIT_FLOOR, LIT_HYBRID))
        if args.suite in (LIT_ALL, LIT_BUDGET):
            for case, mode, budget in BUDGET_CAPTURES:
                stem = capture_stem(case, mode, budget)
                metrics[stem] = ReflectionImageAnalysis.analyze_panels(read_bmp_24_rows(args.output_directory / (stem + LIT_BMP)), case, mode)
        write_report(args, completed, metrics, statistics)
        print(f"PASS: reflection {args.suite} geometric marker, route, and composition checks\n" + json.dumps(metrics, indent=2), flush=True)
        return 0
    except (SmokeFailure, OSError, subprocess.TimeoutExpired) as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        return 1


if __name__ == LIT_MAIN:
    raise SystemExit(main(sys.argv[1:]))
