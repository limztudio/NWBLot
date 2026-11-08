#!/usr/bin/env python3
"""Verify accepted screen-trace feedback, bounded work, and actual framebuffer image equivalence."""

import base64
from dataclasses import asdict, dataclass
import hashlib
import html
import json
from pathlib import Path
import subprocess
import sys

from reflection_image_analysis import ReflectionImageAnalysis
from reflection_statistics import ReflectionStatistics
import reflection_feedback_image_analysis as feedback_image_analysis
from reflection_feedback_image_analysis import (
    ReflectionFeedbackImageAnalysis,
    LIT_HYBRID,
    LIT_FLOOR,
    LIT_ONSCREEN,
    LIT_OPAQUE_GLASS,
    LIT_ROUGH_FURNACE,
    LIT_FEEDBACK_LONG_MISS,
)
from reflection_smoke import DEFAULT_RAY_BUDGET, SmokeFailure, capture_environment
import reflection_feedback_evidence as feedback_evidence
from reflection_feedback_evidence import (
    ReflectionFeedbackEvidence,
    TraversalEvidence,
    LIT_DIAGNOSTICS,
    LIT_GENERATION,
    LIT_OFFSCREEN,
    LIT_REFLECTIONSMOKEFEEDBACK,
    LIT_REFLECTIONSMOKEHISTORY,
    LIT_REFLECTIONSMOKEOPTICS,
    LIT_REFLECTIONSMOKEPROJECT_SCREEN_FEEDBACK,
    LIT_REFLECTIONSMOKEPROJECT_SCREEN_STEPS_SP,
    LIT_REFLECTIONSMOKESTATISTICS,
    LIT_SCREEN_ATTEMPTS,
    LIT_SCREEN_ITERATIONS,
    LIT_SCREEN_LIMIT_MISSES,
    LIT_SEQUENCE,
    LIT_STABLE_FEEDBACK,
    LIT_STATISTICS,
)
from framebuffer_png_encoder import FramebufferPngEncoder
from window_capture_image import read_bmp_24_rows
from window_capture_smoke import SKIP_EXIT_CODE
from smoke_volume_identity import authored_volume_hashes, file_identity

LIT_CAPTURE_TIMEOUT_MARGIN_SECONDS = 90

LIT_NPOT = "npot"
LIT_HARDWARE_BUDGET = "hardware_budget"
LIT_SCREEN = "screen"
LIT_FEEDBACK = "_feedback"
LIT_BASELINE = "_baseline"
LIT_LONG_MISS_GEOMETRY = "long_miss_geometry"
LIT_MUTATION_INITIAL = "mutation_initial"
LIT_MUTATION_CHANGED = "mutation_changed"
LIT_FEEDBACK_MUTATION = "feedback_mutation"
LIT_MUTATION_FRESH = "mutation_fresh"
LIT_DIAGNOSTICS_OFF = "diagnostics_off_"
LIT_BMP = ".bmp"
LIT_LOG = ".log"
LIT_EXPECT_LOG_MESSAGE = "--expect-log-message"
LIT_UTF_8 = "utf-8"
LIT_IMAGES = "images"
LIT_SCOPE = "scope"
LIT_LONG_MISS_BASELINE = "long_miss_baseline"
LIT_N = "\n"


@dataclass(frozen=True)
class FeedbackCapture:
    name: str
    case: str
    enabled: bool
    mode: str = LIT_HYBRID
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
        return (953, 713) if self.extent == LIT_NPOT else (960, 720)


PAIR_CASES = ((LIT_OFFSCREEN, LIT_OFFSCREEN, {}), (LIT_FLOOR, LIT_FLOOR, {}), ("provisional", LIT_ONSCREEN, {LIT_HARDWARE_BUDGET: 64}), ("zero_budget", LIT_FLOOR, {LIT_HARDWARE_BUDGET: 0}), ("tight_budget", LIT_FLOOR, {LIT_HARDWARE_BUDGET: 64}),
    (LIT_OPAQUE_GLASS, LIT_OPAQUE_GLASS, {}), (LIT_NPOT, LIT_FLOOR, {"extent": LIT_NPOT}), ("rough_guard", LIT_ROUGH_FURNACE, {"roughness": 1.0}), ("screen_guard", LIT_FLOOR, {"mode": LIT_SCREEN}), ("long_miss", LIT_FEEDBACK_LONG_MISS, {"screen_steps": 16}),)
