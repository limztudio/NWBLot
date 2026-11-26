#!/usr/bin/env python3
"""Compare actual CSG photons against ordinary carved pieces and Beer/Fresnel attenuation."""

import argparse
import json
from pathlib import Path
import subprocess
import sys

from caustic_quality_smoke import verify_settings
from csg_caustic_reference import analyze_route, compare_routes
from csg_shadow_smoke import capture_environment
from smoke_capture_suite import build_window_capture_command, run_window_capture_subprocess
from window_capture_image_codec import Bmp24Codec
from window_capture_smoke import SKIP_EXIT_CODE, SmokeFailure

LIT_CAPTURE_TIMEOUT_MARGIN_SECONDS = 45.0

LIT_LOG_SUFFIX = ".log"

LIT_DISABLED = "disabled"
LIT_EXECUTABLE = "--executable"
LIT_WORKING_DIRECTORY = "--working-directory"
LIT_TIMEOUT = "--timeout"
LIT_GPU_VALIDATION = "--gpu-validation"
LIT_SOFTWARE = "software"
LIT_REFERENCE = "reference"
LIT_CUT = "cut"
LIT_UNCUT = "uncut"
LIT_LOGSERVER_EXECUTABLE = "--logserver-executable"
LIT_UTF_8 = "utf-8"
LIT_HARDWARE = "hardware"


PHOTON_GRID_DIVISOR = 4


def capture(args, route, arm):
    output = args.output_directory / (route + "_" + arm + ".bmp")
    capture_script = Path(__file__).with_name("window_capture_smoke.py") if arm != LIT_DISABLED \
        else Path(__file__).parent / "ui_layer" / "raster_ir_flat_capture.py"
    expect_logs = ["CsgShadowSmokeProject: shutdown",
        "CsgCausticSmokeProject: split cavity has four interfaces before the receiver; control has two",
        "CsgCausticSmokeProject: photons " + (LIT_DISABLED if arm == LIT_DISABLED else "enabled")]
    reject_logs = []
    skip_logs = []
    extra_args = []
    if arm != LIT_DISABLED:
        expect_logs.append("RendererSystem: dispatched " + route + " caustic producer")
    else:
        reject_logs.append("caustic producer (")
    if route == LIT_SOFTWARE:
        extra_args.append("--application-arg=--disable-hardware-ray-tracing")
        expect_logs.append("Loader: hardware ray tracing disabled before device creation")
    else:
        index = {LIT_REFERENCE: 0, LIT_CUT: 1, LIT_UNCUT: 2, LIT_DISABLED: 1}[arm]
        skip_logs.append(f"atlas arm={index} light=directional hardware=0")
    reject_logs.extend(("[ERROR]", "[WARNING]", "VUID-", "Validation Error", "failed to resolve shader"))
    command = build_window_capture_command(args, output, frames=args.frames, log_output=output.with_suffix(LIT_LOG_SUFFIX),
        expect_logs=expect_logs, reject_logs=reject_logs, skip_logs=skip_logs, extra_args=extra_args,
        script_path=capture_script, gpu_validation=args.gpu_validation, application_args=())
    environment = capture_environment(LIT_CUT if arm == LIT_DISABLED else arm, "directional")
    for key in tuple(environment):
        if key.startswith(("NWB_CAUSTIC_", "NWB_REFLECTION_SMOKE_", "NWB_REFRACTION_SMOKE_")):
            environment.pop(key)
    environment["NWB_CSG_CAUSTIC_DISABLED"] = "1" if arm == LIT_DISABLED else "0"
    environment["NWB_CAUSTIC_PHOTON_GRID_DIVISOR"] = str(PHOTON_GRID_DIVISOR)
    print(f"Capturing CSG caustics {route}/{arm} at frame {args.frames}...", flush=True)
    if run_window_capture_subprocess(command, env=environment, timeout=args.timeout,
            failure_message=f"CSG caustic {route}/{arm} capture failed: {{code}}", timeout_margin=LIT_CAPTURE_TIMEOUT_MARGIN_SECONDS) is None:
        return None
    verify_settings(output.with_suffix(LIT_LOG_SUFFIX).read_text(encoding=LIT_UTF_8), PHOTON_GRID_DIVISOR, producer_enabled=arm != LIT_DISABLED)
    frame = Bmp24Codec.read_bmp_24_rows(output)
    if frame[:2] != (960, 720):
        raise SmokeFailure("CSG caustic smoke requires the actual 960x720 framebuffer")
    return frame


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(LIT_EXECUTABLE, type=Path, required=True)
    parser.add_argument(LIT_WORKING_DIRECTORY, type=Path, required=True)
    parser.add_argument("--output-directory", type=Path, required=True)
    parser.add_argument(LIT_LOGSERVER_EXECUTABLE, type=Path)
    parser.add_argument("--frames", type=int, default=360)
    parser.add_argument(LIT_TIMEOUT, type=float, default=120.0)
    parser.add_argument(LIT_GPU_VALIDATION, action=argparse.BooleanOptionalAction, default=True)
    args = parser.parse_args(argv)
    if args.frames < 360 or args.timeout <= 0.0:
        parser.error("frames must be at least 360 and timeout must be positive")
    args.output_directory.mkdir(parents=True, exist_ok=True)
    try:
        results = {"photon_grid_divisor": PHOTON_GRID_DIVISOR}
        for route in (LIT_SOFTWARE, LIT_HARDWARE):
            frames = {}
            for arm in (LIT_REFERENCE, LIT_CUT, LIT_UNCUT, LIT_DISABLED):
                frame = capture(args, route, arm)
                if frame is None:
                    if route == LIT_SOFTWARE:
                        return SKIP_EXIT_CODE
                    results["hardware_skipped"] = True
                    break
                frames[arm] = frame
            if len(frames) == 4:
                results[route] = analyze_route(frames)
        if LIT_HARDWARE in results:
            compare_routes(results)
        (args.output_directory / "result.json").write_text(json.dumps(results, indent=2) + "\n", encoding=LIT_UTF_8)
        print("PASS: carved photons match physical pieces, four-interface optics and enabled/disabled controls")
        return 0
    except (SmokeFailure, subprocess.TimeoutExpired) as error:
        (args.output_directory / "failure.json").write_text(json.dumps({"error": str(error)}, indent=2) + "\n", encoding=LIT_UTF_8)
        print(str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
