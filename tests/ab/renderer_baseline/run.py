#!/usr/bin/env python3
"""Capture an immutable renderer baseline or compare one candidate against it.

The workflow deliberately lives entirely under ``tests/``.  It takes no renderer
feature switch: a baseline is an image and a manifest from a known source revision,
while a candidate is the normal selected smoke executable at a later revision.
"""

from __future__ import annotations

import argparse
import json
import math
import platform
import re
import struct
import subprocess
import sys
import time
from dataclasses import asdict, dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Dict, List, Mapping, Optional, Sequence, Tuple

LIT_MINIMUM_COMPLEX_GI_SETTLE_SECONDS = 30.0

LIT_ENABLED_FLAG = "1"

REPO = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO / "tests" / "smoke"))

from profiles import BaselineProfile, corpus_profile_names, get_profile, profile_names  # noqa: E402
from harness_test_support import make_abnormal_exit_terminator  # noqa: E402
from window_capture_smoke import (  # noqa: E402
    FRAMEBUFFER_CAPTURE_SKIP_MESSAGE, SKIP_EXIT_CODE, STRICT_LOG_FAILURE_MESSAGES, SmokeFailure, SmokeSkip, WindowsCapture, read_bmp_24, wait_for_application_capture_exit,
    build_launch_environment, make_runtime_launch_args, create_capture_backend, ensure_process_running, launch_logserver, launch_testbed, require_normal_process_exit,
    shutdown_logserver_and_collect, terminate_process, validate_capture_result, wait_for_log_message,)
import renderer_baseline_self_test  # noqa: E402
from renderer_baseline_corpus import CorpusReference, sha256_file, effective_frozen_environment, load_reference, load_corpus_reference, apply_corpus_limits, verify_corpus  # noqa: E402

LIT_NWB_GI_SMOKE_COMPLEX_SCENE = "NWB_GI_SMOKE_COMPLEX_SCENE"
LIT_B_BM = b"BM"
LIT_COMPLEX_GI_PROFILE_REQUIRES_ITS_SELECT = "complex GI profile requires its selected complex scene"
LIT_F = "f"
LIT_NWB_GI_SMOKE_MIN_SETTLE_SECONDS = "NWB_GI_SMOKE_MIN_SETTLE_SECONDS"
LIT_START = "start"
LIT_END = "end"
LIT_READBACK = "readback"
LIT_COMPLEX_GI_HAS_MALFORMED_LIVE_WARMUP_E = "complex GI has malformed live warmup evidence"
LIT_CAPTURE_SOURCE_FRAME = "capture_source_frame"
LIT_NWB_SMOKE_FRAMEBUFFER_CAPTURE_PATH = "NWB_SMOKE_FRAMEBUFFER_CAPTURE_PATH"
LIT_NWB_SMOKE_FRAMEBUFFER_CAPTURE_FRAME_CO = "NWB_SMOKE_FRAMEBUFFER_CAPTURE_FRAME_COUNT"
LIT_SETTLE_SECONDS_MUST_BE_POSITIVE_AND_FI = "--settle-seconds must be positive and finite"
LIT_SETTLE_SECONDS_2 = "--settle-seconds"
LIT_STARTUP_TIMEOUT = "--startup-timeout"
LIT_MAXIMUM_MEAN_ABS_2 = "--maximum-mean-abs"

