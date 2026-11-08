#!/usr/bin/env python3
"""Measure a CSG frame sequence from one process with reproducible source-frame poses."""

import argparse
import json
import os
from pathlib import Path
import subprocess
import sys

from csg_gi_temporal_analysis import CsgTemporalAnalysis
from csg_gi_temporal_gates import CsgTemporalGates
from window_capture_smoke import SKIP_EXIT_CODE, SmokeFailure
from csg_gi_temporal_contract import (
    CAP_STATE_REQUIRED_PROBES,
    EVENT_DATA_FLAGS_MARKER,
    EVENT_ORDER_MARKER,
    FRAME_INTERVAL,
    GEOMETRY_LIMITS,
    GEOMETRY_PROBES,
    LIT_ANIMATED,
    LIT_CAPTURE_TIMEOUT_MARGIN_SECONDS,
    LIT_CAP_STATE,
    LIT_CSGVISIBLESMOKEPROJECT_SHUTDOWN,
    LIT_CSGVISIBLESMOKEPROJECT_TEMPORAL_WARMUP,
    LIT_DIRECT,
    LIT_EVENT_DATA,
    LIT_EVENT_ORDER,
    LIT_EVENT_STATE,
    LIT_EXECUTABLE,
    LIT_EXPECT_LOG_MESSAGE,
    LIT_FULL,
    LIT_GPU_VALIDATION,
    LIT_LAGGED_LIGHTING,
    LIT_LOADER_HARDWARE_RAY_TRACING_DISABLED_B,
    LIT_LOGSERVER_EXECUTABLE,
    LIT_MANIFEST_JSON,
    LIT_MAXIMUM_FRAME_INTERVAL,
    LIT_MINIMUM_FRAME_INTERVAL,
    LIT_MOTION,
    LIT_NATURAL,
    LIT_NORMAL,
    LIT_NWB_CSG_GI_TEMPORAL,
    LIT_NWB_GPU_TIMING_FILE,
    LIT_OVERLAPPING,
    LIT_PARTIAL_SUFFIX,
    LIT_PIXEL,
    LIT_POSITION,
    LIT_REGIONS,
    LIT_REJECT_LOG_MESSAGE,
    LIT_RENDERERSYSTEM_DISPATCHED_SURFEL_GI_RE,
    LIT_RENDERERSYSTEM_FRAME_LAGGED_ASYNC_LIGH,
    LIT_ROUTE,
    LIT_RUNTIME_LOG,
    LIT_SAMPLES,
    LIT_SOFTWARE,
    LIT_SPAN_STATE,
    LIT_STATIC,
    LIT_STORE_TRUE,
    LIT_TEMPORAL_SERIES_COMPLETE,
    LIT_TEMPORAL_WARMUP_COMPLETE,
    LIT_TIMEOUT,
    LIT_UTF_8,
    LIT_VIEW,
    LIT_WORKING_DIRECTORY,
    OVERLAPPING_MARKER,
    QUALITY_LIMITS,
    SAMPLE_COUNT,
    SPAN_EXPECTATIONS,
    SPAN_STATE_MARKER,
    STATE_CHANNELS,
)


