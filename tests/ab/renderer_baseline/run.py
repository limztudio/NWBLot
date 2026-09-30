#!/usr/bin/env python3
"""Capture an immutable renderer baseline or compare one candidate against it.

The workflow deliberately lives entirely under ``tests/``.  It takes no renderer
feature switch: a baseline is an image and a manifest from a known source revision,
while a candidate is the normal selected smoke executable at a later revision.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import platform
import struct
import subprocess
import sys
import tempfile
import time
from dataclasses import asdict, dataclass
from datetime import datetime, timezone
from pathlib import Path
from types import SimpleNamespace
from typing import Dict, List, Mapping, Optional, Sequence, Tuple
from unittest import mock


REPO = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO / "tests" / "smoke"))

from profiles import BaselineProfile, corpus_profile_names, get_profile, profile_names  # noqa: E402
from window_capture_smoke import (  # noqa: E402
    SKIP_EXIT_CODE,
    STRICT_LOG_FAILURE_MESSAGES,
    SmokeFailure,
    SmokeSkip,
    WindowsCapture,
    build_launch_environment,
    make_runtime_launch_args,
    create_capture_backend,
    ensure_process_running,
    launch_logserver,
    launch_testbed,
    require_normal_process_exit,
    shutdown_logserver_and_collect,
    terminate_process,
    validate_capture_result,
    wait_for_log_message,
)

# Shared literals (no inline hardcodes below this block).
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
LIT_OPAQUE_TEXTURE = "opaque-texture"
LIT_APP_STOP = "app-stop"
LIT_CLEANUP_NONE = "cleanup-none"
LIT_LOGSERVER_LOG = "logserver_*.log"
LIT_LOGSERVER_HELPER = "logserver-helper"
LIT_CAPTURED_RUNTIME_EVIDENCE = "captured runtime evidence"
LIT_BUILD_LAUNCH_ENVIRONMENT = "build_launch_environment"
LIT_CREATE_CAPTURE_BACKEND = "create_capture_backend"
LIT_LAUNCH_LOGSERVER = "launch_logserver"
LIT_LAUNCH_TESTBED = "launch_testbed"
LIT_TERMINATE_PROCESS = "terminate_process"
LIT_SHUTDOWN_LOGSERVER_AND_COLLECT = "shutdown_logserver_and_collect"
LIT_PREPARE = "prepare"
LIT_CLOSE = "close"
LIT_CAPTURE = "capture"
LIT_SOFT_SHADOWS = "soft-shadows"
LIT_READY = "ready"
LIT_SETTLE = "settle"
LIT_TEST_REFERENCE = "test-reference"
LIT_MAIN = "__main__"
LIT_APPEND = "append"


SCHEMA = "nwb.renderer-baseline.v1"
CORPUS_SCHEMA = "nwb.renderer-baseline-corpus.v1"
CURRENT_CORPUS_ID = "current-renderer-v1"
CURRENT_CORPUS_FILE = Path(__file__).with_name("current_renderer_corpus.json")
FORBIDDEN_LOG_MESSAGES = (
    *STRICT_LOG_FAILURE_MESSAGES,
    "cannot safely continue after an unresolved frame recovery submission",
)


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


@dataclass(frozen=True)
class CorpusReference:
    corpus_id: str
    reference_directory: Path
    profile_entry: Mapping[str, object]
    limits: Mapping[str, float]


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    try:
        with path.open("rb") as source:
            while True:
                chunk = source.read(1024 * 1024)
                if not chunk:
                    break
                digest.update(chunk)
    except OSError as error:
        raise SmokeFailure(f"could not hash '{path}': {error}") from error
    return digest.hexdigest()


def source_revision() -> str:
    try:
        result = subprocess.run(
            [LIT_GIT, "rev-parse", "HEAD"],
            cwd=REPO,
            check=False,
            capture_output=True,
            text=True,
        )
    except OSError:
        return LIT_UNAVAILABLE
    return result.stdout.strip() if result.returncode == 0 and result.stdout.strip() else LIT_UNAVAILABLE


def source_worktree_clean() -> bool:
    try:
        result = subprocess.run(
            [LIT_GIT, "status", "--porcelain"],
            cwd=REPO,
            check=False,
            capture_output=True,
            text=True,
        )
    except OSError:
        return False
    return result.returncode == 0 and not result.stdout.strip()


def effective_frozen_environment(profile: BaselineProfile) -> Dict[str, str]:
    return {
        name: os.environ.get(name, default_value)
        for name, default_value in sorted(profile.frozen_environment.items())
    }


def canonical_frozen_environment(profile: BaselineProfile) -> Dict[str, str]:
    return dict(sorted(profile.frozen_environment.items()))


def capture_mode(profile: BaselineProfile) -> str:
    return LIT_FRAME_LOCKED if profile.capture_freeze_frame != 0 else LIT_SETTLED


def read_bmp_rgb(path: Path) -> Tuple[int, int, bytes]:
    try:
        data = path.read_bytes()
    except OSError as error:
        raise SmokeFailure(f"could not read BMP '{path}': {error}") from error
    if len(data) < 54 or data[:2] != b"BM":
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
        output.write(struct.pack("<2sIHHI", b"BM", 14 + 40 + image_size, 0, 0, 54))
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
        raise SmokeFailure(
            "reference and candidate captures have different dimensions: "
            f"{reference_width}x{reference_height} vs {candidate_width}x{candidate_height}"
        )

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
    return PixelDifference(
        width=reference_width,
        height=reference_height,
        max_abs=max_abs,
        mean_abs=total_abs / len(reference_rgb),
        changed_pixels=changed_pixels,
        total_pixels=total_pixels,
    )


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
        failures.append(
            f"changed fraction {difference.changed_fraction:.6f} exceeds {maximum_changed_fraction:.6f}"
        )
    return failures


def validate_runtime_log(log_text: str, rejected_messages: Sequence[str]) -> Tuple[str, ...]:
    if not log_text:
        raise SmokeFailure("renderer baseline capture produced no captured logger output")
    forbidden = tuple(message for message in FORBIDDEN_LOG_MESSAGES if message in log_text)
    forbidden += tuple(message for message in rejected_messages if message in log_text)
    if forbidden:
        raise SmokeFailure(f"renderer baseline capture found forbidden log messages: {list(forbidden)}")
    return forbidden


def capture_scene(
    args: argparse.Namespace,
    profile: BaselineProfile,
    capture_path: Path,
    log_path: Path,
    frozen_environment: Mapping[str, str],
) -> CaptureResult:
    if not args.executable.is_file():
        raise SmokeFailure(f"renderer baseline executable does not exist: {args.executable}")
    if not args.runtime_dir.is_dir():
        raise SmokeFailure(f"renderer baseline runtime directory does not exist: {args.runtime_dir}")

    launch_args = make_runtime_launch_args(args)
    environment = build_launch_environment(launch_args)
    environment["NWB_RENDER_UNFOCUSED"] = "1"
    environment.update(frozen_environment)
    if profile.capture_freeze_frame != 0:
        environment["NWB_RENDERER_BASELINE_CAPTURE_FREEZE_FRAME"] = str(profile.capture_freeze_frame)
    if profile.fixed_delta_seconds != 0.0:
        environment["NWB_RENDERER_BASELINE_FIXED_DELTA_SECONDS"] = f"{profile.fixed_delta_seconds:.9g}"
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
    try:
        backend = create_capture_backend()
        logserver_process, log_port, log_directory, log_baseline, log_pattern = launch_logserver(
            launch_args, args.executable, environment
        )
        app_process = launch_testbed(launch_args, args.executable, environment, log_port)
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
            wait_for_log_message(
                log_directory,
                log_baseline,
                log_pattern,
                profile.capture_ready_log,
                args.startup_timeout,
            )
            # The marker identifies the fixture's held update phase. This delay precedes client capture;
            # it does not identify or wait on an accepted GPU source-frame fence.
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
        log_text = shutdown_logserver_and_collect(
            logserver_process,
            log_directory,
            log_baseline,
            log_pattern,
            LIT_RENDERER_BASELINE_LOGSERVER,
        )
        logserver_process = None
    finally:
        terminate_process(app_process, LIT_RENDERER_BASELINE_CAPTURE, window)
        terminate_process(logserver_process, LIT_RENDERER_BASELINE_LOGSERVER)
        if backend:
            backend.close()

    require_normal_process_exit(app_exit_code, app_exit_tail, "testbed")
    if not capture_path.is_file():
        raise SmokeFailure(f"renderer baseline did not create capture: {capture_path}")
    forbidden = validate_runtime_log(log_text, args.reject_log)
    log_path.write_text(log_text, encoding=LIT_UTF_8)
    return CaptureResult(
        capture_file=capture_path.name,
        log_file=log_path.name,
        capture_sha256=sha256_file(capture_path),
        executable_sha256=sha256_file(args.executable),
        source_revision=source_revision(),
        source_worktree_clean=source_worktree_clean(),
        frozen_environment=dict(frozen_environment),
        forbidden_log_messages=forbidden,
    )


def manifest_payload(
    capture_kind: str,
    profile_name: str,
    profile: BaselineProfile,
    args: argparse.Namespace,
    capture: CaptureResult,
    reference_manifest: Optional[Mapping[str, object]] = None,
) -> Dict[str, object]:
    payload: Dict[str, object] = {
        LIT_SCHEMA: SCHEMA,
        LIT_CAPTURE_KIND: capture_kind,
        "captured_utc": datetime.now(timezone.utc).isoformat(),
        LIT_PROFILE: profile_name,
        "profile_description": profile.description,
        "target": profile.target,
        "window_title": profile.window_title,
        LIT_CAPTURE_MODE: capture_mode(profile),
        LIT_CAPTURE_FREEZE_FRAME: profile.capture_freeze_frame,
        LIT_FIXED_DELTA_SECONDS: profile.fixed_delta_seconds,
        LIT_SETTLE_SECONDS: args.settle_seconds,
        LIT_GPU_VALIDATION: args.gpu_validation,
        "runtime_directory": str(args.runtime_dir),
        LIT_SOURCE_REVISION: capture.source_revision,
        LIT_SOURCE_WORKTREE_CLEAN: capture.source_worktree_clean,
        "executable_sha256": capture.executable_sha256,
        LIT_CAPTURE_FILE: capture.capture_file,
        LIT_CAPTURE_SHA256: capture.capture_sha256,
        "log_file": capture.log_file,
        LIT_FROZEN_ENVIRONMENT: dict(capture.frozen_environment),
        "platform": platform.platform(),
        "python_version": platform.python_version(),
    }
    if reference_manifest is not None:
        payload[LIT_REFERENCE_SOURCE_REVISION] = reference_manifest.get(LIT_SOURCE_REVISION)
        payload["reference_capture_sha256"] = reference_manifest.get(LIT_CAPTURE_SHA256)
    return payload


def write_json(path: Path, payload: Mapping[str, object]) -> None:
    path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding=LIT_UTF_8)


def load_reference(
    reference_directory: Path,
    profile_name: str,
    frozen_environment: Mapping[str, str],
    settle_seconds: float,
    gpu_validation: bool,
    capture_freeze_frame: int,
    fixed_delta_seconds: float,
) -> Tuple[Mapping[str, object], Path]:
    manifest_path = reference_directory / LIT_MANIFEST_JSON
    try:
        manifest = json.loads(manifest_path.read_text(encoding=LIT_UTF_8))
    except OSError as error:
        raise SmokeFailure(f"could not read baseline manifest '{manifest_path}': {error}") from error
    except json.JSONDecodeError as error:
        raise SmokeFailure(f"baseline manifest is not valid JSON: {manifest_path}: {error}") from error
    if not isinstance(manifest, dict) or manifest.get(LIT_SCHEMA) != SCHEMA:
        raise SmokeFailure(f"baseline manifest has an unsupported schema: {manifest_path}")
    if manifest.get(LIT_CAPTURE_KIND) != LIT_BASELINE:
        raise SmokeFailure(f"reference manifest is not an immutable baseline: {manifest_path}")
    if manifest.get(LIT_PROFILE) != profile_name:
        raise SmokeFailure(
            f"baseline profile mismatch: reference is {manifest.get('profile')!r}, candidate is {profile_name!r}"
        )
    if manifest.get(LIT_FROZEN_ENVIRONMENT) != dict(frozen_environment):
        raise SmokeFailure("baseline frozen environment differs from the candidate capture")
    if manifest.get(LIT_GPU_VALIDATION) != gpu_validation:
        raise SmokeFailure("baseline GPU-validation mode differs from the candidate capture")
    if manifest.get(LIT_SETTLE_SECONDS) != settle_seconds:
        raise SmokeFailure("baseline settle duration differs from the candidate capture")
    expected_capture_mode = LIT_FRAME_LOCKED if capture_freeze_frame != 0 else LIT_SETTLED
    if manifest.get(LIT_CAPTURE_MODE) != expected_capture_mode:
        raise SmokeFailure("baseline capture mode differs from the candidate capture")
    if manifest.get(LIT_CAPTURE_FREEZE_FRAME) != capture_freeze_frame:
        raise SmokeFailure("baseline capture freeze frame differs from the candidate capture")
    if manifest.get(LIT_FIXED_DELTA_SECONDS) != fixed_delta_seconds:
        raise SmokeFailure("baseline fixed simulation delta differs from the candidate capture")
    capture_name = manifest.get(LIT_CAPTURE_FILE)
    if not isinstance(capture_name, str) or not capture_name:
        raise SmokeFailure(f"baseline manifest has no capture filename: {manifest_path}")
    capture_path = reference_directory / capture_name
    if not capture_path.is_file():
        raise SmokeFailure(f"baseline capture does not exist: {capture_path}")
    expected_hash = manifest.get(LIT_CAPTURE_SHA256)
    if not isinstance(expected_hash, str) or sha256_file(capture_path) != expected_hash:
        raise SmokeFailure(f"baseline capture checksum does not match its immutable manifest: {capture_path}")
    return manifest, capture_path


def load_corpus(corpus_id: str, corpus_path: Path = CURRENT_CORPUS_FILE) -> Mapping[str, object]:
    try:
        corpus = json.loads(corpus_path.read_text(encoding=LIT_UTF_8))
    except OSError as error:
        raise SmokeFailure(f"could not read renderer baseline corpus '{corpus_path}': {error}") from error
    except json.JSONDecodeError as error:
        raise SmokeFailure(f"renderer baseline corpus is not valid JSON: {corpus_path}: {error}") from error
    if not isinstance(corpus, dict) or corpus.get(LIT_SCHEMA) != CORPUS_SCHEMA:
        raise SmokeFailure(f"renderer baseline corpus has an unsupported schema: {corpus_path}")
    if corpus.get(LIT_CORPUS_ID) != corpus_id:
        raise SmokeFailure(f"renderer baseline corpus id does not match '{corpus_id}': {corpus_path}")
    if not isinstance(corpus.get(LIT_ARTIFACT_ROOT), str):
        raise SmokeFailure(f"renderer baseline corpus has no artifact root: {corpus_path}")
    if not isinstance(corpus.get(LIT_PROFILES), dict):
        raise SmokeFailure(f"renderer baseline corpus has no profile table: {corpus_path}")
    return corpus


def corpus_artifact_root(corpus: Mapping[str, object], supplied_root: Optional[Path]) -> Path:
    if supplied_root is not None:
        return supplied_root
    artifact_root = corpus.get(LIT_ARTIFACT_ROOT)
    assert isinstance(artifact_root, str)
    return REPO / artifact_root


def corpus_child(root: Path, relative_path: str, description: str) -> Path:
    relative = Path(relative_path)
    if relative.is_absolute():
        raise SmokeFailure(f"renderer baseline corpus {description} must be relative: {relative_path}")
    resolved_root = root.resolve()
    resolved_child = (resolved_root / relative).resolve()
    try:
        resolved_child.relative_to(resolved_root)
    except ValueError as error:
        raise SmokeFailure(f"renderer baseline corpus {description} escapes its artifact root: {relative_path}") from error
    return resolved_child


def corpus_string(entry: Mapping[str, object], key: str, profile_name: str) -> str:
    value = entry.get(key)
    if not isinstance(value, str) or not value:
        raise SmokeFailure(f"renderer baseline corpus entry '{profile_name}' has no valid {key}")
    return value


def corpus_number(entry: Mapping[str, object], key: str, profile_name: str) -> float:
    value = entry.get(key)
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise SmokeFailure(f"renderer baseline corpus entry '{profile_name}' has no valid {key}")
    return float(value)


def load_corpus_reference(
    corpus_id: str,
    supplied_root: Optional[Path],
    profile_name: str,
    profile: BaselineProfile,
    gpu_validation: bool,
    frozen_environment: Optional[Mapping[str, str]] = None,
    corpus_path: Path = CURRENT_CORPUS_FILE,
) -> Tuple[Mapping[str, object], Path, CorpusReference]:
    corpus = load_corpus(corpus_id, corpus_path)
    capture_environment = corpus.get(LIT_CAPTURE_ENVIRONMENT)
    if not isinstance(capture_environment, dict):
        raise SmokeFailure(f"renderer baseline corpus '{corpus_id}' has no capture environment")
    expected_gpu_validation = capture_environment.get(LIT_GPU_VALIDATION)
    if not isinstance(expected_gpu_validation, bool):
        raise SmokeFailure(f"renderer baseline corpus '{corpus_id}' has no GPU-validation mode")
    if gpu_validation != expected_gpu_validation:
        raise SmokeFailure(
            f"renderer baseline corpus '{corpus_id}' requires gpu_validation={expected_gpu_validation}, "
            f"got {gpu_validation}"
        )

    profiles = corpus.get(LIT_PROFILES)
    assert isinstance(profiles, dict)
    entry = profiles.get(profile_name)
    if not isinstance(entry, dict):
        raise SmokeFailure(f"renderer baseline corpus '{corpus_id}' has no profile '{profile_name}'")
    reference = entry.get(LIT_REFERENCE)
    if not isinstance(reference, dict):
        raise SmokeFailure(f"renderer baseline corpus entry '{profile_name}' has no reference")
    artifact_root = corpus_artifact_root(corpus, supplied_root)
    reference_directory = corpus_child(
        artifact_root,
        corpus_string(reference, LIT_DIRECTORY, profile_name),
        f"reference directory for '{profile_name}'",
    )
    manifest_path = reference_directory / LIT_MANIFEST_JSON
    log_path = reference_directory / LIT_RUNTIME_LOG
    if sha256_file(manifest_path) != corpus_string(reference, LIT_MANIFEST_SHA256, profile_name):
        raise SmokeFailure(f"renderer baseline corpus manifest checksum does not match for '{profile_name}'")
    if sha256_file(log_path) != corpus_string(reference, LIT_RUNTIME_LOG_SHA256, profile_name):
        raise SmokeFailure(f"renderer baseline corpus runtime-log checksum does not match for '{profile_name}'")

    manifest, capture = load_reference(
        reference_directory,
        profile_name,
        effective_frozen_environment(profile) if frozen_environment is None else frozen_environment,
        profile.settle_seconds,
        gpu_validation,
        profile.capture_freeze_frame,
        profile.fixed_delta_seconds,
    )
    if manifest.get(LIT_SOURCE_REVISION) != corpus_string(reference, LIT_SOURCE_REVISION, profile_name):
        raise SmokeFailure(f"renderer baseline corpus source revision does not match for '{profile_name}'")
    if manifest.get(LIT_CAPTURE_SHA256) != corpus_string(reference, LIT_CAPTURE_SHA256, profile_name):
        raise SmokeFailure(f"renderer baseline corpus capture manifest does not match for '{profile_name}'")
    if sha256_file(capture) != corpus_string(reference, LIT_CAPTURE_SHA256, profile_name):
        raise SmokeFailure(f"renderer baseline corpus image checksum does not match for '{profile_name}'")

    limits_entry = entry.get(LIT_LIMITS)
    if not isinstance(limits_entry, dict):
        raise SmokeFailure(f"renderer baseline corpus entry '{profile_name}' has no comparison limits")
    limits = {
        LIT_MAXIMUM_MAX_ABS: corpus_number(limits_entry, LIT_MAXIMUM_MAX_ABS, profile_name),
        LIT_MAXIMUM_MEAN_ABS: corpus_number(limits_entry, LIT_MAXIMUM_MEAN_ABS, profile_name),
        LIT_MAXIMUM_CHANGED_FRACTION: corpus_number(limits_entry, LIT_MAXIMUM_CHANGED_FRACTION, profile_name),
    }
    if limits[LIT_MAXIMUM_MAX_ABS] < 0.0 or limits[LIT_MAXIMUM_MEAN_ABS] < 0.0:
        raise SmokeFailure(f"renderer baseline corpus entry '{profile_name}' has negative comparison limits")
    if not 0.0 <= limits[LIT_MAXIMUM_CHANGED_FRACTION] <= 1.0:
        raise SmokeFailure(f"renderer baseline corpus entry '{profile_name}' has an invalid changed-fraction limit")
    recapture = entry.get(LIT_SAME_REVISION_RECAPTURE)
    if not isinstance(recapture, dict):
        raise SmokeFailure(f"renderer baseline corpus entry '{profile_name}' has no same-revision re-capture")
    recapture_directory = corpus_child(
        artifact_root,
        corpus_string(recapture, LIT_DIRECTORY, profile_name),
        f"same-revision re-capture directory for '{profile_name}'",
    )
    comparison_path = recapture_directory / LIT_COMPARISON_JSON
    if sha256_file(comparison_path) != corpus_string(recapture, LIT_COMPARISON_SHA256, profile_name):
        raise SmokeFailure(f"renderer baseline corpus re-capture checksum does not match for '{profile_name}'")
    try:
        comparison = json.loads(comparison_path.read_text(encoding=LIT_UTF_8))
    except OSError as error:
        raise SmokeFailure(f"could not read renderer baseline corpus re-capture '{comparison_path}': {error}") from error
    except json.JSONDecodeError as error:
        raise SmokeFailure(f"renderer baseline corpus re-capture is not valid JSON: {comparison_path}: {error}") from error
    if not isinstance(comparison, dict) or comparison.get(LIT_SCHEMA) != LIT_NWB_RENDERER_BASELINE_COMPARISON_V1:
        raise SmokeFailure(f"renderer baseline corpus re-capture has an unsupported schema for '{profile_name}'")
    if comparison.get(LIT_PROFILE) != profile_name:
        raise SmokeFailure(f"renderer baseline corpus re-capture profile does not match for '{profile_name}'")
    expected_revision = corpus_string(reference, LIT_SOURCE_REVISION, profile_name)
    if comparison.get(LIT_REFERENCE_SOURCE_REVISION) != expected_revision:
        raise SmokeFailure(f"renderer baseline corpus re-capture reference revision does not match for '{profile_name}'")
    if comparison.get(LIT_CANDIDATE_SOURCE_REVISION) != expected_revision:
        raise SmokeFailure(f"renderer baseline corpus re-capture candidate revision does not match for '{profile_name}'")
    difference = comparison.get(LIT_DIFFERENCE)
    if not isinstance(difference, dict):
        raise SmokeFailure(f"renderer baseline corpus re-capture has no difference metrics for '{profile_name}'")
    for corpus_key, comparison_key, limit_key in (
        (LIT_MAX_ABS, LIT_MAX_ABS, LIT_MAXIMUM_MAX_ABS),
        (LIT_MEAN_ABS, LIT_MEAN_ABS, LIT_MAXIMUM_MEAN_ABS),
        (LIT_CHANGED_FRACTION, LIT_CHANGED_FRACTION, LIT_MAXIMUM_CHANGED_FRACTION),
    ):
        recorded_value = corpus_number(recapture, corpus_key, profile_name)
        observed_value = corpus_number(difference, comparison_key, profile_name)
        if observed_value != recorded_value:
            raise SmokeFailure(f"renderer baseline corpus re-capture metric does not match for '{profile_name}': {corpus_key}")
        if recorded_value > limits[limit_key]:
            raise SmokeFailure(f"renderer baseline corpus limit is below its observed re-capture for '{profile_name}'")
    return manifest, capture, CorpusReference(corpus_id, reference_directory, entry, limits)


def apply_corpus_limits(args: argparse.Namespace, reference: CorpusReference) -> None:
    for argument_name, corpus_key in (
        (LIT_MAXIMUM_MAX_ABS, LIT_MAXIMUM_MAX_ABS),
        (LIT_MAXIMUM_MEAN_ABS, LIT_MAXIMUM_MEAN_ABS),
        (LIT_MAXIMUM_CHANGED_FRACTION, LIT_MAXIMUM_CHANGED_FRACTION),
    ):
        approved_limit = reference.limits[corpus_key]
        requested_limit = getattr(args, argument_name)
        if requested_limit is None:
            setattr(args, argument_name, approved_limit)
        elif requested_limit > approved_limit:
            raise SmokeFailure(
                f"--{argument_name.replace('_', '-')}={requested_limit} relaxes the immutable corpus limit "
                f"{approved_limit}; omit it or choose a stricter value"
            )


def verify_corpus(corpus_id: str, supplied_root: Optional[Path]) -> int:
    corpus = load_corpus(corpus_id)
    profiles = corpus.get(LIT_PROFILES)
    assert isinstance(profiles, dict)
    expected_profiles = set(corpus_profile_names())
    actual_profiles = set(profiles)
    if actual_profiles != expected_profiles:
        missing = sorted(expected_profiles - actual_profiles)
        unexpected = sorted(actual_profiles - expected_profiles)
        raise SmokeFailure(
            f"renderer baseline corpus '{corpus_id}' profile set mismatch: missing={missing}, unexpected={unexpected}"
        )
    capture_environment = corpus.get(LIT_CAPTURE_ENVIRONMENT)
    assert isinstance(capture_environment, dict)
    gpu_validation = capture_environment.get(LIT_GPU_VALIDATION)
    assert isinstance(gpu_validation, bool)
    for profile_name in sorted(expected_profiles):
        profile = get_profile(profile_name)
        load_corpus_reference(
            corpus_id,
            supplied_root,
            profile_name,
            profile,
            gpu_validation,
            canonical_frozen_environment(profile),
        )
    print(f"renderer baseline corpus verified: {corpus_id}")
    return 0


def prepare_output_directory(path: Path) -> None:
    if path.exists():
        raise SmokeFailure(f"baseline output directory already exists and will not be overwritten: {path}")
    try:
        path.mkdir(parents=True)
    except OSError as error:
        raise SmokeFailure(f"could not create baseline output directory '{path}': {error}") from error


def run(args: argparse.Namespace) -> int:
    profile = get_profile(args.profile)
    args.settle_seconds = profile.settle_seconds if args.settle_seconds is None else args.settle_seconds
    if args.settle_seconds <= 0.0:
        raise SmokeFailure("--settle-seconds must be positive")
    frozen_environment = effective_frozen_environment(profile)
    reference_manifest: Optional[Mapping[str, object]] = None
    reference_capture: Optional[Path] = None
    corpus_reference: Optional[CorpusReference] = None
    reference_directory: Optional[Path] = None
    if args.reference_corpus is not None:
        reference_manifest, reference_capture, corpus_reference = load_corpus_reference(
            args.reference_corpus,
            args.corpus_root,
            args.profile,
            profile,
            args.gpu_validation,
            frozen_environment,
        )
        reference_directory = corpus_reference.reference_directory
        apply_corpus_limits(args, corpus_reference)
    elif args.reference_dir is not None:
        reference_manifest, reference_capture = load_reference(
            args.reference_dir,
            args.profile,
            frozen_environment,
            args.settle_seconds,
            args.gpu_validation,
            profile.capture_freeze_frame,
            profile.fixed_delta_seconds,
        )
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
    comparison = {
        LIT_SCHEMA: LIT_NWB_RENDERER_BASELINE_COMPARISON_V1,
        LIT_PROFILE: args.profile,
        "reference_directory": str(reference_directory),
        "candidate_directory": str(args.output_dir),
        LIT_REFERENCE_SOURCE_REVISION: reference_manifest.get(LIT_SOURCE_REVISION),
        LIT_CANDIDATE_SOURCE_REVISION: capture.source_revision,
        "reference_capture": str(reference_capture),
        "candidate_capture": str(capture_path),
        "difference_capture": str(args.output_dir / LIT_DIFFERENCE_BMP),
        LIT_DIFFERENCE: asdict(difference) | {LIT_CHANGED_FRACTION: difference.changed_fraction},
        "reference_corpus": corpus_reference.corpus_id if corpus_reference is not None else None,
        "effective_limits": {
            LIT_MAXIMUM_MAX_ABS: args.maximum_max_abs,
            LIT_MAXIMUM_MEAN_ABS: args.maximum_mean_abs,
            LIT_MAXIMUM_CHANGED_FRACTION: args.maximum_changed_fraction,
            "require_exact": args.require_exact,
        },
        "threshold_failures": failures,
        "verdict": "fail" if failures else ("pass" if corpus_reference is not None else "report-only"),
    }
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
    reference_group.add_argument(
        LIT_REFERENCE_CORPUS,
        choices=(CURRENT_CORPUS_ID,),
        help="Compare against the checked-in formal current-renderer corpus and enforce its limits.",
    )
    parser.add_argument(
        "--corpus-root",
        type=Path,
        help="Restored artifact root for --reference-corpus or --verify-corpus (defaults to the corpus artifact root).",
    )
    parser.add_argument(
        "--verify-corpus",
        choices=(CURRENT_CORPUS_ID,),
        help="Verify every restored current-renderer corpus artifact without running Vulkan.",
    )
    parser.add_argument("--logserver-executable", type=Path, help="Override the logserver executable.")
    parser.add_argument("--no-logserver", action=LIT_STORE_TRUE, help="Use standalone loader logs instead of logserver.")
    validation_group = parser.add_mutually_exclusive_group()
    validation_group.add_argument("--gpu-validation", dest=LIT_GPU_VALIDATION, action=LIT_STORE_TRUE)
    validation_group.add_argument("--no-gpu-validation", dest=LIT_GPU_VALIDATION, action="store_false")
    parser.set_defaults(gpu_validation=False)
    parser.add_argument("--settle-seconds", type=float, help="Override the profile's fixed temporal settle duration.")
    parser.add_argument("--startup-timeout", type=float, default=30.0, help="Seconds to wait for the native smoke window.")
    parser.add_argument("--require-exact", action=LIT_STORE_TRUE, help="Require bit-exact RGB output for a comparison.")
    parser.add_argument("--maximum-max-abs", type=int, help="Optional maximum per-channel RGB difference.")
    parser.add_argument("--maximum-mean-abs", type=float, help="Optional maximum mean absolute RGB difference.")
    parser.add_argument("--maximum-changed-fraction", type=float, help="Optional maximum fraction of changed pixels.")
    parser.add_argument("--reject-log", action=LIT_APPEND, default=[], help="Additional runtime log text that invalidates a capture.")
    parser.add_argument("--self-test", action=LIT_STORE_TRUE, help="Exercise manifest and pixel-difference logic without Vulkan.")
    return parser


def validate_args(args: argparse.Namespace) -> None:
    if args.self_test:
        return
    if args.verify_corpus is not None:
        conflicting = [
            name
            for name, value in (
                (LIT_PROFILE_2, args.profile),
                (LIT_EXECUTABLE, args.executable),
                (LIT_RUNTIME_DIR, args.runtime_dir),
                (LIT_OUTPUT_DIR, args.output_dir),
                (LIT_REFERENCE_DIR, args.reference_dir),
                (LIT_REFERENCE_CORPUS, args.reference_corpus),
            )
            if value is not None
        ]
        if conflicting:
            raise SmokeFailure("--verify-corpus cannot be combined with " + ", ".join(conflicting))
        return
    if args.corpus_root is not None and args.reference_corpus is None:
        raise SmokeFailure("--corpus-root requires --reference-corpus or --verify-corpus")
    missing = [
        flag
        for flag, value in ((LIT_PROFILE_2, args.profile), (LIT_EXECUTABLE, args.executable), (LIT_RUNTIME_DIR, args.runtime_dir), (LIT_OUTPUT_DIR, args.output_dir))
        if value is None
    ]
    if missing:
        raise SmokeFailure("renderer baseline runner requires " + ", ".join(missing))
    if args.startup_timeout <= 0.0:
        raise SmokeFailure("--startup-timeout must be positive")
    if args.maximum_max_abs is not None and args.maximum_max_abs < 0:
        raise SmokeFailure("--maximum-max-abs must not be negative")
    if args.maximum_mean_abs is not None and args.maximum_mean_abs < 0.0:
        raise SmokeFailure("--maximum-mean-abs must not be negative")
    if args.maximum_changed_fraction is not None and not 0.0 <= args.maximum_changed_fraction <= 1.0:
        raise SmokeFailure("--maximum-changed-fraction must be in [0, 1]")


def run_self_test() -> int:
    try:
        validate_runtime_log("", ())
    except SmokeFailure:
        pass
    else:
        raise AssertionError("renderer baseline empty runtime-log evidence must fail closed")
    with tempfile.TemporaryDirectory(prefix="nwb_renderer_baseline_") as temporary_directory:
        root = Path(temporary_directory)
        module = sys.modules[__name__]
        executable = root / "orchestration.exe"
        executable.write_bytes(b"exe")
        capture_path = root / "orchestration.bmp"
        runtime_log_path = root / "orchestration.log"
        app = SimpleNamespace(pid=4321, poll=lambda: None)
        logserver = object()
        baseline = {root / "old.log": 7}
        backend = mock.Mock()
        backend.wait_for_window.return_value = 17

        def capture(_window, path):
            path.write_bytes(b"bmp")
            return SimpleNamespace(appears_empty_or_white=False, has_pixel_variation=True)

        backend.capture_window.side_effect = capture
        orchestration_args = SimpleNamespace(
            executable=executable,
            runtime_dir=root,
            no_logserver=False,
            logserver_executable=None,
            startup_timeout=1.0,
            gpu_validation=False,
            settle_seconds=0.0,
            profile=LIT_OPAQUE_TEXTURE,
            reject_log=[],
        )
        events = []

        def terminate(process, name, window_handle=None):
            if process is app:
                events.append((LIT_APP_STOP, name, window_handle))
                return 7, "simulated abnormal exit"
            assert process is None
            events.append((LIT_CLEANUP_NONE, name, window_handle))
            return None, ""

        def shutdown(process, directory, received_baseline, pattern, shutdown_name="logserver"):
            assert process is logserver
            assert directory == root
            assert received_baseline == baseline
            assert pattern == LIT_LOGSERVER_LOG
            assert events == [(LIT_APP_STOP, LIT_RENDERER_BASELINE_CAPTURE, 17)]
            events.append((LIT_LOGSERVER_HELPER, shutdown_name))
            return LIT_CAPTURED_RUNTIME_EVIDENCE

        with mock.patch.object(module, LIT_BUILD_LAUNCH_ENVIRONMENT, return_value={}), \
             mock.patch.object(module, LIT_CREATE_CAPTURE_BACKEND, return_value=backend), \
             mock.patch.object(module, LIT_LAUNCH_LOGSERVER, return_value=(logserver, 49152, root, baseline, LIT_LOGSERVER_LOG)), \
             mock.patch.object(module, LIT_LAUNCH_TESTBED, return_value=app), \
             mock.patch.object(module, LIT_TERMINATE_PROCESS, side_effect=terminate) as terminate_mock, \
             mock.patch.object(module, LIT_SHUTDOWN_LOGSERVER_AND_COLLECT, side_effect=shutdown) as shutdown_mock:
            try:
                capture_scene(orchestration_args, get_profile(LIT_OPAQUE_TEXTURE), capture_path, runtime_log_path, {})
            except SmokeFailure as error:
                assert "exit 7" in str(error)
            else:
                raise AssertionError("renderer orchestration accepted an abnormal Testbed exit")

        assert events == [
            (LIT_APP_STOP, LIT_RENDERER_BASELINE_CAPTURE, 17),
            (LIT_LOGSERVER_HELPER, LIT_RENDERER_BASELINE_LOGSERVER),
            (LIT_CLEANUP_NONE, LIT_RENDERER_BASELINE_CAPTURE, 17),
            (LIT_CLEANUP_NONE, LIT_RENDERER_BASELINE_LOGSERVER, None),
        ]
        assert terminate_mock.mock_calls == [
            mock.call(app, LIT_RENDERER_BASELINE_CAPTURE, 17),
            mock.call(None, LIT_RENDERER_BASELINE_CAPTURE, 17),
            mock.call(None, LIT_RENDERER_BASELINE_LOGSERVER),
        ]
        shutdown_mock.assert_called_once_with(
            logserver, root, baseline, LIT_LOGSERVER_LOG, LIT_RENDERER_BASELINE_LOGSERVER
        )
        backend.close.assert_called_once_with()
        backend.prepare_window.assert_called_once_with(17)

        raw_backend = object.__new__(WindowsCapture)
        raw_events = []
        raw_backend.wait_for_window = mock.Mock(return_value=17)
        raw_backend.prepare_raw_client_window = lambda window: raw_events.append((LIT_PREPARE, window))
        raw_backend.prepare_window = mock.Mock(side_effect=AssertionError("raw preparation required"))
        raw_backend.capture_window = mock.Mock(side_effect=AssertionError("prepared raw capture required"))
        raw_backend.close = lambda: raw_events.append((LIT_CLOSE,))

        def raw_capture(window, path):
            raw_events.append((LIT_CAPTURE, window))
            return capture(window, path)

        raw_backend.capture_prepared_raw_client_window = raw_capture
        raw_args = SimpleNamespace(**vars(orchestration_args))
        raw_args.profile = LIT_SOFT_SHADOWS
        with mock.patch.object(module, LIT_BUILD_LAUNCH_ENVIRONMENT, return_value={}), \
             mock.patch.object(module, LIT_CREATE_CAPTURE_BACKEND, return_value=raw_backend), \
             mock.patch.object(module, LIT_LAUNCH_LOGSERVER, return_value=(logserver, 49152, root, baseline, LIT_LOGSERVER_LOG)), \
             mock.patch.object(module, LIT_LAUNCH_TESTBED, return_value=app), \
             mock.patch.object(module, "wait_for_log_message", side_effect=lambda *args: raw_events.append((LIT_READY,))), \
             mock.patch.object(time, "sleep", side_effect=lambda delay: raw_events.append((LIT_SETTLE, delay))), \
             mock.patch.object(module, LIT_TERMINATE_PROCESS, return_value=(0, "")), \
             mock.patch.object(module, LIT_SHUTDOWN_LOGSERVER_AND_COLLECT, return_value=LIT_CAPTURED_RUNTIME_EVIDENCE), \
             mock.patch.object(module, "validate_runtime_log", return_value=()), \
             mock.patch.object(module, LIT_SOURCE_REVISION, return_value="test-source"), \
             mock.patch.object(module, LIT_SOURCE_WORKTREE_CLEAN, return_value=True):
            capture_scene(raw_args, get_profile(LIT_SOFT_SHADOWS), capture_path, runtime_log_path, {})
        assert raw_events == [(LIT_PREPARE, 17), (LIT_READY,), (LIT_SETTLE, 0.0), (LIT_CAPTURE, 17), (LIT_CLOSE,)]
        raw_backend.prepare_window.assert_not_called()
        raw_backend.capture_window.assert_not_called()

        reference_directory = root / LIT_OPAQUE_TEXTURE / LIT_REFERENCE
        reference_directory.mkdir(parents=True)
        reference_image = reference_directory / LIT_BASELINE_BMP
        candidate_image = root / LIT_CANDIDATE_BMP
        write_bmp_rgb(reference_image, 2, 1, bytes((8, 16, 24, 32, 40, 48)))
        write_bmp_rgb(candidate_image, 2, 1, bytes((8, 16, 24, 35, 40, 50)))
        manifest = {
            LIT_SCHEMA: SCHEMA,
            LIT_CAPTURE_KIND: LIT_BASELINE,
            LIT_PROFILE: LIT_OPAQUE_TEXTURE,
            LIT_CAPTURE_FILE: LIT_BASELINE_BMP,
            LIT_CAPTURE_SHA256: sha256_file(reference_image),
            LIT_FROZEN_ENVIRONMENT: {},
            LIT_GPU_VALIDATION: False,
            LIT_SETTLE_SECONDS: 4.0,
            LIT_CAPTURE_MODE: LIT_SETTLED,
            LIT_CAPTURE_FREEZE_FRAME: 0,
            LIT_FIXED_DELTA_SECONDS: 0.0,
            LIT_SOURCE_REVISION: LIT_TEST_REFERENCE,
        }
        write_json(reference_directory / LIT_MANIFEST_JSON, manifest)
        (reference_directory / LIT_RUNTIME_LOG).write_text("test runtime log\n", encoding=LIT_UTF_8)
        recapture_directory = root / LIT_OPAQUE_TEXTURE / "recapture"
        recapture_directory.mkdir(parents=True)
        comparison_path = recapture_directory / LIT_COMPARISON_JSON
        write_json(
            comparison_path,
            {
                LIT_SCHEMA: LIT_NWB_RENDERER_BASELINE_COMPARISON_V1,
                LIT_PROFILE: LIT_OPAQUE_TEXTURE,
                LIT_REFERENCE_SOURCE_REVISION: LIT_TEST_REFERENCE,
                LIT_CANDIDATE_SOURCE_REVISION: LIT_TEST_REFERENCE,
                LIT_DIFFERENCE: {
                    LIT_MAX_ABS: 2,
                    LIT_MEAN_ABS: 0.5,
                    LIT_CHANGED_FRACTION: 0.25,
                },
            },
        )
        corpus_path = root / "corpus.json"
        corpus = {
            LIT_SCHEMA: CORPUS_SCHEMA,
            LIT_CORPUS_ID: CURRENT_CORPUS_ID,
            LIT_ARTIFACT_ROOT: ".",
            LIT_CAPTURE_ENVIRONMENT: {LIT_GPU_VALIDATION: False},
            LIT_PROFILES: {
                LIT_OPAQUE_TEXTURE: {
                    LIT_REFERENCE: {
                        LIT_DIRECTORY: "opaque-texture/reference",
                        LIT_SOURCE_REVISION: LIT_TEST_REFERENCE,
                        LIT_CAPTURE_SHA256: sha256_file(reference_image),
                        LIT_MANIFEST_SHA256: sha256_file(reference_directory / LIT_MANIFEST_JSON),
                        LIT_RUNTIME_LOG_SHA256: sha256_file(reference_directory / LIT_RUNTIME_LOG),
                    },
                    LIT_SAME_REVISION_RECAPTURE: {
                        LIT_DIRECTORY: "opaque-texture/recapture",
                        LIT_COMPARISON_SHA256: sha256_file(comparison_path),
                        LIT_MAX_ABS: 2,
                        LIT_MEAN_ABS: 0.5,
                        LIT_CHANGED_FRACTION: 0.25,
                    },
                    LIT_LIMITS: {
                        LIT_MAXIMUM_MAX_ABS: 3,
                        LIT_MAXIMUM_MEAN_ABS: 1.0,
                        LIT_MAXIMUM_CHANGED_FRACTION: 0.75,
                    },
                }
            },
        }
        write_json(corpus_path, corpus)
        corpus_manifest, corpus_capture, corpus_reference = load_corpus_reference(
            CURRENT_CORPUS_ID,
            root,
            LIT_OPAQUE_TEXTURE,
            get_profile(LIT_OPAQUE_TEXTURE),
            False,
            corpus_path=corpus_path,
        )
        reference_log = reference_directory / LIT_RUNTIME_LOG
        reference_log.write_text("tampered runtime log\n", encoding=LIT_UTF_8)
        try:
            load_corpus_reference(
                CURRENT_CORPUS_ID,
                root,
                LIT_OPAQUE_TEXTURE,
                get_profile(LIT_OPAQUE_TEXTURE),
                False,
                corpus_path=corpus_path,
            )
        except SmokeFailure:
            pass
        else:
            raise AssertionError("corpus artifact tampering must fail closed")
        reference_log.write_text("test runtime log\n", encoding=LIT_UTF_8)
        corpus_args = SimpleNamespace(
            maximum_max_abs=None,
            maximum_mean_abs=None,
            maximum_changed_fraction=None,
        )
        apply_corpus_limits(corpus_args, corpus_reference)
        corpus_args.maximum_max_abs = 4
        try:
            apply_corpus_limits(corpus_args, corpus_reference)
        except SmokeFailure:
            pass
        else:
            raise AssertionError("corpus comparison limits must not be relaxed")
        complex_surfel_profile = get_profile("surfel-gi-complex")
        with mock.patch.dict(os.environ, {"NWB_GI_SMOKE_MIN_SETTLE_SECONDS": "100"}):
            assert effective_frozen_environment(complex_surfel_profile) == {
                "NWB_GI_SMOKE_COMPLEX_SCENE": "1",
                "NWB_GI_SMOKE_MIN_SETTLE_SECONDS": "100",
            }
        difference = compare_bmp_rgb(reference_image, candidate_image, root / LIT_DIFFERENCE_BMP)
        args = SimpleNamespace(require_exact=False, maximum_max_abs=2, maximum_mean_abs=None, maximum_changed_fraction=None)
        assert difference_failures(args, difference) == ["max abs 3 exceeds 2"]
        args.require_exact = True
        assert difference_failures(args, difference)
    print("renderer-baseline runner self-test passed")
    return 0


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