LIT_GIT = "git"
LIT_UNAVAILABLE = "unavailable"
LIT_FRAME_LOCKED = "frame-locked"
LIT_SETTLED = "settled"
LIT_I = "<I"
LIT_RENDERER_BASELINE_CAPTURE = "renderer baseline capture"
LIT_RENDERER_BASELINE_LOGSERVER = "renderer baseline logserver"
LIT_UTF_8 = "utf-8"
LIT_SCHEMA = "schema"
LIT_CAPTURE_KIND = "capture_kind"
LIT_PROFILE = "profile"
LIT_CAPTURE_MODE = "capture_mode"
LIT_CAPTURE_FREEZE_FRAME = "capture_freeze_frame"
LIT_FIXED_DELTA_SECONDS = "fixed_delta_seconds"
LIT_SETTLE_SECONDS = "settle_seconds"
LIT_GPU_VALIDATION = "gpu_validation"
LIT_SOURCE_REVISION = "source_revision"
LIT_SOURCE_WORKTREE_CLEAN = "source_worktree_clean"
LIT_CAPTURE_FILE = "capture_file"
LIT_CAPTURE_SHA256 = "capture_sha256"
LIT_FROZEN_ENVIRONMENT = "frozen_environment"
LIT_REFERENCE_SOURCE_REVISION = "reference_source_revision"
LIT_MANIFEST_JSON = "manifest.json"
LIT_BASELINE = "baseline"
LIT_CORPUS_ID = "corpus_id"
LIT_ARTIFACT_ROOT = "artifact_root"
LIT_PROFILES = "profiles"
LIT_CAPTURE_ENVIRONMENT = "capture_environment"
LIT_REFERENCE = "reference"
LIT_DIRECTORY = "directory"
LIT_RUNTIME_LOG = "runtime.log"
LIT_MANIFEST_SHA256 = "manifest_sha256"
LIT_RUNTIME_LOG_SHA256 = "runtime_log_sha256"
LIT_LIMITS = "limits"
LIT_MAXIMUM_MAX_ABS = "maximum_max_abs"
LIT_MAXIMUM_MEAN_ABS = "maximum_mean_abs"
LIT_MAXIMUM_CHANGED_FRACTION = "maximum_changed_fraction"
LIT_SAME_REVISION_RECAPTURE = "same_revision_recapture"
LIT_COMPARISON_JSON = "comparison.json"
LIT_COMPARISON_SHA256 = "comparison_sha256"
LIT_NWB_RENDERER_BASELINE_COMPARISON_V1 = "nwb.renderer-baseline-comparison.v1"
LIT_CANDIDATE_SOURCE_REVISION = "candidate_source_revision"
LIT_DIFFERENCE = "difference"
LIT_MAX_ABS = "max_abs"
LIT_MEAN_ABS = "mean_abs"
LIT_CHANGED_FRACTION = "changed_fraction"
LIT_CANDIDATE_BMP = "candidate.bmp"
LIT_BASELINE_BMP = "baseline.bmp"
LIT_DIFFERENCE_BMP = "difference.bmp"
LIT_PROFILE_2 = "--profile"
LIT_EXECUTABLE = "--executable"
LIT_RUNTIME_DIR = "--runtime-dir"
LIT_OUTPUT_DIR = "--output-dir"
LIT_REFERENCE_DIR = "--reference-dir"
LIT_REFERENCE_CORPUS = "--reference-corpus"
LIT_STORE_TRUE = "store_true"
LIT_CAPTURE = "capture"
LIT_MAIN = "__main__"
LIT_APPEND = "append"


SCHEMA = "nwb.renderer-baseline.v1"
CORPUS_SCHEMA = "nwb.renderer-baseline-corpus.v1"
CURRENT_CORPUS_ID = "current-renderer-v1"
FORBIDDEN_LOG_MESSAGES = (*STRICT_LOG_FAILURE_MESSAGES, "cannot safely continue after an unresolved frame recovery submission",)


@dataclass(frozen=True)
class PixelDifference:
    width: int
    height: int
    max_abs: int
    mean_abs: float
    changed_pixels: int
    total_pixels: int

    @property
    def changed_fraction(self) -> float:
        return self.changed_pixels / self.total_pixels if self.total_pixels else 0.0


@dataclass(frozen=True)
class CaptureResult:
    capture_file: str
    log_file: str
    capture_sha256: str
    executable_sha256: str
    source_revision: str
    source_worktree_clean: bool
    frozen_environment: Mapping[str, str]
    forbidden_log_messages: Tuple[str, ...]
    live_warmup: Optional[Mapping[str, object]] = None


def source_revision() -> str:
    try:
        result = subprocess.run([LIT_GIT, "rev-parse", "HEAD"], cwd=REPO, check=False, capture_output=True, text=True,)
    except OSError:
        return LIT_UNAVAILABLE
    return result.stdout.strip() if result.returncode == 0 and result.stdout.strip() else LIT_UNAVAILABLE


def source_worktree_clean() -> bool:
    try:
        result = subprocess.run([LIT_GIT, "status", "--porcelain"], cwd=REPO, check=False, capture_output=True, text=True,)
    except OSError:
        return False
    return result.returncode == 0 and not result.stdout.strip()


def capture_mode(profile: BaselineProfile) -> str:
    if profile.frozen_environment.get(LIT_NWB_GI_SMOKE_COMPLEX_SCENE) == LIT_ENABLED_FLAG:
        return "settled-readback"
    return LIT_FRAME_LOCKED if profile.capture_freeze_frame != 0 else LIT_SETTLED