CAPTURES = tuple(FeedbackCapture(name + (LIT_FEEDBACK if enabled else LIT_BASELINE), case, enabled, **options) for name, case, options in PAIR_CASES for enabled in (False, True)) + (
    FeedbackCapture(LIT_LONG_MISS_GEOMETRY, LIT_FEEDBACK_LONG_MISS, False, mode=LIT_SCREEN, screen_steps=96), FeedbackCapture(LIT_MUTATION_INITIAL, "feedback_boundary", True), FeedbackCapture(LIT_MUTATION_CHANGED, LIT_FEEDBACK_MUTATION, True, mutation=True),
    FeedbackCapture(LIT_MUTATION_FRESH, LIT_FEEDBACK_MUTATION, True, mutation=True, final_state=True),)
DIAGNOSTICS_OFF_CAPTURES = tuple(FeedbackCapture(LIT_DIAGNOSTICS_OFF + case + (LIT_FEEDBACK if enabled else LIT_BASELINE), case, enabled, diagnostics=False) for case in (LIT_OFFSCREEN, LIT_FLOOR, LIT_OPAQUE_GLASS) for enabled in (False, True))


def spec_environment(spec):
    environment = capture_environment(spec.case, spec.mode, spec.hardware_budget)
    environment.update({"NWB_REFLECTION_SMOKE_FEEDBACK": str(int(spec.enabled)), "NWB_REFLECTION_SMOKE_SCREEN_STEPS": str(spec.screen_steps), "NWB_REFLECTION_SMOKE_HISTORY_SAMPLES": str(spec.accepted_frames),
        "NWB_REFLECTION_SMOKE_ROUGHNESS": str(spec.roughness), "NWB_REFLECTION_SMOKE_TEMPORAL": "0", "NWB_REFLECTION_SMOKE_SPATIAL": "0", "NWB_REFLECTION_SMOKE_DIAGNOSTICS": str(int(spec.diagnostics)), "NWB_REFLECTION_SMOKE_SEED": str(spec.seed),
        "NWB_REFLECTION_SMOKE_FINAL_STATE": str(int(spec.final_state)), "NWB_REFLECTION_SMOKE_POST_RESET_SAMPLES": "1", "NWB_REFLECTION_SMOKE_FEEDBACK_CAPTURE": str(int(spec.diagnostics)), "NWB_REFLECTION_SMOKE_EXTENT": spec.extent,
        "NWB_REFLECTION_SMOKE_OPTICAL_QUERIES": "16"})
    return environment


def capture(args, spec):
    output = args.output_directory / (spec.name + LIT_BMP)
    command = [sys.executable, str(Path(__file__).with_name("window_capture_smoke.py")), "--executable", str(args.executable), "--working-directory", str(args.working_directory),
        "--output", str(output), "--application-capture", "--application-capture-frame-count", "1" if spec.diagnostics else str(max(64, spec.accepted_frames) + 1), "--timeout", str(args.timeout), "--log-output", str(output.with_suffix(LIT_LOG)),
        LIT_EXPECT_LOG_MESSAGE, f"ReflectionSmokeProject: case {spec.case} created", LIT_EXPECT_LOG_MESSAGE, f"ReflectionSmokeProject: reflection mode {spec.mode}", LIT_EXPECT_LOG_MESSAGE, LIT_REFLECTIONSMOKEPROJECT_SCREEN_FEEDBACK.format(v=int(spec.enabled)),
        LIT_EXPECT_LOG_MESSAGE, LIT_REFLECTIONSMOKEPROJECT_SCREEN_STEPS_SP.format(v=spec.screen_steps), LIT_EXPECT_LOG_MESSAGE, "ReflectionSmokeProject: shutdown",
        LIT_EXPECT_LOG_MESSAGE, "Reflection resolve: " + ("hardware" if spec.mode == LIT_HYBRID and spec.hardware_budget else "screen-space"), LIT_EXPECT_LOG_MESSAGE if args.require_hardware else "--skip-log-message",
        "ReflectionSmokeProject: hardware available" if args.require_hardware else "ReflectionSmokeProject: hardware unavailable"]
    if spec.diagnostics:
        command += [LIT_EXPECT_LOG_MESSAGE, LIT_REFLECTIONSMOKEFEEDBACK]
    else:
        for prefix in (LIT_REFLECTIONSMOKESTATISTICS, LIT_REFLECTIONSMOKEHISTORY, LIT_REFLECTIONSMOKEFEEDBACK, LIT_REFLECTIONSMOKEOPTICS):
            command += ["--reject-log-message", prefix]
    if args.logserver_executable:
        command += ["--logserver-executable", str(args.logserver_executable)]
    else:
        command.append("--no-logserver")
    command.extend("--application-arg=" + argument for argument in args.application_arg)
    print(f"Capturing {spec.name}: feedback={spec.enabled}, steps={spec.screen_steps}, budget={spec.hardware_budget}...", flush=True)
    result = subprocess.run(command, env=spec_environment(spec), check=False, timeout=args.timeout + LIT_CAPTURE_TIMEOUT_MARGIN_SECONDS)
    if result.returncode == SKIP_EXIT_CODE:
        return None
    if result.returncode:
        raise SmokeFailure(f"{spec.name} capture failed: {result.returncode}")
    frame = read_bmp_24_rows(output)
    ReflectionImageAnalysis.validate_frame(frame)
    if frame[:2] != spec.dimensions:
        raise SmokeFailure("feedback readback dimensions do not match the selected native/NPOT preset")
    text = output.with_suffix(LIT_LOG).read_text(encoding=LIT_UTF_8)
    if not spec.diagnostics:
        return ReflectionFeedbackEvidence.validate_diagnostics_off(text, spec)
    statistics = ReflectionStatistics.parse_statistics(text)
    ReflectionStatistics.validate_statistics(statistics, spec.case, spec.mode, spec.hardware_budget, allow_zero_samples=spec.roughness > 0, extent=spec.dimensions)
    return ReflectionFeedbackEvidence.validate_feedback(text, statistics, spec)


