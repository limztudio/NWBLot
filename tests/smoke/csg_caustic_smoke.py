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
from window_capture_smoke import SKIP_EXIT_CODE, SmokeFailure, read_bmp_24_rows

LIT_CAPTURE_TIMEOUT_MARGIN_SECONDS = 45.0

LIT_LOG_SUFFIX = ".log"

LIT_DISABLED = "disabled"
LIT_EXECUTABLE = "--executable"
LIT_WORKING_DIRECTORY = "--working-directory"
LIT_TIMEOUT = "--timeout"
LIT_EXPECT_LOG_MESSAGE = "--expect-log-message"
LIT_GPU_VALIDATION = "--gpu-validation"
LIT_REJECT_LOG_MESSAGE = "--reject-log-message"
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
    command = [sys.executable, str(capture_script), LIT_EXECUTABLE, str(args.executable), LIT_WORKING_DIRECTORY, str(args.working_directory), "--output", str(output), "--application-capture", "--application-capture-frame-count", str(args.frames),
        LIT_TIMEOUT, str(args.timeout), "--log-output", str(output.with_suffix(LIT_LOG_SUFFIX)), LIT_EXPECT_LOG_MESSAGE, "CsgShadowSmokeProject: shutdown",
        LIT_EXPECT_LOG_MESSAGE, "CsgCausticSmokeProject: split cavity has four interfaces before the receiver; control has two", LIT_EXPECT_LOG_MESSAGE, "CsgCausticSmokeProject: photons " + (LIT_DISABLED if arm == LIT_DISABLED else "enabled"),
        LIT_GPU_VALIDATION if args.gpu_validation else "--no-gpu-validation"]
    if arm != LIT_DISABLED:
        command += [LIT_EXPECT_LOG_MESSAGE, "RendererSystem: dispatched " + route + " caustic producer"]
    else:
        command += [LIT_REJECT_LOG_MESSAGE, "caustic producer ("]
    if route == LIT_SOFTWARE:
        command += ["--application-arg=--disable-hardware-ray-tracing", LIT_EXPECT_LOG_MESSAGE, "Loader: hardware ray tracing disabled before device creation"]
    else:
        index = {LIT_REFERENCE: 0, LIT_CUT: 1, LIT_UNCUT: 2, LIT_DISABLED: 1}[arm]
        command += ["--skip-log-message", f"atlas arm={index} light=directional hardware=0"]
    for message in ("[ERROR]", "[WARNING]", "VUID-", "Validation Error", "failed to resolve shader"):
        command += [LIT_REJECT_LOG_MESSAGE, message]
    if args.logserver_executable:
        command += [LIT_LOGSERVER_EXECUTABLE, str(args.logserver_executable)]
    else:
        command += ["--no-logserver"]
    environment = capture_environment(LIT_CUT if arm == LIT_DISABLED else arm, "directional")
    for key in tuple(environment):
        if key.startswith(("NWB_CAUSTIC_", "NWB_REFLECTION_SMOKE_", "NWB_REFRACTION_SMOKE_")):
            environment.pop(key)
    environment["NWB_CSG_CAUSTIC_DISABLED"] = "1" if arm == LIT_DISABLED else "0"
    # Keep each absorbed photon above the fixed-point splat precision while preserving total emitted energy.
    environment["NWB_CAUSTIC_PHOTON_GRID_DIVISOR"] = str(PHOTON_GRID_DIVISOR)
    print(f"Capturing CSG caustics {route}/{arm} at frame {args.frames}...", flush=True)
    result = subprocess.run(command, env=environment, check=False, timeout=args.timeout + LIT_CAPTURE_TIMEOUT_MARGIN_SECONDS)
    if result.returncode == SKIP_EXIT_CODE:
        return None
    if result.returncode:
        raise SmokeFailure(f"CSG caustic {route}/{arm} capture failed: {result.returncode}")
    verify_settings(output.with_suffix(LIT_LOG_SUFFIX).read_text(encoding=LIT_UTF_8), PHOTON_GRID_DIVISOR, producer_enabled=arm != LIT_DISABLED)
    frame = read_bmp_24_rows(output)
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
