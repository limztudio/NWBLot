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
from fixture_environment import build_fixture_environment

LIT_EXECUTABLE = "--executable"
LIT_WORKING_DIRECTORY = "--working-directory"
LIT_LOGSERVER_EXECUTABLE = "--logserver-executable"
LIT_TIMEOUT = "--timeout"
LIT_GPU_VALIDATION = "--gpu-validation"
LIT_STARTUP = "startup"
LIT_EXPECT_LOG_MESSAGE = "--expect-log-message"
LIT_APPLICATION_CAPTURE = "--application-capture"
LIT_LOG_OUTPUT = "--log-output"
LIT_APPLICATION_CAPTURE_FRAME_COUNT = "--application-capture-frame-count"
LIT_PAINT = "paint"
LIT_TEXTURE_IMAGE = "texture_image"
LIT_REPLAY = "replay"
LIT_UTF_8 = "utf-8"
LIT_PASSED = "passed"
LIT_SCENES = "scenes"
LIT_DIRECT_OBSERVATION = "direct_observation"
LIT_REPLAY_OBSERVATION = "replay_observation"
LIT_PARITY = "parity"


SCENES = ("startup", "paint", "texture_image")
MODES = ("direct", "replay")


def parse_args(argv):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(LIT_EXECUTABLE, type=Path, required=True)
    parser.add_argument(LIT_WORKING_DIRECTORY, type=Path, required=True)
    parser.add_argument("--output-directory", type=Path, required=True)
    parser.add_argument(LIT_LOGSERVER_EXECUTABLE, type=Path)
    parser.add_argument("--replay-marker", required=True, help="Exact log substring emitted only after retained raster IR was actually replayed.")
    parser.add_argument("--mode-environment", default="NWB_UI_IR_REPLAY")
    parser.add_argument("--frames", type=int, default=60)
    parser.add_argument(LIT_TIMEOUT, type=float, default=75.0)
    parser.add_argument(LIT_GPU_VALIDATION, action=argparse.BooleanOptionalAction, default=True, help="Request graphics validation; use --no-gpu-validation for a fin application.")
    args = parser.parse_args(argv)
    if args.frames < 3:
        parser.error("--frames must include both empty startup frames and painted UI")
    if not math.isfinite(args.timeout) or args.timeout <= 0.0:
        parser.error("--timeout must be finite and positive")
    if not args.mode_environment or args.mode_environment.startswith("NWB_UI_LAYER_"):
        parser.error("--mode-environment must be a separate test-only mode switch")
    return args


def capture_command(args, scene, mode, path, log_path=None, *, resize=False):
    script = Path(__file__).resolve().parent / "raster_ir_flat_capture.py" if scene == LIT_STARTUP or resize \
        else SMOKE_DIRECTORY / "window_capture_smoke.py"
    command = [sys.executable, str(script), LIT_EXECUTABLE, str(args.executable.resolve()), LIT_WORKING_DIRECTORY, str(args.working_directory.resolve()), "--output", str(path), "--window-title", "NWB UI Layer Smoke", LIT_TIMEOUT, str(args.timeout),
        LIT_GPU_VALIDATION if args.gpu_validation else "--no-gpu-validation", LIT_EXPECT_LOG_MESSAGE, "Loader: project startup complete", LIT_EXPECT_LOG_MESSAGE, "UiLayerSmokeProject: shutdown",]
    if resize:
        command += [LIT_APPLICATION_CAPTURE, LIT_LOG_OUTPUT, str(log_path), LIT_APPLICATION_CAPTURE_FRAME_COUNT, "240", "--resize-before-capture", "800", "600", LIT_EXPECT_LOG_MESSAGE, "GraphicsRuntime: Back buffer resized to 800x600"]
    else:
        command += [LIT_APPLICATION_CAPTURE, LIT_LOG_OUTPUT, str(log_path), LIT_APPLICATION_CAPTURE_FRAME_COUNT, "2" if scene == LIT_STARTUP else str(args.frames)]
    if scene == LIT_PAINT or resize:
        command += [LIT_EXPECT_LOG_MESSAGE, "UiLayerSmokeProject: deterministic solid, skin, alpha and nested clip geometry submitted"]
    elif scene == LIT_TEXTURE_IMAGE:
        command += [LIT_EXPECT_LOG_MESSAGE, "UiTextureImageSmoke: state sequence="]
    if mode == LIT_REPLAY and scene != LIT_STARTUP:
        command += [LIT_EXPECT_LOG_MESSAGE, args.replay_marker]
    elif mode == "direct":
        command += ["--reject-log-message", args.replay_marker]
    if args.logserver_executable:
        command += [LIT_LOGSERVER_EXECUTABLE, str(args.logserver_executable.resolve())]
    else:
        command += ["--no-logserver"]
    return command