def analyze_suite(directory, evidence, specs=CAPTURES):
    available = {spec.name: spec for spec in specs}
    def frame(name):
        return read_bmp_24_rows(directory / (name + LIT_BMP))
    metrics = {}
    for case in (LIT_OFFSCREEN, LIT_FLOOR, LIT_OPAQUE_GLASS):
        first, second = LIT_DIAGNOSTICS_OFF + case + LIT_BASELINE, LIT_DIAGNOSTICS_OFF + case + LIT_FEEDBACK
        if first in available and second in available:
            metrics[LIT_DIAGNOSTICS_OFF + case] = {LIT_IMAGES: ReflectionFeedbackImageAnalysis.compare_static_images(frame(first), frame(second), case), LIT_DIAGNOSTICS: False, "work_counters": None,
                LIT_SCOPE: "Frozen smooth image equivalence after completed framebuffer readbacks; no feedback counter evidence."}
    for prefix, case, _ in PAIR_CASES:
        first, second = prefix + LIT_BASELINE, prefix + LIT_FEEDBACK
        if first not in available or second not in available:
            continue
        metrics[prefix] = {LIT_IMAGES: ReflectionFeedbackImageAnalysis.compare_static_images(frame(first), frame(second), case), "completed_work": ReflectionFeedbackEvidence.compare_work(evidence[first], evidence[second], available[second])}
    if LIT_LONG_MISS_BASELINE in available:
        original = evidence[LIT_LONG_MISS_BASELINE]
        stats = {(sample[LIT_SEQUENCE], sample[LIT_GENERATION]): sample for sample in original[LIT_STATISTICS]}
        observations = [TraversalEvidence(stats[(sample[LIT_SEQUENCE], sample[LIT_GENERATION])][LIT_SCREEN_ATTEMPTS], sample[LIT_SCREEN_ITERATIONS], sample[LIT_SCREEN_LIMIT_MISSES], available[LIT_LONG_MISS_BASELINE].screen_steps)
            for sample in original[LIT_STABLE_FEEDBACK]]
        metrics["costly_miss_qualification"] = ReflectionFeedbackEvidence.qualify_costly_misses(observations)
    if LIT_LONG_MISS_GEOMETRY in available:
        metrics[LIT_LONG_MISS_GEOMETRY] = ReflectionFeedbackImageAnalysis.analyze_long_miss_panels(frame(LIT_LONG_MISS_GEOMETRY))
    if all(name in available for name in (LIT_MUTATION_INITIAL, LIT_MUTATION_CHANGED, LIT_MUTATION_FRESH)):
        metrics["mutation"] = ReflectionFeedbackImageAnalysis.compare_mutation_images(frame(LIT_MUTATION_INITIAL), frame(LIT_MUTATION_CHANGED), frame(LIT_MUTATION_FRESH))
    return metrics