def capture_series(args):
    args.output_directory.mkdir(parents=True, exist_ok=True)
    output = CsgTemporalAnalysis.sample_path(args.output_directory, SAMPLE_COUNT - 1)
    if args.overwrite:
        for sample in range(SAMPLE_COUNT):
            path = CsgTemporalAnalysis.sample_path(args.output_directory, sample)
            path.unlink(missing_ok=True)
            path.with_name(path.name + LIT_PARTIAL_SUFFIX).unlink(missing_ok=True)
        for name in (LIT_MANIFEST_JSON, LIT_RUNTIME_LOG):
            (args.output_directory / name).unlink(missing_ok=True)
    if any(CsgTemporalAnalysis.sample_path(args.output_directory, index).exists() for index in range(SAMPLE_COUNT)):
        raise SmokeFailure("temporal acquisition requires an empty capture directory")
    environment = os.environ.copy()
    for name in tuple(environment):
        if name.startswith(("NWB_GI_SMOKE_", "NWB_RENDERER_BASELINE_", LIT_NWB_CSG_GI_TEMPORAL)):
            environment.pop(name)
    environment.pop(LIT_NWB_GPU_TIMING_FILE, None)
    if args.gpu_timing_output:
        args.gpu_timing_output.parent.mkdir(parents=True, exist_ok=True)
        if args.overwrite:
            args.gpu_timing_output.unlink(missing_ok=True)
        elif args.gpu_timing_output.exists():
            raise SmokeFailure("temporal acquisition requires a fresh GPU timing file")
        environment[LIT_NWB_GPU_TIMING_FILE] = str(args.gpu_timing_output.resolve())
    environment.update({LIT_NWB_CSG_GI_TEMPORAL: "1", "NWB_CSG_GI_TEMPORAL_VIEW": args.view, "NWB_CSG_GI_TEMPORAL_ANIMATED": str(int(args.motion == LIT_ANIMATED)), "NWB_CSG_GI_TEMPORAL_INTERVAL": str(args.frame_interval),
        "NWB_CSG_GI_TEMPORAL_LAGGED": str(int(args.lagged_lighting)), "NWB_CSG_GI_TEMPORAL_OVERLAPPING": str(int(args.overlapping))})
    command = [sys.executable, str(Path(__file__).with_name("window_capture_smoke.py")), LIT_EXECUTABLE, str(args.executable), LIT_WORKING_DIRECTORY, str(args.working_directory),
        "--output", str(output), "--application-capture", "--application-capture-frame-count", "1", LIT_TIMEOUT, str(args.timeout), "--log-output", str(args.output_directory / LIT_RUNTIME_LOG),
        LIT_EXPECT_LOG_MESSAGE, f"CsgVisibleSmokeProject: temporal scene animated={int(args.motion == LIT_ANIMATED)} view={args.view} minimum_frame=360 warmup_seconds=30 interval={args.frame_interval} samples=32",
        LIT_EXPECT_LOG_MESSAGE, LIT_CSGVISIBLESMOKEPROJECT_TEMPORAL_WARMUP, LIT_EXPECT_LOG_MESSAGE, LIT_TEMPORAL_WARMUP_COMPLETE,
        LIT_EXPECT_LOG_MESSAGE, LIT_TEMPORAL_SERIES_COMPLETE, LIT_EXPECT_LOG_MESSAGE, LIT_CSGVISIBLESMOKEPROJECT_SHUTDOWN, LIT_EXPECT_LOG_MESSAGE, LIT_RENDERERSYSTEM_DISPATCHED_SURFEL_GI_RE,
        LIT_GPU_VALIDATION if args.gpu_validation else "--no-gpu-validation"]
    if args.route == LIT_SOFTWARE:
        command += ["--application-arg=--disable-hardware-ray-tracing", LIT_EXPECT_LOG_MESSAGE, LIT_LOADER_HARDWARE_RAY_TRACING_DISABLED_B, LIT_REJECT_LOG_MESSAGE, "RendererSystem: created surfel HW trace compute pipeline"]
    if args.lagged_lighting:
        command += [LIT_EXPECT_LOG_MESSAGE, "RendererSystem: frame-lagged async lighting bootstrap accepted", LIT_EXPECT_LOG_MESSAGE, LIT_RENDERERSYSTEM_FRAME_LAGGED_ASYNC_LIGH,
            "--skip-log-message", "RendererSystem: frame-lagged async lighting Graphics queue route accepted (no dedicated Compute queue"]
    if args.view == LIT_EVENT_DATA:
        command += [LIT_EXPECT_LOG_MESSAGE, EVENT_DATA_FLAGS_MARKER]
    elif args.view == LIT_EVENT_ORDER:
        command += [LIT_EXPECT_LOG_MESSAGE, EVENT_ORDER_MARKER]
    elif args.view == LIT_SPAN_STATE:
        command += [LIT_EXPECT_LOG_MESSAGE, SPAN_STATE_MARKER]
    if args.overlapping:
        command += [LIT_EXPECT_LOG_MESSAGE, OVERLAPPING_MARKER]
    else:
        command += [LIT_REJECT_LOG_MESSAGE, OVERLAPPING_MARKER]
    if args.logserver_executable:
        command += [LIT_LOGSERVER_EXECUTABLE, str(args.logserver_executable)]
    else:
        command.append("--no-logserver")
    result = subprocess.run(command, env=environment, check=False, timeout=args.timeout + LIT_CAPTURE_TIMEOUT_MARGIN_SECONDS)
    if result.returncode and result.returncode != SKIP_EXIT_CODE:
        raise SmokeFailure(f"temporal capture failed with exit {result.returncode}")
    return result.returncode


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(LIT_EXECUTABLE, type=Path)
    parser.add_argument(LIT_WORKING_DIRECTORY, type=Path)
    parser.add_argument(LIT_LOGSERVER_EXECUTABLE, type=Path)
    parser.add_argument("--output-directory", type=Path, required=True)
    parser.add_argument("--route", choices=(LIT_NATURAL, LIT_SOFTWARE), default=LIT_NATURAL)
    parser.add_argument("--view", choices=(LIT_FULL, "indirect", LIT_DIRECT, LIT_NORMAL, LIT_POSITION, *STATE_CHANNELS), default=LIT_FULL)
    parser.add_argument("--motion", choices=(LIT_STATIC, LIT_ANIMATED), default=LIT_STATIC)
    parser.add_argument("--frame-interval", type=int, default=FRAME_INTERVAL, help="Use 1 for consecutive frames with serialized readbacks, or 8 for asynchronous longer-term sampling.")
    parser.add_argument("--lagged-lighting", action=LIT_STORE_TRUE)
    parser.add_argument("--overlapping", action=LIT_STORE_TRUE, help="Add a static plane receiver at z+0.125 with the same cutter group.")
    parser.add_argument(LIT_GPU_VALIDATION, action=argparse.BooleanOptionalAction, default=False, help="Request graphics validation; use --no-gpu-validation for a fin application.")
    parser.add_argument("--gpu-timing-output", type=Path)
    parser.add_argument(LIT_TIMEOUT, type=float, default=150.0)
    parser.add_argument("--analysis-only", action=LIT_STORE_TRUE)
    parser.add_argument("--check-quality", action=LIT_STORE_TRUE)
    parser.add_argument("--check-geometry", action=LIT_STORE_TRUE, help="Check physical cap probes in a static normal or position view.")
    parser.add_argument("--check-cap-state", action=LIT_STORE_TRUE, help="Require produced, visible, depth-owning caps at all static regression probes.")
    parser.add_argument("--check-spans", action=LIT_STORE_TRUE, help="Check independent spans for overlapping closed receivers and three controls.")
    parser.add_argument("--overwrite", action=LIT_STORE_TRUE, help="Replace only this sequence's known capture artifacts.")
    args = parser.parse_args()
    if not LIT_MINIMUM_FRAME_INTERVAL <= args.frame_interval <= LIT_MAXIMUM_FRAME_INTERVAL:
        parser.error("--frame-interval must be between 1 and 64")
    if args.check_quality and args.view in (LIT_NORMAL, LIT_POSITION, *STATE_CHANNELS):
        parser.error("diagnostic views do not support the lighting quality gate")
    if args.check_geometry and (args.view not in (LIT_NORMAL, LIT_POSITION) or args.motion != LIT_STATIC):
        parser.error("--check-geometry requires --motion static and --view normal or position")
    if args.check_cap_state and (args.view != LIT_CAP_STATE or args.motion != LIT_STATIC):
        parser.error("--check-cap-state requires --motion static and --view cap_state")
    if args.overlapping and args.motion != LIT_STATIC:
        parser.error("--overlapping requires --motion static")
    if args.check_spans and (args.view != LIT_SPAN_STATE or args.motion != LIT_STATIC or not args.overlapping):
        parser.error("--check-spans requires --motion static, --view span_state, and --overlapping")
    if not args.analysis_only and (not args.executable or not args.working_directory):
        parser.error("acquisition requires --executable and --working-directory")
    try:
        if not args.analysis_only:
            if capture_series(args) == SKIP_EXIT_CODE:
                return SKIP_EXIT_CODE
        result = CsgTemporalAnalysis.measure_series(args.output_directory, args.frame_interval)
        for control in (LIT_VIEW, LIT_MOTION, LIT_ROUTE, LIT_LAGGED_LIGHTING, LIT_OVERLAPPING):
            if result[control] != getattr(args, control):
                raise SmokeFailure(f"logged temporal {control} differs from its requested value")
        result.update({ "readback_contract": "Interval 1 waits for the accepted readback before advancing; other intervals poll asynchronously.",
            "warmup_contract": "After at least 360 graphics frames, render for at least 30 steady-clock seconds before selecting the first capture frame.",
            "timing_contract": "Interval 1 serializes readbacks for functional coverage; GPU timings describe only the matched capture workload.",
            "control_contract": "Direct view omits displayed GI; identical GI computation and direct lighting remain active."})
        if args.view == LIT_CAP_STATE:
            result["cap_state_contract"] = "RGB: raw removed count > 0, nearest radius-one cap visible, visible cap projected depth exactly equals completed G-buffer depth."
        elif args.view == "interval_state":
            result["interval_state_contract"] = "RGB: receiver span count > 0, any nonzero peel ID in the first four layers, any such peel ID has back-cap alpha > 0."
        elif args.view == LIT_EVENT_STATE:
            result["event_state_contract"] = "RGB: min(raw receiver event count,33)/33, front count/33, back count/33; scan at most 32 allocated layers and decode round(channel*33). Raw 33 is the overflow sentinel."
        elif args.view == LIT_EVENT_DATA:
            result["event_data_contract"] = "RGB: 1 when receiver span count > 0, otherwise existing span flags/33; exactly two events with front/back receiver IDs matching the first nonzero peel ID; both matching faces present with finite depths and back depth > front depth. Decode span presence with red > .5 and missing-span flags with round(red*33)."
        elif args.view == LIT_EVENT_ORDER:
            result["event_order_contract"] = "RGB: 1 when receiver span count > 0, otherwise existing span flags/33; raw event indices 0/1 encode FRONT=1 or BACK=.5 only when exactly two events match the first nonzero peel receiver, contain one front and one back, and have finite back depth > front depth. Other pairs encode green/blue=0. Decode valid pairs with both channels > .25, front with channel > .75; invalid face fields are null."
        elif args.view == LIT_SPAN_STATE:
            result["span_state_contract"] = "RGB: receiver span count/33, existing span flags/33, and raw receiver event count/33. Decode each channel with round(channel*33)."
        if args.overlapping:
            result["overlapping_contract"] = "One additional static plane receiver at the same x/y and z+0.125 shares the plane's group, cutter, and diagnostic material; four receiver event depths are distinct at the interior probe."
        if args.gpu_timing_output:
            result["gpu_timing_file"] = str(args.gpu_timing_output.resolve())
        failures = CsgTemporalGates.check_quality(result) if args.check_quality else []
        if args.check_quality:
            result["quality_limits"] = QUALITY_LIMITS
            result["quality_failures"] = failures
        if args.check_geometry:
            geometry_failures = CsgTemporalGates.check_geometry(result)
            result["geometry_probes"] = GEOMETRY_PROBES
            result["geometry_limits"] = GEOMETRY_LIMITS
            result["geometry_failures"] = geometry_failures
            failures += geometry_failures
        if args.check_cap_state:
            cap_state_failures = CsgTemporalGates.check_cap_state(result)
            result["cap_state_required_probes"] = CAP_STATE_REQUIRED_PROBES
            result["cap_state_gate_contract"] = "Every captured frame must have a produced removed interval, a visible selected cap, and exact completed G-buffer depth ownership at all four central probes and the 12-pixel capsule rim block. Exploratory outer-rim probes are not acceptance requirements."
            result["cap_state_failures"] = cap_state_failures
            failures += cap_state_failures
        if args.check_spans:
            span_failures = CsgTemporalGates.check_spans(result)
            result["span_required_probes"] = {name: GEOMETRY_PROBES[name][LIT_PIXEL] for name in SPAN_EXPECTATIONS}
            result["span_expectations"] = SPAN_EXPECTATIONS
            result["span_failures"] = span_failures
            failures += span_failures
        (args.output_directory / LIT_MANIFEST_JSON).write_text(json.dumps(result, indent=2) + "\n", encoding=LIT_UTF_8)
        if failures:
            raise SmokeFailure("; ".join(failures[:8]) + f" ({len(failures)} failed checks)")
        print(json.dumps({LIT_SAMPLES: SAMPLE_COUNT, LIT_REGIONS: result[LIT_REGIONS]}))
        return 0
    except (SmokeFailure, OSError, ValueError, subprocess.SubprocessError) as error:
        print("FAIL: " + str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
