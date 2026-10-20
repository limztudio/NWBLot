#!/usr/bin/env python3
"""Compare live skinned HW photons across ordinary/CSG transitions and lagged lighting."""

import argparse
from array import array
import hashlib
import json
import math
import os
from pathlib import Path
import re
import statistics
import subprocess
import sys
import tempfile

from window_capture_smoke import (SKIP_EXIT_CODE, STRICT_LOG_FAILURE_MESSAGES, SmokeFailure, read_bmp_24_rows, validate_expected_log_text)

LIT_LAGGED = "lagged"
LIT_NO_PHOTONS = "no_photons"
LIT_SKINNEDCAUSTICSMOKEPROJECT_SHUTDOWN = "SkinnedCausticSmokeProject: shutdown"
LIT_VARIANT = "variant"
LIT_RECEIVER = "receiver"
LIT_MEAN_ABSOLUTE_CHANNEL_ERROR = "mean_absolute_channel_error"
LIT_P95_CHANNEL_ERROR = "p95_channel_error"
LIT_LARGE_ERROR_FRACTION = "large_error_fraction"
LIT_CHANGED_PIXELS = "changed_pixels"
LIT_CURRENT_VS_LAGGED = "current_vs_lagged"
LIT_REPEATED_STATES = "repeated_states"
LIT_CSG_SIGNAL = "csg_signal"
LIT_MOTION_SIGNAL = "motion_signal"
LIT_PHOTON_SIGNAL = "photon_signal"
LIT_SAMPLE = "sample"
LIT_REGION = "region"
LIT_CURRENT = "current"
LIT_GROUND = "ground"
LIT_NWB_GPU_TIMING_FILE = "NWB_GPU_TIMING_FILE"
LIT_EXECUTABLE = "--executable"
LIT_WORKING_DIRECTORY = "--working-directory"
LIT_TIMEOUT = "--timeout"
LIT_RUNTIME_LOG = "runtime.log"
LIT_EXPECT_LOG_MESSAGE = "--expect-log-message"
LIT_SKIP_LOG_MESSAGE = "--skip-log-message"
LIT_NO_GPU_VALIDATION = "--no-gpu-validation"
LIT_LOGSERVER_EXECUTABLE = "--logserver-executable"
LIT_UTF_8 = "utf-8"


SAMPLE_COUNT = 8
PHASE_FRAMES = 64
CAPTURE_INTERVAL = 32
EXPECTED_EXTENT = (1280, 900)
VARIANTS = ("current", "lagged", "no_photons")
REGIONS = {"receiver": (320, 100, 960, 735), "ground": (260, 640, 1020, 870)}
MATCH_LIMITS = {"mean_absolute_channel_error": 5.0, "p95_channel_error": 12, "large_error_fraction": .03, "large_error_channel_threshold": 24}
SIGNAL_LIMITS = {"csg_changed_pixels": 256, "motion_changed_pixels": 64, "photon_changed_pixels": 32, "photon_mean_channel_error": .03, "channel_threshold": 3}


def sample_path(directory, sample):
    return directory / ("series.bmp" if sample + 1 == SAMPLE_COUNT else f"series.bmp.{sample}.bmp")