def write_report(args, completed, evidence, metrics=None):
    diagnostics_off = bool(completed) and all(not spec.diagnostics for spec in completed)
    stem = "reflection_feedback_diagnostics_off" if diagnostics_off else "reflection_feedback"
    note = "Actual framebuffer readbacks. Diagnostic captures include completed feedback observations for budget guards, returns, probes, source-change invalidation and actual traversal work. The separate diagnostics-off suite checks frozen smooth images after the framebuffer completion fence and contains no feedback counter evidence. Counter reductions do not establish GPU speed; optimized benchmarks remain separate."
    build_identity = {"executable_sha256": file_identity(args.executable)["sha256"], "asset_volumes": authored_volume_hashes(args.working_directory)}
    metadata = {"frame_source": "actual application framebuffer readback", "captures": [asdict(spec) for spec in completed], "build_identity": build_identity, "temporal": False, "spatial_filter": False,
        "fixed_delta_seconds": 0.016666667, "optical_queries": 16, "completed_frame_evidence": evidence, "metrics": metrics, LIT_SCOPE: note,
        "analyzer_source_sha256_lf": hashlib.sha256(Path(__file__).read_text(encoding=LIT_UTF_8).encode(LIT_UTF_8)).hexdigest(),
        "analyzer_dependencies_sha256_lf": {
            "framebuffer_png_encoder.py": hashlib.sha256(Path(__file__).with_name("framebuffer_png_encoder.py").read_text(encoding=LIT_UTF_8).encode(LIT_UTF_8)).hexdigest(),
            "reflection_feedback_image_analysis.py": hashlib.sha256(Path(feedback_image_analysis.__file__).read_text(encoding=LIT_UTF_8).encode(LIT_UTF_8)).hexdigest(),
            "reflection_roughness_reference.py": hashlib.sha256(Path(__file__).with_name("reflection_roughness_reference.py").read_text(encoding=LIT_UTF_8).encode(LIT_UTF_8)).hexdigest(),
            "reflection_roughness_oracle.py": hashlib.sha256(Path(__file__).with_name("reflection_roughness_oracle.py").read_text(encoding=LIT_UTF_8).encode(LIT_UTF_8)).hexdigest(),
            "reflection_roughness_image_evidence.py": hashlib.sha256(Path(__file__).with_name("reflection_roughness_image_evidence.py").read_text(encoding=LIT_UTF_8).encode(LIT_UTF_8)).hexdigest(),
            "reflection_feedback_evidence.py": hashlib.sha256(Path(feedback_evidence.__file__).read_text(encoding=LIT_UTF_8).encode(LIT_UTF_8)).hexdigest(),
        }}
    (args.output_directory / (stem + "_manifest.json")).write_text(json.dumps(metadata, indent=2) + LIT_N, encoding=LIT_UTF_8)
    cards = []
    for spec in completed:
        path = args.output_directory / (spec.name + LIT_BMP)
        png = FramebufferPngEncoder.encode(read_bmp_24_rows(path))
        path.with_suffix(".png").write_bytes(png)
        cards.append(f'<article><h2>{html.escape(spec.name)}</h2><a href="{spec.name}.bmp">Raw BMP</a> / ' f'<a href="{spec.name}.log">Completed-frame log</a><img alt="Actual {html.escape(spec.name)} capture" '
            f'src="data:image/png;base64,{base64.b64encode(png).decode("ascii")}"></article>')
    document = f'<!doctype html><html lang="en"><meta charset="{LIT_UTF_8}"><title>Reflection feedback evidence</title>'
    document += '<style>body{font:16px system-ui;background:#141922;color:#eee;margin:28px}main{display:grid;grid-template-columns:repeat(auto-fit,minmax(440px,1fr));gap:20px}article{background:#202937;padding:16px}img{width:100%}a{color:#8cf}pre{white-space:pre-wrap}</style>'
    document += '<h1>Reflection feedback — actual captures</h1><p>' + html.escape(note) + '</p><main>' + ''.join(cards) + '</main><pre>'
    document += html.escape(json.dumps(metrics, indent=2) if metrics else 'Captured evidence; assertions pending.') + '</pre></html>'
    (args.output_directory / (stem + ".html")).write_text(document, encoding=LIT_UTF_8)


def run_suite(args):
    completed, evidence = [], {}
    try:
        universe = DIAGNOSTICS_OFF_CAPTURES if args.suite == "feedback-diagnostics-off" else CAPTURES
        selected = args.feedback_cases
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
        print("PASS: " + label + LIT_N + json.dumps(metrics, indent=2), flush=True)
        return 0
    except (SmokeFailure, OSError, subprocess.TimeoutExpired) as exc:
        print("FAIL: " + str(exc), file=sys.stderr)
        return 1