def read_bmp_rgb(path: Path) -> Tuple[int, int, bytes]:
    try:
        data = path.read_bytes()
    except OSError as error:
        raise SmokeFailure(f"could not read BMP '{path}': {error}") from error
    if len(data) < 54 or data[:2] != LIT_B_BM:
        raise SmokeFailure(f"capture is not a BMP: {path}")

    pixel_offset = struct.unpack_from(LIT_I, data, 10)[0]
    dib_size = struct.unpack_from(LIT_I, data, 14)[0]
    if dib_size < 40 or len(data) < 14 + dib_size:
        raise SmokeFailure(f"capture has an unsupported DIB header: {path}")

    width, signed_height, planes, bits_per_pixel, compression = struct.unpack_from("<iiHHI", data, 18)
    if width <= 0 or signed_height == 0 or planes != 1 or bits_per_pixel != 24 or compression != 0:
        raise SmokeFailure(f"capture must be an uncompressed 24-bit BMP: {path}")

    height = abs(signed_height)
    source_stride = ((width * 3 + 3) // 4) * 4
    if pixel_offset + source_stride * height > len(data):
        raise SmokeFailure(f"capture pixel data is truncated: {path}")

    rows: List[bytes] = []
    bottom_up = signed_height > 0
    for logical_row in range(height):
        source_row = height - 1 - logical_row if bottom_up else logical_row
        source_offset = pixel_offset + source_row * source_stride
        bgr = data[source_offset:source_offset + width * 3]
        rgb = bytearray(width * 3)
        for pixel in range(0, len(bgr), 3):
            rgb[pixel] = bgr[pixel + 2]
            rgb[pixel + 1] = bgr[pixel + 1]
            rgb[pixel + 2] = bgr[pixel]
        rows.append(bytes(rgb))
    return width, height, b"".join(rows)


def write_bmp_rgb(path: Path, width: int, height: int, rgb: bytes) -> None:
    if width <= 0 or height <= 0 or len(rgb) != width * height * 3:
        raise ValueError("invalid RGB baseline image dimensions or byte count")

    path.parent.mkdir(parents=True, exist_ok=True)
    row_stride = ((width * 3 + 3) // 4) * 4
    image_size = row_stride * height
    padding = b"\0" * (row_stride - width * 3)
    with path.open("wb") as output:
        output.write(struct.pack("<2sIHHI", LIT_B_BM, 14 + 40 + image_size, 0, 0, 54))
        output.write(struct.pack("<IIIHHIIIIII", 40, width, height, 1, 24, 0, image_size, 0, 0, 0, 0))
        for row_index in range(height - 1, -1, -1):
            row = rgb[row_index * width * 3:(row_index + 1) * width * 3]
            bgr = bytearray(width * 3)
            for pixel in range(0, len(row), 3):
                bgr[pixel] = row[pixel + 2]
                bgr[pixel + 1] = row[pixel + 1]
                bgr[pixel + 2] = row[pixel]
            output.write(bgr)
            output.write(padding)


def compare_bmp_rgb(reference_path: Path, candidate_path: Path, difference_path: Path) -> PixelDifference:
    reference_width, reference_height, reference_rgb = read_bmp_rgb(reference_path)
    candidate_width, candidate_height, candidate_rgb = read_bmp_rgb(candidate_path)
    if (reference_width, reference_height) != (candidate_width, candidate_height):
        raise SmokeFailure("reference and candidate captures have different dimensions: " f"{reference_width}x{reference_height} vs {candidate_width}x{candidate_height}")

    total_abs = 0
    max_abs = 0
    changed_pixels = 0
    difference_rgb = bytearray(len(reference_rgb))
    for offset in range(0, len(reference_rgb), 3):
        changed = False
        for channel in range(3):
            delta = abs(reference_rgb[offset + channel] - candidate_rgb[offset + channel])
            total_abs += delta
            max_abs = max(max_abs, delta)
            difference_rgb[offset + channel] = min(255, delta * 8)
            changed = changed or delta != 0
        if changed:
            changed_pixels += 1

    write_bmp_rgb(difference_path, reference_width, reference_height, bytes(difference_rgb))
    total_pixels = reference_width * reference_height
    return PixelDifference(width=reference_width, height=reference_height, max_abs=max_abs, mean_abs=total_abs / len(reference_rgb), changed_pixels=changed_pixels, total_pixels=total_pixels,)


def difference_failures(args: argparse.Namespace, difference: PixelDifference) -> List[str]:
    maximum_max_abs = 0 if args.require_exact else args.maximum_max_abs
    maximum_mean_abs = 0.0 if args.require_exact else args.maximum_mean_abs
    maximum_changed_fraction = 0.0 if args.require_exact else args.maximum_changed_fraction
    failures: List[str] = []
    if maximum_max_abs is not None and difference.max_abs > maximum_max_abs:
        failures.append(f"max abs {difference.max_abs} exceeds {maximum_max_abs}")
    if maximum_mean_abs is not None and difference.mean_abs > maximum_mean_abs:
        failures.append(f"mean abs {difference.mean_abs:.6f} exceeds {maximum_mean_abs:.6f}")
    if maximum_changed_fraction is not None and difference.changed_fraction > maximum_changed_fraction:
        failures.append(f"changed fraction {difference.changed_fraction:.6f} exceeds {maximum_changed_fraction:.6f}")
    return failures


def validate_runtime_log(log_text: str, rejected_messages: Sequence[str]) -> Tuple[str, ...]:
    if not log_text:
        raise SmokeFailure("renderer baseline capture produced no captured logger output")
    forbidden = tuple(message for message in FORBIDDEN_LOG_MESSAGES if message in log_text)
    forbidden += tuple(message for message in rejected_messages if message in log_text)
    if forbidden:
        raise SmokeFailure(f"renderer baseline capture found forbidden log messages: {list(forbidden)}")
    return forbidden


def validate_complex_gi_warmup(log_text: str, profile: BaselineProfile, frozen_environment: Mapping[str, str]) -> Optional[Mapping[str, object]]:
    if profile.frozen_environment.get(LIT_NWB_GI_SMOKE_COMPLEX_SCENE) != LIT_ENABLED_FLAG:
        return None
    if frozen_environment.get(LIT_NWB_GI_SMOKE_COMPLEX_SCENE) != LIT_ENABLED_FLAG:
        raise SmokeFailure(LIT_COMPLEX_GI_PROFILE_REQUIRES_ITS_SELECT)
    try:
        requested_seconds = struct.unpack(LIT_F, struct.pack(LIT_F, float(frozen_environment.get(LIT_NWB_GI_SMOKE_MIN_SETTLE_SECONDS, "30"))))[0]
    except (ValueError, OverflowError, struct.error) as error:
        raise SmokeFailure("complex GI requires a finite live warmup of at least 30 seconds") from error
    if not math.isfinite(requested_seconds) or requested_seconds < LIT_MINIMUM_COMPLEX_GI_SETTLE_SECONDS or profile.capture_freeze_frame < 360:
        raise SmokeFailure("complex GI requires at least 360 presentations then 30 live warmup seconds")
    patterns = {
        LIT_START: r"GiTestSmokeProject: complex GI live warmup start graphics_frame=(\d+) presentations=(\d+) required_presentations=(\d+) required_seconds=(\S+)",
        LIT_END: r"GiTestSmokeProject: complex GI live warmup end graphics_frame=(\d+) presentations=(\d+) elapsed_seconds=(\S+)",
        LIT_READBACK: r"FramebufferCapture: graphics source frame (\d+)",
        LIT_CAPTURE: r"GiTestSmokeProject: complex GI capture source_frame=(\d+)",
    }
    matches = {}
    for name, pattern in patterns.items():
        records = list(re.finditer(pattern, log_text))
        if len(records) != 1:
            raise SmokeFailure(f"complex GI requires exactly one {name} live warmup evidence record")
        matches[name] = records[0]
    start_frame, start_presentations, required_presentations = map(int, matches[LIT_START].groups()[:3])
    end_frame, end_presentations = map(int, matches[LIT_END].groups()[:2])
    readback_frame = int(matches[LIT_READBACK].group(1))
    capture_frame = int(matches[LIT_CAPTURE].group(1))
    numeric_pattern = r"[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?"
    for value in (matches[LIT_START].group(4), matches[LIT_END].group(3)):
        if not re.fullmatch(numeric_pattern, value):
            raise SmokeFailure(LIT_COMPLEX_GI_HAS_MALFORMED_LIVE_WARMUP_E)
    try:
        required_seconds = float(matches[LIT_START].group(4))
        elapsed_seconds = float(matches[LIT_END].group(3))
    except ValueError as error:
        raise SmokeFailure(LIT_COMPLEX_GI_HAS_MALFORMED_LIVE_WARMUP_E) from error
    if required_presentations != profile.capture_freeze_frame or start_presentations < required_presentations:
        raise SmokeFailure("complex GI live warmup began before the presentation warmup")
    if not math.isfinite(required_seconds) or required_seconds != requested_seconds:
        raise SmokeFailure("complex GI live warmup duration does not match its configured requirement")
    if not math.isfinite(elapsed_seconds) or elapsed_seconds < required_seconds:
        raise SmokeFailure("complex GI live warmup ended before its required duration")
    if end_frame <= start_frame or end_presentations <= start_presentations:
        raise SmokeFailure("complex GI live warmup has no rendered presentation progress")
    if capture_frame != readback_frame or capture_frame < end_frame or capture_frame >= 2**64 - 1:
        raise SmokeFailure("complex GI capture source does not match its completed readback and live warmup")
    if not (matches[LIT_START].start() < matches[LIT_END].start() < matches[LIT_READBACK].start() < matches[LIT_CAPTURE].start()):
        raise SmokeFailure("complex GI live warmup and readback evidence is out of order")
    return {"start_graphics_frame": start_frame, "start_presentations": start_presentations, "required_presentations": required_presentations, "required_live_seconds": required_seconds,
        "elapsed_live_seconds": elapsed_seconds, "warmup_end_graphics_frame": end_frame, "end_presentations": end_presentations, LIT_CAPTURE_SOURCE_FRAME: capture_frame}


def capture_scene(args: argparse.Namespace, profile: BaselineProfile, capture_path: Path, log_path: Path, frozen_environment: Mapping[str, str],) -> CaptureResult:
    if not args.executable.is_file():
        raise SmokeFailure(f"renderer baseline executable does not exist: {args.executable}")
    if not args.runtime_dir.is_dir():
        raise SmokeFailure(f"renderer baseline runtime directory does not exist: {args.runtime_dir}")

    launch_args = make_runtime_launch_args(args)
    environment = build_launch_environment(launch_args)
    environment.update(frozen_environment)
    if profile.capture_freeze_frame != 0:
        environment["NWB_RENDERER_BASELINE_CAPTURE_FREEZE_FRAME"] = str(profile.capture_freeze_frame)
    if profile.fixed_delta_seconds != 0.0:
        environment["NWB_RENDERER_BASELINE_FIXED_DELTA_SECONDS"] = f"{profile.fixed_delta_seconds:.9g}"
    complex_readback = profile.frozen_environment.get(LIT_NWB_GI_SMOKE_COMPLEX_SCENE) == LIT_ENABLED_FLAG
    if complex_readback and frozen_environment.get(LIT_NWB_GI_SMOKE_COMPLEX_SCENE) != LIT_ENABLED_FLAG:
        raise SmokeFailure(LIT_COMPLEX_GI_PROFILE_REQUIRES_ITS_SELECT)
    if complex_readback:
        environment[LIT_NWB_SMOKE_FRAMEBUFFER_CAPTURE_PATH] = str(capture_path.resolve())
        environment[LIT_NWB_SMOKE_FRAMEBUFFER_CAPTURE_FRAME_CO] = str(profile.capture_freeze_frame)
    backend = None
    logserver_process = None
    app_process = None
    window = None
    app_exit_code = None
    app_exit_tail = ""
    log_directory: Optional[Path] = None
    log_baseline: Mapping[Path, int] = {}
    log_pattern = ""
    log_text = ""
    wait_failure = None
    try:
        if not complex_readback:
            backend = create_capture_backend()
        logserver_process, log_port, log_directory, log_baseline, log_pattern = launch_logserver(launch_args, args.executable, environment)
        app_process = launch_testbed(launch_args, args.executable, environment, log_port)
        if complex_readback:
            try:
                wait_for_application_capture_exit(app_process, capture_path, args.startup_timeout)
            except SmokeFailure as error:
                wait_failure = error
        else:
            window = backend.wait_for_window(app_process.pid, args.startup_timeout, profile.window_title)
            if not window:
                ensure_process_running(app_process, f"while waiting for '{profile.window_title}'")
                raise SmokeFailure(f"renderer baseline did not expose expected window '{profile.window_title}'")
            if isinstance(backend, WindowsCapture):
                backend.prepare_raw_client_window(window)
            else:
                backend.prepare_window(window)
            if profile.capture_freeze_frame != 0:
                if not profile.capture_ready_log:
                    raise SmokeFailure(f"frame-locked profile '{args.profile}' has no capture-ready log marker")
                if not log_directory:
                    raise SmokeFailure("frame-locked renderer baseline requires a readable runtime-log directory")
                wait_for_log_message(log_directory, log_baseline, log_pattern, profile.capture_ready_log, args.startup_timeout,)
                # The marker identifies the fixture's held update phase.
                # This delay precedes client capture; it does not identify or wait on an accepted GPU source-frame fence.
                time.sleep(args.settle_seconds)
            else:
                time.sleep(args.settle_seconds)
            ensure_process_running(app_process, "before baseline capture")
            if isinstance(backend, WindowsCapture):
                capture = backend.capture_prepared_raw_client_window(window, capture_path)
            else:
                capture = backend.capture_window(window, capture_path)
            validate_capture_result(capture)
        app_exit_code, app_exit_tail = terminate_process(app_process, LIT_RENDERER_BASELINE_CAPTURE, window)
        app_process = None
        log_text = shutdown_logserver_and_collect(logserver_process, log_directory, log_baseline, log_pattern, LIT_RENDERER_BASELINE_LOGSERVER,)
        logserver_process = None
    finally:
        terminate_process(app_process, LIT_RENDERER_BASELINE_CAPTURE, window)
        terminate_process(logserver_process, LIT_RENDERER_BASELINE_LOGSERVER)
        if backend:
            backend.close()

    log_path.write_text(log_text, encoding=LIT_UTF_8)
    if wait_failure:
        raise wait_failure
    require_normal_process_exit(app_exit_code, app_exit_tail, "testbed")
    forbidden = validate_runtime_log(log_text, args.reject_log)
    if complex_readback and FRAMEBUFFER_CAPTURE_SKIP_MESSAGE in log_text:
        raise SmokeSkip(FRAMEBUFFER_CAPTURE_SKIP_MESSAGE)
    if not capture_path.is_file():
        raise SmokeFailure(f"renderer baseline did not create capture: {capture_path}")
    if complex_readback:
        validate_capture_result(read_bmp_24(capture_path))
        if Path(str(capture_path) + ".partial").exists():
            raise SmokeFailure("complex GI application capture left an incomplete readback")
    live_warmup = validate_complex_gi_warmup(log_text, profile, frozen_environment)
    return CaptureResult(capture_file=capture_path.name, log_file=log_path.name, capture_sha256=sha256_file(capture_path), executable_sha256=sha256_file(args.executable), source_revision=source_revision(), source_worktree_clean=source_worktree_clean(),
        frozen_environment=dict(frozen_environment), forbidden_log_messages=forbidden, live_warmup=live_warmup,)


def manifest_payload(capture_kind: str, profile_name: str, profile: BaselineProfile, args: argparse.Namespace, capture: CaptureResult, reference_manifest: Optional[Mapping[str, object]] = None,) -> Dict[str, object]:
    payload: Dict[str, object] = { LIT_SCHEMA: SCHEMA, LIT_CAPTURE_KIND: capture_kind, "captured_utc": datetime.now(timezone.utc).isoformat(), LIT_PROFILE: profile_name, "profile_description": profile.description, "target": profile.target,
        "window_title": profile.window_title, LIT_CAPTURE_MODE: capture_mode(profile), LIT_CAPTURE_FREEZE_FRAME: profile.capture_freeze_frame, LIT_FIXED_DELTA_SECONDS: profile.fixed_delta_seconds, LIT_SETTLE_SECONDS: args.settle_seconds,
        LIT_GPU_VALIDATION: args.gpu_validation, "runtime_directory": str(args.runtime_dir), LIT_SOURCE_REVISION: capture.source_revision, LIT_SOURCE_WORKTREE_CLEAN: capture.source_worktree_clean, "executable_sha256": capture.executable_sha256,
        LIT_CAPTURE_FILE: capture.capture_file, LIT_CAPTURE_SHA256: capture.capture_sha256, "log_file": capture.log_file, LIT_FROZEN_ENVIRONMENT: dict(capture.frozen_environment), "platform": platform.platform(), "python_version": platform.python_version(), }
    if capture.live_warmup is not None:
        payload["live_warmup"] = dict(capture.live_warmup)
    if reference_manifest is not None:
        payload[LIT_REFERENCE_SOURCE_REVISION] = reference_manifest.get(LIT_SOURCE_REVISION)
        payload["reference_capture_sha256"] = reference_manifest.get(LIT_CAPTURE_SHA256)
    return payload


def write_json(path: Path, payload: Mapping[str, object]) -> None:
    path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding=LIT_UTF_8)


def prepare_output_directory(path: Path) -> None:
    if path.exists():
        raise SmokeFailure(f"baseline output directory already exists and will not be overwritten: {path}")
    try:
        path.mkdir(parents=True)
    except OSError as error:
        raise SmokeFailure(f"could not create baseline output directory '{path}': {error}") from error


def run(args: argparse.Namespace) -> int:
    profile = get_profile(args.profile)
    if profile.frozen_environment.get(LIT_NWB_GI_SMOKE_COMPLEX_SCENE) == LIT_ENABLED_FLAG:
        if args.settle_seconds is not None:
            raise SmokeFailure("complex GI readback uses NWB_GI_SMOKE_MIN_SETTLE_SECONDS instead of --settle-seconds")
        args.settle_seconds = 0.0
    else:
        args.settle_seconds = profile.settle_seconds if args.settle_seconds is None else args.settle_seconds
        if not math.isfinite(args.settle_seconds) or args.settle_seconds <= 0.0:
            raise SmokeFailure(LIT_SETTLE_SECONDS_MUST_BE_POSITIVE_AND_FI)
    frozen_environment = effective_frozen_environment(profile)
    reference_manifest: Optional[Mapping[str, object]] = None
    reference_capture: Optional[Path] = None
    corpus_reference: Optional[CorpusReference] = None
    reference_directory: Optional[Path] = None
    if args.reference_corpus is not None:
        reference_manifest, reference_capture, corpus_reference = load_corpus_reference(args.reference_corpus, args.corpus_root, args.profile, profile, args.gpu_validation, frozen_environment,)
        reference_directory = corpus_reference.reference_directory
        apply_corpus_limits(args, corpus_reference)
    elif args.reference_dir is not None:
        reference_manifest, reference_capture = load_reference(args.reference_dir, args.profile, frozen_environment, args.settle_seconds, args.gpu_validation, profile.capture_freeze_frame, profile.fixed_delta_seconds,)
        reference_directory = args.reference_dir

    if reference_capture is None and not source_worktree_clean():
        raise SmokeFailure("refusing to create an immutable baseline from a dirty source worktree")

    prepare_output_directory(args.output_dir)
    capture_kind = "candidate" if reference_capture is not None else LIT_BASELINE
    capture_path = args.output_dir / (LIT_CANDIDATE_BMP if reference_capture is not None else LIT_BASELINE_BMP)
    log_path = args.output_dir / LIT_RUNTIME_LOG
    capture = capture_scene(args, profile, capture_path, log_path, frozen_environment)
    manifest = manifest_payload(capture_kind, args.profile, profile, args, capture, reference_manifest)
    write_json(args.output_dir / LIT_MANIFEST_JSON, manifest)

    if reference_capture is None:
        print(f"captured immutable renderer baseline: {args.output_dir}")
        return 0

    difference = compare_bmp_rgb(reference_capture, capture_path, args.output_dir / LIT_DIFFERENCE_BMP)
    failures = difference_failures(args, difference)
    comparison = { LIT_SCHEMA: LIT_NWB_RENDERER_BASELINE_COMPARISON_V1, LIT_PROFILE: args.profile, "reference_directory": str(reference_directory), "candidate_directory": str(args.output_dir),
        LIT_REFERENCE_SOURCE_REVISION: reference_manifest.get(LIT_SOURCE_REVISION), LIT_CANDIDATE_SOURCE_REVISION: capture.source_revision, "reference_capture": str(reference_capture), "candidate_capture": str(capture_path),
        "difference_capture": str(args.output_dir / LIT_DIFFERENCE_BMP), LIT_DIFFERENCE: asdict(difference) | {LIT_CHANGED_FRACTION: difference.changed_fraction}, "reference_corpus": corpus_reference.corpus_id if corpus_reference is not None else None,
        "effective_limits": { LIT_MAXIMUM_MAX_ABS: args.maximum_max_abs, LIT_MAXIMUM_MEAN_ABS: args.maximum_mean_abs, LIT_MAXIMUM_CHANGED_FRACTION: args.maximum_changed_fraction, "require_exact": args.require_exact, }, "threshold_failures": failures,
        "verdict": "fail" if failures else ("pass" if corpus_reference is not None else "report-only"), }
    write_json(args.output_dir / LIT_COMPARISON_JSON, comparison)
    print(f"renderer baseline comparison artifacts: {args.output_dir}")
    if failures:
        print("renderer baseline comparison failed: " + "; ".join(failures), file=sys.stderr)
        return 1
    return 0


def make_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(LIT_PROFILE_2, choices=profile_names(), help="Renderer scene to capture.")
    parser.add_argument(LIT_EXECUTABLE, type=Path, help="Selected smoke executable.")
    parser.add_argument(LIT_RUNTIME_DIR, type=Path, help="Selected cooked smoke runtime directory.")
    parser.add_argument(LIT_OUTPUT_DIR, type=Path, help="New, empty artifact directory.")
    reference_group = parser.add_mutually_exclusive_group()
    reference_group.add_argument(LIT_REFERENCE_DIR, type=Path, help="Immutable baseline directory to compare against.")
    reference_group.add_argument(LIT_REFERENCE_CORPUS, choices=(CURRENT_CORPUS_ID,), help="Compare against the checked-in formal current-renderer corpus and enforce its limits.",)
    parser.add_argument("--corpus-root", type=Path, help="Restored artifact root for --reference-corpus or --verify-corpus (defaults to the corpus artifact root).",)
    parser.add_argument("--verify-corpus", choices=(CURRENT_CORPUS_ID,), help="Verify every restored current-renderer corpus artifact without starting a graphics application.",)
    parser.add_argument("--logserver-executable", type=Path, help="Override the logserver executable.")
    parser.add_argument("--no-logserver", action=LIT_STORE_TRUE, help="Use standalone loader logs instead of logserver.")
    validation_group = parser.add_mutually_exclusive_group()
    validation_group.add_argument("--gpu-validation", dest=LIT_GPU_VALIDATION, action=LIT_STORE_TRUE)
    validation_group.add_argument("--no-gpu-validation", dest=LIT_GPU_VALIDATION, action="store_false")
    parser.set_defaults(gpu_validation=False)
    parser.add_argument(LIT_SETTLE_SECONDS_2, type=float, help="Override the profile's fixed temporal settle duration.")
    parser.add_argument(LIT_STARTUP_TIMEOUT, type=float, default=180.0, help="Seconds to wait for the native window and, separately, its capture-ready marker.")
    parser.add_argument("--require-exact", action=LIT_STORE_TRUE, help="Require bit-exact RGB output for a comparison.")
    parser.add_argument("--maximum-max-abs", type=int, help="Optional maximum per-channel RGB difference.")
    parser.add_argument(LIT_MAXIMUM_MEAN_ABS_2, type=float, help="Optional maximum mean absolute RGB difference.")
    parser.add_argument("--maximum-changed-fraction", type=float, help="Optional maximum fraction of changed pixels.")
    parser.add_argument("--reject-log", action=LIT_APPEND, default=[], help="Additional runtime log text that invalidates a capture.")
    parser.add_argument("--self-test", action=LIT_STORE_TRUE, help="Exercise manifest and pixel-difference logic without starting a graphics application.")
    return parser


def validate_args(args: argparse.Namespace) -> None:
    if args.self_test:
        return
    if args.verify_corpus is not None:
        conflicting = [name for name, value in ((LIT_PROFILE_2, args.profile), (LIT_EXECUTABLE, args.executable), (LIT_RUNTIME_DIR, args.runtime_dir), (LIT_OUTPUT_DIR, args.output_dir), (LIT_REFERENCE_DIR, args.reference_dir),
                (LIT_REFERENCE_CORPUS, args.reference_corpus),) if value is not None]
        if conflicting:
            raise SmokeFailure("--verify-corpus cannot be combined with " + ", ".join(conflicting))
        return
    if args.corpus_root is not None and args.reference_corpus is None:
        raise SmokeFailure("--corpus-root requires --reference-corpus or --verify-corpus")
    missing = [flag for flag, value in ((LIT_PROFILE_2, args.profile), (LIT_EXECUTABLE, args.executable), (LIT_RUNTIME_DIR, args.runtime_dir), (LIT_OUTPUT_DIR, args.output_dir)) if value is None]
    if missing:
        raise SmokeFailure("renderer baseline runner requires " + ", ".join(missing))
    if not math.isfinite(args.startup_timeout) or args.startup_timeout <= 0.0:
        raise SmokeFailure("--startup-timeout must be positive and finite")
    if args.settle_seconds is not None and (not math.isfinite(args.settle_seconds) or args.settle_seconds <= 0.0):
        raise SmokeFailure(LIT_SETTLE_SECONDS_MUST_BE_POSITIVE_AND_FI)
    if args.maximum_max_abs is not None and args.maximum_max_abs < 0:
        raise SmokeFailure("--maximum-max-abs must not be negative")
    if args.maximum_mean_abs is not None and (not math.isfinite(args.maximum_mean_abs) or args.maximum_mean_abs < 0.0):
        raise SmokeFailure("--maximum-mean-abs must be non-negative and finite")
    if args.maximum_changed_fraction is not None and not 0.0 <= args.maximum_changed_fraction <= 1.0:
        raise SmokeFailure("--maximum-changed-fraction must be in [0, 1]")


def run_self_test() -> int:
    return renderer_baseline_self_test.run_self_test(sys.modules[__name__])


def main(argv: Sequence[str]) -> int:
    args = make_parser().parse_args(argv)
    try:
        validate_args(args)
        if args.self_test:
            return run_self_test()
        if args.verify_corpus is not None:
            return verify_corpus(args.verify_corpus, args.corpus_root)
        return run(args)
    except SmokeSkip as error:
        print(f"renderer baseline skipped: {error}", file=sys.stderr)
        return SKIP_EXIT_CODE
    except SmokeFailure as error:
        print(f"renderer baseline failed: {error}", file=sys.stderr)
        return 1


if __name__ == LIT_MAIN:
    raise SystemExit(main(sys.argv[1:]))