def capture_environment(args, scene, mode):
    fixture = "TEXTURE_IMAGE" if scene == LIT_TEXTURE_IMAGE else None
    environment = build_fixture_environment(os.environ, fixture, skin="alternate" if fixture else "default", force_x11=True)
    environment[args.mode_environment] = "1" if mode == LIT_REPLAY else "0"
    return environment


def launch_capture(args, scene, mode, path, log_path=None, *, resize=False):
    environment = capture_environment(args, scene, mode)
    environment["NWB_UI_LAYER_RESIZE_CAPTURE"] = "1" if resize else "0"
    result = subprocess.run(capture_command(args, scene, mode, path, log_path, resize=resize), env=environment, check=False, timeout=args.timeout + 45.0)
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
    log_text = log_path.read_text(encoding=LIT_UTF_8)
    return frame, observe_scene(scene, frame, log_text)


def capture_resize(args, mode):
    path = args.output_directory / f"paint_resize_{mode}.bmp"
    log_path = args.output_directory / f"paint_resize_{mode}.log"
    if not launch_capture(args, LIT_PAINT, mode, path, log_path, resize=True):
        return None
    frame = read_bmp_24_rows(path)
    if frame[:2] != (800, 600):
        raise SmokeFailure(f"paint {mode} resized framebuffer extent was {frame[:2]}, expected 800x600")
    return frame, observe_scene(LIT_PAINT, frame, log_path.read_text(encoding=LIT_UTF_8))


def run(args):
    if not args.executable.is_file():
        raise SmokeFailure(f"executable does not exist: {args.executable}")
    args.output_directory.mkdir(parents=True, exist_ok=True)
    report_path = args.output_directory / "raster_ir_parity.json"
    report = {LIT_PASSED: False, "frames": args.frames, LIT_SCENES: {}}
    report_path.write_text(json.dumps(report, indent=2) + "\n", encoding=LIT_UTF_8)
    for scene in SCENES:
        direct = capture(args, scene, MODES[0])
        if direct is None:
            return SKIP_EXIT_CODE
        replay = capture(args, scene, MODES[1])
        if replay is None:
            return SKIP_EXIT_CODE
        parity = compare_frames(direct[0], replay[0])
        report[LIT_SCENES][scene] = {LIT_DIRECT_OBSERVATION: direct[1], LIT_REPLAY_OBSERVATION: replay[1], LIT_PARITY: parity}
        report_path.write_text(json.dumps(report, indent=2) + "\n", encoding=LIT_UTF_8)
        if not parity[LIT_PASSED]:
            raise SmokeFailure(f"{scene} direct/replay framebuffer pixels differ: {parity}")
    direct_resize = capture_resize(args, MODES[0])
    if direct_resize is None:
        return SKIP_EXIT_CODE
    replay_resize = capture_resize(args, MODES[1])
    if replay_resize is None:
        return SKIP_EXIT_CODE
    parity = compare_frames(direct_resize[0], replay_resize[0])
    resize_report = {"before": "paint scene fixed-frame application capture at 960x540", "after": {LIT_DIRECT_OBSERVATION: direct_resize[1], LIT_REPLAY_OBSERVATION: replay_resize[1], LIT_PARITY: parity}}
    report[LIT_SCENES]["paint_resize"] = resize_report
    report_path.write_text(json.dumps(report, indent=2) + "\n", encoding=LIT_UTF_8)
    if not parity[LIT_PASSED]:
        raise SmokeFailure(f"paint resize direct/replay framebuffer pixels differ: {parity}")
    report[LIT_PASSED] = True
    report_path.write_text(json.dumps(report, indent=2) + "\n", encoding=LIT_UTF_8)
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