def validate_series_log(log, variant):
    lagged, photons = variant == LIT_LAGGED, variant != LIT_NO_PHOTONS
    validate_expected_log_text(log, ("SkinnedCausticTransition: complete samples=8", LIT_SKINNEDCAUSTICSMOKEPROJECT_SHUTDOWN), STRICT_LOG_FAILURE_MESSAGES)
    setup = re.findall(r"SkinnedCausticTransition: setup lagged=([01]) caustics=([01]) minimum_frame=(\d+) warmup_seconds=(\d+) phase_frames=(\d+) samples=(\d+) model=(\w+)", log)
    if setup != [(str(int(lagged)), str(int(photons)), "360", "30", "64", "8", "closed_prism")]:
        raise SmokeFailure("transition setup does not match the requested public controls")
    starts = re.findall(r"SkinnedCausticTransition: warmup started graphics frame (\d+)", log)
    completions = re.findall(r"SkinnedCausticTransition: warmup complete elapsed_seconds=([0-9.]+) first_frame=(\d+)", log)
    if len(starts) != 1 or len(completions) != 1:
        raise SmokeFailure("transition capture requires one completed live warmup")
    elapsed, first = float(completions[0][0]), int(completions[0][1])
    if not math.isfinite(elapsed) or elapsed < 30 or int(starts[0]) < 360 or first <= int(starts[0]):
        raise SmokeFailure("transition captures must follow 360 frames and 30 steady-clock warmup seconds")
    phases = [(int(phase), int(csg), int(frame)) for phase, csg, frame in re.findall(r"SkinnedCausticTransition: phase (\d+) csg=([01]) graphics frame (\d+)", log)]
    if phases != [(phase, phase % 2, first + phase * PHASE_FRAMES) for phase in range(4)]:
        raise SmokeFailure("ordinary/CSG source-frame transitions differ from the deterministic schedule")
    samples = [(int(sample), int(frame), int(phase), int(pose)) for sample, frame, phase, pose in re.findall(r"SkinnedCausticTransition: sample (\d+) graphics frame (\d+) phase (\d+) pose_frame (\d+)", log)]
    expected = [(sample, first + (sample + 1) * CAPTURE_INTERVAL - 1, sample // 2, ((sample + 1) * CAPTURE_INTERVAL - 1) % PHASE_FRAMES) for sample in range(SAMPLE_COUNT)]
    if samples != expected or log.count("FramebufferCapture: capture ready") != SAMPLE_COUNT:
        raise SmokeFailure("transition readbacks do not identify every matched source-frame pose")
    gpu = [(int(phase), int(photon), int(skinning)) for phase, photon, skinning in re.findall(r"SkinnedCausticTransition: completed GPU phase (\d+) photon_samples=(\d+) skinning_samples=(\d+)", log)]
    if [phase for phase, _, _ in gpu] != list(range(4)):
        raise SmokeFailure("transition run did not publish all completed GPU phase counts")
    if any(skinning <= 0 or (photon <= 0 if photons else photon != 0) for _, photon, skinning in gpu):
        raise SmokeFailure("each phase requires completed skinning and the requested photon admission")
    routes = [(int(phase), int(frame), int(queue), int(graphics), int(generation), int(off_graphics)) for phase, frame, queue, graphics, generation, off_graphics in re.findall(
        r"SkinnedCausticTransition: accepted photon route phase (\d+) source_frame=(\d+) queue=(\d+) graphics_queue=(\d+) generation=(\d+) off_graphics=([01])", log)]
    if photons:
        if [route[0] for route in routes] != list(range(4)):
            raise SmokeFailure("each photon phase requires actual accepted native packet queue telemetry")
        for phase, frame, queue, graphics, generation, off_graphics in routes:
            if not (first + phase * PHASE_FRAMES <= frame < first + (phase + 1) * PHASE_FRAMES):
                raise SmokeFailure("accepted photon queue telemetry belongs to an unrelated source-frame phase")
            if generation <= 0 or queue >= 65535 or graphics >= 65535 or off_graphics != int(queue != graphics):
                raise SmokeFailure("accepted photon route has invalid or inconsistent physical queue identities")
            if lagged and not off_graphics:
                raise SmokeFailure("a primary Graphics photon reader cannot qualify the asynchronous posed-buffer hazard")
    elif routes:
        raise SmokeFailure("photons-disabled control must not publish a photon packet route")
    hw = "RendererSystem: dispatched hardware caustic producer"
    if photons and hw not in log or not photons and "caustic producer (" in log:
        raise SmokeFailure("transition fixture did not dispatch the requested hardware photon route")
    if lagged:
        validate_expected_log_text(log, ("RendererSystem: frame-lagged async lighting bootstrap accepted", "RendererSystem: frame-lagged async lighting active history accepted"), ())
        if "RendererSystem: frame-lagged async lighting Graphics queue route accepted" in log:
            raise SmokeFailure("Graphics fallback cannot qualify the dedicated Compute reader lifetime")
    return {LIT_VARIANT: variant, "first_frame": first, "warmup_elapsed_seconds": elapsed, "source_frames": [sample[1] for sample in samples], "gpu_phases": gpu, "accepted_photon_routes": routes}


def frame_regions(frame):
    width, height, rows = frame
    if (width, height) != EXPECTED_EXTENT or len(rows) != height or any(len(row) != width for row in rows):
        raise SmokeFailure("transition comparisons require the actual 1280x900 framebuffer")
    regions = {}
    for name, (left, top, right, bottom) in REGIONS.items():
        values = array("B")
        for y in range(top, bottom, 2):
            for x in range(left, right, 2):
                values.extend(rows[y][x])
        regions[name] = values
    if max(regions[LIT_RECEIVER]) - min(regions[LIT_RECEIVER]) < 12:
        raise SmokeFailure("transition receiver capture is blank or has no visible geometry contrast")
    return regions


def difference(first, second):
    if not first or len(first) != len(second) or len(first) % 3:
        raise SmokeFailure("transition image regions must contain equal nonempty RGB samples")
    errors = [abs(left - right) for left, right in zip(first, second)]
    ordered = sorted(errors)
    changed = sum(max(errors[index:index + 3]) > SIGNAL_LIMITS["channel_threshold"] for index in range(0, len(errors), 3))
    return {LIT_MEAN_ABSOLUTE_CHANNEL_ERROR: statistics.mean(errors), LIT_P95_CHANNEL_ERROR: ordered[round((len(ordered) - 1) * .95)], LIT_LARGE_ERROR_FRACTION: sum(error > MATCH_LIMITS["large_error_channel_threshold"] for error in errors) / len(errors),
        LIT_CHANGED_PIXELS: changed, "pixels": len(errors) // 3}


def require_match(first, second, label):
    measured = difference(first, second)
    if any(measured[key] > MATCH_LIMITS[key] for key in (LIT_MEAN_ABSOLUTE_CHANNEL_ERROR, LIT_P95_CHANNEL_ERROR, LIT_LARGE_ERROR_FRACTION)):
        raise SmokeFailure(f"{label} has stale or unstable posed geometry/light: {measured}")
    return measured


def require_signal(first, second, minimum_pixels, label, minimum_mean=0):
    measured = difference(first, second)
    if measured[LIT_CHANGED_PIXELS] < minimum_pixels or measured[LIT_MEAN_ABSOLUTE_CHANNEL_ERROR] < minimum_mean:
        raise SmokeFailure(f"{label} has no qualifying visible signal: {measured}")
    return measured


def compare_series(series):
    if set(series) != set(VARIANTS) or any(len(samples) != SAMPLE_COUNT for samples in series.values()):
        raise SmokeFailure("transition comparison requires all three complete eight-frame series")
    comparisons = {LIT_CURRENT_VS_LAGGED: [], LIT_REPEATED_STATES: [], LIT_CSG_SIGNAL: [], LIT_MOTION_SIGNAL: [], LIT_PHOTON_SIGNAL: []}
    for sample in range(SAMPLE_COUNT):
        for region in REGIONS:
            comparisons[LIT_CURRENT_VS_LAGGED].append({LIT_SAMPLE: sample, LIT_REGION: region, **require_match(series[LIT_CURRENT][sample][region], series[LIT_LAGGED][sample][region], f"current/lagged sample {sample} {region}")})
        # Only ground pixels qualify photon contribution; body transmission or CSG silhouettes cannot satisfy it.
        if sample < 2:
            comparisons[LIT_PHOTON_SIGNAL].append({LIT_SAMPLE: sample, **require_signal(series[LIT_CURRENT][sample][LIT_GROUND], series[LIT_NO_PHOTONS][sample][LIT_GROUND], SIGNAL_LIMITS["photon_changed_pixels"], "hardware photons on ordinary ground",
                    SIGNAL_LIMITS["photon_mean_channel_error"])})
    for variant in (LIT_CURRENT, LIT_LAGGED):
        for sample in range(4):
            for region in REGIONS:
                comparisons[LIT_REPEATED_STATES].append({LIT_VARIANT: variant, LIT_SAMPLE: sample, LIT_REGION: region, **require_match(series[variant][sample][region], series[variant][sample + 4][region], f"{variant} repeated ordinary/CSG pose {sample} {region}")})
        for sample in range(2):
            comparisons[LIT_CSG_SIGNAL].append({LIT_VARIANT: variant, LIT_SAMPLE: sample, **require_signal(series[variant][sample][LIT_RECEIVER], series[variant][sample + 2][LIT_RECEIVER],
                    SIGNAL_LIMITS["csg_changed_pixels"], "active cutter on the live skinned receiver")})
        comparisons[LIT_MOTION_SIGNAL].append({LIT_VARIANT: variant, **require_signal(series[variant][0][LIT_RECEIVER], series[variant][1][LIT_RECEIVER], SIGNAL_LIMITS["motion_changed_pixels"], "live joint pose motion")})
    return comparisons


def capture(args, variant):
    directory = args.output_directory / variant
    directory.mkdir(parents=True, exist_ok=True)
    if any(directory.iterdir()):
        raise SmokeFailure("transition acquisition requires fresh per-variant output directories")
    environment = os.environ.copy()
    for key in tuple(environment):
        if key.startswith(("NWB_SKINNED_CAUSTIC_CSG_", "NWB_RENDERER_BASELINE_", "NWB_GI_SMOKE_", "NWB_CAUSTIC_SMOKE_")):
            environment.pop(key)
    environment.pop(LIT_NWB_GPU_TIMING_FILE, None)
    environment.update({"NWB_SKINNED_CAUSTIC_CSG_TRANSITION": "1", "NWB_SKINNED_CAUSTIC_CSG_LAGGED": str(int(variant == LIT_LAGGED)), "NWB_SKINNED_CAUSTIC_CSG_NO_PHOTONS": str(int(variant == LIT_NO_PHOTONS)),
        LIT_NWB_GPU_TIMING_FILE: str((directory / "gpu_timing.txt").resolve()), "NWB_SOFTWARE_SHADOW_DIRECTIONAL_RESOLUTION": "128"})
    command = [sys.executable, str(Path(__file__).with_name("window_capture_smoke.py")), LIT_EXECUTABLE, str(args.executable), LIT_WORKING_DIRECTORY, str(args.working_directory),
        "--output", str(sample_path(directory, SAMPLE_COUNT - 1)), "--application-capture", "--application-capture-frame-count", "1", LIT_TIMEOUT, str(args.timeout),
        "--log-output", str(directory / LIT_RUNTIME_LOG), LIT_EXPECT_LOG_MESSAGE, "SkinnedCausticTransition: complete samples=8", LIT_EXPECT_LOG_MESSAGE, LIT_SKINNEDCAUSTICSMOKEPROJECT_SHUTDOWN,
        LIT_SKIP_LOG_MESSAGE, "natural software-only shadow route selected because RayQuery-capable hardware is unavailable", "--gpu-validation" if args.gpu_validation else LIT_NO_GPU_VALIDATION]
    if variant == LIT_LAGGED:
        command += [LIT_SKIP_LOG_MESSAGE, "RendererSystem: frame-lagged async lighting Graphics queue route accepted (no dedicated Compute queue",
            LIT_SKIP_LOG_MESSAGE, "SkinnedCausticTransition: photon reader remains on primary Graphics; asynchronous hazard route unavailable"]
    if args.logserver_executable:
        command += [LIT_LOGSERVER_EXECUTABLE, str(args.logserver_executable)]
    else:
        command.append("--no-logserver")
    print(f"Capturing skinned ordinary/CSG transitions: {variant}", flush=True)
    result = subprocess.run(command, env=environment, check=False, timeout=args.timeout + 45)
    if result.returncode not in (0, SKIP_EXIT_CODE):
        raise SmokeFailure(f"{variant} transition acquisition failed with exit {result.returncode}")
    return result.returncode


def analyze(directory):
    series, runs = {}, []
    for variant in VARIANTS:
        root = directory / variant
        runs.append(validate_series_log((root / LIT_RUNTIME_LOG).read_text(encoding=LIT_UTF_8), variant))
        series[variant] = []
        files = []
        for sample in range(SAMPLE_COUNT):
            path = sample_path(root, sample)
            if path.with_name(path.name + ".partial").exists():
                raise SmokeFailure("transition acquisition left an incomplete framebuffer")
            series[variant].append(frame_regions(read_bmp_24_rows(path)))
            files.append({"path": str(path.resolve()), "sha256": hashlib.sha256(path.read_bytes()).hexdigest()})
        runs[-1]["files"] = files
    return {"runs": runs, "comparisons": compare_series(series), "match_limits": MATCH_LIMITS, "signal_limits": SIGNAL_LIMITS, "regions": REGIONS,
        "contract": "Every frame writes deterministic joint poses. Accepted readbacks are polled without waitForIdle. Four 64-frame phases alternate ordinary/CSG; samples at phase frames 31/63 recur at the same poses. Dedicated Compute lagged lighting must match current lighting; accepted native photon packet telemetry must identify a physical queue distinct from primary Graphics in every lagged phase, and completed skinning/HW photons must cover every admitted phase.",
        "limitations": ["Visual and validation regression exercises the asynchronous reader hazard; it does not prove every possible GPU interleaving.",
            "The 30-second warmup precedes live transitions; this is a posed-geometry/caustic lifetime test, not a settled post-transition GI quality qualification."]}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(LIT_EXECUTABLE, type=Path)
    parser.add_argument(LIT_WORKING_DIRECTORY, type=Path)
    parser.add_argument(LIT_LOGSERVER_EXECUTABLE, type=Path)
    output = parser.add_mutually_exclusive_group(required=True)
    output.add_argument("--output-directory", type=Path, help="Use the exact evidence directory with the existing freshness checks.")
    output.add_argument("--output-root", type=Path, help="Preserve prior evidence and create a unique child directory for this run.")
    parser.add_argument(LIT_TIMEOUT, type=float, default=180)
    parser.add_argument("--analysis-only", action="store_true")
    parser.add_argument(LIT_NO_GPU_VALIDATION, dest="gpu_validation", action="store_false", default=True)
    args = parser.parse_args(argv)
    if args.analysis_only and args.output_root is not None:
        parser.error("--analysis-only requires --output-directory with existing captured evidence")
    if not args.analysis_only and (not args.executable or not args.working_directory):
        parser.error("acquisition requires an executable and working directory")
    if not math.isfinite(args.timeout) or args.timeout <= 0:
        parser.error("timeout must be positive and finite")
    try:
        if args.output_root is not None:
            root = args.output_root.resolve()
            root.mkdir(parents=True, exist_ok=True)
            args.output_directory = Path(tempfile.mkdtemp(prefix="skinned_caustic_transition_", dir=root))
            print(f"Skinned transition capture evidence: {args.output_directory}", flush=True)
        args.output_directory.mkdir(parents=True, exist_ok=True)
        if not args.analysis_only:
            for variant in VARIANTS:
                if capture(args, variant) == SKIP_EXIT_CODE:
                    return SKIP_EXIT_CODE
        result = analyze(args.output_directory)
        (args.output_directory / "manifest.json").write_text(json.dumps(result, indent=2) + "\n", encoding=LIT_UTF_8)
        print(json.dumps({"variants": VARIANTS, "samples_per_variant": SAMPLE_COUNT, "status": "passed"}))
        return 0
    except (SmokeFailure, OSError, ValueError, subprocess.SubprocessError) as error:
        print("FAIL: " + str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
