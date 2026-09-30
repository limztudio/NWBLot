#!/usr/bin/env python3
"""Compare real UI framebuffer pixels after direct recording and retained raster IR replay."""
from __future__ import annotations

import argparse
import json
import math
import os
from pathlib import Path
import subprocess
import sys

SMOKE_DIRECTORY = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(SMOKE_DIRECTORY))

from raster_ir_parity_probe import compare_frames, observe_scene  # noqa: E402
from window_capture_smoke import SKIP_EXIT_CODE, SmokeFailure, read_bmp_24_rows  # noqa: E402


FIXTURE_FLAGS = (
    "INTERACTIVE", "WINDOW", "EDIT", "POPUP", "POPUP_TOOLS", "NESTED_POPUP",
    "LIST", "COMBO", "SEARCH_COMBO", "NUMERIC_EDIT", "TEXT_AREA", "RADIO_GROUP",
    "SLIDER", "PROGRESS", "IMAGE", "TEXTURE_IMAGE",
)
CAPTURE_ENVIRONMENT = (
    "NWB_SMOKE_FRAMEBUFFER_CAPTURE_PATH", "NWB_SMOKE_FRAMEBUFFER_CAPTURE_FRAME_COUNT",
    "NWB_RENDERER_BASELINE_CAPTURE_FREEZE_FRAME", "NWB_RENDERER_BASELINE_FIXED_DELTA_SECONDS",
    "NWB_GPU_TIMING_FILE",
)
SCENES = ("startup", "paint", "texture_image")
MODES = ("direct", "replay")


def parse_args(argv):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--working-directory", type=Path, required=True)
    parser.add_argument("--output-directory", type=Path, required=True)
    parser.add_argument("--logserver-executable", type=Path)
    parser.add_argument("--replay-marker", required=True,
        help="Exact log substring emitted only after retained raster IR was actually replayed.")
    parser.add_argument("--mode-environment", default="NWB_UI_IR_REPLAY")
    parser.add_argument("--frames", type=int, default=60)
    parser.add_argument("--timeout", type=float, default=75.0)
    args = parser.parse_args(argv)
    if args.frames < 3:
        parser.error("--frames must include both empty startup frames and painted UI")
    if not math.isfinite(args.timeout) or args.timeout <= 0.0:
        parser.error("--timeout must be finite and positive")
    if not args.mode_environment or args.mode_environment.startswith("NWB_UI_LAYER_"):
        parser.error("--mode-environment must be a separate test-only mode switch")
    return args


def capture_command(args, scene, mode, path, log_path=None, *, resize=False):
    script = Path(__file__).resolve().parent / "raster_ir_flat_capture.py" if scene == "startup" or resize \
        else SMOKE_DIRECTORY / "window_capture_smoke.py"
    command = [
        sys.executable, str(script),
        "--executable", str(args.executable.resolve()),
        "--working-directory", str(args.working_directory.resolve()),
        "--output", str(path),
        "--window-title", "NWB UI Layer Smoke",
        "--timeout", str(args.timeout),
        "--application-arg=--gpudbg",
        "--expect-log-message", "Loader: project startup complete",
        "--expect-log-message", "UiLayerSmokeProject: shutdown",
    ]
    if resize:
        command += ["--application-capture", "--log-output", str(log_path),
            "--application-capture-frame-count", "240", "--resize-before-capture", "800", "600",
            "--expect-log-message", "GraphicsRuntime: Back buffer resized to 800x600"]
    else:
        command += ["--application-capture", "--log-output", str(log_path),
            "--application-capture-frame-count", "2" if scene == "startup" else str(args.frames)]
    if scene == "paint" or resize:
        command += ["--expect-log-message",
            "UiLayerSmokeProject: deterministic solid, skin, alpha and nested clip geometry submitted"]
    elif scene == "texture_image":
        command += ["--expect-log-message", "UiTextureImageSmoke: state sequence="]
    if mode == "replay" and scene != "startup":
        command += ["--expect-log-message", args.replay_marker]
    elif mode == "direct":
        command += ["--reject-log-message", args.replay_marker]
    if args.logserver_executable:
        command += ["--logserver-executable", str(args.logserver_executable.resolve())]
    else:
        command += ["--no-logserver"]
    return command


