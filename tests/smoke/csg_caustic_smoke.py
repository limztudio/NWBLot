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


PHOTON_GRID_DIVISOR = 4


def capture(args, route, arm):
    output = args.output_directory / (route + "_" + arm + ".bmp")
    capture_script = Path(__file__).with_name("window_capture_smoke.py") if arm != "disabled" \
        else Path(__file__).parent / "ui_layer" / "raster_ir_flat_capture.py"
    command = [sys.executable, str(capture_script),
        "--executable", str(args.executable), "--working-directory", str(args.working_directory),
        "--output", str(output), "--application-capture", "--application-capture-frame-count", str(args.frames),
        "--timeout", str(args.timeout), "--log-output", str(output.with_suffix(".log")),
        "--expect-log-message", "CsgShadowSmokeProject: shutdown",
        "--expect-log-message", "CsgCausticSmokeProject: split cavity has four interfaces before the receiver; control has two",
        "--expect-log-message", "CsgCausticSmokeProject: photons " + ("disabled" if arm == "disabled" else "enabled"),
        "--gpu-validation" if args.gpu_validation else "--no-gpu-validation"]
    if arm != "disabled":
        command += ["--expect-log-message", "RendererSystem: dispatched " + route + " caustic producer"]
    else:
        command += ["--reject-log-message", "caustic producer ("]
    if route == "software":
        command += ["--application-arg=--disable-hardware-ray-tracing",
            "--expect-log-message", "Loader: hardware ray tracing disabled before device creation"]
    else:
        index = {"reference": 0, "cut": 1, "uncut": 2, "disabled": 1}[arm]
        command += ["--skip-log-message", f"atlas arm={index} light=directional hardware=0"]
    for message in ("[ERROR]", "[WARNING]", "VUID-", "Validation Error", "failed to resolve shader"):
        command += ["--reject-log-message", message]
    if args.logserver_executable:
        command += ["--logserver-executable", str(args.logserver_executable)]
    else:
        command += ["--no-logserver"]
    environment = capture_environment("cut" if arm == "disabled" else arm, "directional")
    for key in tuple(environment):
        if key.startswith(("NWB_CAUSTIC_", "NWB_REFLECTION_SMOKE_", "NWB_REFRACTION_SMOKE_")):
            environment.pop(key)
    environment["NWB_CSG_CAUSTIC_DISABLED"] = "1" if arm == "disabled" else "0"
    # Keep each absorbed photon above the fixed-point splat precision while preserving total emitted energy.
    environment["NWB_CAUSTIC_PHOTON_GRID_DIVISOR"] = str(PHOTON_GRID_DIVISOR)
    print(f"Capturing CSG caustics {route}/{arm} at frame {args.frames}...", flush=True)
    result = subprocess.run(command, env=environment, check=False, timeout=args.timeout + 45.0)
    if result.returncode == SKIP_EXIT_CODE:
        return None
    if result.returncode:
        raise SmokeFailure(f"CSG caustic {route}/{arm} capture failed: {result.returncode}")
    verify_settings(output.with_suffix(".log").read_text(encoding="utf-8"), PHOTON_GRID_DIVISOR,
        producer_enabled=arm != "disabled")
    frame = read_bmp_24_rows(output)
    if frame[:2] != (960, 720):
        raise SmokeFailure("CSG caustic smoke requires the actual 960x720 framebuffer")
    return frame


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--working-directory", type=Path, required=True)
    parser.add_argument("--output-directory", type=Path, required=True)
    parser.add_argument("--logserver-executable", type=Path)
    parser.add_argument("--frames", type=int, default=360)
    parser.add_argument("--timeout", type=float, default=120.0)
    parser.add_argument("--gpu-validation", action=argparse.BooleanOptionalAction, default=True)
    args = parser.parse_args(argv)
    if args.frames < 360 or args.timeout <= 0.0:
        parser.error("frames must be at least 360 and timeout must be positive")
    args.output_directory.mkdir(parents=True, exist_ok=True)
    try:
        results = {"photon_grid_divisor": PHOTON_GRID_DIVISOR}
        for route in ("software", "hardware"):
            frames = {}
            for arm in ("reference", "cut", "uncut", "disabled"):
                frame = capture(args, route, arm)
                if frame is None:
                    if route == "software":
                        return SKIP_EXIT_CODE
                    results["hardware_skipped"] = True
                    break
                frames[arm] = frame
            if len(frames) == 4:
                results[route] = analyze_route(frames)
        if "hardware" in results:
            compare_routes(results)
        (args.output_directory / "result.json").write_text(json.dumps(results, indent=2) + "\n", encoding="utf-8")
        print("PASS: carved photons match physical pieces, four-interface optics and enabled/disabled controls")
        return 0
    except (SmokeFailure, subprocess.TimeoutExpired) as error:
        (args.output_directory / "failure.json").write_text(json.dumps({"error": str(error)}, indent=2) + "\n", encoding="utf-8")
        print(str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