def capture_environment(args, scene, mode):
    environment = dict(os.environ)
    for flag in FIXTURE_FLAGS:
        environment[f"NWB_UI_LAYER_{flag}"] = "0"
        environment[f"NWB_UI_LAYER_{flag}_SKIN"] = "0"
    if scene == "texture_image":
        environment["NWB_UI_LAYER_TEXTURE_IMAGE"] = "1"
        environment["NWB_UI_LAYER_TEXTURE_IMAGE_SKIN"] = "1"
    environment[args.mode_environment] = "1" if mode == "replay" else "0"
    for variable in CAPTURE_ENVIRONMENT:
        environment.pop(variable, None)
    if sys.platform.startswith("linux"):
        environment["NWB_LINUX_BACKEND"] = "x11"
    return environment


def launch_capture(args, scene, mode, path, log_path=None, *, resize=False):
    environment = capture_environment(args, scene, mode)
    environment["NWB_UI_LAYER_RESIZE_CAPTURE"] = "1" if resize else "0"
    result = subprocess.run(capture_command(args, scene, mode, path, log_path, resize=resize),
        env=environment, check=False, timeout=args.timeout + 45.0)
    if result.returncode == SKIP_EXIT_CODE:
        return False
    if result.returncode:
        raise SmokeFailure(f"{scene} {mode} UI framebuffer capture failed with exit {result.returncode}")
    return True


def capture(args, scene, mode):
    path = args.output_directory / f"{scene}_{mode}.bmp"
    log_path = args.output_directory / f"{scene}_{mode}.log"
    if not launch_capture(args, scene, mode, path, log_path):
        return None
    frame = read_bmp_24_rows(path)
    if frame[:2] != (960, 540):
        raise SmokeFailure(f"{scene} {mode} framebuffer extent was {frame[:2]}, expected 960x540")
    log_text = log_path.read_text(encoding="utf-8")
    return frame, observe_scene(scene, frame, log_text)


def capture_resize(args, mode):
    path = args.output_directory / f"paint_resize_{mode}.bmp"
    log_path = args.output_directory / f"paint_resize_{mode}.log"
    if not launch_capture(args, "paint", mode, path, log_path, resize=True):
        return None
    frame = read_bmp_24_rows(path)
    if frame[:2] != (800, 600):
        raise SmokeFailure(f"paint {mode} resized framebuffer extent was {frame[:2]}, expected 800x600")
    return frame, observe_scene("paint", frame, log_path.read_text(encoding="utf-8"))


def run(args):
    if not args.executable.is_file():
        raise SmokeFailure(f"executable does not exist: {args.executable}")
    args.output_directory.mkdir(parents=True, exist_ok=True)
    report_path = args.output_directory / "raster_ir_parity.json"
    report = {"passed": False, "frames": args.frames, "scenes": {}}
    report_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    for scene in SCENES:
        direct = capture(args, scene, MODES[0])
        if direct is None:
            return SKIP_EXIT_CODE
        replay = capture(args, scene, MODES[1])
        if replay is None:
            return SKIP_EXIT_CODE
        parity = compare_frames(direct[0], replay[0])
        report["scenes"][scene] = {"direct_observation": direct[1],
            "replay_observation": replay[1], "parity": parity}
        report_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        if not parity["passed"]:
            raise SmokeFailure(f"{scene} direct/replay framebuffer pixels differ: {parity}")
    direct_resize = capture_resize(args, MODES[0])
    if direct_resize is None:
        return SKIP_EXIT_CODE
    replay_resize = capture_resize(args, MODES[1])
    if replay_resize is None:
        return SKIP_EXIT_CODE
    parity = compare_frames(direct_resize[0], replay_resize[0])
    resize_report = {"before": "paint scene fixed-frame application capture at 960x540",
        "after": {"direct_observation": direct_resize[1],
            "replay_observation": replay_resize[1], "parity": parity}}
    report["scenes"]["paint_resize"] = resize_report
    report_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    if not parity["passed"]:
        raise SmokeFailure(f"paint resize direct/replay framebuffer pixels differ: {parity}")
    report["passed"] = True
    report_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print("UI raster IR: startup, passive, engine-image, and resized framebuffers match within one RGB level", flush=True)
    return 0


def main(argv):
    args = parse_args(argv)
    try:
        return run(args)
    except subprocess.TimeoutExpired as error:
        print(f"FAIL: UI parity capture timed out: {error}", flush=True)
        return 1
    except (SmokeFailure, OSError, ValueError) as error:
        print(f"FAIL: {error}", flush=True)
        return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
